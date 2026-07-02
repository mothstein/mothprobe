import type { ParsedInput } from "./types.js";

export function parseInput(raw: string): ParsedInput {
  const text = raw.trim();
  if (text.startsWith("/")) {
    return { kind: "command", payload: text.slice(1).trim(), raw };
  }
  if (text.startsWith("!")) {
    return { kind: "shell", payload: text.slice(1).trim(), raw };
  }
  if (text.startsWith("@")) {
    return { kind: "file", payload: text.slice(1).trim(), raw };
  }
  return { kind: "chat", payload: raw, raw };
}

export function commandSuggestions(prefix: string): string[] {
  const commands = ["/help", "/init", "/tools", "/clear", "/exit"];
  if (!prefix.startsWith("/")) return [];
  return commands.filter((item) => item.startsWith(prefix));
}
