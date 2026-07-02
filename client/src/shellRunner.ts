import { readdir, realpath } from "node:fs/promises";
import { spawn } from "node:child_process";
import { isAbsolute, relative, resolve } from "node:path";

const allowed = new Set(["dir", "ls", "echo", "whoami", "hostname", "ver"]);

function splitCommand(input: string): string[] {
  return input.match(/"[^"]+"|\S+/g)?.map((part) => part.replace(/^"|"$/g, "")) ?? [];
}

export async function runShell(input: string, root = process.cwd()): Promise<{ code: number; output: string }> {
  const parts = splitCommand(input);
  const verb = (parts.shift() ?? "").toLowerCase();
  if (!allowed.has(verb)) {
    return { code: 126, output: "Rejected by shell policy." };
  }
  if (verb === "dir" || verb === "ls") {
    const rootPath = await realpath(root);
    const target = await realpath(resolve(rootPath, parts[0] ?? "."));
    const rel = relative(rootPath, target);
    if (rel.startsWith("..") || isAbsolute(rel)) {
      return { code: 126, output: "Rejected path outside workspace." };
    }
    const entries = await readdir(target, { withFileTypes: true });
    return {
      code: 0,
      output: entries.map((entry) => `${entry.isDirectory() ? "<DIR>" : "     "} ${entry.name}`).join("\n")
    };
  }
  if (verb === "echo") return { code: 0, output: parts.join(" ") };
  if (verb === "ver") return { code: 0, output: `${process.platform} ${process.version}` };
  return spawnAndCapture(verb, parts);
}

function spawnAndCapture(command: string, args: string[]): Promise<{ code: number; output: string }> {
  return new Promise((resolve) => {
    const child = spawn(command, args, { shell: false, windowsHide: true });
    let output = "";
    child.stdout.on("data", (chunk: Buffer) => {
      if (output.length < 12000) output += chunk.toString("utf8");
    });
    child.stderr.on("data", (chunk: Buffer) => {
      if (output.length < 12000) output += chunk.toString("utf8");
    });
    child.on("error", (error) => resolve({ code: 127, output: error.message }));
    child.on("close", (code) => resolve({ code: code ?? -1, output: output || "Command completed with no output." }));
  });
}
