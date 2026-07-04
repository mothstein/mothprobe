#include "mcp/server.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <system_error>

#include "mcp/json_rpc.hpp"
#include "mcp/llm/provider_factory.hpp"
#include "mothon/protocol/upstream.hpp"
#include "mothprobe/version.hpp"

namespace mothprobe::mcp {
namespace {

nlohmann::json TextToolResult(const std::string& text, bool is_error = false,
                              nlohmann::json structured = nlohmann::json::object()) {
  nlohmann::json result{{"content", nlohmann::json::array({{{"type", "text"}, {"text", text}}})},
                        {"isError", is_error}};
  if (!structured.empty()) result["structuredContent"] = std::move(structured);
  return result;
}

bool IsNotification(const nlohmann::json& request) {
  return request.is_object() && !request.contains("id");
}

mothon::tools::PermissionLevel PermissionFromString(const std::string& value) {
  return value == "full" ? mothon::tools::PermissionLevel::Full
                         : mothon::tools::PermissionLevel::Default;
}

std::string TimestampId(const std::string& prefix) {
  const auto now = std::chrono::system_clock::now();
  const auto time = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
#ifdef _WIN32
  gmtime_s(&tm, &time);
#else
  gmtime_r(&time, &tm);
#endif
  std::ostringstream out;
  out << prefix << '-'
      << std::put_time(&tm, "%Y%m%d%H%M%S") << '-'
      << std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
  return out.str();
}

std::string TimestampIso() {
  const auto now = std::chrono::system_clock::now();
  const auto time = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
#ifdef _WIN32
  gmtime_s(&tm, &time);
#else
  gmtime_r(&time, &tm);
#endif
  std::ostringstream out;
  out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
  return out.str();
}

class ScopedTempPath {
 public:
  explicit ScopedTempPath(std::filesystem::path path) : path_(std::move(path)) {}
  ~ScopedTempPath() {
    if (active_) {
      std::error_code ignored;
      std::filesystem::remove(path_, ignored);
    }
  }
  const std::filesystem::path& path() const { return path_; }
  void Release() { active_ = false; }

 private:
  std::filesystem::path path_;
  bool active_ = true;
};

nlohmann::json LoadJsonFile(const std::filesystem::path& path) {
  if (!std::filesystem::exists(path)) {
    return nlohmann::json::object();
  }
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return nlohmann::json::object();
  }
  auto json = nlohmann::json::parse(in, nullptr, false);
  return json.is_discarded() ? nlohmann::json::object() : json;
}

void SaveJsonFile(const std::filesystem::path& path, const nlohmann::json& value) {
  std::filesystem::create_directories(path.parent_path());
  ScopedTempPath temp(path.string() + ".tmp");
  {
    std::ofstream out(temp.path(), std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("cannot write " + path.string());
    out << value.dump(2) << '\n';
  }
  std::error_code ec;
  std::filesystem::rename(temp.path(), path, ec);
  if (ec) {
    std::filesystem::remove(path, ec);
    ec.clear();
    std::filesystem::rename(temp.path(), path, ec);
  }
  if (ec) throw std::runtime_error("cannot replace " + path.string());
  temp.Release();
}

nlohmann::json PathArrayJson(const std::vector<std::filesystem::path>& paths) {
  nlohmann::json out = nlohmann::json::array();
  for (const auto& path : paths) {
    out.push_back(path.string());
  }
  return out;
}

nlohmann::json PermissionConfigJson(const core::PermissionConfig& permissions) {
  nlohmann::json tools = nlohmann::json::object();
  for (const auto& [name, permission] : permissions.tools) {
    tools[name] = core::ToolPermissionName(permission);
  }
  return {{"tools", tools},
          {"readable_paths", PathArrayJson(permissions.readable_paths)},
          {"writable_paths", PathArrayJson(permissions.writable_paths)}};
}

std::string NameParam(const nlohmann::json& params) {
  auto name = params.value("name", std::string{});
  if (name.empty()) name = params.value("id", std::string{});
  return name;
}

}  // namespace

Server::Server(core::RuntimeConfig config, std::shared_ptr<spdlog::logger> logger)
    : config_(std::move(config)),
      logger_(std::move(logger)),
      audit_(config_.audit_file),
      chat_memory_(config_.brains_dir, config_.project_root),
      agent_registry_(config_),
      mcp_clients_(config_),
      llm_providers_(core::LoadLlmProviderConfigs(config_)),
      tool_registry_(config_) {}

nlohmann::json Server::Dispatch(const nlohmann::json& request) {
  if (request.is_object() && !request.contains("method") &&
      (request.contains("result") || request.contains("error"))) {
    return {};
  }
  if (!request.is_object() || request.value("jsonrpc", "") != "2.0" ||
      !request.contains("method")) {
    return Error(RequestId(request), -32600, "Invalid JSON-RPC request");
  }

  const auto method = request["method"].get<std::string>();
  const bool notification = IsNotification(request);
  if (method == "ping") {
    return notification ? nlohmann::json{} : Result(RequestId(request), nlohmann::json::object());
  }
  if (method == "initialize") {
    return Initialize(request);
  }
  if (method == "notifications/initialized") {
    ready_ = true;
    logger_->info("client initialized");
    return {};
  }
  if (method == "notifications/cancelled") {
    logger_->info("client cancellation notification received");
    return {};
  }
  if (!initialized_) {
    return notification ? nlohmann::json{}
                        : Error(RequestId(request), -32002,
                                "MCP server is not initialized. Send initialize first.");
  }
  if (method == "tools/list") {
    return ToolsList(request);
  }
  if (method == "tools/call") {
    return ToolsCall(request);
  }
  if (method == "resources/list") {
    return ResourcesList(request);
  }
  if (method == "resources/read") {
    return ResourcesRead(request);
  }
  if (method == "resources/templates/list") {
    return ResourceTemplatesList(request);
  }
  if (method == "prompts/list") {
    return PromptsList(request);
  }
  if (method == "prompts/get") {
    return PromptsGet(request);
  }
  if (method == "llm/list_providers") {
    return LlmListProviders(request);
  }
  if (method == "llm/fetch_models") {
    return LlmFetchModels(request);
  }
  if (method == "llm/configure_provider") {
    return LlmConfigureProvider(request);
  }
  if (method == "llm/set_reasoning") {
    return LlmSetReasoning(request);
  }
  if (method == "llm/chat") {
    return LlmChat(request);
  }
  if (method == "chat/list_sessions") {
    return ChatListSessions(request);
  }
  if (method == "chat/load_session") {
    return ChatLoadSession(request);
  }
  if (method == "chat/new_session") {
    return ChatNewSession(request);
  }
  if (method == "chat/clear") {
    return ChatClear(request);
  }
  if (method == "workspace/inspect") {
    return WorkspaceInspect(request);
  }
  if (method == "agents/list") {
    return AgentsList(request);
  }
  if (method == "agents/get") {
    return AgentsGet(request);
  }
  if (method == "agents/create") {
    return AgentsCreate(request);
  }
  if (method == "agents/select") {
    return AgentsSelect(request);
  }
  if (method == "agents/run") {
    return AgentsRun(request);
  }
  if (method == "agents/cancel") {
    return AgentsCancel(request);
  }
  if (method == "permission/get") {
    return PermissionGet(request);
  }
  if (method == "permission/set") {
    return PermissionSet(request);
  }
  if (method.rfind("mcp_clients/", 0) == 0) {
    return McpClients(request, method.substr(std::string("mcp_clients/").size()));
  }
  return notification ? nlohmann::json{} : Error(RequestId(request), -32601, "Method not found");
}

nlohmann::json Server::Initialize(const nlohmann::json& request) {
  const auto params = request.value("params", nlohmann::json::object());
  if (params.contains("scope") && params["scope"].contains("allowed_targets")) {
    scope_.SetAllowed(params["scope"]["allowed_targets"].get<std::vector<std::string>>());
  }

  initialized_ = true;
  ready_ = false;
  return Result(
      RequestId(request),
      {{"protocolVersion", std::string(mothprobe::kProtocolVersion)},
       {"serverInfo",
        {{"name", "mothprobe_mcp"},
         {"title", "MothProbe MCP Server"},
         {"version", mothprobe::kVersion},
         {"description",
          "Security-first MCP server for scoped MothProbe tools, resources, prompts, and LLM routing."}}},
       {"capabilities",
        {{"tools", {{"listChanged", true}}},
         {"resources", {{"subscribe", false}, {"listChanged", true}}},
         {"prompts", {{"listChanged", true}}},
         {"logging", nlohmann::json::object()}}},
       {"instructions",
        "Use only with explicit authorization. Active or intrusive security operations require user approval and declared scope."},
       {"mothon", {{"cppMcp", mothon::protocol::CppMcpSnapshotJson()}}},
       {"scope", {{"allowed_targets", scope_.Targets()}}}});
}

nlohmann::json Server::ToolsList(const nlohmann::json& request) const {
  auto tools = Tools();
  tools.push_back({
      {"name", "set_chat_title"},
      {"description", "Set the title of the current chat conversation. Use this whenever the user asks to rename the chat or if a highly specific topic emerges."},
      {"inputSchema", {
          {"type", "object"},
          {"properties", {
              {"title", {{"type", "string"}, {"description", "The new title (max 64 chars)."}}}
          }},
          {"required", nlohmann::json::array({"title"})}
      }}
  });
  return Result(RequestId(request), {{"tools", tools}});
}

nlohmann::json Server::ToolsCall(const nlohmann::json& request) {
  const auto id = RequestId(request);
  const auto params = request.value("params", nlohmann::json::object());
  if (!params.is_object() || !params.contains("name") || !params["name"].is_string()) {
    return Error(id, -32602, "tools/call requires params.name");
  }
  const auto name = params["name"].get<std::string>();
  const auto arguments = params.value("arguments", nlohmann::json::object());

  if (name == "set_chat_title") {
    if (!arguments.contains("title") || !arguments["title"].is_string()) {
       return Result(id, TextToolResult("Missing string argument 'title'", true));
    }
    const auto title = arguments["title"].get<std::string>();
    chat_memory_.SetTitle(title);
    return Result(id, TextToolResult("Chat title updated to: " + title, false));
  }

  auto result = tool_registry_.Call(name, arguments, scope_, PermissionFromString(permission_level_));
  audit_.Event("tool_call", {{"tool", name}, {"ok", result.ok}, {"error", result.error}});
  if (!result.ok) {
    auto structured = result.data.empty() ? nlohmann::json{{"tool", name}} : result.data;
    structured["code"] = result.error_code;
    return Result(id, TextToolResult(result.error, true, structured));
  }
  return Result(id, result.result);
}

nlohmann::json Server::ResourcesList(const nlohmann::json& request) const {
  return Result(RequestId(request),
                {{"resources",
                  nlohmann::json::array({
                      {{"uri", "mothprobe://audit"},
                       {"name", "Audit log"},
                       {"title", "MothProbe Audit Log"},
                       {"description", "Append-only JSONL audit events emitted by the MCP backend."},
                       {"mimeType", "application/jsonl"}},
                      {{"uri", "mothprobe://tools"},
                       {"name", "Tool metadata cache"},
                       {"title", "MothProbe Tool Cache"},
                       {"description", "Cached MCP tool metadata exposed by this server."},
                       {"mimeType", "application/json"}},
                      {{"uri", "mothprobe://memory/chat"},
                       {"name", "Chat memory"},
                       {"title", "MothProbe Chat Memory"},
                       {"description", "Local JSONL chat memory used by the LLM router."},
                       {"mimeType", "application/jsonl"}},
                  })}});
}

nlohmann::json Server::ResourcesRead(const nlohmann::json& request) const {
  const auto id = RequestId(request);
  const auto params = request.value("params", nlohmann::json::object());
  const auto uri = params.value("uri", std::string{});
  std::filesystem::path path;
  std::string mime = "text/plain";
  if (uri == "mothprobe://audit") {
    path = config_.audit_file;
    mime = "application/jsonl";
  } else if (uri == "mothprobe://tools") {
    path = config_.caches_dir / "tools.json";
    mime = "application/json";
  } else if (uri == "mothprobe://memory/chat") {
    return Result(id, {{"contents",
                        nlohmann::json::array({{{"uri", uri},
                                                {"mimeType", "application/json"},
                                                {"text", ChatSessionJson(chat_memory_.ActiveSession()).dump(2)}}})}});
  } else {
    return Error(id, -32602, "unknown resource uri: " + uri);
  }
  if (!std::filesystem::exists(path)) {
    return Error(id, -32004, "resource file does not exist", {{"uri", uri}});
  }
  return Result(id, {{"contents",
                      nlohmann::json::array({{{"uri", uri},
                                              {"mimeType", mime},
                                              {"text", core::ReadTextFileLimited(path, 1024 * 1024)}}})}});
}

nlohmann::json Server::ResourceTemplatesList(const nlohmann::json& request) const {
  return Result(RequestId(request),
                {{"resourceTemplates",
                  nlohmann::json::array({
                      {{"uriTemplate", "mothprobe://memory/{name}"},
                       {"name", "memory"},
                       {"title", "MothProbe Memory Files"},
                       {"description", "Read named local memory resources managed by MothProbe."},
                       {"mimeType", "application/jsonl"}},
                  })}});
}

nlohmann::json Server::PromptsList(const nlohmann::json& request) const {
  nlohmann::json prompts = nlohmann::json::array({
      {{"name", "passive_audit"},
       {"title", "Passive Audit Plan"},
       {"description", "Plan a scope-safe passive audit workflow."}},
      {{"name", "report_summary"},
       {"title", "Report Summary"},
       {"description", "Summarize passive audit findings into a report."}},
      {{"name", "mothprobe-skills"},
       {"title", "MothProbe Skills"},
       {"description", "Skill to guide agents to use tools, review code, and find bugs."}}
  });

  const auto skills_dir = config_.project_root / ".agents" / "skills";
  if (std::filesystem::exists(skills_dir) && std::filesystem::is_directory(skills_dir)) {
    for (const auto& entry : std::filesystem::directory_iterator(skills_dir)) {
      if (entry.is_directory()) {
        std::string skill_name = entry.path().filename().string();
        prompts.push_back({
            {"name", skill_name},
            {"title", "Workspace Skill: " + skill_name},
            {"description", "Skill loaded from workspace .agents/skills/" + skill_name}
        });
      }
    }
  }

  const auto agents_dir = config_.project_root / ".agents" / "agents";
  if (std::filesystem::exists(agents_dir) && std::filesystem::is_directory(agents_dir)) {
    for (const auto& entry : std::filesystem::directory_iterator(agents_dir)) {
      if (entry.is_directory()) {
        std::string agent_name = entry.path().filename().string();
        prompts.push_back({
            {"name", "agent_" + agent_name},
            {"title", "Agent: " + agent_name},
            {"description", "Agent definition from workspace .agents/agents/" + agent_name}
        });
      }
    }
  }

  return Result(RequestId(request), {{"prompts", prompts}});
}

nlohmann::json Server::PromptsGet(const nlohmann::json& request) const {
  const auto id = RequestId(request);
  const auto params = request.value("params", nlohmann::json::object());
  const auto name = params.value("name", std::string{});
  if (name == "passive_audit") {
    return Result(id, {{"description", "Plan passive scoped reconnaissance."},
                       {"messages",
                        nlohmann::json::array({{{"role", "user"},
                                                {"content",
                                                 {{"type", "text"},
                                                  {"text", "Create a passive audit plan using only approved MothProbe tools."}}}}})}});
  }
  if (name == "report_summary") {
    return Result(id, {{"description", "Summarize findings."},
                       {"messages",
                        nlohmann::json::array({{{"role", "user"},
                                                {"content",
                                                 {{"type", "text"},
                                                  {"text", "Summarize the provided findings with evidence and remediation."}}}}})}});
  }
  if (name == "mothprobe-skills") {
    return Result(id, {{"description", "Guides agents to use tools, review code, and find bugs."},
                       {"messages",
                        nlohmann::json::array({{{"role", "user"},
                                                {"content",
                                                 {{"type", "text"},
                                                  {"text", "You are an advanced agent. Use run_command, read_file, write_file, list_dir to explore the workspace. Read .agents/ to load skills (SKILL.md) and agent definitions (AGENTS.md/agent.json). Read MOTHPROBE.md for context. Use your tools to review code and find bugs."}}}}})}});
  }

  const auto skills_dir = config_.project_root / ".agents" / "skills";
  if (std::filesystem::exists(skills_dir / name / "SKILL.md")) {
    std::string content = core::ReadTextFileLimited(skills_dir / name / "SKILL.md", 1024 * 1024);
    return Result(id, {{"description", "Skill: " + name},
                       {"messages",
                        nlohmann::json::array({{{"role", "user"},
                                                {"content",
                                                 {{"type", "text"},
                                                  {"text", content}}}}})}});
  }

  const auto agents_dir = config_.project_root / ".agents" / "agents";
  std::string agent_name = name;
  if (name.rfind("agent_", 0) == 0) agent_name = name.substr(6);
  if (std::filesystem::exists(agents_dir / agent_name / "agent.json")) {
    std::string content = core::ReadTextFileLimited(agents_dir / agent_name / "agent.json", 1024 * 1024);
    return Result(id, {{"description", "Agent: " + agent_name},
                       {"messages",
                        nlohmann::json::array({{{"role", "user"},
                                                {"content",
                                                 {{"type", "text"},
                                                  {"text", content}}}}})}});
  }
  
  return Error(id, -32602, "unknown prompt: " + name);
}

nlohmann::json Server::LlmListProviders(const nlohmann::json& request) const {
  nlohmann::json providers = nlohmann::json::array();
  for (const auto& [name, config] : llm_providers_) {
    const bool requires_api_key = config.requires_api_key;
    const bool api_key_configured = !config.api_key.empty();
    const bool configured = !requires_api_key || api_key_configured;
    auto models = config.models;
    if (!config.model_name.empty() &&
        std::find(models.begin(), models.end(), config.model_name) == models.end()) {
      models.insert(models.begin(), config.model_name);
    }
    providers.push_back({{"name", name},
                         {"configured", configured},
                         {"requires_api_key", requires_api_key},
                         {"api_key_configured", api_key_configured},
                         {"current_model", config.model_name},
                         {"models", models},
                         {"max_tokens", config.max_tokens}});
  }
  return Result(RequestId(request), {{"providers", providers}});
}

nlohmann::json Server::LlmFetchModels(const nlohmann::json& request) {
  const auto id = RequestId(request);
  const auto params = request.value("params", nlohmann::json::object());
  const auto provider_name = params.value("provider", std::string{});
  if (provider_name.empty()) {
    return Error(id, -32602, "llm/fetch_models requires params.provider");
  }
  auto result = llm::FetchProviderModels(llm_providers_, provider_name);
  if (!result.ok) {
    return Error(id, -32000, result.error,
                 {{"provider", provider_name},
                  {"kind", result.error_kind},
                  {"http_status", result.http_status},
                  {"retryable", result.retryable}});
  }

  const auto models_dir = config_.caches_dir / "models";
  std::filesystem::create_directories(models_dir);
  std::ofstream out(models_dir / (provider_name + ".json"), std::ios::binary | std::ios::trunc);
  out << nlohmann::json{{"provider", provider_name},
                        {"source", result.source},
                        {"models", result.models}}
             .dump(2)
      << '\n';
  return Result(id, {{"provider", provider_name},
                     {"source", result.source},
                     {"models", result.models}});
}

nlohmann::json Server::LlmConfigureProvider(const nlohmann::json& request) {
  const auto id = RequestId(request);
  const auto params = request.value("params", nlohmann::json::object());
  const auto provider_name = params.value("provider", std::string{});
  const auto api_key = params.value("api_key", std::string{});
  if (provider_name.empty()) {
    return Error(id, -32602, "llm/configure_provider requires params.provider");
  }
  if (api_key.empty()) {
    return Error(id, -32602, "llm/configure_provider requires params.api_key");
  }
  auto it = llm_providers_.find(provider_name);
  if (it == llm_providers_.end()) {
    return Error(id, -32602, "unknown provider: " + provider_name);
  }

  try {
    core::SaveLlmProviderApiKey(config_, provider_name, api_key);
    it->second.api_key = api_key;
    audit_.Event("llm_provider_configured",
                 {{"provider", provider_name}, {"api_key_length", api_key.size()}});
  } catch (const std::exception& error) {
    return Error(id, -32000, error.what(), {{"provider", provider_name}});
  }

  return Result(id, {{"provider", provider_name},
                     {"configured", true},
                     {"api_key_configured", true},
                     {"config_path", (config_.runtime_root / "config.toml").string()}});
}

nlohmann::json Server::LlmSetReasoning(const nlohmann::json& request) {
  const auto id = RequestId(request);
  const auto params = request.value("params", nlohmann::json::object());
  const auto mode = params.value("mode", std::string("default"));
  try {
    auto session = chat_memory_.SetReasoningMode(mode);
    audit_.Event("llm_reasoning_mode_set",
                 {{"session_id", session.summary.session_id}, {"mode", mode}});
    return Result(id, {{"reasoning_mode", session.summary.reasoning_mode},
                       {"session", ChatSessionJson(session)}});
  } catch (const std::exception& error) {
    return Error(id, -32602, error.what());
  }
}

nlohmann::json Server::ChatListSessions(const nlohmann::json& request) const {
  nlohmann::json sessions = nlohmann::json::array();
  for (const auto& summary : chat_memory_.ListSessions()) {
    sessions.push_back(ChatSessionSummaryJson(summary));
  }
  return Result(RequestId(request), {{"sessions", sessions}});
}

nlohmann::json Server::ChatLoadSession(const nlohmann::json& request) {
  const auto id = RequestId(request);
  const auto params = request.value("params", nlohmann::json::object());
  const auto session_id = params.value("session_id", std::string{});
  if (session_id.empty()) return Error(id, -32602, "chat/load_session requires params.session_id");
  try {
    auto session = chat_memory_.LoadSession(session_id);
    audit_.Event("chat_session_loaded",
                 {{"session_id", session.summary.session_id},
                  {"message_count", session.summary.message_count}});
    return Result(id, {{"session", ChatSessionJson(session)}});
  } catch (const std::exception& error) {
    return Error(id, -32004, error.what(), {{"session_id", session_id}});
  }
}

nlohmann::json Server::ChatNewSession(const nlohmann::json& request) {
  auto session = chat_memory_.NewSession();
  audit_.Event("chat_session_created", {{"session_id", session.summary.session_id}});
  return Result(RequestId(request), {{"session", ChatSessionJson(session)}});
}

nlohmann::json Server::ChatClear(const nlohmann::json& request) {
  auto session = chat_memory_.ClearActiveSession();
  audit_.Event("chat_session_cleared", {{"session_id", session.summary.session_id}});
  return Result(RequestId(request), {{"session", ChatSessionJson(session)}});
}

nlohmann::json Server::LlmChat(const nlohmann::json& request) {
  const auto id = RequestId(request);
  const auto params = request.value("params", nlohmann::json::object());
  if (!params.is_object() || !params.contains("messages") || !params["messages"].is_array()) {
    return Error(id, -32602, "llm/chat requires params.messages array");
  }
  const auto provider_name = params.value("provider", std::string("gemini"));
  if (params.value("stream", false)) {
    return Error(id, -32602, "llm/chat streaming is not enabled in this build");
  }
  std::vector<llm::ChatMessage> messages;
  for (const auto& item : params["messages"]) {
    if (!item.is_object() || !item.contains("role") || !item.contains("content")) {
      return Error(id, -32602, "each message requires role and content");
    }
    messages.push_back({item["role"].get<std::string>(), item["content"].get<std::string>()});
  }
  auto prompt_source = messages;
  try {
    const auto active = chat_memory_.ActiveSession().summary.active_agent;
    prompt_source.insert(prompt_source.begin(),
                         {"system", agent_registry_.BuildSystemContext(active.empty() ? "pentest-agent" : active)});
  } catch (const std::exception& error) {
    logger_->warn("agent context unavailable: {}", error.what());
  }
  auto providers = llm_providers_;
  std::string selected_model;
  if (auto model = params.value("model", std::string{}); !model.empty()) {
    auto it = providers.find(provider_name);
    if (it == providers.end()) {
      audit_.LlmChat(provider_name, messages.size(), 0, false);
      return Error(id, -32602, "unknown or unconfigured LLM provider: " + provider_name);
    }
    it->second.model_name = model;
    selected_model = model;
  } else if (auto it = providers.find(provider_name); it != providers.end()) {
    selected_model = it->second.model_name;
  }
  auto provider = llm::CreateProvider(providers, provider_name);
  if (!provider) {
    audit_.LlmChat(provider_name, messages.size(), 0, false);
    return Error(id, -32602, "unknown or unconfigured LLM provider: " + provider_name);
  }
  const auto reasoning = llm::ReasoningConfig{chat_memory_.ReasoningMode()};
  const auto prompt_messages = chat_memory_.BuildPrompt(prompt_source, 24);
  auto result = provider->Chat(prompt_messages, reasoning);
  audit_.LlmChat(provider_name, messages.size(), result.text.size(), result.ok);
  if (!result.ok) {
    return Error(id, -32000, result.error,
                 {{"provider", provider_name},
                  {"kind", result.error_kind},
                  {"http_status", result.http_status},
                  {"retryable", result.retryable},
                  {"finish_reason", result.finish_reason},
                  {"truncated", result.truncated}});
  }
  chat_memory_.AppendTurn(messages, result.text, result.reasoning);
  audit_.Event("chat_memory_append",
               {{"session_id", chat_memory_.ActiveSessionId()},
                {"request_messages", messages.size()},
                {"response_length", result.text.size()},
                {"reasoning_mode", reasoning.mode}});
  return Result(id, {{"provider", provider_name},
                     {"model", selected_model},
                     {"text", result.text},
                     {"reasoning", result.reasoning},
                     {"finish_reason", result.finish_reason},
                     {"truncated", result.truncated},
                     {"session", ChatSessionJson(chat_memory_.ActiveSession())},
                     {"reasoning_mode", reasoning.mode}});
}

nlohmann::json Server::WorkspaceInspect(const nlohmann::json& request) const {
  auto workspace = agent_registry_.WorkspaceInspect();
  const auto workspace_config = core::LoadWorkspaceConfig(config_);
  const auto permissions = core::LoadPermissionConfig(config_);
  const auto clients = core::LoadMcpClientConfigs(config_);
  workspace["config"] = core::WorkspaceConfigJson(workspace_config);
  workspace["permissions"] = PermissionConfigJson(permissions);
  workspace["mcp_clients_count"] = clients.size();
  workspace["runtime"] = {{"bin_dir", config_.bin_dir.string()},
                          {"caches_dir", config_.caches_dir.string()},
                          {"brains_dir", config_.brains_dir.string()},
                          {"agents_dir", config_.agents_dir.string()},
                          {"agent_runs_dir", config_.agent_runs_dir.string()},
                          {"skills_dir", config_.skills_dir.string()},
                          {"logs_dir", config_.logs_dir.string()}};
  return Result(RequestId(request), {{"workspace", workspace}});
}

nlohmann::json Server::AgentsList(const nlohmann::json& request) const {
  nlohmann::json agents = nlohmann::json::array();
  for (const auto& agent : agent_registry_.ListAgents()) agents.push_back(AgentDefinitionJson(agent));
  return Result(RequestId(request), {{"agents", agents}});
}

nlohmann::json Server::AgentsGet(const nlohmann::json& request) const {
  const auto id = RequestId(request);
  const auto params = request.value("params", nlohmann::json::object());
  try {
    return Result(id, {{"agent", AgentDefinitionJson(agent_registry_.GetAgent(NameParam(params)))}});
  } catch (const std::exception& error) {
    return Error(id, -32602, error.what());
  }
}

nlohmann::json Server::AgentsCreate(const nlohmann::json& request) {
  const auto id = RequestId(request);
  try {
    auto agent = agent_registry_.CreateAgent(request.value("params", nlohmann::json::object()));
    audit_.Event("agent_created", {{"name", agent.name}, {"scope", agent.scope}});
    return Result(id, {{"agent", AgentDefinitionJson(agent)}});
  } catch (const std::exception& error) {
    return Error(id, -32602, error.what());
  }
}

nlohmann::json Server::AgentsSelect(const nlohmann::json& request) {
  const auto id = RequestId(request);
  try {
    const auto name = request.value("params", nlohmann::json::object()).value("name", std::string{});
    auto agent = agent_registry_.GetAgent(name);
    auto session = chat_memory_.SetActiveAgent(agent.name);
    audit_.Event("agent_selected", {{"name", agent.name}, {"session_id", session.summary.session_id}});
    return Result(id, {{"agent", AgentDefinitionJson(agent)}, {"session", ChatSessionJson(session)}});
  } catch (const std::exception& error) {
    return Error(id, -32602, error.what());
  }
}

nlohmann::json Server::AgentsRun(const nlohmann::json& request) {
  const auto id = RequestId(request);
  const auto params = request.value("params", nlohmann::json::object());
  auto prompt = params.value("prompt", std::string{});
  if (prompt.empty()) prompt = params.value("task", std::string{});
  if (prompt.empty()) return Error(id, -32602, "agents/run requires params.prompt or params.task");
  const auto run_id = TimestampId("agent-run");
  nlohmann::json selected_agents = params.value("agents", nlohmann::json::array());
  if (!selected_agents.is_array() || selected_agents.empty()) {
    auto agent = params.value("agent", std::string{});
    if (agent.empty()) agent = params.value("name", std::string("pentest-agent"));
    selected_agents = nlohmann::json::array({agent});
  }
  if (selected_agents.size() == 1 && selected_agents.front().is_string()) {
    try {
      chat_memory_.SetActiveAgent(selected_agents.front().get<std::string>());
    } catch (const std::exception&) {
    }
  }
  nlohmann::json results = nlohmann::json::array();
  const auto started_at = TimestampIso();
  for (const auto& item : selected_agents) {
    const auto agent_name = item.is_string() ? item.get<std::string>() : std::string("pentest-agent");
    try {
      auto agent = agent_registry_.GetAgent(agent_name);
      auto providers = llm_providers_;
      auto provider_name = params.value("provider", agent.provider.empty() ? "gemini" : agent.provider);
      if (auto model = params.value("model", agent.model); !model.empty() && providers.count(provider_name)) {
        providers[provider_name].model_name = model;
      }
      auto provider = llm::CreateProvider(providers, provider_name);
      if (!provider) {
        results.push_back({{"agent", agent_name},
                           {"ok", false},
                           {"error", "unknown or unconfigured LLM provider: " + provider_name}});
        continue;
      }
      const std::vector<llm::ChatMessage> messages{
          {"system", agent_registry_.BuildSystemContext(agent_name)},
          {"user", prompt},
      };
      auto result = provider->Chat(messages, llm::ReasoningConfig{chat_memory_.ReasoningMode()});
      results.push_back({{"agent", agent_name},
                         {"provider", provider_name},
                         {"ok", result.ok},
                         {"text", result.text},
                         {"reasoning", result.reasoning},
                         {"error", result.error},
                         {"http_status", result.http_status}});
    } catch (const std::exception& error) {
       results.push_back({{"agent", agent_name}, {"ok", false}, {"error", error.what()}});
    }
  }
  const nlohmann::json artifact{{"run_id", run_id},
                                {"task_id", run_id},
                                {"status", "completed"},
                                {"created_at", started_at},
                                {"updated_at", TimestampIso()},
                                {"prompt", prompt},
                                {"permission_level", permission_level_},
                                {"results", results}};
  SaveJsonFile(config_.agent_runs_dir / (run_id + ".json"), artifact);
  audit_.Event("agents_run", {{"run_id", run_id}, {"agents", selected_agents.size()}});
  return Result(id, {{"run", artifact}});
}

nlohmann::json Server::AgentsCancel(const nlohmann::json& request) {
  const auto id = RequestId(request);
  const auto params = request.value("params", nlohmann::json::object());
  auto run_id = params.value("run_id", std::string{});
  if (run_id.empty()) run_id = params.value("task_id", std::string{});
  if (run_id.empty()) run_id = params.value("id", std::string{});
  if (run_id.empty()) return Error(id, -32602, "agents/cancel requires params.run_id");
  const auto path = config_.agent_runs_dir / (run_id + ".json");
  auto run = LoadJsonFile(path);
  if (!run.is_object() || run.empty()) {
    return Error(id, -32004, "agent run not found", {{"run_id", run_id}});
  }
  run["status"] = "cancelled";
  run["updated_at"] = TimestampIso();
  SaveJsonFile(path, run);
  audit_.Event("agents_cancel", {{"run_id", run_id}});
  return Result(id, {{"run", run}});
}

nlohmann::json Server::PermissionGet(const nlohmann::json& request) const {
  const auto permissions = core::LoadPermissionConfig(config_);
  return Result(RequestId(request),
                {{"permission_level", permission_level_},
                 {"permissions", PermissionConfigJson(permissions)}});
}

nlohmann::json Server::PermissionSet(const nlohmann::json& request) {
  const auto id = RequestId(request);
  const auto params = request.value("params", nlohmann::json::object());
  if (!params.is_object()) return Error(id, -32602, "permission/set requires object params");

  auto permissions = core::LoadPermissionConfig(config_);
  auto apply_path_array = [&](const char* key, std::vector<std::filesystem::path>* out) {
    if (!params.contains(key)) return;
    if (!params[key].is_array()) throw std::runtime_error(std::string(key) + " must be an array");
    std::vector<std::filesystem::path> paths;
    for (const auto& item : params[key]) {
      if (!item.is_string()) throw std::runtime_error(std::string(key) + " entries must be strings");
      std::filesystem::path path = item.get<std::string>();
      if (path.is_relative()) path = config_.project_root / path;
      paths.push_back(std::filesystem::absolute(path).lexically_normal());
    }
    if (!paths.empty()) *out = std::move(paths);
  };

  try {
    if (auto level = params.value("level", std::string{}); !level.empty()) {
      if (level != "default" && level != "full") {
        return Error(id, -32602, "permission level must be default or full");
      }
      permission_level_ = level;
      chat_memory_.SetPermissionLevel(level);
    }
    if (params.contains("tools")) {
      if (!params["tools"].is_object()) return Error(id, -32602, "permission/set params.tools must be an object");
      for (const auto& [tool, value] : params["tools"].items()) {
        if (!value.is_string()) return Error(id, -32602, "tool permission values must be strings");
        auto parsed = core::ParseToolPermission(value.get<std::string>());
        if (!parsed) return Error(id, -32602, "unknown permission: " + value.get<std::string>());
        permissions.tools[tool] = *parsed;
      }
    }
    if (auto tool = params.value("tool", std::string{}); !tool.empty()) {
      auto permission = params.value("permission", std::string{});
      auto parsed = core::ParseToolPermission(permission);
      if (!parsed) return Error(id, -32602, "unknown permission: " + permission);
      permissions.tools[tool] = *parsed;
    }
    apply_path_array("readable_paths", &permissions.readable_paths);
    apply_path_array("writable_paths", &permissions.writable_paths);
    core::SavePermissionConfig(config_, permissions);
    auto session = chat_memory_.ActiveSession();
    audit_.Event("permission_set", {{"level", permission_level_}, {"session_id", session.summary.session_id}});
    return Result(id, {{"permission_level", permission_level_},
                       {"permissions", PermissionConfigJson(permissions)},
                       {"session", ChatSessionJson(session)}});
  } catch (const std::exception& error) {
    return Error(id, -32602, error.what());
  }
}

nlohmann::json Server::McpClients(const nlohmann::json& request, const std::string& action) {
  const auto id = RequestId(request);
  const auto params = request.value("params", nlohmann::json::object());
  try {
    if (action == "list") return Result(id, mcp_clients_.List());
    if (action == "add") return Result(id, mcp_clients_.Add(params));
    const auto name = NameParam(params);
    if (name.empty()) return Error(id, -32602, "mcp_clients/" + action + " requires params.name");
    if (action == "get") return Result(id, mcp_clients_.Get(name));
    if (action == "remove") return Result(id, mcp_clients_.Remove(name));
    if (action == "login") return Result(id, mcp_clients_.Login(name, params));
    if (action == "logout") return Result(id, mcp_clients_.Logout(name));
    if (action == "connect") return Result(id, mcp_clients_.Connect(name));
    if (action == "disconnect") return Result(id, mcp_clients_.Disconnect(name));
    return Error(id, -32601, "unknown MCP client action: " + action);
  } catch (const std::exception& error) {
    return Error(id, -32602, error.what());
  }
}

nlohmann::json Server::Tools() const {
  return tool_registry_.List();
}

void Server::WriteToolCache() const {
  std::ofstream out(config_.caches_dir / "tools.json", std::ios::binary | std::ios::trunc);
  out << nlohmann::json{{"daemon", "mothprobe_mcp"},
                        {"version", mothprobe::kVersion},
                        {"tools", Tools()}}
             .dump(2)
      << '\n';
}

}  // namespace mothprobe::mcp
