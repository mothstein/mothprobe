# MothProbe TypeScript TUI

This is the primary frontend direction for MothProbe. The terminal client is written in TypeScript and talks to the C++ `mothprobe_mcp` daemon over JSON-RPC stdio.

## Run

```powershell
cmake --build ..\build --config Debug --target mothprobe_mcp
npm install
npm run build
npm start
```

The frontend looks for the MCP daemon in this order:

1. `MOTHPROBE_MCP_PATH`
2. `data/.mothprobe/bin/mothprobe_mcp.exe`
3. `build/Debug/mothprobe_mcp.exe`
4. `build/mothprobe_mcp`

## Input Modes

- `/help`, `/init`, `/tools`, `/clear`, `/exit`
- `!dir`, `!whoami`, `!hostname`, `!echo text`
- `@README.md` attaches a local workspace file as context
- regular text is handled as chat input
