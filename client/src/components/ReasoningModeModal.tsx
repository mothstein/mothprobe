import React from "react";
import { Box, Text, useInput } from "ink";
import SelectInput from "ink-select-input";
import { setReasoningMode as setBackendReasoningMode, type ReasoningMode } from "../mcpClient.js";
import { useStore } from "../store.js";

type Item = {
  label: string;
  value: ReasoningMode;
};

const items: Item[] = [
  { label: "default - provider default reasoning", value: "default" },
  { label: "advanced - high effort / thinking budget", value: "advanced" },
  { label: "fast - lower effort where supported", value: "fast" }
];

export function ReasoningModeModal() {
  const theme = useStore((state) => state.theme);
  const setReasoningPickerOpen = useStore((state) => state.setReasoningPickerOpen);
  const setReasoningMode = useStore((state) => state.setReasoningMode);
  const setActiveSessionId = useStore((state) => state.setActiveSessionId);
  const addMessage = useStore((state) => state.addMessage);

  useInput((_input, key) => {
    if (key.escape) setReasoningPickerOpen(false);
  });

  const handleSelect = async (item: Item) => {
    try {
      const session = await setBackendReasoningMode(item.value);
      setReasoningMode(session.reasoning_mode || item.value);
      setActiveSessionId(session.session_id);
      addMessage({ type: "system", content: `Reasoning mode set to **${session.reasoning_mode || item.value}**.` });
      setReasoningPickerOpen(false);
    } catch (err) {
      addMessage({ type: "system", content: `Reasoning mode update failed: ${err instanceof Error ? err.message : String(err)}` });
      setReasoningPickerOpen(false);
    }
  };

  return (
    <Box flexDirection="column" alignItems="center" marginY={1}>
      <Box flexDirection="column" width={72} borderStyle="round" borderColor={theme.accent} paddingX={2} paddingY={1}>
        <Box justifyContent="space-between">
          <Text bold color={theme.accent}>Reasoning Mode</Text>
          <Text color={theme.muted}>esc close</Text>
        </Box>
        <Text color={theme.muted}>Stored in the active C++ chat session.</Text>
        <SelectInput items={items} onSelect={handleSelect} />
      </Box>
    </Box>
  );
}
