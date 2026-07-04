import React from "react";
import { Box, Text } from "ink";
import { useStore } from "../store.js";

const commands = [
  { cmd: "/help", desc: "show help", key: "ctrl+h" },
  { cmd: "/init", desc: "open mcp session", key: "ctrl+i" },
  { cmd: "/tools", desc: "list tools", key: "ctrl+t" },
  { cmd: "/model", desc: "switch provider/model", key: "tab" },
  { cmd: "/theme", desc: "switch theme", key: "tab" },
  { cmd: "/clear", desc: "clear conversation", key: "ctrl+l" },
  { cmd: "@file", desc: "attach file context", key: "tab" },
  { cmd: "!cmd", desc: "run approved shell", key: "ctrl+!" }
];

export function HelpPanel() {
  const theme = useStore((state) => state.theme);
  return (
    <Box flexDirection="column" alignItems="center" marginBottom={3}>
      <Box flexDirection="column" width={64}>
        {commands.map((item) => (
          <Box key={item.cmd}>
            <Box width={14}>
              <Text color={theme.text}>{item.cmd}</Text>
            </Box>
            <Box width={34}>
              <Text color={theme.muted}>{item.desc}</Text>
            </Box>
            <Box width={12} justifyContent="flex-end">
              <Text color={theme.warning}>{item.key}</Text>
            </Box>
          </Box>
        ))}
      </Box>
    </Box>
  );
}
