#include "mcp/chat_memory.hpp"

#include <algorithm>
#include <fstream>
#include <utility>

#include <nlohmann/json.hpp>

namespace mothprobe::mcp {
namespace {

constexpr std::size_t kMaxStoredContent = 12000;

std::string TrimForMemory(std::string value) {
  if (value.size() <= kMaxStoredContent) return value;
  value.resize(kMaxStoredContent);
  value += "\n[truncated by MothProbe memory]";
  return value;
}

bool IsPersistableRole(const std::string& role) {
  return role == "user" || role == "assistant";
}

}  // namespace

ChatMemory::ChatMemory(std::filesystem::path memory_file) : memory_file_(std::move(memory_file)) {}

std::vector<llm::ChatMessage> ChatMemory::LoadRecent(std::size_t limit) const {
  std::vector<llm::ChatMessage> messages;
  std::ifstream in(memory_file_, std::ios::binary);
  if (!in || limit == 0) return messages;

  std::string line;
  while (std::getline(in, line)) {
    const auto json = nlohmann::json::parse(line, nullptr, false);
    if (json.is_discarded() || !json.is_object()) continue;
    const auto role = json.value("role", std::string{});
    const auto content = json.value("content", std::string{});
    if (!IsPersistableRole(role) || content.empty()) continue;
    messages.push_back({role, content});
    if (messages.size() > limit) {
      const auto overflow = static_cast<std::ptrdiff_t>(messages.size() - limit);
      messages.erase(messages.begin(), messages.begin() + overflow);
    }
  }
  return messages;
}

std::vector<llm::ChatMessage> ChatMemory::BuildPrompt(
    const std::vector<llm::ChatMessage>& current, std::size_t history_limit) const {
  std::vector<llm::ChatMessage> prompt;
  for (const auto& message : current) {
    if (message.role == "system") prompt.push_back(message);
  }
  auto history = LoadRecent(history_limit);
  prompt.insert(prompt.end(), history.begin(), history.end());
  for (const auto& message : current) {
    if (message.role != "system") prompt.push_back(message);
  }
  return prompt;
}

void ChatMemory::Append(const std::string& role, const std::string& content) const {
  if (!IsPersistableRole(role) || content.empty()) return;
  std::filesystem::create_directories(memory_file_.parent_path());
  std::ofstream out(memory_file_, std::ios::binary | std::ios::app);
  if (!out) return;
  out << nlohmann::json{{"role", role}, {"content", TrimForMemory(content)}}.dump() << '\n';
}

void ChatMemory::AppendTurn(const std::vector<llm::ChatMessage>& request_messages,
                            const std::string& assistant_response) const {
  for (const auto& message : request_messages) {
    if (message.role == "user") Append(message.role, message.content);
  }
  Append("assistant", assistant_response);
}

}  // namespace mothprobe::mcp
