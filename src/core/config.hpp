#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace mothprobe::core {

struct RuntimeConfig {
  std::filesystem::path project_root;
  std::filesystem::path runtime_root;
  std::filesystem::path bin_dir;
  std::filesystem::path caches_dir;
  std::filesystem::path brains_dir;
  std::filesystem::path agents_dir;
  std::filesystem::path agent_runs_dir;
  std::filesystem::path skills_dir;
  std::filesystem::path logs_dir;
  std::filesystem::path audit_file;
  std::filesystem::path workspace_file;
  std::filesystem::path permissions_file;
  std::filesystem::path mcp_clients_file;
};

struct LlmProviderConfig {
  std::string provider;
  std::string api_key;
  std::string base_url;
  std::string chat_path = "/chat/completions";
  std::string models_path = "/models";
  std::string model_name;
  std::vector<std::string> models;
  int max_tokens = 4096;
  bool requires_api_key = true;
  bool openai_compatible = true;
};

struct WorkspaceConfig {
  std::filesystem::path project_root;
  std::filesystem::path runtime_root;
  std::vector<std::filesystem::path> readable_paths;
  std::vector<std::filesystem::path> writable_paths;
};

enum class ToolPermission {
  Allow,
  Ask,
  Deny,
};

struct PermissionConfig {
  std::map<std::string, ToolPermission> tools;
  std::vector<std::filesystem::path> readable_paths;
  std::vector<std::filesystem::path> writable_paths;

  ToolPermission PermissionFor(const std::string& tool_name) const;
};

struct McpClientConfig {
  std::string id;
  std::string name;
  std::string transport = "stdio";
  std::string command;
  std::vector<std::string> args;
  std::map<std::string, std::string> env;
  std::string url;
  std::string sse_endpoint = "/sse";
  bool authenticated = false;
  bool connected = false;
  std::string status = "disconnected";
  std::string created_at;
  std::string updated_at;
  std::string last_error;
  nlohmann::json metadata = nlohmann::json::object();
};

RuntimeConfig LoadRuntimeConfig();
std::map<std::string, LlmProviderConfig> LoadLlmProviderConfigs(const RuntimeConfig& config);
void SaveLlmProviderApiKey(const RuntimeConfig& config, const std::string& provider,
                           const std::string& api_key);
void EnsureRuntimeLayout(const RuntimeConfig& config);
WorkspaceConfig LoadWorkspaceConfig(const RuntimeConfig& config);
nlohmann::json WorkspaceConfigJson(const WorkspaceConfig& config);
PermissionConfig LoadPermissionConfig(const RuntimeConfig& config);
void SavePermissionConfig(const RuntimeConfig& config, const PermissionConfig& permissions);
std::string ToolPermissionName(ToolPermission permission);
std::optional<ToolPermission> ParseToolPermission(const std::string& value);
std::vector<McpClientConfig> LoadMcpClientConfigs(const RuntimeConfig& config);
void SaveMcpClientConfigs(const RuntimeConfig& config,
                          const std::vector<McpClientConfig>& clients);
nlohmann::json McpClientConfigJson(const McpClientConfig& client, bool include_secrets = false);
std::filesystem::path FindProjectRoot();
std::string ReadTextFileLimited(const std::filesystem::path& path, std::size_t max_bytes);

}  // namespace mothprobe::core
