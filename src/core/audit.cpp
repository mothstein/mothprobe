#include "core/audit.hpp"

#include <array>
#include <chrono>
#include <ctime>

#include <sodium.h>

#include "core/logger.hpp"

namespace mothprobe::core {

AuditLogger::AuditLogger(const std::filesystem::path& file)
    : logger_(CreateFileLogger("mothprobe-audit", file)) {
  if (sodium_init() < 0) {
    logger_->error(nlohmann::json{{"event", "sodium_init_failed"}}.dump());
  }
}

void AuditLogger::Event(const std::string& name, const nlohmann::json& payload) {
  logger_->info(nlohmann::json{{"ts", Timestamp()}, {"event", name}, {"payload", payload}}.dump());
}

void AuditLogger::UserInput(const std::string& mode, const std::string& text) {
  Event("user_input", {{"mode", mode}, {"text", text}});
}

void AuditLogger::ShellCommand(const std::string& command, int exit_code) {
  Event("shell_command", {{"command", command}, {"exit_code", exit_code}});
}

void AuditLogger::FileAttachment(const std::filesystem::path& path, bool accepted) {
  Event("file_attachment", {{"path_hash", HashPath(path)}, {"accepted", accepted}});
}

std::string AuditLogger::Timestamp() {
  const auto now = std::chrono::system_clock::now();
  const auto time = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &time);
#else
  localtime_r(&time, &tm);
#endif
  char buffer[32]{};
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S%z", &tm);
  return buffer;
}

std::string AuditLogger::HashPath(const std::filesystem::path& path) {
  const std::string input = path.string();
  std::array<unsigned char, crypto_generichash_BYTES> hash{};
  crypto_generichash(hash.data(), hash.size(),
                     reinterpret_cast<const unsigned char*>(input.data()), input.size(), nullptr,
                     0);
  std::string hex((hash.size() * 2) + 1, '\0');
  sodium_bin2hex(hex.data(), hex.size(), hash.data(), hash.size());
  hex.pop_back();
  return hex;
}

}  // namespace mothprobe::core
