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
  if (!ms) return "";
  if (ms < 1000) return "1s";
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

function frameLine(left: string, fill: string, right: string, width: number, label = "") {
  const labelText = label ? ` ${label} ` : "";
  const fillCount = Math.max(1, width - left.length - right.length - labelText.length);
  return `${left}${labelText}${fill.repeat(fillCount)}${right}`;
}

function ThoughtBlock({ msg, width }: { msg: ChatMessage; width: number }) {
  const theme = useStore((state) => state.theme);
  if (!msg.reasoning && !msg.thoughtDurationMs) return null;

  const duration = formatDuration(msg.thoughtDurationMs);
  const label = duration ? `thought for ${duration}` : "thought";
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
  const frameWidth = Math.max(32, Math.min(width - 4, 120));
  const innerWidth = Math.max(20, frameWidth - 4);
  return (
    <Box marginTop={1} marginBottom={1} flexDirection="column" width={frameWidth}>
      <Text color={theme.border}>{frameLine("+", "-", "+", frameWidth, `${label} ctrl+o collapse`)}</Text>
      <Box flexDirection="row">
        <Text color={theme.border}>| </Text>
        <Box flexDirection="column" width={innerWidth}>
          <MarkdownRenderer content={msg.reasoning} theme={theme} maxWidth={innerWidth} />
        </Box>
        <Text color={theme.border}> |</Text>
      </Box>
      <Text color={theme.border}>{frameLine("+", "-", "+", frameWidth)}</Text>
    </Box>
  );
}

function ChatMessageView({ msg, frame, width }: { msg: ChatMessage; frame: number; width: number }) {
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
      <ThoughtBlock msg={msg} width={width} />
      <MarkdownRenderer content={msg.content} theme={theme} maxWidth={Math.max(24, width - 4)} />
      {msg.isTyping && <Text color={theme.muted}>|</Text>}
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
  const contentWidth = Math.max(40, columns - 4);

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
        <Box key={msg.id} marginBottom={1} flexDirection="column" width={contentWidth}>
          <ChatMessageView msg={msg} frame={frame} width={contentWidth} />
          <Divider width={contentWidth} />
        </Box>
      ))}
    </Box>
  );
}
