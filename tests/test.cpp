#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <filesystem>
#include <algorithm>
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

  HANDLE Stdin() const { return in_.Get(); }

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
  auto ping = daemon.Request({{"jsonrpc", "2.0"}, {"id", 0}, {"method", "ping"}});
  REQUIRE(ping["result"].is_object());

  auto init = daemon.Request({{"jsonrpc", "2.0"},
                              {"id", 1},
                              {"method", "initialize"},
                              {"params",
                               {{"protocolVersion", "2025-11-25"},
                                {"capabilities", nlohmann::json::object()},
                                {"clientInfo",
                                 {{"name", "mothprobe-smoke-test"}, {"version", "0.1.0"}}}}}});
  REQUIRE(init["id"] == 1);
  REQUIRE(init["result"]["serverInfo"]["name"] == "mothprobe_mcp");
  REQUIRE(init["result"]["protocolVersion"] == "2025-11-25");
  REQUIRE(init["result"]["capabilities"]["tools"]["listChanged"] == true);
  REQUIRE(init["result"]["capabilities"]["resources"]["listChanged"] == true);
  REQUIRE(init["result"]["capabilities"]["prompts"]["listChanged"] == true);

  const std::string ready = json({{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}}).dump() + "\n";
  DWORD written = 0;
  REQUIRE(WriteFile(daemon.Stdin(), ready.data(), static_cast<DWORD>(ready.size()), &written,
                    nullptr));

  auto tools = daemon.Request({{"jsonrpc", "2.0"}, {"id", 2}, {"method", "tools/list"}});
  REQUIRE(tools["result"]["tools"].is_array());
  std::vector<std::string> tool_names;
  for (const auto& tool : tools["result"]["tools"]) {
    tool_names.push_back(tool["name"].get<std::string>());
  }
  REQUIRE(std::find(tool_names.begin(), tool_names.end(), "lookup_dns") != tool_names.end());
  REQUIRE(std::find(tool_names.begin(), tool_names.end(), "detect_headers") != tool_names.end());
  REQUIRE(std::find(tool_names.begin(), tool_names.end(), "report_export") != tool_names.end());

  auto providers =
      daemon.Request({{"jsonrpc", "2.0"}, {"id", 3}, {"method", "llm/list_providers"}});
  REQUIRE(providers["result"]["providers"].is_array());
  REQUIRE_FALSE(providers["result"]["providers"].empty());
  for (const auto& provider : providers["result"]["providers"]) {
    REQUIRE(provider["name"].is_string());
    REQUIRE(provider["configured"].is_boolean());
    REQUIRE(provider["current_model"].is_string());
    REQUIRE(provider["models"].is_array());
  }

  auto dns = daemon.Request({{"jsonrpc", "2.0"},
                             {"id", 4},
                             {"method", "tools/call"},
                             {"params", {{"name", "lookup_dns"},
                                         {"arguments", {{"target", "localhost"}}}}}});
  REQUIRE(dns["result"]["isError"] == false);
  REQUIRE(dns["result"]["content"][0]["type"] == "text");
  REQUIRE(dns["result"]["structuredContent"]["host"] == "localhost");
  REQUIRE(dns["result"]["structuredContent"]["addresses"].is_array());

  auto resources =
      daemon.Request({{"jsonrpc", "2.0"}, {"id", 5}, {"method", "resources/list"}});
  REQUIRE(resources["result"]["resources"].is_array());
  REQUIRE_FALSE(resources["result"]["resources"].empty());

  auto resource_templates =
      daemon.Request({{"jsonrpc", "2.0"}, {"id", 50}, {"method", "resources/templates/list"}});
  REQUIRE(resource_templates["result"]["resourceTemplates"].is_array());

  auto prompts = daemon.Request({{"jsonrpc", "2.0"}, {"id", 6}, {"method", "prompts/list"}});
  REQUIRE(prompts["result"]["prompts"].is_array());
  REQUIRE_FALSE(prompts["result"]["prompts"].empty());

  auto model_error =
      daemon.Request({{"jsonrpc", "2.0"},
                      {"id", 7},
                      {"method", "llm/fetch_models"},
                      {"params", {{"provider", "missing-provider"}}}});
  REQUIRE(model_error["id"] == 7);
  REQUIRE(model_error["error"]["code"] == -32000);
  REQUIRE(model_error["error"]["data"]["provider"] == "missing-provider");

  auto configure =
      daemon.Request({{"jsonrpc", "2.0"},
                      {"id", 8},
                      {"method", "llm/configure_provider"},
                      {"params", {{"provider", "missing-provider"}, {"api_key", "test-key"}}}});
  REQUIRE(configure["id"] == 8);
  REQUIRE(configure["error"]["code"] == -32602);
}
#endif
