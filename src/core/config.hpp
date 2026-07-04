#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace mothprobe::core {

struct RuntimeConfig {
  std::filesystem::path project_root;
  std::filesystem::path runtime_root;
  std::filesystem::path bin_dir;
  std::filesystem::path caches_dir;
  std::filesystem::path brains_dir;
  std::filesystem::path logs_dir;
  std::filesystem::path audit_file;
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

RuntimeConfig LoadRuntimeConfig();
std::map<std::string, LlmProviderConfig> LoadLlmProviderConfigs(const RuntimeConfig& config);
void SaveLlmProviderApiKey(const RuntimeConfig& config, const std::string& provider,
                           const std::string& api_key);
void EnsureRuntimeLayout(const RuntimeConfig& config);
std::filesystem::path FindProjectRoot();
std::string ReadTextFileLimited(const std::filesystem::path& path, std::size_t max_bytes);

}  // namespace mothprobe::core
