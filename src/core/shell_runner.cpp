#include "core/shell_runner.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <regex>
#include <sstream>

namespace mothprobe::core {
namespace {

constexpr std::size_t kMaxShellOutput = 12000;

}  // namespace

bool ShellCommandAllowed(const std::string& command) {
  return ShellCommandAllowed(command, ShellPermissionLevel::Default);
}

bool ShellCommandAllowed(const std::string& command, ShellPermissionLevel level) {
  if (command.empty() || command.size() > 500 ||
      command.find('\0') != std::string::npos ||
      command.find('\n') != std::string::npos ||
      command.find('\r') != std::string::npos) {
    return false;
  }
  if (level == ShellPermissionLevel::Full) {
    return true;
  }
  static const std::regex unsafe(R"([&|;<>()`])");
  if (command.size() > 200 || std::regex_search(command, unsafe)) {
    return false;
  }
  std::istringstream in(command);
  std::string verb;
  in >> verb;
  std::transform(verb.begin(), verb.end(), verb.begin(), ::tolower);
  return verb == "dir" || verb == "ls" || verb == "echo" || verb == "whoami" ||
         verb == "hostname" || verb == "ver";
}

std::string RunShellCommand(const std::string& command, int* exit_code) {
  return RunShellCommand(command, exit_code, ShellPermissionLevel::Default);
}

std::string RunShellCommand(const std::string& command, int* exit_code,
                            ShellPermissionLevel level) {
  *exit_code = -1;
  if (!ShellCommandAllowed(command, level)) {
    return "Rejected by shell policy.";
  }
#ifdef _WIN32
  SECURITY_ATTRIBUTES sa{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
  HANDLE read_pipe = nullptr;
  HANDLE write_pipe = nullptr;
  if (!CreatePipe(&read_pipe, &write_pipe, &sa, 0)) {
    return "Failed to create shell pipe.";
  }
  SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);
  std::string actual = command.rfind("ls", 0) == 0 ? "dir" + command.substr(2) : command;
  std::string cmd = "cmd.exe /C " + actual;
  STARTUPINFOA si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdOutput = write_pipe;
  si.hStdError = write_pipe;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  PROCESS_INFORMATION pi{};
  if (!CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                      nullptr, &si, &pi)) {
    CloseHandle(read_pipe);
    CloseHandle(write_pipe);
    return "Failed to start shell process.";
  }
  CloseHandle(write_pipe);
  std::string output;
  char buffer[256];
  DWORD read = 0;
  while (ReadFile(read_pipe, buffer, sizeof(buffer), &read, nullptr) && read > 0 &&
         output.size() < kMaxShellOutput) {
    output.append(buffer, buffer + read);
  }
  WaitForSingleObject(pi.hProcess, 3000);
  DWORD code = 0;
  GetExitCodeProcess(pi.hProcess, &code);
  *exit_code = static_cast<int>(code);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  CloseHandle(read_pipe);
  return output.empty() ? "Command completed with no output." : output;
#else
  int pipefd[2];
  if (pipe(pipefd) != 0) {
    return "Failed to create shell pipe.";
  }
  pid_t pid = fork();
  if (pid == 0) {
    dup2(pipefd[1], STDOUT_FILENO);
    dup2(pipefd[1], STDERR_FILENO);
    close(pipefd[0]);
    close(pipefd[1]);
    execlp("sh", "sh", "-c", command.c_str(), nullptr);
    _exit(127);
  }
  close(pipefd[1]);
  std::string output;
  char buffer[256];
  ssize_t n = 0;
  while ((n = read(pipefd[0], buffer, sizeof(buffer))) > 0 && output.size() < kMaxShellOutput) {
    output.append(buffer, buffer + n);
  }
  close(pipefd[0]);
  int status = 0;
  waitpid(pid, &status, 0);
  *exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  return output.empty() ? "Command completed with no output." : output;
#endif
}

}  // namespace mothprobe::core
