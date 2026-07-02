import { spawn, type ChildProcessByStdio } from "node:child_process";
import { existsSync } from "node:fs";
import { join } from "node:path";
import { createInterface, type Interface } from "node:readline";
import type { Readable, Writable } from "node:stream";
import type { JsonRpcRequest, JsonRpcResponse } from "./types.js";

type Pending = {
  resolve: (value: JsonRpcResponse) => void;
  reject: (error: Error) => void;
};

export class McpClient {
  private child?: ChildProcessByStdio<Writable, Readable, null>;
  private lines?: Interface;
  private nextId = 1;
  private readonly pending = new Map<number, Pending>();

  constructor(private readonly root: string) {}

  start(): void {
    const executable = this.resolveExecutable();
    const child = spawn(executable, [], {
      cwd: this.root,
      stdio: ["pipe", "pipe", "inherit"],
      windowsHide: true
    });
    this.child = child;
    this.lines = createInterface({ input: child.stdout });
    this.lines.on("line", (line) => this.handleLine(line));
    child.on("exit", (code) => {
      for (const wait of this.pending.values()) {
        wait.reject(new Error(`MCP daemon exited with code ${code ?? "unknown"}`));
      }
      this.pending.clear();
    });
  }

  stop(): void {
    this.lines?.close();
    this.child?.stdin.end();
    this.child?.kill();
  }

  request(method: string, params?: unknown): Promise<JsonRpcResponse> {
    const child = this.child;
    if (!child) throw new Error("MCP daemon is not running.");
    const id = this.nextId++;
    const request: JsonRpcRequest = { jsonrpc: "2.0", id, method, params };
    return new Promise((resolve, reject) => {
      this.pending.set(id, { resolve, reject });
      child.stdin.write(`${JSON.stringify(request)}\n`);
    });
  }

  notify(method: string, params?: unknown): void {
    const child = this.child;
    if (!child) throw new Error("MCP daemon is not running.");
    const request: JsonRpcRequest = { jsonrpc: "2.0", method, params };
    child.stdin.write(`${JSON.stringify(request)}\n`);
  }

  private handleLine(line: string): void {
    let response: JsonRpcResponse;
    try {
      response = JSON.parse(line) as JsonRpcResponse;
    } catch {
      return;
    }
    if (typeof response.id !== "number") return;
    const wait = this.pending.get(response.id);
    if (!wait) return;
    this.pending.delete(response.id);
    wait.resolve(response);
  }

  private resolveExecutable(): string {
    const envPath = process.env.MOTHPROBE_MCP_PATH;
    const candidates = [
      envPath,
      join(this.root, "data", ".mothprobe", "bin", process.platform === "win32" ? "mothprobe_mcp.exe" : "mothprobe_mcp"),
      join(this.root, "build", "Debug", process.platform === "win32" ? "mothprobe_mcp.exe" : "mothprobe_mcp"),
      join(this.root, "build", process.platform === "win32" ? "mothprobe_mcp.exe" : "mothprobe_mcp")
    ].filter(Boolean) as string[];
    const found = candidates.find((file) => existsSync(file));
    if (!found) {
      throw new Error("Cannot find mothprobe_mcp. Build it first or set MOTHPROBE_MCP_PATH.");
    }
    return found;
  }
}
