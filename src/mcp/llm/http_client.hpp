#pragma once

#include <string>
#include <utility>
#include <vector>

namespace mothprobe::mcp::llm {

struct HttpResponse {
  bool ok = false;
  int status = 0;
  std::string body;
  std::string error;
  int attempts = 0;
};

using Headers = std::vector<std::pair<std::string, std::string>>;

HttpResponse PostJson(const std::string& url, const Headers& headers, const std::string& body);
HttpResponse GetJson(const std::string& url, const Headers& headers);

}  // namespace mothprobe::mcp::llm
