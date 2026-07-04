#include "mothon/tools/tool_registry.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <sys/socket.h>
#endif

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <memory>
#include <set>
#include <sstream>
#include <system_error>
#include <utility>

#include <httplib.h>

#include "core/shell_runner.hpp"
#include "mothon/protocol/upstream.hpp"

namespace mothprobe::mothon::tools {
namespace {

struct UrlParts {
  std::string scheme = "http";
  std::string host;
  std::string path = "/";
};

#ifdef _WIN32
class WsaSession {
 public:
  WsaSession() { ok_ = WSAStartup(MAKEWORD(2, 2), &data_) == 0; }
  ~WsaSession() {
    if (ok_) WSACleanup();
  }
  bool ok() const { return ok_; }

 private:
  WSADATA data_{};
  bool ok_ = false;
};
#endif

std::string RequiredString(const nlohmann::json& arguments, const std::string& key) {
  if (!arguments.contains(key) || !arguments[key].is_string() || arguments[key].get<std::string>().empty()) {
    return {};
  }
  return arguments[key].get<std::string>();
}

UrlParts ParseUrl(std::string value) {
  UrlParts out;
  const auto scheme = value.find("://");
  if (scheme != std::string::npos) {
    out.scheme = value.substr(0, scheme);
    value = value.substr(scheme + 3);
  }
  const auto slash = value.find('/');
  out.host = slash == std::string::npos ? value : value.substr(0, slash);
  out.path = slash == std::string::npos ? "/" : value.substr(slash);
  return out;
}

std::string HostOnly(const std::string& target) {
  auto parsed = ParseUrl(target);
  auto host = parsed.host.empty() ? target : parsed.host;
  if (!host.empty() && host.front() == '[') {
    const auto close = host.find(']');
    return close == std::string::npos ? host : host.substr(1, close - 1);
  }
  const auto colon = host.find(':');
  return colon == std::string::npos ? host : host.substr(0, colon);
}

nlohmann::json TextContent(const std::string& text, nlohmann::json structured) {
  return {{"content", nlohmann::json::array({{{"type", "text"}, {"text", text}}})},
          {"isError", false},
          {"structuredContent", std::move(structured)}};
}

std::string TimestampId() {
  const auto now = std::chrono::system_clock::now();
  const auto time = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &time);
#else
  localtime_r(&time, &tm);
#endif
  std::ostringstream out;
  out << std::put_time(&tm, "%Y%m%d-%H%M%S");
  return out.str();
}

std::string MarkdownEscape(std::string value) {
  std::replace(value.begin(), value.end(), '\r', ' ');
  return value;
}

ToolCallResult ErrorResult(int code, std::string message, nlohmann::json data = nlohmann::json::object()) {
  return {false, {}, code, std::move(message), std::move(data)};
}

ToolDefinition PassiveTool(std::string name, std::string description, nlohmann::json schema) {
  return {std::move(name), std::move(description), std::move(schema), RiskClass::Passive, true,
          false};
}

ToolCallResult ApprovalRequired(const ToolDefinition& tool) {
  return ErrorResult(-32020,
                     "approval required for tool: " + tool.name,
                     {{"tool", tool.name},
                      {"riskClass", RiskClassName(tool.risk)},
                      {"approvalRequired", true}});
}

ToolCallResult PermissionDenied(const ToolDefinition& tool, core::ToolPermission permission) {
  return ErrorResult(-32020,
                     "permission denied for tool: " + tool.name,
                     {{"tool", tool.name},
                      {"riskClass", RiskClassName(tool.risk)},
                      {"permission", core::ToolPermissionName(permission)}});
}

bool RequiresApproval(const ToolDefinition& tool, PermissionLevel permission,
                      const nlohmann::json& arguments) {
  if (permission == PermissionLevel::Full) return false;
  if (arguments.value("approved", false)) return false;
  return tool.approval_required || tool.risk != RiskClass::Passive;
}

const ToolDefinition* FindTool(const std::vector<ToolDefinition>& tools, const std::string& name) {
  for (const auto& tool : tools) {
    if (tool.name == name) return &tool;
  }
  return nullptr;
}

std::filesystem::path CanonicalRoot(const std::filesystem::path& root) {
  std::error_code ec;
  auto canonical = std::filesystem::weakly_canonical(root, ec);
  return ec ? std::filesystem::absolute(root).lexically_normal() : canonical;
}

bool IsInside(const std::filesystem::path& root, const std::filesystem::path& target) {
  std::error_code ec;
  const auto rel = std::filesystem::relative(target, root, ec);
  if (ec) {
    return false;
  }
  if (rel.empty() || rel == ".") {
    return true;
  }
  const auto first = rel.begin();
  return first == rel.end() || (*first != ".." && !rel.is_absolute());
}

bool IsInsideAny(const std::vector<std::filesystem::path>& roots,
                 const std::filesystem::path& target) {
  for (const auto& root : roots) {
    if (IsInside(CanonicalRoot(root), target)) {
      return true;
    }
  }
  return false;
}

std::filesystem::path ResolveExistingPath(const core::RuntimeConfig& config,
                                          const std::vector<std::filesystem::path>& roots,
                                          const std::string& value) {
  auto path = std::filesystem::path(value);
  if (path.is_relative()) path = config.project_root / path;
  std::error_code ec;
  path = std::filesystem::weakly_canonical(path, ec);
  if (ec || !IsInsideAny(roots, path)) {
    throw std::runtime_error("path is outside permitted roots");
  }
  return path;
}

std::filesystem::path ResolveWritablePath(const core::RuntimeConfig& config,
                                          const std::vector<std::filesystem::path>& roots,
                                          const std::string& value) {
  auto path = std::filesystem::path(value);
  if (path.is_relative()) path = config.project_root / path;
  path = path.lexically_normal();
  std::error_code ec;
  const auto parent = std::filesystem::weakly_canonical(path.parent_path(), ec);
  const auto checked_parent = ec ? std::filesystem::absolute(path.parent_path()).lexically_normal()
                                 : parent;
  if (!IsInsideAny(roots, checked_parent)) {
    throw std::runtime_error("path is outside permitted roots");
  }
  return checked_parent / path.filename();
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

struct AddrInfoDeleter {
  void operator()(addrinfo* value) const {
    if (value) freeaddrinfo(value);
  }
};

using AddrInfoPtr = std::unique_ptr<addrinfo, AddrInfoDeleter>;

PermissionLevel EffectivePermissionLevel(core::ToolPermission persisted,
                                         PermissionLevel requested) {
  if (persisted == core::ToolPermission::Allow) {
    return PermissionLevel::Full;
  }
  return requested;
}

}  // namespace

std::string RiskClassName(RiskClass risk) {
  switch (risk) {
    case RiskClass::Passive:
      return "passive";
    case RiskClass::LowActive:
      return "low_active";
    case RiskClass::Intrusive:
      return "intrusive";
  }
  return "unknown";
}

ToolRegistry::ToolRegistry(core::RuntimeConfig config) : config_(std::move(config)) {
  tools_.push_back(PassiveTool(
      "lookup_dns", "Resolve a scoped hostname to IP addresses using the local resolver.",
      {{"type", "object"},
       {"required", {"target"}},
       {"properties", {{"target", {{"type", "string"}, {"description", "Domain, host, or URL in scope."}}}}}}));
  tools_.push_back(PassiveTool(
      "detect_headers", "Fetch HTTP response headers for a scoped URL.",
      {{"type", "object"},
       {"required", {"url"}},
       {"properties", {{"url", {{"type", "string"}, {"description", "HTTP or HTTPS URL in scope."}}}}}}));
  tools_.push_back({"report_export",
                    "Write a Markdown report from structured findings.",
                    {{"type", "object"},
                     {"properties",
                      {{"title", {{"type", "string"}}},
                       {"summary", {{"type", "string"}}},
                       {"findings", {{"type", "array"}}}}}},
                    RiskClass::Passive,
                    false,
                    false});
  tools_.push_back({"run_command",
                    "Execute a shell command through the MothProbe permission policy.",
                    {{"type", "object"},
                     {"required", {"command"}},
                     {"properties",
                      {{"command", {{"type", "string"}, {"description", "Command to execute."}}},
                       {"approved", {{"type", "boolean"}}}}}},
                    RiskClass::LowActive,
                    false,
                    true});
  tools_.push_back(PassiveTool(
      "read_file", "Read file contents.",
      {{"type", "object"},
       {"required", {"path"}},
       {"properties", {{"path", {{"type", "string"}, {"description", "Path to file."}}}}}}));
  tools_.push_back({"write_file",
                    "Write file contents inside the current workspace.",
                    {{"type", "object"},
                     {"required", {"path", "content"}},
                     {"properties", {{"path", {{"type", "string"}}},
                                     {"content", {{"type", "string"}}},
                                     {"approved", {{"type", "boolean"}}}}}},
                    RiskClass::Intrusive,
                    false,
                    true});
  tools_.push_back(PassiveTool(
      "list_dir", "List directory contents.",
      {{"type", "object"},
       {"required", {"path"}},
       {"properties", {{"path", {{"type", "string"}, {"description", "Path to directory."}}}}}}));
}

nlohmann::json ToolRegistry::List() const {
  nlohmann::json out = nlohmann::json::array();
  const auto permissions = core::LoadPermissionConfig(config_);
  for (const auto& tool : tools_) {
    nlohmann::json item{{"name", tool.name},
                        {"description", tool.description},
                        {"inputSchema", tool.input_schema},
                        {"annotations",
                         {{"riskClass", RiskClassName(tool.risk)},
                          {"scopeRequired", tool.scope_required},
                          {"approvalRequired", tool.approval_required},
                          {"permission",
                           core::ToolPermissionName(permissions.PermissionFor(tool.name))}}}};
    out.push_back(protocol::CppMcpToolShape(item));
  }
  return out;
}

ToolCallResult ToolRegistry::Call(const std::string& name, const nlohmann::json& arguments,
                                  const ::mothprobe::mcp::ScopeValidator& scope,
                                  PermissionLevel permission) const {
  const auto* definition = FindTool(tools_, name);
  if (!definition) {
    return ErrorResult(-32602, "unknown tool: " + name, {{"tool", name}});
  }
  const auto permissions = core::LoadPermissionConfig(config_);
  const auto persisted_permission = permissions.PermissionFor(name);
  if (persisted_permission == core::ToolPermission::Deny) {
    return PermissionDenied(*definition, persisted_permission);
  }
  permission = EffectivePermissionLevel(persisted_permission, permission);
  if (persisted_permission == core::ToolPermission::Ask &&
      RequiresApproval(*definition, permission, arguments)) {
    return ApprovalRequired(*definition);
  }
  if (name == "lookup_dns") return LookupDns(arguments, scope);
  if (name == "detect_headers") return DetectHeaders(arguments, scope);
  if (name == "report_export") return ExportReport(arguments);
  if (name == "run_command") return RunCommand(arguments, permission);
  if (name == "read_file") return ReadFile(arguments, scope);
  if (name == "write_file") return WriteFile(arguments, permission);
  if (name == "list_dir") return ListDir(arguments, scope);
  return ErrorResult(-32602, "unknown tool: " + name, {{"tool", name}});
}

ToolCallResult ToolRegistry::LookupDns(const nlohmann::json& arguments,
                                       const ::mothprobe::mcp::ScopeValidator& scope) const {
  const auto target = RequiredString(arguments, "target");
  if (target.empty()) return ErrorResult(-32602, "lookup_dns requires arguments.target");
  if (!scope.Allowed(target)) {
    return ErrorResult(-32010, "target is outside allowed scope", {{"target", target}});
  }

#ifdef _WIN32
  WsaSession wsa;
  if (!wsa.ok()) return ErrorResult(-32000, "WSAStartup failed");
#endif

  const auto host = HostOnly(target);
  addrinfo hints{};
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_family = AF_UNSPEC;
  addrinfo* result = nullptr;
  const int rc = getaddrinfo(host.c_str(), nullptr, &hints, &result);
  if (rc != 0) {
    return ErrorResult(-32000, "DNS resolution failed", {{"target", target}, {"host", host}});
  }
  AddrInfoPtr results(result);

  std::set<std::string> addresses;
  char buffer[INET6_ADDRSTRLEN]{};
  for (auto* item = results.get(); item != nullptr; item = item->ai_next) {
    void* address = nullptr;
    if (item->ai_family == AF_INET) {
      address = &reinterpret_cast<sockaddr_in*>(item->ai_addr)->sin_addr;
    } else if (item->ai_family == AF_INET6) {
      address = &reinterpret_cast<sockaddr_in6*>(item->ai_addr)->sin6_addr;
    }
    if (address && inet_ntop(item->ai_family, address, buffer, sizeof(buffer))) {
      addresses.insert(buffer);
    }
  }

  nlohmann::json structured{{"target", target}, {"host", host}, {"addresses", addresses}};
  return {true, TextContent("Resolved " + host + " to " + std::to_string(addresses.size()) +
                                " address(es).",
                            structured)};
}

ToolCallResult ToolRegistry::DetectHeaders(const nlohmann::json& arguments,
                                           const ::mothprobe::mcp::ScopeValidator& scope) const {
  auto url = RequiredString(arguments, "url");
  if (url.empty()) return ErrorResult(-32602, "detect_headers requires arguments.url");
  if (url.find("://") == std::string::npos) url = "http://" + url;
  if (!scope.Allowed(url)) {
    return ErrorResult(-32010, "url is outside allowed scope", {{"url", url}});
  }

  const auto parsed = ParseUrl(url);
  if (parsed.scheme != "http" && parsed.scheme != "https") {
    return ErrorResult(-32602, "detect_headers only supports http and https URLs");
  }

  std::unique_ptr<httplib::Client> client;
  if (parsed.scheme == "https") {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
    client = std::make_unique<httplib::SSLClient>(parsed.host);
#else
    return ErrorResult(-32000, "HTTPS header detection requires OpenSSL support");
#endif
  } else {
    client = std::make_unique<httplib::Client>("http://" + parsed.host);
  }
  client->set_connection_timeout(10, 0);
  client->set_read_timeout(20, 0);

  auto response = client->Head(parsed.path);
  if (!response) response = client->Get(parsed.path);
  if (!response) {
    return ErrorResult(-32000, "HTTP request failed", {{"url", url}});
  }

  nlohmann::json headers = nlohmann::json::object();
  for (const auto& header : response->headers) {
    headers[header.first] = header.second;
  }
  nlohmann::json structured{{"url", url}, {"status", response->status}, {"headers", headers}};
  return {true, TextContent("Fetched " + std::to_string(headers.size()) + " response header(s).",
                            structured)};
}

ToolCallResult ToolRegistry::ExportReport(const nlohmann::json& arguments) const {
  const auto reports_dir = config_.runtime_root / "reports";
  std::filesystem::create_directories(reports_dir);
  const auto title = arguments.value("title", std::string("MothProbe Passive Audit Report"));
  const auto summary = arguments.value("summary", std::string("Generated by Mothon-CPP."));
  const auto findings = arguments.value("findings", nlohmann::json::array());
  const auto file = reports_dir / ("report-" + TimestampId() + ".md");

  std::ofstream out(file, std::ios::binary | std::ios::trunc);
  if (!out) return ErrorResult(-32000, "failed to create report", {{"path", file.string()}});
  out << "# " << MarkdownEscape(title) << "\n\n";
  out << MarkdownEscape(summary) << "\n\n";
  out << "## Findings\n\n";
  if (findings.empty()) {
    out << "No findings were provided.\n";
  } else {
    for (const auto& finding : findings) {
      out << "- `" << finding.dump() << "`\n";
    }
  }

  nlohmann::json structured{{"path", file.string()}, {"findings_count", findings.size()}};
  return {true, TextContent("Report written to " + file.string(), structured)};
}

ToolCallResult ToolRegistry::RunCommand(const nlohmann::json& arguments,
                                        PermissionLevel permission) const {
  const auto command = RequiredString(arguments, "command");
  if (command.empty()) return ErrorResult(-32602, "run_command requires arguments.command");
  int exit_code = -1;
  const auto shell_level = permission == PermissionLevel::Full
                               ? core::ShellPermissionLevel::Full
                               : core::ShellPermissionLevel::Default;
  auto output = core::RunShellCommand(command, &exit_code, shell_level);
  if (exit_code < 0 && output == "Rejected by shell policy.") {
    return ErrorResult(-32021, output, {{"command", command}, {"permission", "default"}});
  }
  return {true, TextContent("Command output:\n" + output,
                            {{"command", command}, {"output", output}, {"exit_code", exit_code}})};
}

ToolCallResult ToolRegistry::ReadFile(const nlohmann::json& arguments,
                                      const ::mothprobe::mcp::ScopeValidator& scope) const {
  const auto path = RequiredString(arguments, "path");
  if (path.empty()) return ErrorResult(-32602, "read_file requires arguments.path");
  std::filesystem::path safe_path;
  try {
    const auto permissions = core::LoadPermissionConfig(config_);
    safe_path = ResolveExistingPath(config_, permissions.readable_paths, path);
  } catch (const std::exception& error) {
    return ErrorResult(-32022, error.what(), {{"path", path}});
  }
  if (!std::filesystem::is_regular_file(safe_path)) {
    return ErrorResult(-32000, "path is not a regular file", {{"path", path}});
  }
  std::ifstream in(safe_path, std::ios::binary);
  if (!in) return ErrorResult(-32000, "failed to open file");

  constexpr std::size_t kMaxRead = 12000;
  std::string content;
  content.resize(kMaxRead);
  in.read(content.data(), static_cast<std::streamsize>(content.size()));
  content.resize(static_cast<std::size_t>(in.gcount()));
  const bool truncated = !in.eof();
  if (truncated) content += "\n[truncated by MothProbe]";
  return {true, TextContent(content, {{"path", safe_path.string()}, {"truncated", truncated}})};
}

ToolCallResult ToolRegistry::WriteFile(const nlohmann::json& arguments,
                                       PermissionLevel permission) const {
  const auto path = RequiredString(arguments, "path");
  if (path.empty() || !arguments.contains("content") || !arguments["content"].is_string()) {
    return ErrorResult(-32602, "write_file requires path and content");
  }
  const auto content = arguments["content"].get<std::string>();
  std::filesystem::path safe_path;
  try {
    const auto permissions = core::LoadPermissionConfig(config_);
    safe_path = ResolveWritablePath(config_, permissions.writable_paths, path);
  } catch (const std::exception& error) {
    return ErrorResult(-32022, error.what(), {{"path", path}});
  }
  std::filesystem::create_directories(safe_path.parent_path());
  ScopedTempPath temp(safe_path.string() + ".mothprobe.tmp");
  std::ofstream out(temp.path(), std::ios::binary | std::ios::trunc);
  if (!out) return ErrorResult(-32000, "failed to open file for writing");
  if (content.size() > 1024 * 1024) {
    return ErrorResult(-32602, "write_file content is too large");
  }
  out << content;
  out.close();
  std::error_code ec;
  std::filesystem::rename(temp.path(), safe_path, ec);
  if (ec) {
    return ErrorResult(-32000, "failed to replace file", {{"path", safe_path.string()}});
  }
  temp.Release();
  return {true, TextContent("File written successfully.", {{"path", safe_path.string()}})};
}

ToolCallResult ToolRegistry::ListDir(const nlohmann::json& arguments,
                                     const ::mothprobe::mcp::ScopeValidator& scope) const {
  const auto path = RequiredString(arguments, "path");
  if (path.empty()) return ErrorResult(-32602, "list_dir requires arguments.path");
  std::filesystem::path dir;
  try {
    const auto permissions = core::LoadPermissionConfig(config_);
    dir = ResolveExistingPath(config_, permissions.readable_paths, path);
  } catch (const std::exception& error) {
    return ErrorResult(-32022, error.what(), {{"path", path}});
  }
  if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir)) {
    return ErrorResult(-32000, "path is not a directory or does not exist");
  }
  
  std::string result;
  nlohmann::json entries = nlohmann::json::array();
  std::size_t count = 0;
  for (const auto& entry : std::filesystem::directory_iterator(dir)) {
    if (count++ >= 200) break;
    result += entry.path().filename().string();
    if (entry.is_directory()) result += "/";
    result += "\n";
    entries.push_back({{"name", entry.path().filename().string()},
                       {"directory", entry.is_directory()}});
  }
  return {true, TextContent("Directory contents:\n" + result,
                            {{"path", dir.string()}, {"entries", entries}})};
}

}  // namespace mothprobe::mothon::tools
