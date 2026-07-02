import { readFile, realpath } from "node:fs/promises";
import { isAbsolute, relative, resolve } from "node:path";

export interface AttachedFile {
  path: string;
  relativePath: string;
  preview: string;
}

export async function attachFile(root: string, inputPath: string): Promise<AttachedFile> {
  const rootPath = await realpath(root);
  const fullPath = await realpath(resolve(root, inputPath));
  const rel = relative(rootPath, fullPath);
  if (rel.startsWith("..") || isAbsolute(rel)) {
    throw new Error("Rejected path outside workspace.");
  }
  const content = await readFile(fullPath, "utf8");
  return {
    path: fullPath,
    relativePath: rel,
    preview: content.slice(0, 4096)
  };
}
