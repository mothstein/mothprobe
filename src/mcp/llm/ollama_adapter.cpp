#include "mcp/llm/ollama_adapter.hpp"

#include <nlohmann/json.hpp>

#include <utility>

#include "mcp/llm/http_client.hpp"

namespace mothprobe::mcp::llm {

OllamaAdapter::OllamaAdapter(core::LlmProviderConfig config) : config_(std::move(config)) {}

std::string OllamaAdapter::Name() const {
  return "ollama";
}

ChatResult OllamaAdapter::Chat(const std::vector<ChatMessage>& messages) {
  nlohmann::json body{{"model", config_.model_name},
                      {"stream", false},
                      {"messages", nlohmann::json::array()}};
  for (const auto& msg : messages) {
    body["messages"].push_back({{"role", msg.role}, {"content", msg.content}});
  }
  auto response = PostJson(config_.base_url + "/chat/completions", {}, body.dump());
  if (!response.ok) {
    return {false, "", "Ollama network error: " + response.error, "network_error", "",
            response.status, true, false};
  }
  if (response.status < 200 || response.status >= 300) {
    return HttpErrorResult("Ollama", response.status, response.body);
  }
  const auto json = nlohmann::json::parse(response.body, nullptr, false);
  if (json.is_discarded()) {
    return {false, "", "Ollama returned invalid JSON.", "invalid_json", "", response.status,
            false, false};
  }
  if (!json.contains("choices") || !json["choices"].is_array() || json["choices"].empty() ||
      !json["choices"][0].is_object()) {
    return {false, "", "Ollama returned no choices.", "empty_response", "",
            response.status, false, false};
  }
  const auto& choice = json["choices"][0];
  if (!choice.contains("message") || !choice["message"].is_object()) {
  const auto finish = choice.contains("finish_reason") && choice["finish_reason"].is_string() ? choice["finish_reason"].get<std::string>() : "";
    return {false, "", "Ollama response did not include a message.", "empty_response",
            finish, response.status, false, finish == "length"};
  }
  std::string text = choice["message"].contains("content") && choice["message"]["content"].is_string() ? choice["message"]["content"].get<std::string>() : "";
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

  const auto finish = choice.contains("finish_reason") && choice["finish_reason"].is_string() ? choice["finish_reason"].get<std::string>() : "";
  if (text.empty() && reasoning.empty()) {
    return ChatResult{false, "", "Ollama returned an empty response.", "empty_response", finish, response.status, false, false, ""};
  }
  return ChatResult{true, text, "", "", finish, response.status, false, finish == "length", reasoning};
}

}  // namespace mothprobe::mcp::llm
