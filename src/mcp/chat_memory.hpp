#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "mcp/llm/provider_interface.hpp"

namespace mothprobe::mcp {

class ChatMemory {
 public:
  explicit ChatMemory(std::filesystem::path memory_file);

  std::vector<llm::ChatMessage> LoadRecent(std::size_t limit) const;
  std::vector<llm::ChatMessage> BuildPrompt(const std::vector<llm::ChatMessage>& current,
                                            std::size_t history_limit) const;
  void Append(const std::string& role, const std::string& content) const;
  void AppendTurn(const std::vector<llm::ChatMessage>& request_messages,
                  const std::string& assistant_response) const;

 private:
  std::filesystem::path memory_file_;
};

}  // namespace mothprobe::mcp
