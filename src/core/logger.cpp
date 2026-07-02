#include "core/logger.hpp"

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace mothprobe::core {

std::shared_ptr<spdlog::logger> CreateFileLogger(const std::string& name,
                                                 const std::filesystem::path& file) {
  auto logger = spdlog::basic_logger_mt(name, file.string(), true);
  logger->set_pattern("%v");
  logger->flush_on(spdlog::level::info);
  return logger;
}

std::shared_ptr<spdlog::logger> CreateStderrFileLogger(const std::string& name,
                                                       const std::filesystem::path& file) {
  auto err = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
  auto disk = std::make_shared<spdlog::sinks::basic_file_sink_mt>(file.string(), true);
  auto logger = std::make_shared<spdlog::logger>(name, spdlog::sinks_init_list{err, disk});
  logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
  logger->flush_on(spdlog::level::info);
  spdlog::register_logger(logger);
  return logger;
}

}  // namespace mothprobe::core
