#include "mothon/protocol/upstream.hpp"

#include <mcp_message.h>
#include <mcp_tool.h>

namespace mothprobe::mothon::protocol {

UpstreamSnapshot CppMcpSnapshot() {
  return {"cpp-mcp",
          "https://github.com/hkr04/cpp-mcp",
          "a0eb22c98dbd8ce8b3ef69679310c1a038905c08",
          mcp::MCP_VERSION,
          "MIT"};
}

nlohmann::json CppMcpSnapshotJson() {
  const auto snapshot = CppMcpSnapshot();
  return {{"name", snapshot.name},
          {"repository", snapshot.repository},
          {"commit", snapshot.commit},
          {"protocolVersion", snapshot.protocol_version},
          {"license", snapshot.license}};
}

nlohmann::json CppMcpToolShape(const nlohmann::json& tool) {
  mcp::tool upstream;
  upstream.name = tool.value("name", "");
  upstream.description = tool.value("description", "");
  upstream.parameters_schema =
      mcp::json::parse(tool.value("inputSchema", nlohmann::json::object()).dump());
  if (tool.contains("annotations")) {
    upstream.annotations = mcp::json::parse(tool["annotations"].dump());
  }
  return nlohmann::json::parse(upstream.to_json().dump());
}

}  // namespace mothprobe::mothon::protocol
