import React, { useMemo, useState } from "react";
import { existsSync, readdirSync, statSync } from "node:fs";
import { basename, dirname, isAbsolute, relative, resolve, sep } from "node:path";
import { Box, Text, useInput } from "ink";
import TextInput from "ink-text-input";
import { useStore } from "../store.js";
import { handleInput } from "../inputParser.js";

type Suggestion = {
  value: string;
  label: string;
  description: string;
};

const commands: Suggestion[] = [
  { value: "/help", label: "/help", description: "Show this help message" },
  { value: "/init", label: "/init", description: "Initialize the MCP connection" },
  { value: "/tools", label: "/tools", description: "List available MCP tools" },
  { value: "/status", label: "/status", description: "Check MCP server status" },
  { value: "/model", label: "/model", description: "Open provider and model picker" },
  { value: "/theme", label: "/theme <name>", description: "Switch or list UI themes" },
  { value: "/permission", label: "/permission", description: "Manage shell command permissions" },
  { value: "/resume", label: "/resume", description: "Restore previous chat session" },
  { value: "/reasoning", label: "/reasoning", description: "Toggle reasoning output" },
  { value: "/skills", label: "/skills", description: "List available skills and agents" },
  { value: "/clear", label: "/clear", description: "Clear the conversation history" },
  { value: "/exit", label: "/exit", description: "Exit the application" }
];

function insideWorkspace(root: string, target: string): boolean {
  const rel = relative(root, target);
  return rel === "" || (!rel.startsWith("..") && !isAbsolute(rel));
}

function commandSuggestions(query: string): Suggestion[] {
  const prefix = query.startsWith("/") ? query : `/${query}`;
  return commands.filter((item) => item.value.startsWith(prefix));
}

function fileSuggestions(query: string): Suggestion[] {
  const root = process.cwd();
  //Get the last word if they are typing multiple files inline
  const match = query.match(/(?:^|\s)@\[?([^\s\]]*)$/);
  let fragment = match ? match[1] : query;
  if (fragment.startsWith("@[")) fragment = fragment.slice(2);
  else if (fragment.startsWith("@")) fragment = fragment.slice(1);
  
  const normalized = fragment.replaceAll("/", sep).replaceAll("\\", sep);
  const candidate = resolve(root, normalized || ".");
  const dir = normalized.endsWith(sep) || (existsSync(candidate) && statSync(candidate).isDirectory())
    ? candidate
    : resolve(root, dirname(normalized));
  if (!insideWorkspace(root, dir) || !existsSync(dir)) return [];
  const prefix = normalized.endsWith(sep) ? "" : basename(normalized).toLowerCase();
  
  const results = [];
  if (dir !== root && (prefix === "" || "..".startsWith(prefix))) {
    const parentDir = resolve(dir, "..");
    if (insideWorkspace(root, parentDir)) {
      results.push({ name: "..", isDirectory: () => true } as any);
    }
  }
  results.push(...readdirSync(dir, { withFileTypes: true }));

  return results
    .filter((entry) => entry.name.toLowerCase().startsWith(prefix))
    .sort((a, b) => Number(b.isDirectory()) - Number(a.isDirectory()) || a.name.localeCompare(b.name))
    .map((entry) => {
      const full = resolve(dir, entry.name);
      const rel = relative(root, full).replaceAll("\\", "/");
      return {
        value: entry.isDirectory() ? `@[${rel}/` : `@[${rel}]`,
        label: `${entry.name}${entry.isDirectory() ? "/" : ""}`,
        description: entry.isDirectory() ? "directory" : "file"
      };
    });
}

export function InputBar() {
  const [query, setQuery] = useState("");
  const [selectedIndex, setSelectedIndex] = useState(0);
  const isConnected = useStore((state) => state.isConnected);
  const mode = useStore((state) => state.mode);
  const llmConfig = useStore((state) => state.llmConfig);
  const theme = useStore((state) => state.theme);
  const availableThemes = useStore((state) => state.availableThemes);
  const setMode = useStore((state) => state.setMode);
  const suggestions = useMemo(() => {
    if (query.startsWith("/theme ")) {
      const prefix = query.slice("/theme ".length).toLowerCase();
      return availableThemes
        .filter((name) => name.startsWith(prefix))
        .map((name) => ({ value: `/theme ${name}`, label: name, description: "theme" }));
    }
    if (query.startsWith("/") || mode === "command") return commandSuggestions(query);
    if (query.match(/(?:^|\s)@/) || mode === "file") return fileSuggestions(query);
    return [];
  }, [availableThemes, mode, query]);

  useInput((input, key) => {
    if (key.tab && suggestions.length > 0) {
      const match = query.match(/(?:^|\s)(@\[?[^\s\]]*)$/);
      if (match) {
        setQuery(query.slice(0, -match[1].length) + suggestions[Math.min(selectedIndex, suggestions.length - 1)].value);
      } else {
        setQuery(suggestions[Math.min(selectedIndex, suggestions.length - 1)].value);
      }
      return;
    }
    if (key.upArrow && suggestions.length > 0) {
      setSelectedIndex((current) => Math.max(0, current - 1));
      return;
    }
    if (key.downArrow && suggestions.length > 0) {
      setSelectedIndex((current) => Math.min(suggestions.length - 1, current + 1));
      return;
    }
    if (query === "") {
      if (input === "/") setMode("command");
      else if (input === "!") setMode("shell");
      else if (input === "@") setMode("file");
      else if (key.backspace || key.delete) setMode("chat");
    }
    if (query.length === 1 && (key.backspace || key.delete)) setMode("chat");
  });

  const handleSubmit = async (value: string) => {
    if (suggestions.length > 0) {
      const selected = suggestions[Math.min(selectedIndex, Math.max(0, suggestions.length - 1))];
      
      if ((value.startsWith("/") || mode === "command") && value.trim() !== selected.value) {
        setQuery(selected.value);
        return;
      }
      
      const match = value.match(/(?:^|\s)(@\[?[^\s\]]*)$/);
      if (match) {
        if (!value.endsWith(selected.value)) {
          setQuery(value.slice(0, -match[1].length) + selected.value);
          return;
        }
      }
    }

    let finalInput = value;
    if (mode === "command" && !value.startsWith("/")) finalInput = `/${value}`;
    if (mode === "shell" && !value.startsWith("!")) finalInput = `!${value}`;
    if (mode === "file" && !value.startsWith("@")) finalInput = `@${value}`;
    setQuery("");
    setSelectedIndex(0);
    setMode("chat");
    await handleInput(finalInput);
  };

  const modeColor = mode === "command" ? theme.accent : mode === "shell" ? theme.warning : mode === "file" ? theme.user : theme.accent;
  const prompt = "> "
  const provider = llmConfig?.provider ? `${llmConfig.provider}${llmConfig.model ? ` ${llmConfig.model}` : ""}` : "AI disabled";

  const visibleCount = 8;
  let startIndex = selectedIndex - Math.floor(visibleCount / 2);
  if (startIndex + visibleCount > suggestions.length) startIndex = suggestions.length - visibleCount;
  if (startIndex < 0) startIndex = 0;
  const visibleSuggestions = suggestions.slice(startIndex, startIndex + visibleCount);

  return (
    <Box flexDirection="column" alignItems="center" marginTop={1}>
      <Box width={78} borderStyle="single" borderColor={theme.border} paddingX={1}>
        <Box marginRight={1}>
          <Text color={modeColor}>{prompt}</Text>
        </Box>
        <Box flexGrow={1}>
          <TextInput value={query} onChange={(value) => {
            setQuery(value);
            setSelectedIndex(0);
          }} onSubmit={handleSubmit} placeholder="Start Typing to send Message, Type @ to attach file" />
        </Box>
      </Box>
      <Box width={78} justifyContent="space-between">
        <Text color={theme.muted}>enter send</Text>
        <Text color={isConnected ? theme.muted : theme.error}>
          {isConnected ? "Connected" : "Disconnected"} | {provider}
        </Text>
      </Box>
      {suggestions.length > 0 && (
        <Box width={78} flexDirection="column" borderStyle="single" borderColor={theme.border} paddingX={1}>
          {visibleSuggestions.map((item, index) => {
            const actualIndex = startIndex + index;
            const isSelected = actualIndex === selectedIndex;
            let scrollChar = " ";
            if (suggestions.length > visibleCount) {
              const thumbPos = Math.floor((selectedIndex / (suggestions.length - 1)) * (visibleCount - 1));
              scrollChar = index === thumbPos ? "█" : "│";
            }
            return (
              <Box key={item.value}>
                <Box width={3}>
                  <Text color={isSelected ? theme.accent : theme.muted}>{isSelected ? ">" : " "}</Text>
                </Box>
                <Box width={28}>
                  <Text color={isSelected ? theme.accent : theme.text}>{item.label}</Text>
                </Box>
                <Box flexGrow={1}>
                  <Text color={theme.muted}>{item.description}</Text>
                </Box>
                {suggestions.length > visibleCount && (
                  <Box width={1} marginLeft={1}>
                    <Text color={theme.muted}>{scrollChar}</Text>
                  </Box>
                )}
              </Box>
            );
          })}
          <Text color={theme.muted}>up/down navigate | tab complete | enter send</Text>
        </Box>
      )}
    </Box>
  );
}
