# ADR 0001: Mothon-CPP Backend Migration

## Status

Accepted

## Context

MothProbe needs a production-grade MCP backend quickly. Maintaining a complete MCP
protocol implementation from scratch would slow product delivery and increase review
risk.

`hkr04/cpp-mcp` is MIT licensed and already provides MCP protocol primitives, tool
and resource abstractions, HTTP/SSE transports, session handling, and examples.

## Decision

Vendor a pinned snapshot of `hkr04/cpp-mcp` under `third_party/cpp-mcp` and expose it
only through MothProbe-owned `src/mothon/*` wrappers.

The production executable remains `mothprobe_mcp` for TypeScript TUI compatibility.
The default transport remains stdio until protocol parity and safety tests prove the
Mothon wrapper is ready for wider transports.

## Consequences

- MothProbe can reuse upstream MCP protocol structures without leaking upstream APIs
  into product modules.
- Legal attribution is explicit through `UPSTREAM.md` and the upstream MIT license.
- Future upstream updates can be reviewed as snapshot bumps.
- MothProbe-specific safety, audit, LLM, tool registry, and agent orchestration remain
  owned code.
