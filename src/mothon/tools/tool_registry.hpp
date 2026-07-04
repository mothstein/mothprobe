#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/config.hpp"
#include "mcp/scope_validator.hpp"

namespace mothprobe::mothon::tools {

enum class RiskClass {
  Passive,
  LowActive,
  Intrusive,
};

enum class PermissionLevel {
  Default,
  Full,
};

struct ToolDefinition {
  std::string name;
  std::string description;
  nlohmann::json input_schema;
  RiskClass risk = RiskClass::Passive;
  bool scope_required = true;
  bool approval_required = false;
};

struct ToolCallResult {
  bool ok = false;
  nlohmann::json result;
  int error_code = -32000;
  std::string error;
  nlohmann::json data = nlohmann::json::object();
};

class ToolRegistry {
 public:
  explicit ToolRegistry(core::RuntimeConfig config);

  nlohmann::json List() const;
  ToolCallResult Call(const std::string& name, const nlohmann::json& arguments,
                      const ::mothprobe::mcp::ScopeValidator& scope,
                      PermissionLevel permission = PermissionLevel::Default) const;

 private:
  ToolCallResult LookupDns(const nlohmann::json& arguments,
                           const ::mothprobe::mcp::ScopeValidator& scope) const;
  ToolCallResult DetectHeaders(const nlohmann::json& arguments,
                               const ::mothprobe::mcp::ScopeValidator& scope) const;
  ToolCallResult ExportReport(const nlohmann::json& arguments) const;

  ToolCallResult RunCommand(const nlohmann::json& arguments,
                            PermissionLevel permission) const;
  ToolCallResult ReadFile(const nlohmann::json& arguments,
                          const ::mothprobe::mcp::ScopeValidator& scope) const;
  ToolCallResult WriteFile(const nlohmann::json& arguments,
                           PermissionLevel permission) const;
  ToolCallResult ListDir(const nlohmann::json& arguments,
                         const ::mothprobe::mcp::ScopeValidator& scope) const;

  core::RuntimeConfig config_;
  std::vector<ToolDefinition> tools_;
};

std::string RiskClassName(RiskClass risk);

}  // namespace mothprobe::mothon::tools
