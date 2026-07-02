#include <exception>
#include <iostream>

#include <spdlog/spdlog.h>

#include "core/config.hpp"
#include "core/logger.hpp"
#include "mcp/json_rpc.hpp"
#include "mcp/server.hpp"

int main() {
  try {
    auto config = mothprobe::core::LoadRuntimeConfig();
    mothprobe::core::EnsureRuntimeLayout(config);
    auto logger =
        mothprobe::core::CreateStderrFileLogger("mothprobe-mcp", config.logs_dir / "mcp.log");

    mothprobe::mcp::Server server(config, logger);
    server.WriteToolCache();
    logger->info("mothprobe_mcp started");

    while (auto message = mothprobe::mcp::ReadMessage()) {
      auto response = server.Dispatch(message->value);
      if (!response.is_null() && !response.empty()) {
        mothprobe::mcp::WriteMessage(response);
      }
    }
    logger->info("mothprobe_mcp stopped");
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "mothprobe_mcp fatal: " << error.what() << '\n';
    return 1;
  }
}
