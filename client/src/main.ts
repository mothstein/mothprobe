#!/usr/bin/env node
import { createInterface } from "node:readline/promises";
import { stdin as input, stdout as output } from "node:process";
import { AuditLog } from "./audit.js";
import { attachFile, type AttachedFile } from "./fileContext.js";
import { parseInput } from "./inputParser.js";
import { McpClient } from "./mcpClient.js";
import { runShell } from "./shellRunner.js";
import type { ChatMessage } from "./types.js";

const root = process.cwd();
const audit = new AuditLog(root);
const mcp = new McpClient(root);
const messages: ChatMessage[] = [];
const files: AttachedFile[] = [];

function print(role: ChatMessage["role"], text: string): void {
  messages.push({ role, text });
  const label = role.padEnd(6, " ");
  output.write(`\x1b[35m[${label}]\x1b[0m ${text}\n`);
}

async function handleCommand(command: string): Promise<boolean> {
  await audit.event("command", { command });
  if (command === "exit" || command === "quit") return false;
  if (command === "clear") {
    console.clear();
    return true;
  }
  if (command === "help" || command.length === 0) {
    print("system", "/init, /tools, /clear, /exit, !dir, !whoami, @path");
    return true;
  }
  if (command === "init") {
    const response = await mcp.request("initialize", {
      protocolVersion: "2026-7-2",
      capabilities: {}
    });
    mcp.notify("notifications/initialized");
    print("mcp", JSON.stringify(response.result ?? response.error, null, 2));
    return true;
  }
  if (command === "tools") {
    const response = await mcp.request("tools/list");
    print("mcp", JSON.stringify(response.result ?? response.error, null, 2));
    return true;
  }
  print("system", `Unknown command: /${command}`);
  return true;
}

async function main(): Promise<void> {
  mcp.start();
  console.clear();
  output.write("\x1b[38;5;93mMothProbe\x1b[0m TypeScript TUI -> C++ MCP backend\n");
  output.write("\x1b[38;5;208mType /help, /init, /tools, !dir, @README.md, or /exit\x1b[0m\n\n");
  const rl = createInterface({ input, output });
  let running = true;
  while (running) {
    const line = await rl.question("\x1b[34m>\x1b[0m ");
    const parsed = parseInput(line);
    if (!line.trim()) continue;
    if (parsed.kind === "command") {
      running = await handleCommand(parsed.payload);
    } else if (parsed.kind === "shell") {
      const result = await runShell(parsed.payload, root);
      await audit.event("shell", { command: parsed.payload, exit_code: result.code });
      print("shell", result.output);
    } else if (parsed.kind === "file") {
      try {
        const attached = await attachFile(root, parsed.payload);
        files.push(attached);
        await audit.event("file", { path_hash: audit.hashPath(attached.path), ok: true });
        print("system", `Attached file: ${attached.relativePath}`);
      } catch (error) {
        await audit.event("file", { path_hash: audit.hashPath(parsed.payload), ok: false });
        print("system", error instanceof Error ? error.message : "File attach failed.");
      }
    } else {
      await audit.event("chat", { size: parsed.payload.length, context_files: files.length });
      print("user", parsed.payload);
      print("system", "AI frontend chat orchestration will call the provider layer in the next milestone.");
    }
  }
  rl.close();
  mcp.stop();
}

main().catch((error) => {
  console.error(error instanceof Error ? error.message : error);
  process.exitCode = 1;
});
