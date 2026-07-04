#pragma once

#include <string>

namespace mothprobe::core {

enum class ShellPermissionLevel {
  Default,
  Full,
};

bool ShellCommandAllowed(const std::string& command);
bool ShellCommandAllowed(const std::string& command, ShellPermissionLevel level);
std::string RunShellCommand(const std::string& command, int* exit_code);
std::string RunShellCommand(const std::string& command, int* exit_code,
                            ShellPermissionLevel level);

}  // namespace mothprobe::core
