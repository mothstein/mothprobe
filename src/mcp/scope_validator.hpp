#pragma once

#include <string>
#include <vector>

namespace mothprobe::mcp {

class ScopeValidator {
 public:
  ScopeValidator();

  void SetAllowed(std::vector<std::string> targets);
  bool Allowed(const std::string& target) const;
  const std::vector<std::string>& Targets() const;

 private:
  std::vector<std::string> allowed_;
};

}  // namespace mothprobe::mcp
