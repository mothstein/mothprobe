# Usage & Commands

MothProbe provides several built-in commands within the chat interface to control the environment.

### `/resume`
Opens a UI modal listing all previous chat sessions. Selecting a session will restore its chat history and context, allowing you to continue where you left off.

### `/skills`
Queries the MCP Daemon for all loaded agents and skills from the `.agents/` directory and displays them in the chat.

### `/permission [grant|revoke] <cmd>`
Manages shell command permissions for the `run_command` tool.
- `/permission`: Lists all currently allowed shell commands.
- `/permission grant nmap`: Allows the agent to run `nmap` without explicit interactive approval.
- `/permission revoke nmap`: Revokes the permission.

### `/reasoning`
Opens a toggle menu to enable or disable "Reasoning Mode". When enabled, models that support reasoning (like DeepSeek R1) will display their chain-of-thought `<think>` tags in an expandable UI block before answering.

### `/status`
Displays the current connection status of the C++ MCP Daemon, PID, pending RPCs, and the active LLM configuration.

### `/model`
Opens an interactive UI picker to change the active LLM Provider and Model on the fly.

### `/clear`
Clears the current conversation history from the screen and context window.

### `/exit`
Terminates the MothProbe CLI and safely shuts down the background daemon.
