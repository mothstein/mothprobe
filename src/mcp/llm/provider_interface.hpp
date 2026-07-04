#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/config.hpp"

namespace mothprobe::mcp::llm {

struct ChatMessage {
  std::string role;
  std::string content;
};

struct ReasoningConfig {
  std::string mode = "default";
};

struct ChatResult {
  bool ok = false;
  std::string text;
  std::string error;
  std::string error_kind = "provider_error";
  std::string finish_reason;
  int http_status = 0;
  bool retryable = false;
  bool truncated = false;
  std::string reasoning;
};

struct ModelListResult {
  bool ok = false;
  std::vector<std::string> models;
  std::string error;
  std::string error_kind = "provider_error";
  int http_status = 0;
  bool retryable = false;
  std::string source;
};

class IProvider {
 public:
  virtual ~IProvider() = default;
  virtual std::string Name() const = 0;
  virtual ChatResult Chat(const std::vector<ChatMessage>& messages,
                          const ReasoningConfig& reasoning = {}) = 0;
};

using ProviderMap = std::map<std::string, core::LlmProviderConfig>;

ChatResult HttpErrorResult(const std::string& provider, int status, const std::string& body);
ModelListResult FetchModels(const core::LlmProviderConfig& config);

}  // namespace mothprobe::mcp::llm
