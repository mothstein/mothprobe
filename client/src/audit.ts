import { mkdir, appendFile } from "node:fs/promises";
import { dirname, join } from "node:path";
import { createHash } from "node:crypto";

export class AuditLog {
  private readonly file: string;

  constructor(root: string) {
    this.file = join(root, "data", ".mothprobe", "audit.jsonl");
  }

  async event(type: string, fields: Record<string, unknown>): Promise<void> {
    await mkdir(dirname(this.file), { recursive: true });
    const entry = {
      ts: new Date().toISOString(),
      type,
      ...fields
    };
    await appendFile(this.file, `${JSON.stringify(entry)}\n`, "utf8");
  }

  hashPath(path: string): string {
    return createHash("sha256").update(path).digest("hex");
  }
}
