import React from "react";
import { Box, Text } from "ink";
import { useStore } from "../store.js";

export function Divider({ width }: { width: number }) {
  const theme = useStore((state) => state.theme);
  return (
    <Box width={width}>
      <Text color={theme.border} wrap="truncate">
        {"\u2500".repeat(Math.max(0, width - 2))}
      </Text>
    </Box>
  );
}
