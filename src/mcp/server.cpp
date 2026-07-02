#include "mcp/server.hpp"

#include <fstream>

#include "mcp/json_rpc.hpp"
#include "mothprobe/version.hpp"

namespace mothprobe::mcp {

Server::Server(core::RuntimeConfig config, std::shared_ptr<spdlog::logger> logger)
    : config_(std::move(config)), logger_(std::move(logger)) {}

nlohmann::json Server::Dispatch(const nlohmann::json& request) {
  if (!request.is_object() || request.value("jsonrpc", "") != "2.0" ||
      !request.contains("method")) {
    return Error(RequestId(request), -32600, "Invalid JSON-RPC request");
  }

  const auto method = request["method"].get<std::string>();
  if (method == "initialize") {
    return Initialize(request);
  }
  if (method == "notifications/initialized") {
    logger_->info("client initialized");
    return {};
  }
  if (method == "tools/list") {
    return ToolsList(request);
  }
  return Error(RequestId(request), -32601, "Method not found");
}

nlohmann::json Server::Initialize(const nlohmann::json& request) {
  const auto params = request.value("params", nlohmann::json::object());
  if (params.contains("scope") && params["scope"].contains("allowed_targets")) {
    scope_.SetAllowed(params["scope"]["allowed_targets"].get<std::vector<std::string>>());
  }

  return Result(RequestId(request),
                {{"protocolVersion", params.value("protocolVersion",
                                                   std::string(mothprobe::kProtocolVersion))},
                 {"serverInfo", {{"name", "mothprobe_mcp"}, {"version", mothprobe::kVersion}}},
                 {"capabilities", {{"tools", nlohmann::json::object()}}},
                 {"scope", {{"allowed_targets", scope_.Targets()}}}});
}

nlohmann::json Server::ToolsList(const nlohmann::json& request) const {
  return Result(RequestId(request), {{"tools", Tools()}});
}

nlohmann::json Server::Tools() const {
  return nlohmann::json::array({
      {{"name", "scan_tcp"},
       {"description", "Passive foundation TCP scan descriptor."},
       {"inputSchema", {{"type", "object"}, {"required", {"target"}}}}},
      {{"name", "lookup_dns"},
       {"description", "Passive DNS lookup descriptor."},
       {"inputSchema", {{"type", "object"}, {"required", {"target"}}}}},
  });
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
