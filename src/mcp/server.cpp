#include "mcp/server.hpp"

#include <algorithm>
#include <fstream>

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

}  // namespace

Server::Server(core::RuntimeConfig config, std::shared_ptr<spdlog::logger> logger)
    : config_(std::move(config)),
      logger_(std::move(logger)),
      audit_(config_.audit_file),
      chat_memory_(config_.brains_dir / "chat_memory.jsonl"),
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
  if (method == "llm/chat") {
    return LlmChat(request);
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
  return Result(RequestId(request), {{"tools", Tools()}});
}

nlohmann::json Server::ToolsCall(const nlohmann::json& request) {
  const auto id = RequestId(request);
  const auto params = request.value("params", nlohmann::json::object());
  if (!params.is_object() || !params.contains("name") || !params["name"].is_string()) {
    return Error(id, -32602, "tools/call requires params.name");
  }
  const auto name = params["name"].get<std::string>();
  const auto arguments = params.value("arguments", nlohmann::json::object());
  auto result = tool_registry_.Call(name, arguments, scope_);
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
    path = config_.brains_dir / "chat_memory.jsonl";
    mime = "application/jsonl";
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
  const auto prompt_messages = chat_memory_.BuildPrompt(messages, 24);
  auto result = provider->Chat(prompt_messages);
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
  chat_memory_.AppendTurn(messages, result.text);
  audit_.Event("chat_memory_append",
               {{"request_messages", messages.size()}, {"response_length", result.text.size()}});
  return Result(id, {{"provider", provider_name},
                     {"model", selected_model},
                     {"text", result.text},
                     {"reasoning", result.reasoning},
                     {"finish_reason", result.finish_reason},
                     {"truncated", result.truncated}});
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
