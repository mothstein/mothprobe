#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include <spdlog/logger.h>

namespace mothprobe::core {

std::shared_ptr<spdlog::logger> CreateFileLogger(const std::string& name,
                                                 const std::filesystem::path& file);
std::shared_ptr<spdlog::logger> CreateStderrFileLogger(const std::string& name,
                                                       const std::filesystem::path& file);

}  // namespace mothprobe::core
