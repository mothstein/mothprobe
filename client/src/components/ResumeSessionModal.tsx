import React, { useEffect, useState } from "react";
import { Box, Text, useInput } from "ink";
import SelectInput from "ink-select-input";
import { loadChatSession, listChatSessions, type ChatSessionPayload, type ChatSessionSummary } from "../mcpClient.js";
import { useStore, type ChatMessage } from "../store.js";

type Item = {
  label: string;
  value: string;
};

function sessionToHistory(session: ChatSessionPayload): ChatMessage[] {
  const marker: ChatMessage = {
    id: `session-${session.summary.session_id}`,
    type: "system",
    content: `Session resumed: **${session.summary.title}** (${session.summary.updated_at})`
  };
  const messages = (session.messages || []).map((message: any, index: number) => ({
    id: `${session.summary.session_id}-${index}`,
    type: message.role === "assistant" ? "ai" as const : message.role === "user" ? "user" as const : "system" as const,
    content: message.content || "",
    reasoning: message.reasoning || undefined
  }));
  return [marker, ...messages];
}

function labelForSession(session: ChatSessionSummary) {
  const title = session.title || "Untitled session";
  return `${title.slice(0, 42)} | ${session.updated_at} | ${session.message_count} msg`;
}

export function ResumeSessionModal() {
  const theme = useStore((state) => state.theme);
  const setResumePickerOpen = useStore((state) => state.setResumePickerOpen);
  const setChatHistory = useStore((state) => state.setChatHistory);
  const setActiveSessionId = useStore((state) => state.setActiveSessionId);
  const setReasoningMode = useStore((state) => state.setReasoningMode);
  const [sessions, setSessions] = useState<ChatSessionSummary[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState("");

  useInput((_input, key) => {
    if (key.escape) setResumePickerOpen(false);
  });

  useEffect(() => {
    let active = true;
    listChatSessions()
      .then((items: ChatSessionSummary[]) => {
        if (active) setSessions(items);
      })
      .catch((err: any) => {
        if (active) setError(err instanceof Error ? err.message : String(err));
      })
      .finally(() => {
        if (active) setLoading(false);
      });
    return () => {
      active = false;
    };
  }, []);

  const handleSelect = async (item: Item) => {
    try {
      const session = await loadChatSession(item.value);
      setChatHistory(sessionToHistory(session));
      setActiveSessionId(session.session_id);
      setReasoningMode(session.reasoning_mode || "default");
      setResumePickerOpen(false);
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    }
  };

  const items = sessions.map((session) => ({
    label: labelForSession(session),
    value: session.session_id
  }));

  return (
    <Box flexDirection="column" alignItems="center" marginY={1}>
      <Box flexDirection="column" width={86} borderStyle="round" borderColor={theme.accent} paddingX={2} paddingY={1}>
        <Box justifyContent="space-between">
          <Text bold color={theme.accent}>Resume Session</Text>
          <Text color={theme.muted}>esc close</Text>
        </Box>
        <Text color={theme.muted}>Sessions are loaded from the C++ MCP backend under .mothprobe/brains/.</Text>
        {loading && <Text color={theme.warning}>Loading sessions...</Text>}
        {error && <Text color={theme.error}>{error}</Text>}
        {!loading && items.length === 0 && <Text color={theme.muted}>No saved chat sessions yet.</Text>}
        {!loading && items.length > 0 && (
          <SelectInput items={items} limit={10} onSelect={handleSelect} />
        )}
      </Box>
    </Box>
  );
}
