import { useStore } from './store.js';
import { callLLM, McpRequestError, sendMcpRequest, getMcpStatus } from './mcpClient.js';
import { runShellCommand } from './shellRunner.js';
import { attachFile } from './fileContext.js';
import fs from 'fs';
import path from 'path';

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
        const dataDir = path.resolve(process.cwd(), 'data');
        const bakPath = path.join(dataDir, 'history.bak.json');
        if (fs.existsSync(bakPath)) {
          const histData = fs.readFileSync(bakPath, 'utf8');
          try {
            const history = JSON.parse(histData);
            if (Array.isArray(history)) {
              useStore.setState({ chatHistory: history });
              useStore.getState().addMessage({ type: 'system', content: `> Restored previous session.` });
            }
          } catch (e) {
            store.addMessage({ type: 'system', content: `> Failed to restore session.` });
          }
        } else {
          store.addMessage({ type: 'system', content: `> No previous session found.` });
        }
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
        store.toggleReasoning();
        const newState = useStore.getState().reasoningEnabled;
        store.addMessage({ type: 'system', content: `> Reasoning is now **${newState ? 'enabled' : 'disabled'}**.` });
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
    // Regex for parsing multiple @filename or @[filename] inline
    const fileMatches = [...trimmed.matchAll(/(?:^|\s)@(?:\[([^\]]+)\]|([^\s\[\]]+))/g)];
    if (fileMatches.length > 0) {
      const provider = store.llmConfig?.provider || 'gemini';
      const model = store.llmConfig?.model;
      store.addMessage({ type: 'ai', content: '', isLoading: true });
      try {
        let fullPrompt = trimmed;
        for (const match of fileMatches) {
          const target = (match[1] || match[2]).trim();
          const file = await attachFile(process.cwd(), target);
          store.addMessage({ type: 'system', content: `> Attached file: **${file.relativePath}**` });
          
          // Inject file content into the prompt context invisibly or append it
          fullPrompt += `\n\n--- Content of ${file.relativePath} ---\n${file.preview}\n--- End of ${file.relativePath} ---\n`;
        }
        
        const response = await callLLM([{ role: 'user', content: fullPrompt }], provider, model, store.reasoningEnabled);
        const suffix = response.truncated
          ? `\n\n> Response may be truncated by provider limit (${response.finishReason || 'length'}).`
          : '';
        useStore.getState().startTypingLastMessage(response.text + suffix, response.reasoning);
      } catch (error) {
        if (!isInterrupted(error)) {
          store.updateLastMessage(`**File attach or LLM failed:**\n${error instanceof Error ? error.message : String(error)}`);
        }
      }
    } else {
      // It had an @ but didn't match the regex for a file path, treat as normal text
      const provider = store.llmConfig?.provider || 'gemini';
      const model = store.llmConfig?.model;
      store.addMessage({ type: 'ai', content: '', isLoading: true });
      try {
        const response = await callLLM([{ role: 'user', content: trimmed }], provider, model, store.reasoningEnabled);
        const suffix = response.truncated
          ? `\n\n> Response may be truncated by provider limit (${response.finishReason || 'length'}).`
          : '';
        useStore.getState().startTypingLastMessage(response.text + suffix, response.reasoning);
      } catch (error) {
        if (!isInterrupted(error)) {
          store.updateLastMessage(`**LLM Error:**\n${error instanceof Error ? error.message : String(error)}`);
        }
      }
    }
  } else {
    const provider = store.llmConfig?.provider || 'gemini';
    const model = store.llmConfig?.model;
    store.addMessage({ type: 'ai', content: '', isLoading: true });
    try {
      const response = await callLLM([{ role: 'user', content: trimmed }], provider, model, store.reasoningEnabled);
      const suffix = response.truncated
        ? `\n\n> Response may be truncated by provider limit (${response.finishReason || 'length'}).`
        : '';
      useStore.getState().startTypingLastMessage(response.text + suffix, response.reasoning);
    } catch (error) {
      if (!isInterrupted(error)) {
        store.updateLastMessage(formatMcpError(error));
      }
    }
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
