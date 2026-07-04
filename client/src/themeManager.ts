import { existsSync, mkdirSync, readdirSync, readFileSync } from "node:fs";
import { resolve } from "node:path";

export type Theme = {
  name: string;
  background: string;
  text: string;
  accent: string;
  muted: string;
  border: string;
  user: string;
  ai: string;
  error: string;
  success: string;
  warning: string;
  codeBackground: string;
};

export const fallbackTheme: Theme = {
  name: "Default",
  background: "#1f2130",
  text: "#f7f7fb",
  accent: "#c084fc",
  muted: "#a6adc8",
  border: "#5b6078",
  user: "#7dd3fc",
  ai: "#a7f3d0",
  error: "#f87171",
  success: "#86efac",
  warning: "#fbbf24",
  codeBackground: "#303244"
};

const requiredKeys = ["background", "text", "accent", "muted", "border", "user", "ai", "error"];

export function themesDir(): string {
  return resolve(process.cwd(), "data", "themes");
}

export function normalizeThemeName(name: string): string {
  return name.trim().toLowerCase().replace(/\s+/g, "-");
}

export function loadTheme(name: string): Theme {
  const key = normalizeThemeName(name || "default");
  const dir = themesDir();
  const file = resolve(dir, `${key}.json`);
  if (!existsSync(file)) return fallbackTheme;
  const parsed = JSON.parse(readFileSync(file, "utf8")) as Partial<Theme>;
  for (const required of requiredKeys) {
    if (typeof parsed[required as keyof Theme] !== "string") return fallbackTheme;
  }
  return {
    ...fallbackTheme,
    ...parsed,
    name: parsed.name || key
  };
}

export function listThemes(): string[] {
  const dir = themesDir();
  if (!existsSync(dir)) mkdirSync(dir, { recursive: true });
  const files = readdirSync(dir).filter((file) => file.endsWith(".json"));
  const names = files.map((file) => file.replace(/\.json$/i, ""));
  return names.length > 0 ? names : ["default"];
}
