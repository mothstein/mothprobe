import React from "react";
import { Box, Text } from "ink";
import gradient from "gradient-string";
import { useStore } from "../store.js";

const LOGO = [
  " __  __  ___ _____ _   _ ____  ____  ____   ___  ____  _____",
  "|  \\/  |/ _ \\_   _| | | |  _ \\|  _ \\|  _ \\ / _ \\| __ )| ____|",
  "| |\\/| | | | || | | |_| | |_) | |_) | |_) | | | |  _ \\|  _|",
  "| |  | | |_| || | |  _  |  __/|  _ <|  __/| |_| | |_) | |___",
  "|_|  |_|\\___/ |_| |_| |_|_|   |_| \\_\\_|    \\___/|____/|_____|",
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
