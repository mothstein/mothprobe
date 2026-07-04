#include "mcp/json_rpc.hpp"

#include <algorithm>
#include <cctype>
#include <iostream>

namespace mothprobe::mcp {

namespace {

std::string TrimCr(std::string line) {
  if (!line.empty() && line.back() == '\r') {
    line.pop_back();
  }
  return line;
}

std::optional<std::string> ReadRaw() {
  std::string line;
  while (std::getline(std::cin, line)) {
    line = TrimCr(line);
    if (line.empty()) {
      continue;
    }

    constexpr const char* kHeader = "Content-Length:";
    if (line.rfind(kHeader, 0) != 0) {
      return line;
    }

    const std::string text = line.substr(std::char_traits<char>::length(kHeader));
    auto it = std::find_if(text.begin(), text.end(), [](unsigned char c) {
      return std::isdigit(c) != 0;
    });
    if (it == text.end()) {
      return std::nullopt;
    }
    const auto length = static_cast<std::size_t>(std::stoul(std::string(it, text.end())));
    while (std::getline(std::cin, line) && !TrimCr(line).empty()) {
    }

    std::string body(length, '\0');
    std::cin.read(body.data(), static_cast<std::streamsize>(length));
    return body;
  }
  return std::nullopt;
}

}  // namespace

std::optional<RpcMessage> ReadMessage() {
  auto raw = ReadRaw();
  if (!raw) {
    return std::nullopt;
  }
  auto parsed = nlohmann::json::parse(*raw, nullptr, false);
  if (parsed.is_discarded()) {
    return RpcMessage{
        {{"jsonrpc", "2.0"},
         {"id", nullptr},
         {"error", {{"code", -32700}, {"message", "Parse error"}}}},
        true};
  }
  return RpcMessage{std::move(parsed), false};
}

void WriteMessage(const nlohmann::json& value) {
  std::cout << value.dump() << '\n';
  std::cout.flush();
}

nlohmann::json Result(nlohmann::json id, nlohmann::json result) {
  return {{"jsonrpc", "2.0"}, {"id", std::move(id)}, {"result", std::move(result)}};
}

nlohmann::json Error(nlohmann::json id, int code, const std::string& message) {
  return {{"jsonrpc", "2.0"}, {"id", std::move(id)}, {"error", {{"code", code}, {"message", message}}}};
}

nlohmann::json Error(nlohmann::json id, int code, const std::string& message,
                     nlohmann::json data) {
  return {{"jsonrpc", "2.0"},
          {"id", std::move(id)},
          {"error", {{"code", code}, {"message", message}, {"data", std::move(data)}}}};
}

nlohmann::json RequestId(const nlohmann::json& request) {
  return request.contains("id") ? request["id"] : nlohmann::json(nullptr);
}

}  // namespace mothprobe::mcp
