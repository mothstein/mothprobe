#pragma once

#include <filesystem>
#include <string>

#include <nlohmann/json.hpp>

#include "core/config.hpp"

namespace mothprobe::mcp {

class McpClientRegistry {
 public:
  explicit McpClientRegistry(core::RuntimeConfig config);

  nlohmann::json List() const;
  nlohmann::json Get(const std::string& name) const;
  nlohmann::json Add(const nlohmann::json& params) const;
  nlohmann::json Remove(const std::string& name) const;
  nlohmann::json Login(const std::string& name, const nlohmann::json& params) const;
  nlohmann::json Logout(const std::string& name) const;
  nlohmann::json Connect(const std::string& name) const;
  nlohmann::json Disconnect(const std::string& name) const;

 private:
  core::RuntimeConfig config_;
};

}  // namespace mothprobe::mcp
