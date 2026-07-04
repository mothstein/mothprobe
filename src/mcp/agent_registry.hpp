#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/config.hpp"

namespace mothprobe::mcp {

struct AgentDefinition {
  std::string name;
  std::string scope;
  std::string path;
  std::string description;
  std::string provider;
  std::string model;
  std::string system_prompt;
};

class AgentRegistry {
 public:
  explicit AgentRegistry(core::RuntimeConfig config);

  nlohmann::json WorkspaceInspect() const;
  std::vector<AgentDefinition> ListAgents() const;
  AgentDefinition GetAgent(const std::string& name) const;
  AgentDefinition CreateAgent(const nlohmann::json& params) const;
  std::string BuildSystemContext(const std::string& active_agent) const;

 private:
  std::vector<nlohmann::json> ListSkills() const;
  void LoadAgentDir(const std::filesystem::path& dir, const std::string& scope,
                    std::vector<AgentDefinition>* out) const;

  core::RuntimeConfig config_;
};

nlohmann::json AgentDefinitionJson(const AgentDefinition& agent);

}  // namespace mothprobe::mcp
