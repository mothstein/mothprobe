#pragma once

#include <filesystem>
#include <string>

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

RuntimeConfig LoadRuntimeConfig();
void EnsureRuntimeLayout(const RuntimeConfig& config);
std::filesystem::path FindProjectRoot();
std::string ReadTextFileLimited(const std::filesystem::path& path, std::size_t max_bytes);

}  // namespace mothprobe::core
