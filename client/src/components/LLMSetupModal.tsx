import React, { useState } from "react";
import { Box, Text } from "ink";
import SelectInput from "ink-select-input";
import TextInput from "ink-text-input";
import { useStore } from "../store.js";

const providers = [
  { label: "Gemini", value: "gemini" },
  { label: "OpenRouter", value: "openrouter" },
  { label: "Ollama (Local)", value: "ollama" }
];

export function LLMSetupModal() {
  const [provider, setProvider] = useState("");
  const theme = useStore((state) => state.theme);
  const setLlmConfig = useStore((state) => state.setLlmConfig);

  const handleProviderSelect = (item: { label: string; value: string }) => {
    setProvider(item.value);
  };

  const handleFinish = () => {
    if (provider) setLlmConfig({ provider });
  };

  return (
    <Box flexDirection="column" alignItems="center" marginBottom={1}>
      <Box flexDirection="column" width={64} borderStyle="single" borderColor={theme.border} padding={1}>
        <Text color={theme.warning} bold>Select AI provider handled by the C++ MCP backend.</Text>
        {!provider ? (
          <Box flexDirection="column" marginTop={1}>
            <SelectInput items={providers} onSelect={handleProviderSelect} />
          </Box>
        ) : (
          <Box flexDirection="column" marginTop={1}>
            <Text color={theme.success}>Provider: {provider}</Text>
            <Text color={theme.muted}>API keys remain in C++ config/env. Press Enter to continue.</Text>
            <TextInput value="" onChange={() => undefined} onSubmit={handleFinish} />
          </Box>
        )}
      </Box>
    </Box>
  );
}
