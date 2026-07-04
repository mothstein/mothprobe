#include "mcp/llm/provider_factory.hpp"

#include <algorithm>
#include <string>

#include <nlohmann/json.hpp>

#include "mcp/llm/gemini_adapter.hpp"
#include "mcp/llm/groq_adapter.hpp"
#include "mcp/llm/http_client.hpp"
#include "mcp/llm/ollama_adapter.hpp"
#include "mcp/llm/claude_adapter.hpp"
#include "mcp/llm/openai_compatible_adapter.hpp"
#include "mcp/llm/openrouter_adapter.hpp"

namespace mothprobe::mcp::llm {

namespace {

std::string ExtractProviderMessage(const std::string& body) {
  const auto json = nlohmann::json::parse(body, nullptr, false);
  if (!json.is_discarded()) {
    if (json["error"].is_object()) {
      if (auto message = json["error"].value("message", std::string{}); !message.empty()) {
        return message;
      }
    }
    if (auto message = json.value("message", std::string{}); !message.empty()) {
      return message;
    }
  }
  return body.substr(0, 500);
}

ModelListResult HttpModelError(const std::string& provider, int status, const std::string& body) {
  const auto chat = HttpErrorResult(provider, status, body);
  return {false, {}, chat.error, chat.error_kind, chat.http_status, chat.retryable, "provider"};
}

std::vector<std::string> UniqueSorted(std::vector<std::string> models) {
  models.erase(std::remove_if(models.begin(), models.end(),
                              [](const std::string& item) { return item.empty(); }),
               models.end());
  std::sort(models.begin(), models.end());
  models.erase(std::unique(models.begin(), models.end()), models.end());
  return models;
}

std::vector<std::string> ParseOpenAiModels(const nlohmann::json& json) {
  std::vector<std::string> models;
  if (!json.contains("data") || !json["data"].is_array()) return models;
  for (const auto& item : json["data"]) {
    if (item.is_object()) {
      if (auto id = item.value("id", std::string{}); !id.empty()) models.push_back(id);
    }
  }
  return UniqueSorted(std::move(models));
}

}  // namespace

ChatResult HttpErrorResult(const std::string& provider, int status, const std::string& body) {
  ChatResult result;
  result.ok = false;
  result.http_status = status;
  const auto detail = ExtractProviderMessage(body);
  if (status == 400) {
    result.error_kind = "invalid_request";
    result.error = provider + " rejected the request: " + detail;
  } else if (status == 401 || status == 403) {
    result.error_kind = "auth_error";
    result.error = provider + " authentication failed";
  } else if (status == 404) {
    result.error_kind = "model_not_found";
    result.error = provider + " endpoint or model was not found";
  } else if (status == 408 || status == 504) {
    result.error_kind = "timeout";
    result.retryable = true;
    result.error = provider + " request timed out";
  } else if (status == 429) {
    result.error_kind = "rate_limited";
    result.retryable = true;
    result.error = provider + " rate limit or quota was exceeded";
  } else if (status == 502 || status == 503) {
    result.error_kind = "bad_gateway";
    result.retryable = true;
    result.error = provider + " upstream service is unavailable";
  } else if (status >= 500) {
    result.error_kind = "server_error";
    result.retryable = true;
    result.error = provider + " server error";
  } else {
    result.error_kind = "provider_error";
    result.error = provider + " HTTP " + std::to_string(status) + ": " + detail;
  }
  if (!detail.empty() && result.error.find(detail) == std::string::npos &&
      result.error_kind != "auth_error") {
    result.error += " (" + detail + ")";
  }
  return result;
}

std::unique_ptr<IProvider> CreateProvider(const ProviderMap& providers,
                                          const std::string& provider) {
  auto it = providers.find(provider);
  if (it == providers.end()) {
    return nullptr;
  }
  if (provider == "gemini") return std::make_unique<GeminiAdapter>(it->second);
  if (provider == "claude") return std::make_unique<ClaudeAdapter>(it->second);
  if (provider == "openrouter") return std::make_unique<OpenRouterAdapter>(it->second);
  if (provider == "groq") return std::make_unique<GroqAdapter>(it->second);
  if (provider == "ollama") return std::make_unique<OllamaAdapter>(it->second);
  if (it->second.openai_compatible) {
    return std::make_unique<OpenAiCompatibleAdapter>(it->second);
  }
  return nullptr;
}

std::vector<std::string> ConfiguredProviders(const ProviderMap& providers) {
  std::vector<std::string> names;
  for (const auto& [name, config] : providers) {
    if (!config.requires_api_key || !config.api_key.empty()) {
      names.push_back(name);
    }
  }
  std::sort(names.begin(), names.end());
  return names;
}

ModelListResult FetchModels(const core::LlmProviderConfig& config) {
  Headers headers;
  std::string url;
  std::string provider_label = config.provider;
  if (config.provider == "gemini") {
    if (config.api_key.empty()) {
      return {false, {}, "Gemini API key is not configured.", "auth_error", 0, false, "config"};
    }
    provider_label = "Gemini";
    url = config.base_url + config.models_path + "?key=" + config.api_key;
  } else if (config.provider == "claude") {
    if (config.api_key.empty()) {
      return {false, {}, "Claude API key is not configured.", "auth_error", 0, false, "config"};
    }
    provider_label = "Claude";
    url = config.base_url + config.models_path;
    headers.emplace_back("x-api-key", config.api_key);
    headers.emplace_back("anthropic-version", "2023-06-01");
  } else {
    if (config.base_url.empty()) {
      return {false, {}, config.provider + " base_url is not configured.", "config_error", 0,
              false, "config"};
    }
    if (config.requires_api_key && config.api_key.empty()) {
      return {false, {}, config.provider + " API key is not configured.", "auth_error", 0, false,
              "config"};
    }
    provider_label = config.provider;
    url = config.base_url + config.models_path;
    if (!config.api_key.empty()) headers.emplace_back("Authorization", "Bearer " + config.api_key);
    if (config.provider == "openrouter") {
      headers.emplace_back("HTTP-Referer", "https://mothprobe.local");
      headers.emplace_back("X-Title", "MothProbe");
    }
  }

  auto response = GetJson(url, headers);
  if (!response.ok) {
    return {false,
            {},
            provider_label + " model fetch network error: " + response.error,
            "network_error",
            response.status,
            true,
            "provider"};
  }
  if (response.status < 200 || response.status >= 300) {
    return HttpModelError(provider_label, response.status, response.body);
  }
  const auto json = nlohmann::json::parse(response.body, nullptr, false);
  if (json.is_discarded()) {
    return {false, {}, provider_label + " returned invalid model JSON.", "invalid_json",
            response.status, false, "provider"};
  }

  std::vector<std::string> models;
  if (config.provider == "gemini") {
    if (json.contains("models") && json["models"].is_array()) {
      for (const auto& item : json["models"]) {
        if (!item.is_object()) continue;
        const auto methods = item.value("supportedGenerationMethods", nlohmann::json::array());
        if (methods.is_array() &&
            std::find(methods.begin(), methods.end(), "generateContent") == methods.end()) {
          continue;
        }
        auto name = item.value("name", std::string{});
        constexpr const char* prefix = "models/";
        if (name.rfind(prefix, 0) == 0) name = name.substr(std::char_traits<char>::length(prefix));
        if (!name.empty()) models.push_back(name);
      }
    }
  } else if (config.provider == "claude") {
    if (json.contains("data") && json["data"].is_array()) {
      for (const auto& item : json["data"]) {
        if (item.is_object()) {
          if (auto id = item.value("id", std::string{}); !id.empty()) models.push_back(id);
        }
      }
    }
  } else {
    models = ParseOpenAiModels(json);
  }
  models = UniqueSorted(std::move(models));
  if (models.empty()) {
    return {false, {}, provider_label + " returned no models.", "empty_response", response.status,
            false, "provider"};
  }
  return {true, models, "", "", response.status, false, "provider"};
}

ModelListResult FetchProviderModels(const ProviderMap& providers, const std::string& provider) {
  auto it = providers.find(provider);
  if (it == providers.end()) {
    return {false, {}, "unknown provider: " + provider, "invalid_request", 0, false, "config"};
  }
  return FetchModels(it->second);
}

}  // namespace mothprobe::mcp::llm
