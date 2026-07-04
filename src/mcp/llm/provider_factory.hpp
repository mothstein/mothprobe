#pragma once

#include <memory>
#include <string>
#include <vector>

#include "mcp/llm/provider_interface.hpp"

namespace mothprobe::mcp::llm {

std::unique_ptr<IProvider> CreateProvider(const ProviderMap& providers,
                                          const std::string& provider);
std::vector<std::string> ConfiguredProviders(const ProviderMap& providers);
ModelListResult FetchProviderModels(const ProviderMap& providers, const std::string& provider);

}  // namespace mothprobe::mcp::llm
