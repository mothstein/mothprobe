#include "mcp/llm/claude_adapter.hpp"

#include <nlohmann/json.hpp>

#include <utility>

#include "mcp/llm/http_client.hpp"

namespace mothprobe::mcp::llm {

ClaudeAdapter::ClaudeAdapter(core::LlmProviderConfig config) : config_(std::move(config)) {}

std::string ClaudeAdapter::Name() const {
  return "claude";
}

ChatResult ClaudeAdapter::Chat(const std::vector<ChatMessage>& messages,
                               const ReasoningConfig& reasoning_config) {
  if (config_.api_key.empty()) {
    return {false, "", "Claude API key is not configured.", "auth_error", "", 0, false, false};
  }
  if (config_.base_url.empty()) {
    return {false, "", "Claude base_url is not configured.", "config_error", "", 0, false, false};
  }
  nlohmann::json body{{"model", config_.model_name},
                      {"max_tokens", config_.max_tokens},
                      {"temperature", 0},
                      {"messages", nlohmann::json::array()}};
  std::string system;
  for (const auto& msg : messages) {
    if (msg.role == "system") {
      if (!system.empty()) system += "\n";
      system += msg.content;
      continue;
    }
    body["messages"].push_back({{"role", msg.role == "assistant" ? "assistant" : "user"},
                                {"content", msg.content}});
  }
  if (!system.empty()) body["system"] = system;
  if (reasoning_config.mode == "advanced") {
    body["thinking"] = {{"type", "enabled"}, {"budget_tokens", 8000}};
  }

  Headers headers{{"x-api-key", config_.api_key},
                  {"anthropic-version", "2023-06-01"}};
  auto response = PostJson(config_.base_url + config_.chat_path, headers, body.dump());
  if (!response.ok) {
    return {false, "", "Claude network error: " + response.error, "network_error", "",
            response.status, true, false};
  }
  if (response.status < 200 || response.status >= 300) {
    return HttpErrorResult("Claude", response.status, response.body);
  }
  const auto json = nlohmann::json::parse(response.body, nullptr, false);
  if (json.is_discarded()) {
    return {false, "", "Claude returned invalid JSON.", "invalid_json", "", response.status,
            false, false};
  }
  std::string text;
  if (json.contains("content") && json["content"].is_array()) {
    for (const auto& part : json["content"]) {
      if (part.is_object() && part.contains("type") && part["type"].is_string() && part["type"].get<std::string>() == "text") {
        text += part.contains("text") && part["text"].is_string() ? part["text"].get<std::string>() : "";
      }
    }
  }
  const auto finish = json.contains("stop_reason") && json["stop_reason"].is_string() ? json["stop_reason"].get<std::string>() : "";
  const bool truncated = finish == "max_tokens";
  if (text.empty()) {
    return {false, "", "Claude returned an empty response.", "empty_response", finish,
            response.status, false, false};
  }
  return {true, text, "", "", finish, response.status, false, truncated};
}

}  // namespace mothprobe::mcp::llm
