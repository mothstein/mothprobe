#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

#ifdef _WIN32
namespace {

class Handle {
 public:
  ~Handle() { Reset(); }
  HANDLE* Put() {
    Reset();
    return &handle_;
  }
  HANDLE Get() const { return handle_; }
  bool Valid() const { return handle_ && handle_ != INVALID_HANDLE_VALUE; }
  void Reset() {
    if (Valid()) {
      CloseHandle(handle_);
    }
    handle_ = nullptr;
  }
  Handle(Handle&& other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}
  Handle& operator=(Handle&& other) noexcept {
    Reset();
    handle_ = std::exchange(other.handle_, nullptr);
    return *this;
  }
  Handle() = default;
  Handle(const Handle&) = delete;
  Handle& operator=(const Handle&) = delete;

 private:
  HANDLE handle_ = nullptr;
};

fs::path ExeDir() {
  std::vector<char> buffer(MAX_PATH);
  DWORD size = GetModuleFileNameA(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
  return fs::path(std::string(buffer.data(), size)).parent_path();
}

class Daemon {
 public:
  void Start() {
    const fs::path exe = ExeDir() / "mothprobe_mcp.exe";
    REQUIRE(fs::exists(exe));
    SECURITY_ATTRIBUTES sa{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    Handle stdin_read, stdout_write;
    REQUIRE(CreatePipe(stdin_read.Put(), in_.Put(), &sa, 0));
    REQUIRE(CreatePipe(out_.Put(), stdout_write.Put(), &sa, 0));
    SetHandleInformation(in_.Get(), HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(out_.Get(), HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = stdin_read.Get();
    si.hStdOutput = stdout_write.Get();
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    std::string cmd = "\"" + exe.string() + "\"";
    PROCESS_INFORMATION pi{};
    REQUIRE(CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                           ExeDir().string().c_str(), &si, &pi));
    *process_.Put() = pi.hProcess;
    *thread_.Put() = pi.hThread;
  }

  ~Daemon() {
    in_.Reset();
    if (process_.Valid()) {
      WaitForSingleObject(process_.Get(), 250);
      DWORD code = 0;
      if (GetExitCodeProcess(process_.Get(), &code) && code == STILL_ACTIVE) {
        TerminateProcess(process_.Get(), 0);
      }
    }
  }

  json Request(const json& value) {
    const std::string payload = value.dump() + "\n";
    DWORD written = 0;
    REQUIRE(WriteFile(in_.Get(), payload.data(), static_cast<DWORD>(payload.size()), &written,
                      nullptr));
    std::string line;
    char c = '\0';
    DWORD read = 0;
    while (ReadFile(out_.Get(), &c, 1, &read, nullptr) && read == 1) {
      if (c == '\n') {
        break;
      }
      line.push_back(c);
    }
    REQUIRE_FALSE(line.empty());
    return json::parse(line);
  }

 private:
  Handle process_;
  Handle thread_;
  Handle in_;
  Handle out_;
};

}  // namespace

TEST_CASE("mothprobe_mcp responds to initialize and tools list", "[mcp]") {
  Daemon daemon;
  daemon.Start();
  auto init = daemon.Request({{"jsonrpc", "2.0"}, {"id", 1}, {"method", "initialize"}});
  REQUIRE(init["id"] == 1);
  REQUIRE(init["result"]["serverInfo"]["name"] == "mothprobe_mcp");

  auto tools = daemon.Request({{"jsonrpc", "2.0"}, {"id", 2}, {"method", "tools/list"}});
  REQUIRE(tools["result"]["tools"].is_array());
  REQUIRE(tools["result"]["tools"][0]["name"] == "scan_tcp");
  REQUIRE(tools["result"]["tools"][1]["name"] == "lookup_dns");
}
#endif
