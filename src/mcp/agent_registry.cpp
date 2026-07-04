#include "mcp/agent_registry.hpp"

#include <algorithm>
#include <fstream>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace fs = std::filesystem;

namespace mothprobe::mcp {
namespace {

bool ValidName(const std::string& name) {
  static const std::regex pattern(R"(^[A-Za-z0-9_-]{1,64}$)");
  return std::regex_match(name, pattern);
}

std::string ReadOptional(const fs::path& path) {
  if (!fs::exists(path)) return {};
  return core::ReadTextFileLimited(path, 1024 * 1024);
}

nlohmann::json ReadJsonOptional(const fs::path& path) {
  if (!fs::exists(path)) return nlohmann::json::object();
  std::ifstream in(path, std::ios::binary);
  auto json = nlohmann::json::parse(in, nullptr, false);
  return json.is_object() ? json : nlohmann::json::object();
}

class ScopedTempPath {
 public:
  explicit ScopedTempPath(fs::path path) : path_(std::move(path)) {}
  ~ScopedTempPath() {
    if (active_) {
      std::error_code ignored;
      fs::remove(path_, ignored);
    }
  }
  const fs::path& path() const { return path_; }
  void Release() { active_ = false; }

 private:
  fs::path path_;
  bool active_ = true;
};

void WriteText(const fs::path& path, const std::string& content) {
  fs::create_directories(path.parent_path());
  ScopedTempPath temp(path.string() + ".tmp");
  std::ofstream out(temp.path(), std::ios::binary | std::ios::trunc);
  if (!out) throw std::runtime_error("cannot write " + path.string());
  out << content;
  out.close();
  std::error_code ec;
  fs::rename(temp.path(), path, ec);
  if (ec) {
    fs::remove(path, ec);
    ec.clear();
    fs::rename(temp.path(), path, ec);
  }
  if (ec) throw std::runtime_error("cannot replace " + path.string());
  temp.Release();
}

std::vector<fs::path> SkillRoots(const core::RuntimeConfig& config) {
  return {config.skills_dir, config.project_root / ".agents" / "skills"};
}

}  // namespace

AgentRegistry::AgentRegistry(core::RuntimeConfig config) : config_(std::move(config)) {}

void AgentRegistry::LoadAgentDir(const fs::path& dir, const std::string& scope,
                                 std::vector<AgentDefinition>* out) const {
  if (!fs::exists(dir) || !fs::is_directory(dir)) return;
  for (const auto& entry : fs::directory_iterator(dir)) {
    if (!entry.is_directory()) continue;
    AgentDefinition agent;
    agent.name = entry.path().filename().string();
    if (!ValidName(agent.name)) continue;
    agent.scope = scope;
    agent.path = entry.path().string();
    const auto metadata = ReadJsonOptional(entry.path() / "agent.json");
    agent.description = metadata.value("description", "MothProbe agent: " + agent.name);
    agent.provider = metadata.value("provider", std::string{});
    agent.model = metadata.value("model", std::string{});
    agent.system_prompt = ReadOptional(entry.path() / "AGENTS.md");
    if (agent.system_prompt.empty()) {
      agent.system_prompt = metadata.value("system_prompt", std::string{});
    }
    out->push_back(std::move(agent));
  }
}

std::vector<AgentDefinition> AgentRegistry::ListAgents() const {
  std::vector<AgentDefinition> out;
  LoadAgentDir(config_.agents_dir, "global", &out);
  LoadAgentDir(config_.project_root / ".agents" / "agents", "local", &out);
  std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
    if (a.scope != b.scope) return a.scope == "local";
    return a.name < b.name;
  });
  return out;
}

AgentDefinition AgentRegistry::GetAgent(const std::string& name) const {
  if (!ValidName(name)) throw std::runtime_error("invalid agent name");
  for (const auto& agent : ListAgents()) {
    if (agent.name == name) return agent;
  }
  throw std::runtime_error("agent not found: " + name);
}

AgentDefinition AgentRegistry::CreateAgent(const nlohmann::json& params) const {
  const auto name = params.value("name", std::string{});
  if (!ValidName(name)) throw std::runtime_error("invalid agent name");
  const auto scope = params.value("scope", std::string("global"));
  if (scope != "global" && scope != "local") throw std::runtime_error("invalid agent scope");
  const auto root = scope == "local" ? config_.project_root / ".agents" / "agents" : config_.agents_dir;
  const auto dir = root / name;
  const auto prompt = params.value("system_prompt",
                                   "You are " + name + ", a MothProbe security sub-agent.");
  WriteText(dir / "AGENTS.md", prompt + "\n");
  const nlohmann::json metadata{{"name", name},
                                {"description", params.value("description", "MothProbe sub-agent")},
                                {"provider", params.value("provider", std::string{})},
                                {"model", params.value("model", std::string{})}};
  WriteText(dir / "agent.json", metadata.dump(2) + "\n");
  return GetAgent(name);
}

std::vector<nlohmann::json> AgentRegistry::ListSkills() const {
  std::vector<nlohmann::json> skills;
  for (const auto& root : SkillRoots(config_)) {
    if (!fs::exists(root) || !fs::is_directory(root)) continue;
    const auto scope = root == config_.skills_dir ? "global" : "local";
    for (const auto& entry : fs::directory_iterator(root)) {
      if (!entry.is_directory()) continue;
      const auto name = entry.path().filename().string();
      if (!ValidName(name)) continue;
      skills.push_back({{"name", name}, {"scope", scope}, {"path", entry.path().string()}});
    }
  }
  return skills;
}

nlohmann::json AgentRegistry::WorkspaceInspect() const {
  nlohmann::json entries = nlohmann::json::array();
  nlohmann::json agents = nlohmann::json::array();
  std::size_t count = 0;
  if (fs::exists(config_.project_root)) {
    for (const auto& entry : fs::directory_iterator(config_.project_root)) {
      if (count++ >= 80) break;
      entries.push_back({{"name", entry.path().filename().string()}, {"directory", entry.is_directory()}});
    }
  }
  for (const auto& agent : ListAgents()) agents.push_back(AgentDefinitionJson(agent));
  return {{"workspace_path", config_.project_root.string()},
          {"runtime_root", config_.runtime_root.string()},
          {"local_agents_path", (config_.project_root / ".agents" / "agents").string()},
          {"local_skills_path", (config_.project_root / ".agents" / "skills").string()},
          {"agents", agents},
          {"skills", ListSkills()},
          {"entries", entries}};
}

std::string AgentRegistry::BuildSystemContext(const std::string& active_agent) const {
  std::ostringstream out;
  AgentDefinition agent = GetAgent(active_agent.empty() ? "pentest-agent" : active_agent);
  out << agent.system_prompt << "\n\n";
  for (const auto& skill : ListSkills()) {
    const fs::path skill_path = fs::path(skill.value("path", std::string{})) / "SKILL.md";
    const auto text = ReadOptional(skill_path);
    if (!text.empty()) out << "\n# Skill: " << skill.value("name", std::string{}) << "\n" << text << "\n";
  }
  out << "\n# Workspace\nCurrent workspace: " << config_.project_root.string() << "\n";
  return out.str();
}

nlohmann::json AgentDefinitionJson(const AgentDefinition& agent) {
  return {{"name", agent.name},
          {"scope", agent.scope},
          {"path", agent.path},
          {"description", agent.description},
          {"provider", agent.provider},
          {"model", agent.model},
          {"system_prompt", agent.system_prompt}};
}

}  // namespace mothprobe::mcp
