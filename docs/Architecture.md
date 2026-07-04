# MothProbe Architecture

MothProbe adopts a modern decoupled architecture using the **Model Context Protocol (MCP)**, consisting of a high-performance backend and a rich terminal client.

## 1. C++ Backend Daemon (`mothprobe_mcp`)
The core of MothProbe is a C++ daemon that manages the LLM context, chat memory, tool execution, and agent registries. 
- **Tool Registry**: Implements tools like `run_command`, `read_file`, `write_file`, and `list_dir`. 
- **Security & Scope**: Evaluates shell commands against a strict permission model. Harmless commands (e.g. `ls`, `whoami`, `dir`) pass through by default, while others require explicit permission.
- **Chat Memory**: Manages conversation history, intelligently serializing and managing context window limits, while maintaining stateless system prompts.
- **Agent Registry**: Automatically scans the workspace for `.agents/agents/` and `.agents/skills/` to load dynamic AI roles.

## 2. TypeScript/React Client (`mothprobe-cli`)
The frontend is a CLI application built using Node.js, React, and Ink.
- **Agent Loop**: Handles the recursive loop of parsing XML `<tool_call>` outputs from the LLM, forwarding them to the C++ daemon for execution, and returning the results to the LLM context.
- **Interactive Approval**: Renders UI components (dropdowns, modals) directly in the terminal, pausing the agent loop when user intervention is required for risky commands.
- **Session Management**: Automatically persists chat history to disk and allows users to resume previous sessions via a UI picker.
