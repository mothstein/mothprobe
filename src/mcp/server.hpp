#pragma once

#include <filesystem>
#include <memory>

#include <nlohmann/json.hpp>
#include <spdlog/logger.h>

#include "core/audit.hpp"
#include "core/config.hpp"
#include "mcp/chat_memory.hpp"
#include "mcp/llm/provider_interface.hpp"
#include "mcp/scope_validator.hpp"
#include "mothon/tools/tool_registry.hpp"

namespace mothprobe::mcp {

class Server {
 public:
  Server(core::RuntimeConfig config, std::shared_ptr<spdlog::logger> logger);

  nlohmann::json Dispatch(const nlohmann::json& request);
  nlohmann::json Tools() const;
  void WriteToolCache() const;

 private:
  nlohmann::json Initialize(const nlohmann::json& request);
  nlohmann::json ToolsList(const nlohmann::json& request) const;
  nlohmann::json ToolsCall(const nlohmann::json& request);
  nlohmann::json ResourcesList(const nlohmann::json& request) const;
  nlohmann::json ResourcesRead(const nlohmann::json& request) const;
  nlohmann::json ResourceTemplatesList(const nlohmann::json& request) const;
  nlohmann::json PromptsList(const nlohmann::json& request) const;
  nlohmann::json PromptsGet(const nlohmann::json& request) const;
  nlohmann::json LlmListProviders(const nlohmann::json& request) const;
  nlohmann::json LlmFetchModels(const nlohmann::json& request);
  nlohmann::json LlmConfigureProvider(const nlohmann::json& request);
  nlohmann::json LlmChat(const nlohmann::json& request);

  core::RuntimeConfig config_;
  std::shared_ptr<spdlog::logger> logger_;
  core::AuditLogger audit_;
  ChatMemory chat_memory_;
  llm::ProviderMap llm_providers_;
  mothon::tools::ToolRegistry tool_registry_;
  ScopeValidator scope_;
  bool initialized_ = false;
  bool ready_ = false;
};

}  // namespace mothprobe::mcp
