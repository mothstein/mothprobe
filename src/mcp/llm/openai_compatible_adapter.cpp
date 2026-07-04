#include "mcp/llm/openai_compatible_adapter.hpp"

#include <nlohmann/json.hpp>

#include <utility>

#include "mcp/llm/http_client.hpp"

namespace mothprobe::mcp::llm {

OpenAiCompatibleAdapter::OpenAiCompatibleAdapter(core::LlmProviderConfig config)
    : config_(std::move(config)) {}

std::string OpenAiCompatibleAdapter::Name() const {
  return config_.provider;
}

ChatResult OpenAiCompatibleAdapter::Chat(const std::vector<ChatMessage>& messages) {
  if (config_.api_key.empty() && config_.requires_api_key) {
    return {false, "", config_.provider + " API key is not configured.", "auth_error", "", 0,
            false, false};
  }
  if (config_.base_url.empty()) {
    return {false, "", config_.provider + " base_url is not configured.", "config_error", "", 0,
            false, false};
  }
  nlohmann::json body{{"model", config_.model_name},
                      {"stream", false},
                      {"temperature", 0},
                      {"max_tokens", config_.max_tokens},
                      {"messages", nlohmann::json::array()}};
  for (const auto& msg : messages) {
    body["messages"].push_back({{"role", msg.role}, {"content", msg.content}});
  }
  Headers headers;
  if (!config_.api_key.empty()) headers.emplace_back("Authorization", "Bearer " + config_.api_key);
  if (config_.provider == "openrouter") {
    headers.emplace_back("HTTP-Referer", "https://mothprobe.local");
    headers.emplace_back("X-Title", "MothProbe");
  }

  auto response = PostJson(config_.base_url + config_.chat_path, headers, body.dump());
  if (!response.ok) {
    return {false, "", config_.provider + " network error: " + response.error, "network_error",
            "", response.status, true, false};
  }
  if (response.status < 200 || response.status >= 300) {
    return HttpErrorResult(config_.provider, response.status, response.body);
  }
  const auto json = nlohmann::json::parse(response.body, nullptr, false);
  if (json.is_discarded()) {
    return {false, "", config_.provider + " returned invalid JSON.", "invalid_json", "",
            response.status, false, false};
  }
  if (!json.contains("choices") || !json["choices"].is_array() || json["choices"].empty() ||
      !json["choices"][0].is_object()) {
    return {false, "", config_.provider + " returned no choices.", "empty_response", "",
            response.status, false, false};
  }
  const auto& choice = json["choices"][0];
  if (!choice.contains("message") || !choice["message"].is_object()) {
  const auto finish = choice.contains("finish_reason") && choice["finish_reason"].is_string() ? choice["finish_reason"].get<std::string>() : "";
    return {false, "", config_.provider + " response did not include a message.",
            "empty_response", finish, response.status, false, finish == "length"};
  }
  std::string text = choice["message"].contains("content") && choice["message"]["content"].is_string() ? choice["message"]["content"].get<std::string>() : "";
  std::string reasoning = choice["message"].contains("reasoning") && choice["message"]["reasoning"].is_string() ? choice["message"]["reasoning"].get<std::string>() : "";
  const auto finish = choice.contains("finish_reason") && choice["finish_reason"].is_string() ? choice["finish_reason"].get<std::string>() : "";
  const bool truncated = finish == "length";
  if (text.empty() && reasoning.empty()) {
    return {false, "", config_.provider + " returned an empty response.", "empty_response",
            finish, response.status, false, false, ""};
  }
  return {true, text, "", "", finish, response.status, false, truncated, reasoning};
}

}  // namespace mothprobe::mcp::llm
