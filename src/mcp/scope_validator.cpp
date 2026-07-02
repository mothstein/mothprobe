#include "mcp/scope_validator.hpp"

#include <algorithm>
#include <regex>

namespace mothprobe::mcp {

ScopeValidator::ScopeValidator() : allowed_({"localhost", "127.0.0.1", "example.com"}) {}

void ScopeValidator::SetAllowed(std::vector<std::string> targets) {
  targets.erase(std::remove_if(targets.begin(), targets.end(),
                               [](const std::string& value) { return value.empty(); }),
                targets.end());
  if (!targets.empty()) {
    allowed_ = std::move(targets);
  }
}

bool ScopeValidator::Allowed(const std::string& target) const {
  static const std::regex safe(R"(^[A-Za-z0-9._~:/?#\[\]@!$'()*+,;=%-]+$)");
  if (!std::regex_match(target, safe)) {
    return false;
  }
  return std::any_of(allowed_.begin(), allowed_.end(), [&](const std::string& item) {
    if (item == "*") {
      return true;
    }
    if (!item.empty() && item.back() == '*') {
      return target.rfind(item.substr(0, item.size() - 1), 0) == 0;
    }
    return target == item || target.find("://" + item) != std::string::npos;
  });
}

const std::vector<std::string>& ScopeValidator::Targets() const {
  return allowed_;
}

}  // namespace mothprobe::mcp
