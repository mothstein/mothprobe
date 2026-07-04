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

  auto workspace =
      daemon.Request({{"jsonrpc", "2.0"}, {"id", 60}, {"method", "workspace/inspect"}});
  REQUIRE(workspace["result"]["workspace"]["runtime"]["agents_dir"].is_string());
  REQUIRE(workspace["result"]["workspace"]["permissions"]["tools"].is_object());

  auto agents = daemon.Request({{"jsonrpc", "2.0"}, {"id", 61}, {"method", "agents/list"}});
  REQUIRE(agents["result"]["agents"].is_array());
  REQUIRE_FALSE(agents["result"]["agents"].empty());

  auto code_writer =
      daemon.Request({{"jsonrpc", "2.0"},
                      {"id", 62},
                      {"method", "agents/get"},
                      {"params", {{"name", "code_writer"}}}});
  REQUIRE(code_writer["result"]["agent"]["name"] == "code_writer");

  const auto smoke_agent_name = "smoke_agent_" + std::to_string(GetCurrentProcessId());
  auto created_agent =
      daemon.Request({{"jsonrpc", "2.0"},
                      {"id", 63},
                      {"method", "agents/create"},
                      {"params",
                       {{"name", smoke_agent_name},
                        {"description", "Smoke test agent"},
                        {"system_prompt", "You are a smoke test agent."}}}});
  REQUIRE(created_agent["result"]["agent"]["name"] == smoke_agent_name);

  auto permissions =
      daemon.Request({{"jsonrpc", "2.0"}, {"id", 64}, {"method", "permission/get"}});
  REQUIRE(permissions["result"]["permissions"]["tools"].is_object());

  auto permission_set =
      daemon.Request({{"jsonrpc", "2.0"},
                      {"id", 65},
                      {"method", "permission/set"},
                      {"params", {{"tool", "run_command"}, {"permission", "ask"}}}});
  REQUIRE(permission_set["result"]["permissions"]["tools"]["run_command"] == "ask");

  const auto client_name = "smoke-client-" + std::to_string(GetCurrentProcessId());
  auto client_add =
      daemon.Request({{"jsonrpc", "2.0"},
                      {"id", 66},
                      {"method", "mcp_clients/add"},
                      {"params",
                       {{"name", client_name},
                        {"transport", "stdio"},
                        {"command", "mothprobe_mcp"}}}});
  REQUIRE(client_add["result"]["client"]["name"] == client_name);

  auto client_get =
      daemon.Request({{"jsonrpc", "2.0"},
                      {"id", 67},
                      {"method", "mcp_clients/get"},
                      {"params", {{"name", client_name}}}});
  REQUIRE(client_get["result"]["client"]["name"] == client_name);

  auto client_login =
      daemon.Request({{"jsonrpc", "2.0"},
                      {"id", 68},
                      {"method", "mcp_clients/login"},
                      {"params", {{"name", client_name}, {"token", "test-token"}}}});
  REQUIRE(client_login["result"]["client"]["authenticated"] == true);

  auto client_connect =
      daemon.Request({{"jsonrpc", "2.0"},
                      {"id", 69},
                      {"method", "mcp_clients/connect"},
                      {"params", {{"name", client_name}}}});
  REQUIRE(client_connect["result"]["client"]["connected"] == true);

  auto client_disconnect =
      daemon.Request({{"jsonrpc", "2.0"},
                      {"id", 70},
                      {"method", "mcp_clients/disconnect"},
                      {"params", {{"name", client_name}}}});
  REQUIRE(client_disconnect["result"]["client"]["connected"] == false);

  auto client_logout =
      daemon.Request({{"jsonrpc", "2.0"},
                      {"id", 71},
                      {"method", "mcp_clients/logout"},
                      {"params", {{"name", client_name}}}});
  REQUIRE(client_logout["result"]["client"]["authenticated"] == false);

  auto client_remove =
      daemon.Request({{"jsonrpc", "2.0"},
                      {"id", 72},
                      {"method", "mcp_clients/remove"},
                      {"params", {{"name", client_name}}}});
  REQUIRE(client_remove["result"]["removed"] == client_name);

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

  auto session =
      daemon.Request({{"jsonrpc", "2.0"}, {"id", 9}, {"method", "chat/new_session"}});
  REQUIRE(session["result"]["session"]["session_id"].is_string());
  REQUIRE(session["result"]["session"]["messages"].is_array());

  auto reasoning =
      daemon.Request({{"jsonrpc", "2.0"},
                      {"id", 10},
                      {"method", "llm/set_reasoning"},
                      {"params", {{"mode", "advanced"}}}});
  REQUIRE(reasoning["result"]["reasoning_mode"] == "advanced");

  auto sessions =
      daemon.Request({{"jsonrpc", "2.0"}, {"id", 11}, {"method", "chat/list_sessions"}});
  REQUIRE(sessions["result"]["sessions"].is_array());
  REQUIRE_FALSE(sessions["result"]["sessions"].empty());

  const auto session_id = session["result"]["session"]["session_id"].get<std::string>();
  auto loaded =
      daemon.Request({{"jsonrpc", "2.0"},
                      {"id", 12},
                      {"method", "chat/load_session"},
                      {"params", {{"session_id", session_id}}}});
  REQUIRE(loaded["result"]["session"]["session_id"] == session_id);

  auto cleared =
      daemon.Request({{"jsonrpc", "2.0"}, {"id", 13}, {"method", "chat/clear"}});
  REQUIRE(cleared["result"]["session"]["messages"].empty());

  auto workspace2 =
      daemon.Request({{"jsonrpc", "2.0"}, {"id", 14}, {"method", "workspace/inspect"}});
  REQUIRE(workspace2["result"]["workspace_path"].is_string());
  REQUIRE(workspace2["result"]["agents"].is_array());
  REQUIRE(workspace2["result"]["skills"].is_array());

  auto agents2 = daemon.Request({{"jsonrpc", "2.0"}, {"id", 15}, {"method", "agents/list"}});
  REQUIRE(agents2["result"]["agents"].is_array());
  const auto has_pentest_agent =
      std::any_of(agents2["result"]["agents"].begin(), agents2["result"]["agents"].end(),
                  [](const auto& agent) { return agent["name"] == "pentest-agent"; });
  REQUIRE(has_pentest_agent);

  auto selected =
      daemon.Request({{"jsonrpc", "2.0"},
                      {"id", 16},
                      {"method", "agents/select"},
                      {"params", {{"name", "pentest-agent"}}}});
  REQUIRE(selected["result"]["agent"]["name"] == "pentest-agent");

  auto permission =
      daemon.Request({{"jsonrpc", "2.0"},
                      {"id", 17},
                      {"method", "permission/set"},
                      {"params", {{"level", "full"}}}});
  REQUIRE(permission["result"]["permission_level"] == "full");

  auto shell =
      daemon.Request({{"jsonrpc", "2.0"},
                      {"id", 18},
                      {"method", "tools/call"},
                      {"params", {{"name", "run_command"},
                                  {"arguments", {{"command", "echo mothprobe"}}}}}});
  REQUIRE(shell["result"]["isError"] == false);

  const auto client_name2 = "smoke-local";
  auto removed_existing =
      daemon.Request({{"jsonrpc", "2.0"},
                      {"id", 19},
                      {"method", "mcp_clients/remove"},
                      {"params", {{"name", client_name2}}}});
  (void)removed_existing;

  auto client_added =
      daemon.Request({{"jsonrpc", "2.0"},
                      {"id", 20},
                      {"method", "mcp_clients/add"},
                      {"params", {{"name", client_name2},
                                  {"transport", "stdio"},
                                  {"command", "node"},
                                  {"args", nlohmann::json::array({"server.js"})}}}});
  REQUIRE(client_added["result"]["client"]["name"] == client_name2);

  auto clients =
      daemon.Request({{"jsonrpc", "2.0"}, {"id", 21}, {"method", "mcp_clients/list"}});
  REQUIRE(clients["result"]["clients"].is_array());

  auto client_removed =
      daemon.Request({{"jsonrpc", "2.0"},
                      {"id", 22},
                      {"method", "mcp_clients/remove"},
                      {"params", {{"name", client_name2}}}});
  REQUIRE(client_removed["result"]["removed"] == client_name2);
}
#endif
