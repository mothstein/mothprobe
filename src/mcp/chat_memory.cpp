#include "mcp/chat_memory.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <utility>

#include <sodium.h>

namespace fs = std::filesystem;

namespace mothprobe::mcp {
namespace {

constexpr std::size_t kMaxStoredContent = 12000;

std::string IsoNow() {
  const auto now = std::chrono::system_clock::now();
  const auto time = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
#ifdef _WIN32
  gmtime_s(&tm, &time);
#else
  gmtime_r(&time, &tm);
#endif
  std::ostringstream out;
  out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
  return out.str();
}

std::string TrimForMemory(std::string value) {
  if (value.size() <= kMaxStoredContent) return value;
  value.resize(kMaxStoredContent);
  value += "\n[truncated by MothProbe memory]";
  return value;
}

std::string RedactSecrets(std::string value) {
  const std::vector<std::regex> patterns{
      std::regex(R"((api[_-]?key|token|secret|password)\s*[:=]\s*["']?[^"'\s]+)",
                 std::regex::icase),
      std::regex(R"(Bearer\s+[A-Za-z0-9._~+/=-]+)", std::regex::icase),
      std::regex(R"((sk|sk-or|AIza)[A-Za-z0-9._~+/=-]{12,})"),
  };
  for (const auto& pattern : patterns) {
    value = std::regex_replace(value, pattern, "[REDACTED]");
  }
  return value;
}

bool IsPersistableRole(const std::string& role) {
  return role == "user" || role == "assistant" || role == "system";
}

bool ValidSessionId(const std::string& value) {
  if (value.size() < 16 || value.size() > 80) return false;
  return std::all_of(value.begin(), value.end(), [](unsigned char c) {
    return std::isalnum(c) || c == '-';
  });
}

bool ValidAgentName(const std::string& value) {
  if (value.empty() || value.size() > 64) return false;
  return std::all_of(value.begin(), value.end(), [](unsigned char c) {
    return std::isalnum(c) || c == '-' || c == '_';
  });
}

std::string GenerateSessionId() {
  if (sodium_init() < 0) {
    throw std::runtime_error("libsodium initialization failed");
  }
  unsigned char bytes[16]{};
  randombytes_buf(bytes, sizeof(bytes));
  char hex[sizeof(bytes) * 2 + 1]{};
  sodium_bin2hex(hex, sizeof(hex), bytes, sizeof(bytes));
  return std::string(hex);
}

std::string AutoTitle(const std::string& content) {
  auto title = content;
  std::replace(title.begin(), title.end(), '\r', ' ');
  std::replace(title.begin(), title.end(), '\n', ' ');
  title = RedactSecrets(title);
  if (title.size() > 64) title = title.substr(0, 61) + "...";
  return title.empty() ? "New chat" : title;
}

nlohmann::json MessageJson(const std::string& role, std::string content,
                           const std::string& reasoning = "") {
  nlohmann::json message{{"role", role},
                         {"content", TrimForMemory(RedactSecrets(std::move(content)))},
                         {"timestamp", IsoNow()}};
  if (!reasoning.empty()) message["reasoning"] = TrimForMemory(RedactSecrets(reasoning));
  return message;
}

std::optional<ChatSession> ParseSession(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return std::nullopt;
  const auto json = nlohmann::json::parse(in, nullptr, false);
  if (json.is_discarded() || !json.is_object()) return std::nullopt;

  ChatSession session;
  session.summary.session_id = json.value("session_id", std::string{});
  session.summary.title = json.value("title", std::string("Untitled session"));
  session.summary.created_at = json.value("created_at", std::string{});
  session.summary.updated_at = json.value("updated_at", session.summary.created_at);
  session.summary.reasoning_mode = json.value("reasoning_mode", std::string("default"));
  session.summary.workspace_path = json.value("workspace_path", std::string{});
  session.summary.active_agent = json.value("active_agent", std::string("pentest-agent"));
  session.summary.permission_level = json.value("permission_level", std::string("default"));
  session.messages = json.value("messages", nlohmann::json::array());
  session.summary.message_count = session.messages.is_array() ? session.messages.size() : 0;
  if (!ValidSessionId(session.summary.session_id)) return std::nullopt;
  if (!session.messages.is_array()) session.messages = nlohmann::json::array();
  return session;
}

}  // namespace

ChatMemory::ChatMemory(std::filesystem::path brains_dir, std::filesystem::path workspace_path)
    : brains_dir_(std::move(brains_dir)), workspace_path_(std::move(workspace_path)) {
  fs::create_directories(brains_dir_);
}

const std::string& ChatMemory::ActiveSessionId() const {
  return active_.summary.session_id;
}

std::string ChatMemory::ReasoningMode() const {
  return active_.summary.reasoning_mode.empty() ? "default" : active_.summary.reasoning_mode;
}

ChatSession ChatMemory::ActiveSession() const {
  return active_;
}

std::vector<ChatSessionSummary> ChatMemory::ListSessions() const {
  std::vector<ChatSessionSummary> out;
  if (!fs::exists(brains_dir_)) return out;
  for (const auto& entry : fs::directory_iterator(brains_dir_)) {
    if (!entry.is_directory()) continue;
    auto session = ParseSession(entry.path() / "session.json");
    if (session) out.push_back(session->summary);
  }
  std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
    return a.updated_at > b.updated_at;
  });
  return out;
}

ChatSession ChatMemory::LoadSession(const std::string& session_id) {
  if (!ValidSessionId(session_id)) {
    throw std::runtime_error("invalid session_id");
  }
  auto session = ParseSession(SessionFile(session_id));
  if (!session) {
    throw std::runtime_error("chat session not found");
  }
  active_ = *session;
  return active_;
}

ChatSession ChatMemory::NewSession() {
  const auto now = IsoNow();
  active_ = {};
  active_.summary.session_id = GenerateSessionId();
  active_.summary.title = "New chat";
  active_.summary.created_at = now;
  active_.summary.updated_at = now;
  active_.summary.reasoning_mode = "default";
  active_.summary.workspace_path = workspace_path_.string();
  active_.summary.active_agent = "pentest-agent";
  active_.summary.permission_level = "default";
  active_.messages = nlohmann::json::array();
  SaveActiveSession();
  return active_;
}

ChatSession ChatMemory::ClearActiveSession() {
  EnsureActiveSession();
  active_.messages = nlohmann::json::array();
  active_.summary.message_count = 0;
  active_.summary.updated_at = IsoNow();
  SaveActiveSession();
  return active_;
}

ChatSession ChatMemory::SetReasoningMode(const std::string& mode) {
  if (mode != "default" && mode != "advanced" && mode != "fast") {
    throw std::runtime_error("reasoning mode must be default, advanced, or fast");
  }
  EnsureActiveSession();
  active_.summary.reasoning_mode = mode;
  active_.summary.updated_at = IsoNow();
  SaveActiveSession();
  return active_;
}

ChatSession ChatMemory::SetActiveAgent(const std::string& agent) {
  if (!ValidAgentName(agent)) {
    throw std::runtime_error("invalid active agent");
  }
  EnsureActiveSession();
  active_.summary.active_agent = agent;
  active_.summary.updated_at = IsoNow();
  SaveActiveSession();
  return active_;
}

ChatSession ChatMemory::SetPermissionLevel(const std::string& level) {
  if (level != "default" && level != "full") {
    throw std::runtime_error("permission level must be default or full");
  }
  EnsureActiveSession();
  active_.summary.permission_level = level;
  active_.summary.updated_at = IsoNow();
  SaveActiveSession();
  return active_;
}

ChatSession ChatMemory::SetTitle(const std::string& title) {
  EnsureActiveSession();
  active_.summary.title = title.empty() ? "Untitled session" : title;
  active_.summary.updated_at = IsoNow();
  SaveActiveSession();
  return active_;
}

std::vector<llm::ChatMessage> ChatMemory::BuildPrompt(
    const std::vector<llm::ChatMessage>& current, std::size_t history_limit) const {
  std::vector<llm::ChatMessage> prompt;
  for (const auto& message : current) {
    if (message.role == "system") prompt.push_back(message);
  }
  
  if (active_.summary.title == "New chat" || active_.summary.title == "Untitled session") {
    prompt.push_back({"system", "CRITICAL INSTRUCTION: You MUST use the set_chat_title tool in this turn to set a short, concise, and descriptive title for this conversation based on the user's input."});
  }

  std::vector<llm::ChatMessage> history;
  if (active_.messages.is_array()) {
    for (const auto& item : active_.messages) {
      const auto role = item.value("role", std::string{});
      const auto content = item.value("content", std::string{});
      if (!IsPersistableRole(role) || content.empty()) continue;
      history.push_back({role, content});
      if (history.size() > history_limit) {
        const auto overflow = static_cast<std::ptrdiff_t>(history.size() - history_limit);
        history.erase(history.begin(), history.begin() + overflow);
      }
    }
  }
  prompt.insert(prompt.end(), history.begin(), history.end());

  for (const auto& message : current) {
    if (message.role != "system") prompt.push_back(message);
  }
  return prompt;
}

void ChatMemory::AppendTurn(const std::vector<llm::ChatMessage>& request_messages,
                            const std::string& assistant_response,
                            const std::string& reasoning) {
  EnsureActiveSession();
  for (const auto& message : request_messages) {
    if (!IsPersistableRole(message.role) || message.content.empty()) continue;
    if (active_.summary.title == "New chat" && message.role == "user") {
      active_.summary.title = AutoTitle(message.content);
    }
    active_.messages.push_back(MessageJson(message.role, message.content));
  }
  active_.messages.push_back(MessageJson("assistant", assistant_response, reasoning));
  active_.summary.message_count = active_.messages.size();
  active_.summary.updated_at = IsoNow();
  SaveActiveSession();
}

void ChatMemory::EnsureActiveSession() {
  if (active_.summary.session_id.empty()) NewSession();
}

void ChatMemory::SaveActiveSession() const {
  if (active_.summary.session_id.empty()) return;
  fs::create_directories(SessionDir(active_.summary.session_id));
  std::ofstream out(SessionFile(active_.summary.session_id), std::ios::binary | std::ios::trunc);
  if (!out) throw std::runtime_error("cannot write chat session");
  out << ChatSessionJson(active_).dump(2) << '\n';
}

std::filesystem::path ChatMemory::SessionDir(const std::string& session_id) const {
  if (!ValidSessionId(session_id)) throw std::runtime_error("invalid session_id");
  return brains_dir_ / session_id;
}

std::filesystem::path ChatMemory::SessionFile(const std::string& session_id) const {
  return SessionDir(session_id) / "session.json";
}

nlohmann::json ChatSessionSummaryJson(const ChatSessionSummary& summary) {
  return {{"session_id", summary.session_id},
          {"title", summary.title},
          {"created_at", summary.created_at},
          {"updated_at", summary.updated_at},
          {"message_count", summary.message_count},
          {"reasoning_mode", summary.reasoning_mode},
          {"workspace_path", summary.workspace_path},
          {"active_agent", summary.active_agent},
          {"permission_level", summary.permission_level}};
}

nlohmann::json ChatSessionJson(const ChatSession& session) {
  auto json = ChatSessionSummaryJson(session.summary);
  json["messages"] = session.messages;
  return json;
}

}  // namespace mothprobe::mcp
