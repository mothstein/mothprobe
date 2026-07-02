#pragma once

#include <string>

namespace mothprobe::core {

bool ShellCommandAllowed(const std::string& command);
std::string RunShellCommand(const std::string& command, int* exit_code);

}  // namespace mothprobe::core
