import { readdir } from "node:fs/promises";
import { spawn } from "node:child_process";
import { useStore } from "./store.js";

const defaultAllowed = new Set(["dir", "ls", "echo", "whoami", "hostname", "ver"]);

function splitCommand(input: string): string[] {
  return input.match(/"[^"]+"|\S+/g)?.map((part) => part.replace(/^"|"$/g, "")) ?? [];
}

export async function runShellCommand(input: string): Promise<void> {
  const store = useStore.getState();
  store.addMessage({ type: "shell", content: `$ ${input}` });
  const parts = splitCommand(input);
  const verb = (parts.shift() ?? "").toLowerCase();
  const allowed = new Set(store.permissions || ["dir", "ls", "echo", "whoami", "hostname", "ver"]);
  if (!allowed.has(verb)) {
    store.addMessage({ type: "shell", content: "Rejected by shell policy." });
    return;
  }
  if (verb === "dir" || verb === "ls") {
    const target = parts[0] ?? ".";
    const entries = await readdir(target, { withFileTypes: true });
    store.addMessage({
      type: "shell",
      content: entries.map((entry) => `${entry.isDirectory() ? "<DIR>" : "     "} ${entry.name}`).join("\n")
    });
    return;
  }
  if (verb === "echo") {
    store.addMessage({ type: "shell", content: parts.join(" ") });
    return;
  }
  if (verb === "ver") {
    store.addMessage({ type: "shell", content: `${process.platform} ${process.version}` });
    return;
  }
  await new Promise<void>((resolve) => {
    const child = spawn(verb, parts, { shell: false, windowsHide: true });
    let output = "";
    child.stdout.on("data", (chunk: Buffer) => {
      if (output.length < 12000) output += chunk.toString("utf8");
    });
    child.stderr.on("data", (chunk: Buffer) => {
      if (output.length < 12000) output += chunk.toString("utf8");
    });
    child.on("error", (error) => {
      store.addMessage({ type: "shell", content: error.message });
      resolve();
    });
    child.on("close", () => {
      store.addMessage({ type: "shell", content: output.trim() || "Command completed with no output." });
      resolve();
    });
  });
}
