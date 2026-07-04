#include <catch2/catch_test_macros.hpp>

#include "core/config.hpp"
#include "mcp/llm/gemini_adapter.hpp"
#include "mcp/llm/openrouter_adapter.hpp"

using mothprobe::mcp::llm::ChatMessage;
using mothprobe::mcp::llm::GeminiAdapter;
using mothprobe::mcp::llm::OpenRouterAdapter;
using mothprobe::mcp::llm::ChatResult;

namespace {

std::vector<ChatMessage> SmokeMessages() {
  return {{ "user", "Reply with exactly: MothProbe C++ LLM smoke OK" }};
}

void RequireHandledProviderResult(const ChatResult& result) {
  if (result.ok) {
    REQUIRE_FALSE(result.text.empty());
    return;
  }
  INFO(result.error);
  REQUIRE_FALSE(result.error.empty());
  REQUIRE_FALSE(result.error_kind.empty());
  if (result.error_kind == "rate_limited" || result.error_kind == "timeout" ||
      result.error_kind == "bad_gateway" || result.error_kind == "server_error" ||
      result.error_kind == "network_error") {
    REQUIRE(result.retryable);
  }
}

}  // namespace

TEST_CASE("Gemini adapter fetches a real response when configured", "[llm][gemini]") {
  const auto runtime = mothprobe::core::LoadRuntimeConfig();
  const auto providers = mothprobe::core::LoadLlmProviderConfigs(runtime);
  const auto it = providers.find("gemini");
  REQUIRE(it != providers.end());
  if (it->second.api_key.empty()) {
    WARN("Gemini api_key is not configured in .mothprobe/config.toml; skipping live Gemini smoke.");
    return;
  }
  GeminiAdapter adapter(it->second);
  auto result = adapter.Chat(SmokeMessages());
  RequireHandledProviderResult(result);
}

TEST_CASE("OpenRouter adapter fetches a real response when configured", "[llm][openrouter]") {
  const auto runtime = mothprobe::core::LoadRuntimeConfig();
  const auto providers = mothprobe::core::LoadLlmProviderConfigs(runtime);
  const auto it = providers.find("openrouter");
  REQUIRE(it != providers.end());
  if (it->second.api_key.empty()) {
    WARN("OpenRouter api_key is not configured in .mothprobe/config.toml; skipping live OpenRouter smoke.");
    return;
  }
  OpenRouterAdapter adapter(it->second);
  auto result = adapter.Chat(SmokeMessages());
  RequireHandledProviderResult(result);
}
