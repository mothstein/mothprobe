#pragma once

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace mothprobe::mcp {

struct RpcMessage {
  nlohmann::json value;
};

std::optional<RpcMessage> ReadMessage();
void WriteMessage(const nlohmann::json& value);
nlohmann::json Result(nlohmann::json id, nlohmann::json result);
nlohmann::json Error(nlohmann::json id, int code, const std::string& message);
nlohmann::json RequestId(const nlohmann::json& request);

}  // namespace mothprobe::mcp
