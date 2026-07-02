#pragma once

#include <filesystem>
#include <memory>

#include <nlohmann/json.hpp>
#include <spdlog/logger.h>

#include "core/config.hpp"
#include "mcp/scope_validator.hpp"

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

  core::RuntimeConfig config_;
  std::shared_ptr<spdlog::logger> logger_;
  ScopeValidator scope_;
};

}  // namespace mothprobe::mcp
