#pragma once

#include <string>

#include <nlohmann/json.hpp>

namespace mothprobe::mothon::protocol {

struct UpstreamSnapshot {
  std::string name;
  std::string repository;
  std::string commit;
  std::string protocol_version;
  std::string license;
};

UpstreamSnapshot CppMcpSnapshot();
nlohmann::json CppMcpSnapshotJson();
nlohmann::json CppMcpToolShape(const nlohmann::json& tool);

}  // namespace mothprobe::mothon::protocol
