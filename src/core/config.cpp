#include "core/config.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace mothprobe::core {

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
  config.runtime_root = config.project_root / "data" / ".mothprobe";
  config.bin_dir = config.runtime_root / "bin";
  config.caches_dir = config.runtime_root / "caches";
  config.brains_dir = config.runtime_root / "brains";
  config.logs_dir = config.runtime_root / "logs";
  config.audit_file = config.runtime_root / "audit.jsonl";
  return config;
}

void EnsureRuntimeLayout(const RuntimeConfig& config) {
  fs::create_directories(config.bin_dir);
  fs::create_directories(config.caches_dir);
  fs::create_directories(config.brains_dir);
  fs::create_directories(config.logs_dir);
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
