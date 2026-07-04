#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "mcp/llm/provider_interface.hpp"

namespace mothprobe::mcp {

struct ChatSessionSummary {
  std::string session_id;
  std::string title;
  std::string created_at;
  std::string updated_at;
  std::size_t message_count = 0;
  std::string reasoning_mode = "default";
  std::string workspace_path;
  std::string active_agent = "pentest-agent";
  std::string permission_level = "default";
};

struct ChatSession {
  ChatSessionSummary summary;
  nlohmann::json messages = nlohmann::json::array();
};

class ChatMemory {
 public:
  explicit ChatMemory(std::filesystem::path brains_dir, std::filesystem::path workspace_path = {});

  const std::string& ActiveSessionId() const;
  std::string ReasoningMode() const;
  ChatSession ActiveSession() const;

  std::vector<ChatSessionSummary> ListSessions() const;
  ChatSession LoadSession(const std::string& session_id);
  ChatSession NewSession();
  ChatSession ClearActiveSession();
  ChatSession SetReasoningMode(const std::string& mode);
  ChatSession SetActiveAgent(const std::string& agent);
  ChatSession SetPermissionLevel(const std::string& level);
  ChatSession SetTitle(const std::string& title);

  std::vector<llm::ChatMessage> BuildPrompt(const std::vector<llm::ChatMessage>& current,
                                            std::size_t history_limit) const;
  void AppendTurn(const std::vector<llm::ChatMessage>& request_messages,
                  const std::string& assistant_response,
                  const std::string& reasoning);

 private:
  void EnsureActiveSession();
  void SaveActiveSession() const;
  std::filesystem::path SessionDir(const std::string& session_id) const;
  std::filesystem::path SessionFile(const std::string& session_id) const;

  std::filesystem::path brains_dir_;
  std::filesystem::path workspace_path_;
  ChatSession active_;
};

nlohmann::json ChatSessionSummaryJson(const ChatSessionSummary& summary);
nlohmann::json ChatSessionJson(const ChatSession& session);

}  // namespace mothprobe::mcp
