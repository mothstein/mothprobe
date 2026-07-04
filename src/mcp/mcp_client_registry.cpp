#include "mcp/mcp_client_registry.hpp"

#include <algorithm>
#include <regex>
#include <stdexcept>

#include "mcp/llm/http_client.hpp"

namespace mothprobe::mcp {
namespace {

bool ValidName(const std::string& name) {
  static const std::regex pattern(R"(^[A-Za-z0-9_.-]{1,80}$)");
  return std::regex_match(name, pattern);
}

bool ValidTransport(const std::string& transport) {
  return transport == "stdio" || transport == "http" || transport == "sse";
}

std::vector<std::string> ArgsFromJson(const nlohmann::json& value) {
  std::vector<std::string> args;
  if (!value.is_array()) return args;
  for (const auto& item : value) {
    if (item.is_string()) args.push_back(item.get<std::string>());
  }
  return args;
}

std::map<std::string, std::string> EnvFromJson(const nlohmann::json& value) {
  std::map<std::string, std::string> env;
  if (!value.is_object()) return env;
  for (const auto& [key, item] : value.items()) {
    if (item.is_string()) env[key] = item.get<std::string>();
  }
  return env;
}

auto FindClient(std::vector<core::McpClientConfig>& clients, const std::string& name) {
  return std::find_if(clients.begin(), clients.end(), [&](const auto& client) {
    return client.id == name || client.name == name;
  });
}

auto FindClientConst(const std::vector<core::McpClientConfig>& clients, const std::string& name) {
  return std::find_if(clients.begin(), clients.end(), [&](const auto& client) {
    return client.id == name || client.name == name;
  });
}

std::string StatusForConnect(const core::McpClientConfig& client, bool* connected) {
  if (client.transport == "stdio") {
    *connected = true;
    return "stdio configured";
  }
  auto response = llm::GetJson(client.url, {});
  *connected = response.ok && response.status >= 200 && response.status < 500;
  return response.ok ? ("HTTP " + std::to_string(response.status)) : response.error;
}

}  // namespace

McpClientRegistry::McpClientRegistry(core::RuntimeConfig config) : config_(std::move(config)) {}

nlohmann::json McpClientRegistry::List() const {
  nlohmann::json clients = nlohmann::json::array();
  for (const auto& client : core::LoadMcpClientConfigs(config_)) {
    clients.push_back(core::McpClientConfigJson(client));
  }
  return {{"clients", clients}};
}

nlohmann::json McpClientRegistry::Get(const std::string& name) const {
  if (!ValidName(name)) throw std::runtime_error("invalid MCP client name");
  const auto clients = core::LoadMcpClientConfigs(config_);
  const auto it = FindClientConst(clients, name);
  if (it == clients.end()) throw std::runtime_error("MCP client not found: " + name);
  return {{"client", core::McpClientConfigJson(*it)}};
}

nlohmann::json McpClientRegistry::Add(const nlohmann::json& params) const {
  auto name = params.value("name", std::string{});
  auto id = params.value("id", name);
  const auto transport = params.value("transport", std::string{});
  if (!ValidName(id) || !ValidName(name)) throw std::runtime_error("invalid MCP client name");
  if (!ValidTransport(transport)) throw std::runtime_error("transport must be stdio, http, or sse");

  auto clients = core::LoadMcpClientConfigs(config_);
  if (FindClient(clients, id) != clients.end() || FindClient(clients, name) != clients.end()) {
    throw std::runtime_error("MCP client already exists: " + name);
  }

  core::McpClientConfig client;
  client.id = id;
  client.name = name;
  client.transport = transport;
  client.command = params.value("command", std::string{});
  client.args = ArgsFromJson(params.value("args", nlohmann::json::array()));
  client.env = EnvFromJson(params.value("env", nlohmann::json::object()));
  client.url = params.value("url", std::string{});
  client.sse_endpoint = params.value("sse_endpoint", client.sse_endpoint);
  client.metadata = params.value("metadata", nlohmann::json::object());
  client.created_at = params.value("created_at", std::string{});
  client.updated_at = params.value("updated_at", client.created_at);
  if (client.transport == "stdio" && client.command.empty()) {
    throw std::runtime_error("stdio MCP client requires command");
  }
  if ((client.transport == "http" || client.transport == "sse") && client.url.empty()) {
    throw std::runtime_error("HTTP/SSE MCP client requires url");
  }
  clients.push_back(client);
  core::SaveMcpClientConfigs(config_, clients);
  return {{"client", core::McpClientConfigJson(client)}};
}

nlohmann::json McpClientRegistry::Remove(const std::string& name) const {
  if (!ValidName(name)) throw std::runtime_error("invalid MCP client name");
  auto clients = core::LoadMcpClientConfigs(config_);
  const auto before = clients.size();
  clients.erase(std::remove_if(clients.begin(), clients.end(), [&](const auto& client) {
                  return client.id == name || client.name == name;
                }),
                clients.end());
  if (clients.size() == before) throw std::runtime_error("MCP client not found: " + name);
  core::SaveMcpClientConfigs(config_, clients);
  return {{"removed", name}};
}

nlohmann::json McpClientRegistry::Login(const std::string& name,
                                        const nlohmann::json& params) const {
  auto clients = core::LoadMcpClientConfigs(config_);
  auto it = FindClient(clients, name);
  if (it == clients.end()) throw std::runtime_error("MCP client not found: " + name);
  it->authenticated = true;
  it->updated_at = params.value("updated_at", std::string{});
  it->metadata["auth"] = {{"type", params.value("type", std::string("external"))},
                          {"username", params.value("username", std::string{})},
                          {"has_token", params.contains("token") || params.contains("key") ||
                                            params.contains("auth_header")}};
  core::SaveMcpClientConfigs(config_, clients);
  return {{"client", core::McpClientConfigJson(*it)}};
}

nlohmann::json McpClientRegistry::Logout(const std::string& name) const {
  auto clients = core::LoadMcpClientConfigs(config_);
  auto it = FindClient(clients, name);
  if (it == clients.end()) throw std::runtime_error("MCP client not found: " + name);
  it->authenticated = false;
  it->metadata.erase("auth");
  core::SaveMcpClientConfigs(config_, clients);
  return {{"client", core::McpClientConfigJson(*it)}};
}

nlohmann::json McpClientRegistry::Connect(const std::string& name) const {
  auto clients = core::LoadMcpClientConfigs(config_);
  auto it = FindClient(clients, name);
  if (it == clients.end()) throw std::runtime_error("MCP client not found: " + name);
  bool connected = false;
  it->status = StatusForConnect(*it, &connected);
  it->connected = connected;
  it->last_error = connected ? "" : it->status;
  core::SaveMcpClientConfigs(config_, clients);
  return {{"client", core::McpClientConfigJson(*it)}};
}

nlohmann::json McpClientRegistry::Disconnect(const std::string& name) const {
  auto clients = core::LoadMcpClientConfigs(config_);
  auto it = FindClient(clients, name);
  if (it == clients.end()) throw std::runtime_error("MCP client not found: " + name);
  it->connected = false;
  it->status = "disconnected";
  it->last_error.clear();
  core::SaveMcpClientConfigs(config_, clients);
  return {{"client", core::McpClientConfigJson(*it)}};
}

}  // namespace mothprobe::mcp
