# Agents & Skills

MothProbe's true power lies in its ability to orchestrate multiple specialized agents to solve complex problems.

## Directory Structure
MothProbe looks for a `.agents/` folder at the root of your workspace:
```text
.agents/
├── agents/
│   ├── code_writer/
│   │   ├── agent.json
│   │   └── AGENTS.md
│   ├── bug_analyzer/
│   │   ├── agent.json
│   │   └── AGENTS.md
│   └── smoke_tester/
│       ├── agent.json
│       └── AGENTS.md
└── skills/
    └── mothprobe-skills/
        └── SKILL.md
```

## Creating an Agent
To define a new agent, create a folder under `.agents/agents/` with an `agent.json` (for metadata) and an `AGENTS.md` (for the system prompt).

**Example `agent.json`**:
```json
{
  "description": "Expert C++ Developer for backend systems.",
  "provider": "gemini",
  "model": "gemini-2.5-pro"
}
```

**Example `AGENTS.md`**:
```markdown
You are the Code Writer agent. Your sole responsibility is to write and refactor C++ code. You must use `read_file` and `write_file` tools.
```

## Spawning Agents
During a conversation, the main coordinator agent can invoke other agents using a tool call (e.g., `agents/run`), passing the context to the specialized agent to perform the task.

## Skills
Skills are reusable prompt templates or guidelines stored in `.agents/skills/`. Agents can read these markdown files to learn how to perform specific workflows (e.g., "How to perform a passive audit").
