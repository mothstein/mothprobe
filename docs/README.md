# Welcome to the MothProbe Wiki

**MothProbe** is an advanced, multi-agent pentesting and coding framework powered by LLMs (Large Language Models). It is designed to assist security researchers, developers, and pentesters with automated code review, bug analysis, shell execution, and scoped system reconnaissance.

## What makes MothProbe unique?
1. **Agent Swarms**: MothProbe supports a `.agents/` directory structure where you can define independent sub-agents (e.g., `code_writer`, `bug_analyzer`, `smoke_tester`) with unique roles, system prompts, and tool permissions.
2. **Interactive Safety**: MothProbe enforces a strict shell permission policy. If an agent tries to execute a command longer than 200 characters, it will pause and prompt the user via an interactive UI dropdown for approval (`[Yes]`, `[No]`, or provide feedback).
3. **High-Performance Architecture**: The core MCP (Model Context Protocol) Daemon is written in C++ for maximum performance, while the interactive CLI client is built using TypeScript, React, and Ink for a beautiful terminal user experience.
4. **Reasoning Mode**: Supports toggling reasoning outputs (e.g., `<think>` tags) for models that support it (like DeepSeek R1).

## Wiki Contents
- [Architecture](Architecture.md) - Learn how the C++ Backend and TS Client communicate.
- [Agents & Skills](Agents.md) - How to define and spawn multi-agent swarms.
- [Usage & Commands](Usage.md) - A guide to all available CLI commands (e.g. `/resume`, `/skills`).

## License
MothProbe is released under a **Non-Commercial License**. You are free to compile and modify the code for personal or research use, but you may **not** sell, commercialize, or use the software for commercial purposes.
