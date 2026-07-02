#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include <nlohmann/json.hpp>
#include <spdlog/logger.h>

namespace mothprobe::core {

class AuditLogger {
 public:
  explicit AuditLogger(const std::filesystem::path& file);

  void Event(const std::string& name, const nlohmann::json& payload);
  void UserInput(const std::string& mode, const std::string& text);
  void ShellCommand(const std::string& command, int exit_code);
  void FileAttachment(const std::filesystem::path& path, bool accepted);

 private:
  static std::string Timestamp();
  static std::string HashPath(const std::filesystem::path& path);

  std::shared_ptr<spdlog::logger> logger_;
};

}  // namespace mothprobe::core
