import React, { useEffect, useState } from "react";
import { Box, Text, useWindowSize } from "ink";
import { useStore, type ChatMessage } from "../store.js";
import { Divider } from "./Divider.js";
import { MarkdownRenderer } from "./MarkdownRenderer.js";

const spinner = ["   ", ".  ", ".. ", "...", " ..", "  ."];

function roleColor(msg: ChatMessage, theme: ReturnType<typeof useStore.getState>["theme"]) {
  if (msg.type === "user") return theme.user;
  if (msg.type === "ai") return theme.ai;
  if (msg.type === "shell") return theme.warning;
  if (msg.type === "mcp") return theme.accent;
  return theme.muted;
}

function roleTitle(msg: ChatMessage) {
  if (msg.type === "ai") return "mothprobe";
  if (msg.type === "mcp") return "mcp";
  if (msg.type === "shell") return "shell";
  if (msg.type === "system") return "notice";
  return "";
}

function formatDuration(ms?: number) {
  if (!ms || ms < 1000) return "less than 1s";
  const totalSeconds = Math.max(1, Math.round(ms / 1000));
  const hours = Math.floor(totalSeconds / 3600);
  const minutes = Math.floor((totalSeconds % 3600) / 60);
  const seconds = totalSeconds % 60;
  const parts: string[] = [];
  if (hours) parts.push(`${hours}h`);
  if (minutes) parts.push(`${minutes}m`);
  if (seconds || parts.length === 0) parts.push(`${seconds}s`);
  return parts.join(" ");
}

function ThoughtBlock({ msg }: { msg: ChatMessage }) {
  const theme = useStore((state) => state.theme);
  if (!msg.reasoning && !msg.thoughtDurationMs) return null;

  const label = `thought for ${formatDuration(msg.thoughtDurationMs)}`;
  if (!msg.reasoning) {
    return <Text color={theme.muted}>{label}</Text>;
  }
  if (!msg.thoughtExpanded) {
    return (
      <Text color={theme.muted}>
        {label} <Text color={theme.accent}>(ctrl+o expand)</Text>
      </Text>
    );
  }
  return (
    <Box borderStyle="single" borderColor={theme.border} paddingX={1} marginTop={1} marginBottom={1} flexDirection="column">
      <Text color={theme.muted} italic>{label} (ctrl+o collapse)</Text>
      <MarkdownRenderer content={msg.reasoning} theme={theme} />
    </Box>
  );
}

function ChatMessageView({ msg, frame }: { msg: ChatMessage; frame: number }) {
  const theme = useStore((state) => state.theme);
  const color = roleColor(msg, theme);

  if (msg.type === "user") {
    return (
      <Text color={theme.text}>
        <Text color={theme.user}>{"> "}</Text>
        {msg.content}
      </Text>
    );
  }

  if (msg.type === "ai" && msg.isLoading) {
    return (
      <Box flexDirection="column">
        <Text color={theme.ai}>
          mothprobe <Text color={theme.muted}>is thinking{spinner[frame % spinner.length]}</Text>
        </Text>
        <Text color={theme.muted}>esc interrupt</Text>
      </Box>
    );
  }

  return (
    <Box flexDirection="column">
      <Text color={color}>{roleTitle(msg)}</Text>
      <ThoughtBlock msg={msg} />
      <MarkdownRenderer content={`${msg.content}${msg.isTyping ? " |" : ""}`} theme={theme} />
      {msg.interrupted && <Text color={theme.warning}>interrupted</Text>}
    </Box>
  );
}

export function ChatArea() {
  const [frame, setFrame] = useState(0);
  const chatHistory = useStore((state) => state.chatHistory);
  const typeNextChunk = useStore((state) => state.typeNextChunk);
  const visibleHistory = chatHistory.slice(-15);
  const typingMessage = chatHistory.find((message) => message.isTyping);
  const loadingMessage = chatHistory.find((message) => message.isLoading);
  const { columns } = useWindowSize();

  useEffect(() => {
    if (!typingMessage) return;
    const timer = setInterval(() => typeNextChunk(typingMessage.id, 3), 20);
    return () => clearInterval(timer);
  }, [typingMessage?.id, typeNextChunk]);

  useEffect(() => {
    if (!loadingMessage) return;
    const timer = setInterval(() => setFrame((current) => current + 1), 80);
    return () => clearInterval(timer);
  }, [loadingMessage?.id]);

  return (
    <Box flexDirection="column" flexGrow={1} minHeight={10}>
      {visibleHistory.map((msg) => (
        <Box key={msg.id} marginBottom={1} flexDirection="column" width={columns}>
          <ChatMessageView msg={msg} frame={frame} />
          <Divider width={columns} />
        </Box>
      ))}
    </Box>
  );
}
