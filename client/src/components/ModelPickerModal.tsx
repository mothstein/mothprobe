import React, { useEffect, useMemo, useState } from "react";
import { Box, Text, useInput } from "ink";
import SelectInput from "ink-select-input";
import TextInput from "ink-text-input";
import { configureProviderApiKey, fetchModelsForProvider, listLlmProviders, type LlmProviderModel } from "../mcpClient.js";
import { useStore } from "../store.js";

type SelectItem<T> = {
  label: string;
  value: T;
};

type Step = "provider" | "api_key" | "model";

function requiresApiKey(provider: LlmProviderModel | null): boolean {
  if (!provider) return false;
  if (typeof provider.requires_api_key === "boolean") return provider.requires_api_key;
  return !["ollama", "llama.cpp", "llamacpp", "llama-cpp"].includes(provider.name);
}

function apiKeyConfigured(provider: LlmProviderModel | null): boolean {
  if (!provider) return false;
  if (typeof provider.api_key_configured === "boolean") return provider.api_key_configured;
  return provider.configured && requiresApiKey(provider);
}

function providerLabel(provider: LlmProviderModel) {
  const auth = requiresApiKey(provider)
    ? apiKeyConfigured(provider) ? "key ready" : "key missing"
    : "local/no key";
  return `${provider.name} - ${auth}`;
}

function fallbackModels(provider: LlmProviderModel | null): string[] {
  if (!provider) return [];
  if (provider.models?.length) return provider.models;
  return provider.current_model ? [provider.current_model] : [];
}

function apiStatusText(provider: LlmProviderModel | null) {
  if (!provider) return "No provider selected";
  if (!requiresApiKey(provider)) return "API key: not required";
  return apiKeyConfigured(provider)
    ? "API key: configured in .mothprobe/config.toml"
    : "API key: missing in .mothprobe/config.toml";
}

export function ModelPickerModal() {
  const theme = useStore((state) => state.theme);
  const llmConfig = useStore((state) => state.llmConfig);
  const setLlmConfig = useStore((state) => state.setLlmConfig);
  const setModelPickerOpen = useStore((state) => state.setModelPickerOpen);
  const addMessage = useStore((state) => state.addMessage);
  const [step, setStep] = useState<Step>("provider");
  const [providers, setProviders] = useState<LlmProviderModel[]>([]);
  const [selectedProvider, setSelectedProvider] = useState<LlmProviderModel | null>(null);
  const [models, setModels] = useState<string[]>([]);
  const [query, setQuery] = useState("");
  const [loadingProviders, setLoadingProviders] = useState(true);
  const [fetchingModels, setFetchingModels] = useState(false);
  const [error, setError] = useState("");
  const [modelSource, setModelSource] = useState("config");
  const [apiKeyInput, setApiKeyInput] = useState("");
  const [savingApiKey, setSavingApiKey] = useState(false);

  useInput((_input, key) => {
    if (key.escape) setModelPickerOpen(false);
    if ((step === "model" || step === "api_key") && key.leftArrow) setStep("provider");
  });

  useEffect(() => {
    let active = true;
    setLoadingProviders(true);
    setError("");
    listLlmProviders()
      .then((items) => {
        if (!active) return;
        const sorted = [...items].sort((a, b) => Number(b.configured) - Number(a.configured) || a.name.localeCompare(b.name));
        setProviders(sorted);
        const current = sorted.find((item) => item.name === llmConfig?.provider) || sorted[0] || null;
        setSelectedProvider(current);
        setModels(fallbackModels(current));
      })
      .catch((err) => {
        if (active) setError(err instanceof Error ? err.message : String(err));
      })
      .finally(() => {
        if (active) setLoadingProviders(false);
      });
    return () => {
      active = false;
    };
  }, [llmConfig?.provider]);

  useEffect(() => {
    if (!selectedProvider || step !== "model") return;
    let active = true;
    setFetchingModels(true);
    setError("");
    setQuery("");
    fetchModelsForProvider(selectedProvider.name)
      .then((result) => {
        if (!active) return;
        setModels(result.models.length > 0 ? result.models : fallbackModels(selectedProvider));
        setModelSource(result.source || "provider");
      })
      .catch((err) => {
        if (!active) return;
        setModels(fallbackModels(selectedProvider));
        setModelSource("config");
        setError(err instanceof Error ? err.message : String(err));
      })
      .finally(() => {
        if (active) setFetchingModels(false);
      });
    return () => {
      active = false;
    };
  }, [selectedProvider?.name, step]);

  const providerItems = providers.map((provider) => ({
    label: providerLabel(provider),
    value: provider.name
  }));

  const filteredModelItems = useMemo(() => {
    const needle = query.trim().toLowerCase();
    return models
      .filter((model) => !needle || model.toLowerCase().includes(needle))
      .slice(0, 30)
      .map((model) => ({
        label: model === llmConfig?.model ? `${model} (current)` : model,
        value: model
      }));
  }, [llmConfig?.model, models, query]);

  const handleProviderSelect = (item: SelectItem<string>) => {
    const provider = providers.find((entry) => entry.name === item.value) || null;
    setSelectedProvider(provider);
    setModels(fallbackModels(provider));
    setApiKeyInput("");
    setStep(provider && requiresApiKey(provider) && !apiKeyConfigured(provider) ? "api_key" : "model");
  };

  const handleApiKeySubmit = async () => {
    if (!selectedProvider || !apiKeyInput.trim()) return;
    setSavingApiKey(true);
    setError("");
    try {
      await configureProviderApiKey(selectedProvider.name, apiKeyInput.trim());
      const updated = {
        ...selectedProvider,
        configured: true,
        api_key_configured: true
      };
      setSelectedProvider(updated);
      setProviders((current) =>
        current.map((provider) => provider.name === updated.name ? updated : provider)
      );
      setApiKeyInput("");
      setStep("model");
      addMessage({
        type: "system",
        content: `> API key configured for **${updated.name}** in \`.mothprobe/config.toml\`.`
      });
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    } finally {
      setSavingApiKey(false);
    }
  };

  const handleModelSelect = (item: SelectItem<string>) => {
    if (!selectedProvider) return;
    setLlmConfig({ provider: selectedProvider.name, model: item.value });
    setModelPickerOpen(false);
    addMessage({
      type: "system",
      content: `> Model switched to **${selectedProvider.name} / ${item.value}**`
    });
  };

  const apiStatusColor = selectedProvider && requiresApiKey(selectedProvider) && !apiKeyConfigured(selectedProvider)
    ? theme.warning
    : theme.success;

  return (
    <Box flexDirection="column" alignItems="center" marginY={1}>
      <Box flexDirection="column" width={78} borderStyle="round" borderColor={theme.accent} paddingX={2} paddingY={1}>
        <Box justifyContent="space-between">
          <Text bold color={theme.accent}>Model Picker</Text>
          <Text color={theme.muted}>{step !== "provider" ? "left back | " : ""}esc close</Text>
        </Box>
        <Text color={theme.muted}>
          Config file .mothprobe/config.toml.
        </Text>

        <Box marginTop={1}>
          <Text color={step === "provider" ? theme.accent : theme.success}>1 Provider</Text>
          <Text color={theme.muted}>{"  ->  "}</Text>
          <Text color={step === "api_key" ? theme.accent : step === "model" ? theme.success : theme.muted}>2 API Key</Text>
          <Text color={theme.muted}>{"  ->  "}</Text>
          <Text color={step === "model" ? theme.accent : theme.muted}>3 Model</Text>
        </Box>

        <Box flexDirection="column" borderStyle="single" borderColor={theme.border} paddingX={1} marginY={1}>
          <Text color={theme.text}>
            Selected provider: <Text color={theme.accent}>{selectedProvider?.name || "none"}</Text>
          </Text>
          <Text color={apiStatusColor}>{apiStatusText(selectedProvider)}</Text>
          {selectedProvider && (
            <Text color={theme.muted}>
              Current model: {selectedProvider.current_model || "not set"} | max tokens: {selectedProvider.max_tokens}
            </Text>
          )}
        </Box>

        {loadingProviders && <Text color={theme.warning}>Loading providers from MCP...</Text>}
        {error && <Text color={theme.error}>MCP notice: {error}</Text>}

        {!loadingProviders && providers.length === 0 && (
          <Text color={theme.error}>No LLM providers returned by MCP.</Text>
        )}

        {!loadingProviders && providers.length > 0 && step === "provider" && (
          <Box flexDirection="column">
            <Text color={theme.text} bold>Choose Provider</Text>
            <SelectInput items={providerItems} limit={10} onSelect={handleProviderSelect} />
          </Box>
        )}

        {!loadingProviders && selectedProvider && step === "api_key" && (
          <Box flexDirection="column">
            <Text color={theme.text} bold>Configure API Key for {selectedProvider.name}</Text>
            <Text color={theme.muted}>
              The key will be saved by MCP backend into .mothprobe/config.toml.
            </Text>
            <Box borderStyle="single" borderColor={theme.border} paddingX={1} marginY={1}>
              <Text color={theme.muted}>API key: </Text>
              <TextInput
                value={apiKeyInput}
                onChange={setApiKeyInput}
                onSubmit={handleApiKeySubmit}
                mask="*"
                placeholder="paste key and press enter"
              />
            </Box>
            <Text color={savingApiKey ? theme.warning : theme.muted}>
              {savingApiKey ? "Saving API key..." : "Press enter to save, left arrow to choose another provider."}
            </Text>
          </Box>
        )}

        {!loadingProviders && selectedProvider && step === "model" && (
          <Box flexDirection="column">
            <Text color={theme.text} bold>Choose Model for {selectedProvider.name}</Text>
            <Text color={theme.muted}>
              {fetchingModels ? "Fetching live model list..." : `Source: ${modelSource} | ${models.length} model(s)`}
            </Text>
            <Box borderStyle="single" borderColor={theme.border} paddingX={1} marginY={1}>
              <Text color={theme.muted}>Search: </Text>
              <TextInput value={query} onChange={setQuery} placeholder="type keyword..." />
            </Box>
            {!fetchingModels && filteredModelItems.length > 0 && (
              <SelectInput items={filteredModelItems} limit={10} onSelect={handleModelSelect} />
            )}
            {!fetchingModels && filteredModelItems.length === 0 && (
              <Text color={theme.error}>No model matched your search.</Text>
            )}
          </Box>
        )}
      </Box>
    </Box>
  );
}
