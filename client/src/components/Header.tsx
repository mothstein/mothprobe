import React from "react";
import { Box, Text } from "ink";
import gradient from "gradient-string";
import { useStore } from "../store.js";

const LOGO = [
  "▄    ▄",
  "▄▄██▄▄ ▄████▄ ██████ ██  ██ █████▄ █████▄   ▄███▄  █████▄ ██████",
  "█░██░█ ██  ██   ██   ██████ ██▄▄█▀ ██▄▄██▄ ██(+)██ ██▄▄██ ██▄▄",
  "█░[]░█ ▀████▀   ██   ██  ██ ██     ██   ██  ▀███▀  ██▄▄█▀ ██▄▄▄▄",
];

export function Header() {
  const theme = useStore((state) => state.theme);
  const logo = gradient([theme.accent, theme.error])(LOGO.join("\n"));

  return (
    <Box flexDirection="column" alignItems="center" marginTop={2} marginBottom={1}>
      <Text>{logo}</Text>
      <Text color={theme.muted}>v0.1</Text>
    </Box>
  );
}
