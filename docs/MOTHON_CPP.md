# Mothon C++ MCP Backend Strategy

`mothon-cpp` is the planned production-grade C++ MCP backend for MothProbe. It should start as an internal fork/adaptation of `hkr04/cpp-mcp`, then add MothProbe-specific security, agent orchestration, audit, LLM provider routing, and cybersecurity tool execution.

## Research Summary

The `hkr04/cpp-mcp` repository is a lightweight C++ MCP SDK under the MIT license. It already implements several foundation pieces that MothProbe should not rebuild from scratch:

- JSON-RPC 2.0 message handling.
- MCP server lifecycle.
- Tool registration and `tools/list` / tool invocation support.
- Resource registration and resource templates.
- HTTP, HTTP+SSE, streamable HTTP, and stdio-related client support.
- Session handling, inactive session cleanup, and server-side event dispatch.
- Thread pool based async request handling.
- Example servers, clients, stdio client, and an agent example.

The repo targets the MCP `2025-03-26` basic protocol family and includes compatibility for the older HTTP+SSE transport. It uses C++17, CMake, `cpp-httplib`, and optional OpenSSL.

## Decision

Use `cpp-mcp` as the protocol substrate, not as the product architecture.

MothProbe should not become a generic MCP SDK. `mothon-cpp` should be a security-first MCP backend with a stable protocol layer underneath and MothProbe-specific modules above it.

## Product Boundary

TypeScript owns:

- Ink/React TUI.
- Keyboard input, command palette, popup/modal UX.
- Chat rendering, markdown, theme system.
- JSON-RPC client over stdio to the backend.

C++ `mothon-cpp` owns:

- MCP server protocol.
- LLM provider HTTP calls.
- Tool registry and tool execution.
- Scope validation.
- Approval policy.
- Audit logging.
- Session and cache persistence.
- Agent orchestration.
- Scan and OSINT modules.

No API keys or direct LLM HTTP requests should be handled by the TypeScript TUI.

## Target Architecture

```text
client/
  TypeScript Ink TUI
    |
    | JSON-RPC over stdio
    v
src/mothon/
  protocol/       Adapted cpp-mcp protocol/server layer
  runtime/        Config, layout, cache, brains, audit
  llm/            Gemini, OpenRouter, Ollama, Groq, OpenAI-compatible providers
  tools/          Typed cybersecurity tool registry
  safety/         Scope validation, approval, risk policy
  agents/         Agent swarm planner/executor/reviewer
  findings/       Structured finding model and evidence store
```

## Agent Swarm Scope

The first version of agent swarm should be practical and bounded. Do not build an autonomous pentest system before the safety model is strong.

Recommended agents:

- `planner`: turns user intent into a safe scan plan.
- `scope_guard`: validates target scope and blocks unsafe requests.
- `operator`: executes approved tools through MCP.
- `analyst`: converts tool output into findings and risk notes.
- `reviewer`: checks assumptions, false positives, and missing evidence.
- `reporter`: produces Markdown/HTML report sections.

Each agent should be deterministic at the orchestration level:

- Every agent step gets a run ID.
- Every tool call is audited.
- Active tools require policy approval.
- Cloud LLM data sharing must be explicit and logged.

## Migration Plan

### Milestone 1: Vendor and Wrap

- Add `third_party/cpp-mcp` as a vendored dependency or git submodule after license review.
- Build it as an internal static library.
- Create a `src/mothon/protocol/` wrapper so the rest of MothProbe does not depend directly on upstream headers.
- Preserve the current `mothprobe_mcp` executable name during migration to avoid breaking the TypeScript client.

### Milestone 2: Replace Local JSON-RPC Skeleton

- Route `initialize`, `tools/list`, `tools/call`, `resources/list`, and `resources/read` through the adapted protocol layer.
- Keep current `llm/chat`, `llm/list_providers`, audit, config, and provider adapters.
- Add regression tests for stdio JSON-RPC compatibility.

### Milestone 3: Tool Registry

- Build a typed `ToolRegistry` above the protocol layer.
- Every tool definition must include:
  - name
  - description
  - JSON schema
  - risk class
  - scope requirements
  - approval requirement
  - timeout and concurrency policy

### Milestone 4: Security Modules

- Add passive tools first:
  - `lookup_dns`
  - `detect_headers`
  - `tls_summary`
  - `whois_summary`
- Add active tools only after approval flow is implemented:
  - `scan_tcp`
  - `banner_grab`
  - `http_probe`

### Milestone 5: Agent Swarm

- Implement the agent coordinator as a state machine, not unbounded recursion.
- Add max step count, cancellation, and run audit records.
- Use LLM calls only for planning, summarization, and review.
- Keep tool execution deterministic.

## Naming

Recommended names:

- Library or internal subsystem: `mothon-cpp`.
- Product daemon executable: keep `mothprobe_mcp.exe` for compatibility.
- Future package name: `mothon-cpp`.

This keeps the user-facing MothProbe brand stable while allowing the backend framework to evolve independently.

## Immediate Recommendation

Do not pause frontend progress to rewrite the backend immediately.

The fastest revenue path is:

1. Keep the current stdio MCP daemon working.
2. Add basic paid-value tools: passive DNS, HTTP header audit, TLS summary, report export.
3. Start `mothon-cpp` as a migration branch that vendors/wraps `cpp-mcp`.
4. Switch the production daemon to `mothon-cpp` only after protocol parity tests pass.
