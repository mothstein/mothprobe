#include "mcp/llm/gemini_adapter.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <utility>

#include "mcp/llm/http_client.hpp"

namespace mothprobe::mcp::llm {
namespace {

std::string PromptText(const std::vector<ChatMessage>& messages) {
  std::string text;
  for (const auto& msg : messages) {
    text += msg.role + ": " + msg.content + "\n";
  }
  return text;
}

}  // namespace

GeminiAdapter::GeminiAdapter(core::LlmProviderConfig config) : config_(std::move(config)) {}

std::string GeminiAdapter::Name() const {
  return "gemini";
}

ChatResult GeminiAdapter::Chat(const std::vector<ChatMessage>& messages) {
  if (config_.api_key.empty()) {
    return {false, "", "Gemini API key is not configured.", "auth_error", "", 0, false, false};
  }
  if (config_.base_url.empty()) {
    return {false, "", "Gemini base_url is not configured.", "config_error", "", 0, false, false};
  }
  const auto url = config_.base_url + "/models/" + config_.model_name +
                   ":generateContent?key=" + config_.api_key;
  nlohmann::json body{
      {"contents", nlohmann::json::array({{{"role", "user"},
                                            {"parts", nlohmann::json::array({{{"text", PromptText(messages)}}})}}})},
      {"generationConfig", {{"temperature", 0}, {"maxOutputTokens", config_.max_tokens}}},
  };

  auto response = PostJson(url, {}, body.dump());
  if (!response.ok) {
    return {false, "", "Gemini network error: " + response.error, "network_error", "",
            response.status, true, false};
  }
  if (response.status < 200 || response.status >= 300) {
    return HttpErrorResult("Gemini", response.status, response.body);
  }
  const auto json = nlohmann::json::parse(response.body, nullptr, false);
  if (json.is_discarded()) {
    return {false, "", "Gemini returned invalid JSON.", "invalid_json", "", response.status,
            false, false};
  }
  if (json.contains("promptFeedback") && json["promptFeedback"].contains("blockReason")) {
    return {false,
            "",
            "Gemini blocked the prompt: " + (json["promptFeedback"].contains("blockReason") && json["promptFeedback"]["blockReason"].is_string() ? json["promptFeedback"]["blockReason"].get<std::string>() : "unknown"),
            "blocked",
            "",
            response.status,
            false,
            false};
  }
  if (!json.contains("candidates") || !json["candidates"].is_array() ||
      json["candidates"].empty() || !json["candidates"][0].is_object()) {
    return {false, "", "Gemini returned no candidates.", "empty_response", "",
            response.status, false, false};
  }
  const auto& candidate = json["candidates"][0];
  const auto finish = candidate.contains("finishReason") && candidate["finishReason"].is_string() ? candidate["finishReason"].get<std::string>() : "";
  if (!candidate.contains("content") || !candidate["content"].is_object() ||
      !candidate["content"].contains("parts") || !candidate["content"]["parts"].is_array()) {
    return {false, "", "Gemini response did not include text content.", "empty_response",
            finish, response.status, false, finish == "MAX_TOKENS"};
  }
  std::string text;
  for (const auto& part : candidate["content"]["parts"]) {
    text += part.contains("text") && part["text"].is_string() ? part["text"].get<std::string>() : "";
  }
  std::string reasoning = "";
  auto start = text.find("<think>");
  auto end = text.find("</think>");
  if (start != std::string::npos && end != std::string::npos && end > start) {
    reasoning = text.substr(start + 7, end - (start + 7));
    text.erase(start, end + 8 - start);
    while(!text.empty() && (text.front() == '\n' || text.front() == '\r')) text.erase(0, 1);
    while(!reasoning.empty() && (reasoning.front() == '\n' || reasoning.front() == '\r')) reasoning.erase(0, 1);
    while(!reasoning.empty() && (reasoning.back() == '\n' || reasoning.back() == '\r')) reasoning.pop_back();
  }

  const bool truncated = finish == "MAX_TOKENS";
  if (text.empty() && reasoning.empty()) {
    return {false, "", "Gemini returned an empty response.", "empty_response", finish,
            response.status, false, false, ""};
  }
  return {true, text, "", "", finish, response.status, false, truncated, reasoning};
}

}  // namespace mothprobe::mcp::llm
