#pragma once

#include "mcp/llm/provider_interface.hpp"

namespace mothprobe::mcp::llm {

class ClaudeAdapter final : public IProvider {
 public:
  explicit ClaudeAdapter(core::LlmProviderConfig config);

  std::string Name() const override;
  ChatResult Chat(const std::vector<ChatMessage>& messages,
                  const ReasoningConfig& reasoning = {}) override;

 private:
  core::LlmProviderConfig config_;
};

}  // namespace mothprobe::mcp::llm
