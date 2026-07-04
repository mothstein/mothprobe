import { useStore } from './store.js';
import { callLLM, McpRequestError, sendMcpRequest, getMcpStatus } from './mcpClient.js';
import { runShellCommand } from './shellRunner.js';
import { attachFile } from './fileContext.js';
import fs from 'fs';
import path from 'path';

interface LlmMessage {
  role: 'system' | 'user' | 'assistant';
  content: string;
}

async function buildLlmMessagesWithTools(prompt: string): Promise<LlmMessage[]> {
  let toolsInfo = [];
  try {
    const res = await sendMcpRequest('tools/list') as any;
    if (res && res.tools) toolsInfo = res.tools;
  } catch (e) { }
  
  const sysMsg = `You are an autonomous pentesting and coding agent. You have access to the following MCP tools:
${JSON.stringify(toolsInfo, null, 2)}

When the user asks you to perform an action (e.g., list a folder, run a shell command, read a file), you MUST use the appropriate tool.
To use a tool, output an XML block exactly like this:
<tool_call>
{"name": "run_command", "arguments": {"command": "ls"}}
</tool_call>

- You can output multiple <tool_call> blocks to run tools in parallel.
- Do NOT simulate or explain what you are going to do before calling the tool. Just call the tool!
- Wait for the tool results before answering.`;

  return [
    { role: 'system', content: sysMsg },
    { role: 'user', content: prompt }
  ];
}

async function runAgentLoop(aiId: string, initialPrompt: string, provider: string, model: string | undefined) {
  const store = useStore.getState();
  let currentPrompt = initialPrompt;
  let currentAiId = aiId;
  
  while (true) {
    let response;
    try {
      response = await callLLM(await buildLlmMessagesWithTools(currentPrompt), provider, model, store.reasoningMode !== 'fast');
    } catch (error) {
      if (!isInterrupted(error)) {
        useStore.getState().updateLastMessage(`**LLM Error:**\n${error instanceof Error ? error.message : String(error)}`);
      }
      return;
    }
    
    if (response.session?.session_id) {
      useStore.getState().setActiveSessionId(response.session.session_id);
    }
    
    let text = response.text || "";
    const reasoning = response.reasoning;
    
    const toolRegex = /<tool_call>\s*(\{[\s\S]*?\})\s*<\/tool_call>/g;
    let match;
    const calls = [];
    while ((match = toolRegex.exec(text)) !== null) {
      try {
        let jsonStr = match[1].trim();
        if (jsonStr.startsWith("```json")) {
          jsonStr = jsonStr.substring(7).trim();
          if (jsonStr.endsWith("```")) {
            jsonStr = jsonStr.substring(0, jsonStr.length - 3).trim();
          }
        }
        calls.push(JSON.parse(jsonStr));
      } catch (e) {
        useStore.getState().addMessage({ type: 'system', content: `> [WARN] Failed to parse tool call JSON: ${e instanceof Error ? e.message : String(e)}` });
      }
    }
    
    const textToDisplay = text.replace(/<tool_call>[\s\S]*?<\/tool_call>/g, '').trim();
    const suffix = response.truncated ? `\n\n> Response truncated.` : '';
    
    if (textToDisplay || calls.length === 0) {
      useStore.getState().startTypingLastMessage(textToDisplay + suffix, reasoning);
    } else {
      useStore.getState().updateLastMessage(`> Executing ${calls.length} tool(s)...`, reasoning);
    }
    
    if (calls.length === 0) break;
    
    let toolResults = [];
    for (const call of calls) {
      const name = call.name;
      const args = call.arguments || {};
      
      let indicator = name;
      if (name === "read_file") indicator = `Read [${args.path}]`;
      else if (name === "write_file") indicator = `Wrote [${args.path}]`;
      else if (name === "run_command") indicator = `Exec [${args.command}]`;
      else if (name === "agents/run" || name.startsWith("Spawn")) indicator = `SpawnAgents [${args.agent || args.name}]`;
      
      useStore.getState().addMessage({ type: 'system', content: `> [EXEC] ${indicator}` });
      
      if (name === "run_command" && args.command && args.command.length > 200) {
        const approval = await new Promise<string>((resolve) => {
          useStore.getState().setToolApprovalRequest({
            command: args.command,
            reason: args.reason || "Executing long command",
            resolve
          });
        });
        
        if (approval.toLowerCase() === "no") {
          toolResults.push(`Tool ${name} rejected by user.`);
          continue;
        } else if (approval.toLowerCase() !== "yes") {
          toolResults.push(`Tool ${name} rejected. User feedback: ${approval}`);
          continue;
        }
      }
      
      try {
        const res = await sendMcpRequest("tools/call", { name, arguments: args });
        toolResults.push(`Tool ${name} output:\n${JSON.stringify(res)}`);
      } catch (err) {
        toolResults.push(`Tool ${name} error:\n${err instanceof Error ? err.message : String(err)}`);
      }
    }
    
    currentPrompt = "Tool results:\n" + toolResults.join("\n\n");
    useStore.getState().addMessage({ type: 'system', content: `> [WAIT] Thinking...` });
    currentAiId = useStore.getState().addMessage({ type: 'ai', content: '', isLoading: true });
  }
}

const COMMAND_HELP = [
  { cmd: '/help', desc: 'Show this help message' },
  { cmd: '/init', desc: 'Initialize the MCP connection' },
  { cmd: '/tools', desc: 'List available MCP tools' },
  { cmd: '/status', desc: 'Check MCP server status' },
  { cmd: '/model', desc: 'Open provider and model picker' },
  { cmd: '/theme <name>', desc: 'Switch or list UI themes' },
  { cmd: '/permission', desc: 'Manage shell command permissions' },
  { cmd: '/resume', desc: 'Restore previous chat session' },
  { cmd: '/reasoning', desc: 'Toggle reasoning output' },
  { cmd: '/skills', desc: 'List available skills and agents' },
  { cmd: '/clear', desc: 'Clear the conversation history' },
  { cmd: '/exit', desc: 'Exit the application' }
];

export async function handleInput(input: string) {
  const store = useStore.getState();
  const trimmed = input.trim();
  if (!trimmed || trimmed === '/' || trimmed === '@') return;

  store.addMessage({ type: 'user', content: trimmed, mode: store.mode });

  if (trimmed.startsWith('/')) {
    const [cmd, ...args] = trimmed.slice(1).split(' ');
    switch (cmd.toLowerCase()) {
      case 'help': {
        const helpText = COMMAND_HELP.map((item) => `- **${item.cmd.padEnd(15)}** ${item.desc}`).join('\n');
        store.addMessage({ type: 'system', content: `### Available Commands\n\n${helpText}` });
        break;
      }
      case 'init':
        try {
          const res = await sendMcpRequest('initialize', {
            protocolVersion: '2026-7-2',
            capabilities: {}
          });
          store.addMessage({ type: 'mcp', content: `**MCP Initialized Successfully**\n\n\`\`\`json\n${JSON.stringify(res, null, 2)}\n\`\`\`` });
        } catch (error) {
          store.addMessage({ type: 'system', content: formatMcpError(error) });
        }
        break;
      case 'tools':
        try {
          const res = await sendMcpRequest('tools/list');
          store.addMessage({ type: 'mcp', content: `### Available Tools\n\n\`\`\`json\n${JSON.stringify(res, null, 2)}\n\`\`\`` });
        } catch (error) {
          store.addMessage({ type: 'system', content: formatMcpError(error) });
        }
        break;
      case 'skills':
        try {
          const res = await sendMcpRequest('prompts/list');
          store.addMessage({ type: 'mcp', content: `### Available Skills & Agents\n\n\`\`\`json\n${JSON.stringify(res, null, 2)}\n\`\`\`` });
        } catch (error) {
          store.addMessage({ type: 'system', content: formatMcpError(error) });
        }
        break;
      case 'model':
        store.setModelPickerOpen(true);
        break;
      case 'status': {
        const status = getMcpStatus();
        const llmConfig = store.llmConfig;
        const lines = [
          `### MCP Server Status`,
          ``,
          `**Daemon:** \`${status.executable || 'Not resolved'}\``,
          `**State:** ${status.connected ? 'Connected' : 'Disconnected'}`,
          `**PID:** ${status.pid || 'N/A'}`,
          `**Pending RPCs:** ${status.pendingRequests}`,
          ``,
          `**LLM Provider:** ${llmConfig?.provider || 'Not configured'}`,
          `**LLM Model:** ${llmConfig?.model || 'Not configured'}`
        ];
        store.addMessage({ type: 'system', content: lines.join('\n') });
        break;
      }
      case 'exit':
      case 'quit':
        process.exit(0);
        break;
      case 'clear': case 'cls': {
        const dataDir = path.resolve(process.cwd(), 'data');
        if (store.chatHistory.length > 0) {
          fs.writeFileSync(path.join(dataDir, 'history.bak.json'), JSON.stringify(store.chatHistory, null, 2));
        }
        useStore.setState({ chatHistory: [] });
        break;
      }
      case 'resume': {
        store.setResumePickerOpen(true);
        break;
      }
      case 'permission': {
        if (args.length === 0) {
          const perms = store.permissions.join(', ');
          store.addMessage({ type: 'system', content: `### Shell Permissions\n\nAllowed commands: ${perms}\n\nUse \`/permission grant <cmd>\` or \`/permission revoke <cmd>\`.` });
        } else if (args[0] === 'grant' && args[1]) {
          store.grantPermission(args[1].toLowerCase());
          store.addMessage({ type: 'system', content: `> Granted permission for shell command: **${args[1]}**` });
        } else if (args[0] === 'revoke' && args[1]) {
          store.revokePermission(args[1].toLowerCase());
          store.addMessage({ type: 'system', content: `> Revoked permission for shell command: **${args[1]}**` });
        } else {
          store.addMessage({ type: 'system', content: `> Invalid usage. \`/permission [grant|revoke] <cmd>\`` });
        }
        break;
      }
      case 'reasoning': {
        store.setReasoningPickerOpen(true);
        break;
      }
      case 'theme': {
        const themeName = args.join(' ').trim();
        if (!themeName) {
          const themes = store.availableThemes.map((theme) => `- ${theme}`).join('\n');
          store.addMessage({ type: 'system', content: `### Available Themes\n\n${themes}\n\nUse \`/theme <name>\` to apply.` });
          break;
        }
        const ok = store.setTheme(themeName);
        store.addMessage({
          type: 'system',
          content: ok
            ? `> Theme switched to **${useStore.getState().theme.name}**`
            : `**Theme not found:** ${themeName}`
        });
        break;
      }
      default:
        store.addMessage({ type: 'system', content: `**Unknown command:** \`/${cmd}\`\nType \`/help\` for valid commands.` });
    }
  } else if (trimmed.startsWith('!')) {
    const cmd = trimmed.slice(1).trim();
    if (cmd) {
      store.addMessage({ type: 'system', content: `> Executing shell command: \`${cmd}\`` });
      await runShellCommand(cmd);
    }
  } else if (trimmed.includes('@')) {
    const fileMatches = [...trimmed.matchAll(/(?:^|\s)@(?:\[([^\]]+)\]|([^\s\[\]]+))/g)];
    if (fileMatches.length > 0) {
      const provider = store.llmConfig?.provider || 'gemini';
      const model = store.llmConfig?.model;
      const aiId = store.addMessage({ type: 'ai', content: '', isLoading: true });
      try {
        let fullPrompt = trimmed;
        for (const match of fileMatches) {
          const target = (match[1] || match[2]).trim();
          const file = await attachFile(process.cwd(), target);
          store.addMessage({ type: 'system', content: `> Attached file: **${file.relativePath}**` });
          fullPrompt += `\n\n--- Content of ${file.relativePath} ---\n${file.preview}\n--- End of ${file.relativePath} ---\n`;
        }
        await runAgentLoop(aiId, fullPrompt, provider, model);
      } catch (error) {
        if (!isInterrupted(error)) {
          store.updateLastMessage(`**File attach or LLM failed:**\n${error instanceof Error ? error.message : String(error)}`);
        }
      }
    } else {
      const provider = store.llmConfig?.provider || 'gemini';
      const model = store.llmConfig?.model;
      const aiId = store.addMessage({ type: 'ai', content: '', isLoading: true });
      await runAgentLoop(aiId, trimmed, provider, model);
    }
  } else {
    const provider = store.llmConfig?.provider || 'gemini';
    const model = store.llmConfig?.model;
    const aiId = store.addMessage({ type: 'ai', content: '', isLoading: true });
    await runAgentLoop(aiId, trimmed, provider, model);
  }
}

function isInterrupted(error: unknown): boolean {
  return error instanceof McpRequestError && error.data?.kind === 'interrupted';
}

function formatMcpError(error: unknown): string {
  if (!(error instanceof McpRequestError)) {
    return `**LLM Error:**\n${error instanceof Error ? error.message : String(error)}`;
  }
  const data = error.data || {};
  const provider = data.provider ? `[${data.provider}] ` : '';
  const kind = data.kind ? data.kind.replaceAll('_', ' ') : 'provider error';
  const status = data.http_status ? ` HTTP ${data.http_status}` : '';
  const retry = data.retryable ? '\n\n> This looks retryable. Try again later or switch provider/model.' : '';
  return `**${provider}${kind}${status}**\n${error.message}${retry}`;
}
