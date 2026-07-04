#include "core/config.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>
#include <system_error>

#include <toml++/toml.hpp>

namespace fs = std::filesystem;

namespace mothprobe::core {
namespace {

std::string Trim(const std::string& value) {
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return {};
  const auto last = value.find_last_not_of(" \t\r\n");
  return value.substr(first, last - first + 1);
}

std::string TomlString(const std::string& value) {
  std::string out = "\"";
  for (const char c : value) {
    if (c == '\\' || c == '"') out.push_back('\\');
    out.push_back(c);
  }
  out.push_back('"');
  return out;
}

bool IsSection(const std::string& line) {
  const auto trimmed = Trim(line);
  return trimmed.size() >= 2 && trimmed.front() == '[' && trimmed.back() == ']';
}

std::string ProviderSection(const std::string& provider) {
  return "[llm." + provider + "]";
}

void WriteIfMissing(const fs::path& path, const std::string& content) {
  if (fs::exists(path)) return;
  fs::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    throw std::runtime_error("cannot create " + path.string());
  }
  out << content;
}

std::string DefaultPentestAgent() {
  return R"(# MothProbe Pentest Agent

You are MothProbe, an AI Security Penetration Testing Assistant.
Work only on assets the user owns or has explicit permission to test.
Prefer passive reconnaissance first, explain risk before intrusive actions, and use scoped tools.
Before shell or file changes, follow the active MothProbe permission policy.
Keep auditability high: summarize commands, files read, files changed, and assumptions.
)";
}

std::string DefaultMothprobeSkill() {
  return R"(# MothProbe Cyber Skills

- Build a clear testing scope before any active scan.
- Use passive OSINT, DNS lookup, HTTP header checks, and code review before intrusive testing.
- Never exfiltrate secrets; redact API keys, tokens, cookies, passwords, and private keys.
- For reports, include finding, impact, evidence, reproduction steps, and remediation.
- If a requested action is unsafe or out of scope, ask for scope clarification.
)";
}

LlmProviderConfig DefaultProvider(const std::string& provider) {
  if (provider == "gemini") {
    return {provider, "", "https://generativelanguage.googleapis.com/v1beta",
            "",
            "/models",
            "gemini-2.5-flash",
            {"gemini-2.5-flash", "gemini-2.5-pro", "gemini-1.5-flash", "gemini-1.5-pro"},
            4096,
            true,
            false};
  }
  if (provider == "openrouter") {
    return {provider,
            "",
            "https://openrouter.ai/api/v1",
            "/chat/completions",
            "/models",
            "google/gemini-2.5-flash",
            {"google/gemini-2.5-flash",
             "google/gemini-2.5-pro",
             "anthropic/claude-sonnet-4",
             "openai/gpt-4.1",
             "deepseek/deepseek-chat-v3-0324"},
            4096,
            true,
            true};
  }
  if (provider == "openai") {
    return {provider,
            "",
            "https://api.openai.com/v1",
            "/chat/completions",
            "/models",
            "gpt-4o-mini",
            {"gpt-4o-mini", "gpt-4o", "gpt-4.1-mini", "gpt-4.1"},
            4096,
            true,
            true};
  }
  if (provider == "deepseek") {
    return {provider,
            "",
            "https://api.deepseek.com/v1",
            "/chat/completions",
            "/models",
            "deepseek-chat",
            {"deepseek-chat", "deepseek-reasoner"},
            4096,
            true,
            true};
  }
  if (provider == "claude") {
    return {provider,
            "",
            "https://api.anthropic.com/v1",
            "/messages",
            "/models",
            "claude-3-5-haiku-latest",
            {"claude-3-5-haiku-latest", "claude-sonnet-4-0", "claude-opus-4-0"},
            4096,
            true,
            false};
  }
  if (provider == "kimi") {
    return {provider,
            "",
            "https://api.moonshot.ai/v1",
            "/chat/completions",
            "/models",
            "moonshot-v1-8k",
            {"moonshot-v1-8k", "moonshot-v1-32k", "moonshot-v1-128k", "kimi-k2-0711-preview"},
            4096,
            true,
            true};
  }
  if (provider == "mistral") {
    return {provider,
            "",
            "https://api.mistral.ai/v1",
            "/chat/completions",
            "/models",
            "mistral-small-latest",
            {"mistral-small-latest", "mistral-medium-latest", "mistral-large-latest",
             "codestral-latest"},
            4096,
            true,
            true};
  }
  if (provider == "nvidia_nim" || provider == "nvidia-nim") {
    return {"nvidia_nim",
            "",
            "https://integrate.api.nvidia.com/v1",
            "/chat/completions",
            "/models",
            "meta/llama-3.1-8b-instruct",
            {"meta/llama-3.1-8b-instruct", "meta/llama-3.1-70b-instruct",
             "nvidia/llama-3.1-nemotron-70b-instruct"},
            4096,
            true,
            true};
  }
  if (provider == "groq") {
    return {provider,
            "",
            "https://api.groq.com/openai/v1",
            "/chat/completions",
            "/models",
            "llama3-8b-8192",
            {"llama3-8b-8192", "llama3-70b-8192", "mixtral-8x7b-32768", "gemma2-9b-it"},
            4096,
            true,
            true};
  }
  if (provider == "ollama") {
    return {provider,
            "",
            "http://localhost:11434/v1",
            "/chat/completions",
            "/models",
            "llama3.1",
            {"llama3.1", "llama3.2", "qwen2.5", "mistral", "codellama"},
            4096,
            false,
            true};
  }
  return {provider, "", "", "/chat/completions", "/models", "", {}, 4096, true, true};
}

std::string TimestampIso() {
  const auto now = std::chrono::system_clock::now();
  const auto time = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
#ifdef _WIN32
  gmtime_s(&tm, &time);
#else
  gmtime_r(&time, &tm);
#endif
  std::ostringstream out;
  out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
  return out.str();
}

nlohmann::json PathArrayJson(const std::vector<fs::path>& paths) {
  nlohmann::json out = nlohmann::json::array();
  for (const auto& path : paths) {
    out.push_back(path.string());
  }
  return out;
}

fs::path RuntimePath(const RuntimeConfig& config, const std::string& value) {
  fs::path path(value);
  if (path.is_relative()) {
    path = config.project_root / path;
  }
  return fs::absolute(path).lexically_normal();
}

std::vector<fs::path> PathArrayFromJson(const RuntimeConfig& config, const nlohmann::json& value,
                                        std::vector<fs::path> fallback) {
  if (!value.is_array()) {
    return fallback;
  }
  std::vector<fs::path> out;
  for (const auto& item : value) {
    if (item.is_string()) {
      out.push_back(RuntimePath(config, item.get<std::string>()));
    }
  }
  return out.empty() ? std::move(fallback) : std::move(out);
}

nlohmann::json LoadJsonFile(const fs::path& path, nlohmann::json fallback) {
  if (!fs::exists(path)) {
    return fallback;
  }
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return fallback;
  }
  auto parsed = nlohmann::json::parse(in, nullptr, false);
  return parsed.is_discarded() ? fallback : parsed;
}

class ScopedTempFile {
 public:
  explicit ScopedTempFile(fs::path path) : path_(std::move(path)) {}
  ~ScopedTempFile() {
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

void SaveJsonFileAtomic(const fs::path& path, const nlohmann::json& value) {
  fs::create_directories(path.parent_path());
  ScopedTempFile temp(path.string() + ".tmp");
  {
    std::ofstream out(temp.path(), std::ios::binary | std::ios::trunc);
    if (!out) {
      throw std::runtime_error("cannot write temporary file: " + temp.path().string());
    }
    out << value.dump(2) << '\n';
  }
  std::error_code ec;
  fs::rename(temp.path(), path, ec);
  if (ec) {
    fs::remove(path, ec);
    ec.clear();
    fs::rename(temp.path(), path, ec);
  }
  if (ec) {
    throw std::runtime_error("cannot replace file: " + path.string());
  }
  temp.Release();
}

std::map<std::string, ToolPermission> DefaultToolPermissions() {
  return {{"lookup_dns", ToolPermission::Allow},
          {"detect_headers", ToolPermission::Allow},
          {"report_export", ToolPermission::Allow},
          {"read_file", ToolPermission::Allow},
          {"list_dir", ToolPermission::Allow},
          {"write_file", ToolPermission::Ask},
          {"run_command", ToolPermission::Ask}};
}

PermissionConfig DefaultPermissionConfig(const RuntimeConfig& config) {
  return {DefaultToolPermissions(),
          {config.project_root, config.runtime_root},
          {config.project_root, config.runtime_root}};
}

WorkspaceConfig DefaultWorkspaceConfig(const RuntimeConfig& config) {
  return {config.project_root,
          config.runtime_root,
          {config.project_root, config.runtime_root},
          {config.project_root, config.runtime_root}};
}

void WriteTextFileIfMissing(const fs::path& path, const std::string& content) {
  if (fs::exists(path)) {
    return;
  }
  fs::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    throw std::runtime_error("cannot write file: " + path.string());
  }
  out << content;
}

void CopyDirectoryDefaults(const fs::path& source, const fs::path& destination) {
  if (!fs::exists(source) || !fs::is_directory(source)) {
    return;
  }
  fs::create_directories(destination);
  std::error_code ec;
  fs::copy(source, destination,
           fs::copy_options::recursive | fs::copy_options::skip_existing, ec);
}

void EnsureDefaultAgents(const RuntimeConfig& config) {
  CopyDirectoryDefaults(config.project_root / ".agents" / "agents", config.agents_dir);

  WriteTextFileIfMissing(
      config.agents_dir / "code_writer" / "agent.json",
      nlohmann::json{{"name", "code_writer"},
                     {"description", "Writes focused backend or tooling changes."},
                     {"tools", {"read_file", "write_file", "list_dir", "run_command"}},
                     {"instructions",
                      "Inspect the workspace first, keep edits scoped, and verify changes."}}
          .dump(2) +
          "\n");
  WriteTextFileIfMissing(
      config.agents_dir / "bug_analyzer" / "agent.json",
      nlohmann::json{{"name", "bug_analyzer"},
                     {"description", "Analyzes logs, stack traces, and failing behavior."},
                     {"tools", {"read_file", "list_dir", "run_command"}},
                     {"instructions",
                      "Diagnose the root cause before proposing or applying fixes."}}
          .dump(2) +
          "\n");
  WriteTextFileIfMissing(
      config.agents_dir / "smoke_tester" / "agent.json",
      nlohmann::json{{"name", "smoke_tester"},
                     {"description", "Runs focused verification and smoke tests."},
                     {"tools", {"read_file", "list_dir", "run_command"}},
                     {"instructions",
                      "Run the smallest meaningful verification and report failures clearly."}}
          .dump(2) +
          "\n");
}

void EnsureDefaultSkills(const RuntimeConfig& config) {
  CopyDirectoryDefaults(config.project_root / ".agents" / "skills", config.skills_dir);
  WriteTextFileIfMissing(
      config.skills_dir / "mothprobe-runtime" / "SKILL.md",
      "---\n"
      "name: mothprobe-runtime\n"
      "description: Default runtime guidance for local MothProbe agents.\n"
      "---\n\n"
      "# MothProbe Runtime\n\n"
      "Use scoped tools only, keep evidence local to `.mothprobe`, and request permission before "
      "active or destructive work.\n");
}

void EnsureDefaultJsonStores(const RuntimeConfig& config) {
  if (!fs::exists(config.workspace_file)) {
    SaveJsonFileAtomic(config.workspace_file, WorkspaceConfigJson(DefaultWorkspaceConfig(config)));
  }
  if (!fs::exists(config.permissions_file)) {
    SavePermissionConfig(config, DefaultPermissionConfig(config));
  }
  if (!fs::exists(config.mcp_clients_file)) {
    SaveJsonFileAtomic(config.mcp_clients_file,
                       {{"schema", 1},
                        {"updated_at", TimestampIso()},
                        {"clients", nlohmann::json::array()}});
  }
}

}  // namespace

fs::path FindProjectRoot() {
  fs::path current = fs::current_path();
  for (int depth = 0; depth < 8; ++depth) {
    if (fs::exists(current / "CMakeLists.txt") || fs::exists(current / "data")) {
      return fs::weakly_canonical(current);
    }
    if (!current.has_parent_path() || current == current.parent_path()) {
      break;
    }
    current = current.parent_path();
  }
  return fs::weakly_canonical(fs::current_path());
}

RuntimeConfig LoadRuntimeConfig() {
  RuntimeConfig config;
  config.project_root = FindProjectRoot();
  config.runtime_root = config.project_root / ".mothprobe";
  config.bin_dir = config.runtime_root / "bin";
  config.caches_dir = config.runtime_root / "caches";
  config.brains_dir = config.runtime_root / "brains";
  config.agents_dir = config.runtime_root / "agents";
  config.agent_runs_dir = config.runtime_root / "agent_runs";
  config.skills_dir = config.runtime_root / "skills";
  config.logs_dir = config.runtime_root / "logs";
  config.audit_file = config.runtime_root / "audit.jsonl";
  config.workspace_file = config.runtime_root / "workspace.json";
  config.permissions_file = config.runtime_root / "permissions.json";
  config.mcp_clients_file = config.runtime_root / "mcp_clients.json";
  return config;
}

std::map<std::string, LlmProviderConfig> LoadLlmProviderConfigs(const RuntimeConfig& config) {
  std::map<std::string, LlmProviderConfig> providers{
      {"gemini", DefaultProvider("gemini")},
      {"openrouter", DefaultProvider("openrouter")},
      {"openai", DefaultProvider("openai")},
      {"deepseek", DefaultProvider("deepseek")},
      {"claude", DefaultProvider("claude")},
      {"kimi", DefaultProvider("kimi")},
      {"mistral", DefaultProvider("mistral")},
      {"nvidia_nim", DefaultProvider("nvidia_nim")},
      {"groq", DefaultProvider("groq")},
      {"ollama", DefaultProvider("ollama")},
      {"custom", DefaultProvider("custom")},
  };
  const auto config_file = config.runtime_root / "config.toml";
  if (!fs::exists(config_file)) {
    return providers;
  }
  try {
    const auto table = toml::parse_file(config_file.string());
    if (auto llm = table["llm"].as_table()) {
      for (const auto& [key, _node] : *llm) {
        const auto name = std::string(key.str());
        if (providers.find(name) == providers.end()) {
          providers[name] = DefaultProvider(name);
          providers[name].provider = name;
        }
      }
      for (auto& [name, provider] : providers) {
        if (auto entry = (*llm)[name].as_table()) {
          provider.api_key = (*entry)["api_key"].value_or(provider.api_key);
          provider.base_url = (*entry)["base_url"].value_or(provider.base_url);
          provider.chat_path = (*entry)["chat_path"].value_or(provider.chat_path);
          provider.models_path = (*entry)["models_path"].value_or(provider.models_path);
          provider.model_name = (*entry)["model_name"].value_or(provider.model_name);
          provider.model_name = (*entry)["model"].value_or(provider.model_name);
          provider.requires_api_key =
              (*entry)["requires_api_key"].value_or(provider.requires_api_key);
          provider.openai_compatible =
              (*entry)["openai_compatible"].value_or(provider.openai_compatible);
          if (auto models = (*entry)["models"].as_array()) {
            provider.models.clear();
            for (const auto& model : *models) {
              if (auto value = model.value<std::string>()) provider.models.push_back(*value);
            }
          }
          provider.max_tokens = (*entry)["max_tokens"].value_or(provider.max_tokens);
          if (provider.max_tokens < 256) provider.max_tokens = 256;
        }
      }
    }
  } catch (const toml::parse_error&) {
  }
  return providers;
}

void SaveLlmProviderApiKey(const RuntimeConfig& config, const std::string& provider,
                           const std::string& api_key) {
  if (provider.empty()) {
    throw std::runtime_error("provider is required");
  }
  if (api_key.empty()) {
    throw std::runtime_error("api_key is required");
  }

  fs::create_directories(config.runtime_root);
  const auto config_file = config.runtime_root / "config.toml";
  std::vector<std::string> lines;
  if (fs::exists(config_file)) {
    std::ifstream in(config_file);
    std::string line;
    while (std::getline(in, line)) {
      lines.push_back(line);
    }
  }

  const auto section = ProviderSection(provider);
  auto section_pos = lines.end();
  for (auto it = lines.begin(); it != lines.end(); ++it) {
    if (Trim(*it) == section) {
      section_pos = it;
      break;
    }
  }

  const std::string api_key_line = "api_key = " + TomlString(api_key);
  if (section_pos == lines.end()) {
    if (!lines.empty() && !Trim(lines.back()).empty()) {
      lines.push_back("");
    }
    lines.push_back(section);
    lines.push_back(api_key_line);
  } else {
    auto insert_pos = section_pos + 1;
    bool replaced = false;
    for (auto it = section_pos + 1; it != lines.end() && !IsSection(*it); ++it) {
      insert_pos = it + 1;
      const auto trimmed = Trim(*it);
      if (trimmed.rfind("api_key", 0) == 0) {
        *it = api_key_line;
        replaced = true;
        break;
      }
    }
    if (!replaced) {
      lines.insert(insert_pos, api_key_line);
    }
  }

  std::ofstream out(config_file, std::ios::binary | std::ios::trunc);
  if (!out) {
    throw std::runtime_error("cannot write config.toml");
  }
  for (const auto& line : lines) {
    out << line << '\n';
  }
}

void EnsureRuntimeLayout(const RuntimeConfig& config) {
  fs::create_directories(config.runtime_root);
  fs::create_directories(config.bin_dir);
  fs::create_directories(config.caches_dir);
  fs::create_directories(config.brains_dir);
  fs::create_directories(config.agents_dir);
  fs::create_directories(config.agent_runs_dir);
  fs::create_directories(config.skills_dir);
  fs::create_directories(config.logs_dir);
  EnsureDefaultAgents(config);
  EnsureDefaultSkills(config);
  WriteIfMissing(config.agents_dir / "pentest-agent" / "AGENTS.md", DefaultPentestAgent());
  WriteIfMissing(config.skills_dir / "mothprobe-skills" / "SKILL.md", DefaultMothprobeSkill());
  EnsureDefaultJsonStores(config);
}

ToolPermission PermissionConfig::PermissionFor(const std::string& tool_name) const {
  const auto it = tools.find(tool_name);
  return it == tools.end() ? ToolPermission::Ask : it->second;
}

std::string ToolPermissionName(ToolPermission permission) {
  switch (permission) {
    case ToolPermission::Allow:
      return "allow";
    case ToolPermission::Ask:
      return "ask";
    case ToolPermission::Deny:
      return "deny";
  }
  return "ask";
}

std::optional<ToolPermission> ParseToolPermission(const std::string& value) {
  std::string lowered;
  lowered.reserve(value.size());
  for (const unsigned char c : value) {
    lowered.push_back(static_cast<char>(std::tolower(c)));
  }
  if (lowered == "allow" || lowered == "allowed") return ToolPermission::Allow;
  if (lowered == "ask" || lowered == "prompt" || lowered == "approval_required") {
    return ToolPermission::Ask;
  }
  if (lowered == "deny" || lowered == "denied") return ToolPermission::Deny;
  return std::nullopt;
}

WorkspaceConfig LoadWorkspaceConfig(const RuntimeConfig& config) {
  auto workspace = DefaultWorkspaceConfig(config);
  const auto root = LoadJsonFile(config.workspace_file, nlohmann::json::object());
  if (root.is_object()) {
    workspace.readable_paths =
        PathArrayFromJson(config, root.value("readable_paths", nlohmann::json::array()),
                          workspace.readable_paths);
    workspace.writable_paths =
        PathArrayFromJson(config, root.value("writable_paths", nlohmann::json::array()),
                          workspace.writable_paths);
  }
  return workspace;
}

nlohmann::json WorkspaceConfigJson(const WorkspaceConfig& config) {
  return {{"schema", 1},
          {"project_root", config.project_root.string()},
          {"runtime_root", config.runtime_root.string()},
          {"readable_paths", PathArrayJson(config.readable_paths)},
          {"writable_paths", PathArrayJson(config.writable_paths)}};
}

PermissionConfig LoadPermissionConfig(const RuntimeConfig& config) {
  auto permissions = DefaultPermissionConfig(config);
  const auto root = LoadJsonFile(config.permissions_file, nlohmann::json::object());
  if (!root.is_object()) {
    return permissions;
  }

  if (auto tools = root.find("tools"); tools != root.end() && tools->is_object()) {
    for (const auto& [name, value] : tools->items()) {
      if (value.is_string()) {
        if (auto parsed = ParseToolPermission(value.get<std::string>())) {
          permissions.tools[name] = *parsed;
        }
      }
    }
  }
  permissions.readable_paths =
      PathArrayFromJson(config, root.value("readable_paths", nlohmann::json::array()),
                        permissions.readable_paths);
  permissions.writable_paths =
      PathArrayFromJson(config, root.value("writable_paths", nlohmann::json::array()),
                        permissions.writable_paths);
  return permissions;
}

void SavePermissionConfig(const RuntimeConfig& config, const PermissionConfig& permissions) {
  nlohmann::json tools = nlohmann::json::object();
  for (const auto& [name, permission] : permissions.tools) {
    tools[name] = ToolPermissionName(permission);
  }
  SaveJsonFileAtomic(config.permissions_file,
                     {{"schema", 1},
                      {"updated_at", TimestampIso()},
                      {"tools", std::move(tools)},
                      {"readable_paths", PathArrayJson(permissions.readable_paths)},
                      {"writable_paths", PathArrayJson(permissions.writable_paths)}});
}

std::vector<McpClientConfig> LoadMcpClientConfigs(const RuntimeConfig& config) {
  const auto root = LoadJsonFile(config.mcp_clients_file, nlohmann::json::object());
  std::vector<McpClientConfig> clients;
  if (!root.is_object()) {
    return clients;
  }
  const auto items = root.value("clients", nlohmann::json::array());
  if (!items.is_array()) {
    return clients;
  }
  for (const auto& item : items) {
    if (!item.is_object()) {
      continue;
    }
    McpClientConfig client;
    client.id = item.value("id", std::string{});
    client.name = item.value("name", client.id);
    client.transport = item.value("transport", client.transport);
    client.command = item.value("command", std::string{});
    client.url = item.value("url", std::string{});
    client.sse_endpoint = item.value("sse_endpoint", client.sse_endpoint);
    client.authenticated = item.value("authenticated", false);
    client.connected = item.value("connected", false);
    client.status = item.value("status", client.connected ? "connected" : "disconnected");
    client.created_at = item.value("created_at", std::string{});
    client.updated_at = item.value("updated_at", std::string{});
    client.last_error = item.value("last_error", std::string{});
    client.metadata = item.value("metadata", nlohmann::json::object());
    if (auto args = item.find("args"); args != item.end() && args->is_array()) {
      for (const auto& arg : *args) {
        if (arg.is_string()) client.args.push_back(arg.get<std::string>());
      }
    }
    if (auto env = item.find("env"); env != item.end() && env->is_object()) {
      for (const auto& [key, value] : env->items()) {
        if (value.is_string()) client.env[key] = value.get<std::string>();
      }
    }
    if (!client.id.empty()) {
      clients.push_back(std::move(client));
    }
  }
  return clients;
}

void SaveMcpClientConfigs(const RuntimeConfig& config,
                          const std::vector<McpClientConfig>& clients) {
  nlohmann::json items = nlohmann::json::array();
  for (const auto& client : clients) {
    items.push_back(McpClientConfigJson(client, true));
  }
  SaveJsonFileAtomic(config.mcp_clients_file,
                     {{"schema", 1}, {"updated_at", TimestampIso()}, {"clients", items}});
}

nlohmann::json McpClientConfigJson(const McpClientConfig& client, bool include_secrets) {
  nlohmann::json args = nlohmann::json::array();
  for (const auto& arg : client.args) {
    args.push_back(arg);
  }
  nlohmann::json env = nlohmann::json::object();
  for (const auto& [key, value] : client.env) {
    env[key] = include_secrets ? value : (value.empty() ? "" : "***");
  }
  return {{"id", client.id},
          {"name", client.name},
          {"transport", client.transport},
          {"command", client.command},
          {"args", args},
          {"env", env},
          {"url", client.url},
          {"sse_endpoint", client.sse_endpoint},
          {"authenticated", client.authenticated},
          {"connected", client.connected},
          {"status", client.status},
          {"created_at", client.created_at},
          {"updated_at", client.updated_at},
          {"last_error", client.last_error},
          {"metadata", client.metadata}};
}

std::string ReadTextFileLimited(const fs::path& path, std::size_t max_bytes) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    throw std::runtime_error("cannot open file: " + path.string());
  }
  std::ostringstream out;
  char buffer[512];
  std::size_t total = 0;
  while (in && total < max_bytes) {
    const std::size_t want = std::min(sizeof(buffer), max_bytes - total);
    in.read(buffer, static_cast<std::streamsize>(want));
    const auto got = static_cast<std::size_t>(in.gcount());
    out.write(buffer, static_cast<std::streamsize>(got));
    total += got;
  }
  return out.str();
}

}  // namespace mothprobe::core
