#include <iostream>

#include "mothprobe/version.hpp"

int main() {
  std::cout << mothprobe::kProjectName << " " << mothprobe::kVersion
            << "\nRun the TypeScript client or mothprobe_mcp directly.\n";
  return 0;
}
