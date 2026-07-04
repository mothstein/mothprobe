#pragma once

#include "mcp/llm/provider_interface.hpp"

namespace mothprobe::mcp::llm {

class OpenAiCompatibleAdapter final : public IProvider {
 public:
  explicit OpenAiCompatibleAdapter(core::LlmProviderConfig config);

  std::string Name() const override;
  ChatResult Chat(const std::vector<ChatMessage>& messages) override;

 private:
  core::LlmProviderConfig config_;
};

}  // namespace mothprobe::mcp::llm
