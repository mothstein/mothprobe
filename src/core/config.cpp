#include "core/config.hpp"

#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>

#include <toml++/toml.hpp>

namespace fs = std::filesystem;

namespace mothprobe::core {
namespace {

std::string Trim(const std::string& value) {
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return {};
  const auto last = value.find_last_not_of(" \t\r\n");
  return value.substr(first, last - first + 1);
}

std::string TomlString(const std::string& value) {
  std::string out = "\"";
  for (const char c : value) {
    if (c == '\\' || c == '"') out.push_back('\\');
    out.push_back(c);
  }
  out.push_back('"');
  return out;
}

bool IsSection(const std::string& line) {
  const auto trimmed = Trim(line);
  return trimmed.size() >= 2 && trimmed.front() == '[' && trimmed.back() == ']';
}

std::string ProviderSection(const std::string& provider) {
  return "[llm." + provider + "]";
}

LlmProviderConfig DefaultProvider(const std::string& provider) {
  if (provider == "gemini") {
    return {provider, "", "https://generativelanguage.googleapis.com/v1beta",
            "",
            "/models",
            "gemini-2.5-flash",
            {"gemini-2.5-flash", "gemini-2.5-pro", "gemini-1.5-flash", "gemini-1.5-pro"},
            4096,
            true,
            false};
  }
  if (provider == "openrouter") {
    return {provider,
            "",
            "https://openrouter.ai/api/v1",
            "/chat/completions",
            "/models",
            "google/gemini-2.5-flash",
            {"google/gemini-2.5-flash",
             "google/gemini-2.5-pro",
             "anthropic/claude-sonnet-4",
             "openai/gpt-4.1",
             "deepseek/deepseek-chat-v3-0324"},
            4096,
            true,
            true};
  }
  if (provider == "openai") {
    return {provider,
            "",
            "https://api.openai.com/v1",
            "/chat/completions",
            "/models",
            "gpt-4o-mini",
            {"gpt-4o-mini", "gpt-4o", "gpt-4.1-mini", "gpt-4.1"},
            4096,
            true,
            true};
  }
  if (provider == "deepseek") {
    return {provider,
            "",
            "https://api.deepseek.com/v1",
            "/chat/completions",
            "/models",
            "deepseek-chat",
            {"deepseek-chat", "deepseek-reasoner"},
            4096,
            true,
            true};
  }
  if (provider == "claude") {
    return {provider,
            "",
            "https://api.anthropic.com/v1",
            "/messages",
            "/models",
            "claude-3-5-haiku-latest",
            {"claude-3-5-haiku-latest", "claude-sonnet-4-0", "claude-opus-4-0"},
            4096,
            true,
            false};
  }
  if (provider == "kimi") {
    return {provider,
            "",
            "https://api.moonshot.ai/v1",
            "/chat/completions",
            "/models",
            "moonshot-v1-8k",
            {"moonshot-v1-8k", "moonshot-v1-32k", "moonshot-v1-128k", "kimi-k2-0711-preview"},
            4096,
            true,
            true};
  }
  if (provider == "mistral") {
    return {provider,
            "",
            "https://api.mistral.ai/v1",
            "/chat/completions",
            "/models",
            "mistral-small-latest",
            {"mistral-small-latest", "mistral-medium-latest", "mistral-large-latest",
             "codestral-latest"},
            4096,
            true,
            true};
  }
  if (provider == "nvidia_nim" || provider == "nvidia-nim") {
    return {"nvidia_nim",
            "",
            "https://integrate.api.nvidia.com/v1",
            "/chat/completions",
            "/models",
            "meta/llama-3.1-8b-instruct",
            {"meta/llama-3.1-8b-instruct", "meta/llama-3.1-70b-instruct",
             "nvidia/llama-3.1-nemotron-70b-instruct"},
            4096,
            true,
            true};
  }
  if (provider == "groq") {
    return {provider,
            "",
            "https://api.groq.com/openai/v1",
            "/chat/completions",
            "/models",
            "llama3-8b-8192",
            {"llama3-8b-8192", "llama3-70b-8192", "mixtral-8x7b-32768", "gemma2-9b-it"},
            4096,
            true,
            true};
  }
  if (provider == "ollama") {
    return {provider,
            "",
            "http://localhost:11434/v1",
            "/chat/completions",
            "/models",
            "llama3.1",
            {"llama3.1", "llama3.2", "qwen2.5", "mistral", "codellama"},
            4096,
            false,
            true};
  }
  return {provider, "", "", "/chat/completions", "/models", "", {}, 4096, true, true};
}

}  // namespace

fs::path FindProjectRoot() {
  fs::path current = fs::current_path();
  for (int depth = 0; depth < 8; ++depth) {
    if (fs::exists(current / "CMakeLists.txt") || fs::exists(current / "data")) {
      return fs::weakly_canonical(current);
    }
    if (!current.has_parent_path() || current == current.parent_path()) {
      break;
    }
    current = current.parent_path();
  }
  return fs::weakly_canonical(fs::current_path());
}

RuntimeConfig LoadRuntimeConfig() {
  RuntimeConfig config;
  config.project_root = FindProjectRoot();
  config.runtime_root = config.project_root / ".mothprobe";
  config.bin_dir = config.runtime_root / "bin";
  config.caches_dir = config.runtime_root / "caches";
  config.brains_dir = config.runtime_root / "brains";
  config.logs_dir = config.runtime_root / "logs";
  config.audit_file = config.runtime_root / "audit.jsonl";
  return config;
}

std::map<std::string, LlmProviderConfig> LoadLlmProviderConfigs(const RuntimeConfig& config) {
  std::map<std::string, LlmProviderConfig> providers{
      {"gemini", DefaultProvider("gemini")},
      {"openrouter", DefaultProvider("openrouter")},
      {"openai", DefaultProvider("openai")},
      {"deepseek", DefaultProvider("deepseek")},
      {"claude", DefaultProvider("claude")},
      {"kimi", DefaultProvider("kimi")},
      {"mistral", DefaultProvider("mistral")},
      {"nvidia_nim", DefaultProvider("nvidia_nim")},
      {"groq", DefaultProvider("groq")},
      {"ollama", DefaultProvider("ollama")},
      {"custom", DefaultProvider("custom")},
  };
  const auto config_file = config.runtime_root / "config.toml";
  if (!fs::exists(config_file)) {
    return providers;
  }
  try {
    const auto table = toml::parse_file(config_file.string());
    if (auto llm = table["llm"].as_table()) {
      for (const auto& [key, _node] : *llm) {
        const auto name = std::string(key.str());
        if (providers.find(name) == providers.end()) {
          providers[name] = DefaultProvider(name);
          providers[name].provider = name;
        }
      }
      for (auto& [name, provider] : providers) {
        if (auto entry = (*llm)[name].as_table()) {
          provider.api_key = (*entry)["api_key"].value_or(provider.api_key);
          provider.base_url = (*entry)["base_url"].value_or(provider.base_url);
          provider.chat_path = (*entry)["chat_path"].value_or(provider.chat_path);
          provider.models_path = (*entry)["models_path"].value_or(provider.models_path);
          provider.model_name = (*entry)["model_name"].value_or(provider.model_name);
          provider.model_name = (*entry)["model"].value_or(provider.model_name);
          provider.requires_api_key =
              (*entry)["requires_api_key"].value_or(provider.requires_api_key);
          provider.openai_compatible =
              (*entry)["openai_compatible"].value_or(provider.openai_compatible);
          if (auto models = (*entry)["models"].as_array()) {
            provider.models.clear();
            for (const auto& model : *models) {
              if (auto value = model.value<std::string>()) provider.models.push_back(*value);
            }
          }
          provider.max_tokens = (*entry)["max_tokens"].value_or(provider.max_tokens);
          if (provider.max_tokens < 256) provider.max_tokens = 256;
        }
      }
    }
  } catch (const toml::parse_error&) {
  }
  return providers;
}

void SaveLlmProviderApiKey(const RuntimeConfig& config, const std::string& provider,
                           const std::string& api_key) {
  if (provider.empty()) {
    throw std::runtime_error("provider is required");
  }
  if (api_key.empty()) {
    throw std::runtime_error("api_key is required");
  }

  fs::create_directories(config.runtime_root);
  const auto config_file = config.runtime_root / "config.toml";
  std::vector<std::string> lines;
  if (fs::exists(config_file)) {
    std::ifstream in(config_file);
    std::string line;
    while (std::getline(in, line)) {
      lines.push_back(line);
    }
  }

  const auto section = ProviderSection(provider);
  auto section_pos = lines.end();
  for (auto it = lines.begin(); it != lines.end(); ++it) {
    if (Trim(*it) == section) {
      section_pos = it;
      break;
    }
  }

  const std::string api_key_line = "api_key = " + TomlString(api_key);
  if (section_pos == lines.end()) {
    if (!lines.empty() && !Trim(lines.back()).empty()) {
      lines.push_back("");
    }
    lines.push_back(section);
    lines.push_back(api_key_line);
  } else {
    auto insert_pos = section_pos + 1;
    bool replaced = false;
    for (auto it = section_pos + 1; it != lines.end() && !IsSection(*it); ++it) {
      insert_pos = it + 1;
      const auto trimmed = Trim(*it);
      if (trimmed.rfind("api_key", 0) == 0) {
        *it = api_key_line;
        replaced = true;
        break;
      }
    }
    if (!replaced) {
      lines.insert(insert_pos, api_key_line);
    }
  }

  std::ofstream out(config_file, std::ios::binary | std::ios::trunc);
  if (!out) {
    throw std::runtime_error("cannot write config.toml");
  }
  for (const auto& line : lines) {
    out << line << '\n';
  }
}

void EnsureRuntimeLayout(const RuntimeConfig& config) {
  fs::create_directories(config.bin_dir);
  fs::create_directories(config.caches_dir);
  fs::create_directories(config.brains_dir);
  fs::create_directories(config.logs_dir);
}

std::string ReadTextFileLimited(const fs::path& path, std::size_t max_bytes) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    throw std::runtime_error("cannot open file: " + path.string());
  }
  std::ostringstream out;
  char buffer[512];
  std::size_t total = 0;
  while (in && total < max_bytes) {
    const std::size_t want = std::min(sizeof(buffer), max_bytes - total);
    in.read(buffer, static_cast<std::streamsize>(want));
    const auto got = static_cast<std::size_t>(in.gcount());
    out.write(buffer, static_cast<std::streamsize>(got));
    total += got;
  }
  return out.str();
}

}  // namespace mothprobe::core
