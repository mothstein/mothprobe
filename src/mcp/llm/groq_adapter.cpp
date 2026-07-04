#include "mcp/llm/groq_adapter.hpp"

#include <nlohmann/json.hpp>

#include <utility>

#include "mcp/llm/http_client.hpp"

namespace mothprobe::mcp::llm {

GroqAdapter::GroqAdapter(core::LlmProviderConfig config)
    : config_(std::move(config)) {}

std::string GroqAdapter::Name() const {
  return "Groq";
}

ChatResult GroqAdapter::Chat(const std::vector<ChatMessage>& messages,
                             const ReasoningConfig& reasoning_config) {
  (void)reasoning_config;
  if (config_.api_key.empty()) {
    return {false, "", "Groq API key is not configured.", "auth_error", "", 0, false,
            false};
  }
  nlohmann::json body{{"model", config_.model_name},
                      {"stream", false},
                      {"temperature", 0},
                      {"max_tokens", config_.max_tokens},
                      {"messages", nlohmann::json::array()}};
  for (const auto& msg : messages) {
    body["messages"].push_back({{"role", msg.role}, {"content", msg.content}});
  }
  Headers headers{{"Authorization", "Bearer " + config_.api_key},
                  {"HTTP-Referer", "https://mothprobe.local"},
                  {"X-Title", "MothProbe"}};
  auto response = PostJson(config_.base_url + "/chat/completions", headers, body.dump());
  if (!response.ok) {
    return {false, "", "Groq network error: " + response.error, "network_error", "",
            response.status, true, false};
  }
  if (response.status < 200 || response.status >= 300) {
    return HttpErrorResult("Groq", response.status, response.body);
  }
  const auto json = nlohmann::json::parse(response.body, nullptr, false);
  if (json.is_discarded()) {
    return {false, "", "Groq returned invalid JSON.", "invalid_json", "", response.status,
            false, false};
  }
  if (!json.contains("choices") || !json["choices"].is_array() || json["choices"].empty() ||
      !json["choices"][0].is_object()) {
    return {false, "", "Groq returned no choices.", "empty_response", "",
            response.status, false, false};
  }
  const auto& choice = json["choices"][0];
  if (!choice.contains("message") || !choice["message"].is_object()) {
  const auto finish = choice.contains("finish_reason") && choice["finish_reason"].is_string() ? choice["finish_reason"].get<std::string>() : "";
    return {false, "", "Groq response did not include a message.", "empty_response",
            finish, response.status, false, finish == "length"};
  }
  std::string text = choice["message"].contains("content") && choice["message"]["content"].is_string() ? choice["message"]["content"].get<std::string>() : "";
  std::string reasoning = choice["message"].contains("reasoning") && choice["message"]["reasoning"].is_string() ? choice["message"]["reasoning"].get<std::string>() : "";
  const auto finish = choice.contains("finish_reason") && choice["finish_reason"].is_string() ? choice["finish_reason"].get<std::string>() : "";
  const bool truncated = finish == "length";
  
  if (reasoning.empty()) {
    auto start = text.find("<think>");
    auto end = text.find("</think>");
    if (start != std::string::npos && end != std::string::npos && end > start) {
      reasoning = text.substr(start + 7, end - (start + 7));
      text.erase(start, end + 8 - start);
      
      // Trim leading/trailing newlines
      while(!text.empty() && (text.front() == '\n' || text.front() == '\r')) text.erase(0, 1);
      while(!reasoning.empty() && (reasoning.front() == '\n' || reasoning.front() == '\r')) reasoning.erase(0, 1);
      while(!reasoning.empty() && (reasoning.back() == '\n' || reasoning.back() == '\r')) reasoning.pop_back();
    }
  }

  if (text.empty() && reasoning.empty()) {
    return {false, "", "Groq returned an empty response.", "empty_response", finish,
            response.status, false, false, ""};
  }
  return {true, text, "", "", finish, response.status, false, truncated, reasoning};
}

}  // namespace mothprobe::mcp::llm
