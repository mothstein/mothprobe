#!/usr/bin/env node
import React, { useEffect, useState } from 'react';
import { render, Box, Text, useInput } from 'ink';
import { Header } from './components/Header.js';
import { HelpPanel } from './components/HelpPanel.js';
import { ChatArea } from './components/ChatArea.js';
import { InputBar } from './components/InputBar.js';
import { LLMSetupModal } from './components/LLMSetupModal.js';
import { ModelPickerModal } from './components/ModelPickerModal.js';
import { ReasoningModeModal } from './components/ReasoningModeModal.js';
import { ResumeSessionModal } from './components/ResumeSessionModal.js';
import { DEFAULT_SHELL_PERMISSIONS, useStore, type AgentSummary, type PermissionLevel, type ReasoningMode } from './store.js';
import {
  callLLM,
  ensureMcpReady,
  interruptMcpClient,
  mcpClientAction,
  startMcpClient,
  stopMcpClient
} from './mcpClient.js';
import fs from 'fs';
import path from 'path';

const VERSION = '0.1.0-alpha';

type CliOptions = {
  workspacePath?: string;
  permissionLevel?: PermissionLevel;
  activeAgent?: string | null;
  help?: boolean;
  version?: boolean;
};

function printHelp() {
  console.log([
    'MothProbe TUI',
    '',
    'Usage:',
    '  mothprobe-tui [workspace] [options]',
    '',
    'Options:',
    '  -w, --workspace <path>       Workspace path for file context and MCP',
    '      --permission <level>     Shell permission level: default or full',
    '      --agent <name>           Activate a workspace agent prompt',
    '      --default                Shortcut for --permission default',
    '      --full                   Shortcut for --permission full',
    '  -h, --help                   Show this help',
    '  -v, --version                Show version'
  ].join('\n'));
}

function readCliValue(argv: string[], index: number, flag: string): string {
  const value = argv[index + 1];
  if (!value || value.startsWith('-')) {
    console.error(`${flag} requires a value.`);
    process.exit(1);
  }
  return value;
}

function parsePermissionLevel(value: string): PermissionLevel {
  const level = value.toLowerCase();
  if (level === 'default' || level === 'full') return level;
  console.error(`Invalid permission level: ${value}. Expected default or full.`);
  process.exit(1);
}

function normalizeAgentName(value: string): string | null {
  const trimmed = value.trim();
  if (!trimmed || ['none', 'off', 'clear', 'default'].includes(trimmed.toLowerCase())) return null;
  return trimmed.startsWith('agent_') ? trimmed : `agent_${trimmed}`;
}

function parseCliArgs(argv: string[]): CliOptions {
  const options: CliOptions = {};
  for (let index = 0; index < argv.length; index += 1) {
    const arg = argv[index];
    if (arg === '-h' || arg === '--help') {
      options.help = true;
    } else if (arg === '-v' || arg === '--version') {
      options.version = true;
    } else if (arg === '-w' || arg === '--workspace' || arg === '--cwd') {
      options.workspacePath = readCliValue(argv, index, arg);
      index += 1;
    } else if (arg === '--permission') {
      options.permissionLevel = parsePermissionLevel(readCliValue(argv, index, arg));
      index += 1;
    } else if (arg === '--default') {
      options.permissionLevel = 'default';
    } else if (arg === '--full') {
      options.permissionLevel = 'full';
    } else if (arg === '--agent') {
      options.activeAgent = normalizeAgentName(readCliValue(argv, index, arg));
      index += 1;
    } else if (arg.startsWith('-')) {
      console.error(`Unknown option: ${arg}`);
      process.exit(1);
    } else if (!options.workspacePath) {
      options.workspacePath = arg;
    }
  }
  return options;
}

function readClientLlmConfig() {
  try {
    const configPath = path.resolve(process.cwd(), 'data', 'config.json');
    if (!fs.existsSync(configPath)) return { provider: 'gemini', model: undefined as string | undefined };
    const conf = JSON.parse(fs.readFileSync(configPath, 'utf8'));
    return conf.llmConfig || { provider: conf.provider || 'gemini', model: conf.model };
  } catch {
    return { provider: 'gemini', model: undefined as string | undefined };
  }
}

function valueAfter(args: string[], flag: string): string | undefined {
  const index = args.indexOf(flag);
  return index >= 0 ? args[index + 1] : undefined;
}

function isQuestionMode(args: string[]) {
  if (args.length === 0 || args[0] === 'mcp' || args[0].startsWith('-')) return false;
  const maybePath = path.resolve(args[0]);
  return !(args.length === 1 && fs.existsSync(maybePath) && fs.statSync(maybePath).isDirectory());
}

async function runQuestionCli(question: string) {
  await ensureMcpReady();
  const config = readClientLlmConfig();
  const response = await callLLM([{ role: 'user', content: question }], config.provider || 'gemini', config.model, true);
  console.log(response.text);
  stopMcpClient();
}

function printMcpHelp() {
  console.log([
    'Manage MothProbe MCP clients',
    '',
    'Usage:',
    '  mothprobe mcp list',
    '  mothprobe mcp get <name>',
    '  mothprobe mcp add <name> --url <url> [--sse] [--key <key>]',
    '  mothprobe mcp add <name> -- <command> [args...]',
    '  mothprobe mcp remove <name>',
    '  mothprobe mcp login <name> --key <key>',
    '  mothprobe mcp logout <name>',
    '  mothprobe mcp connect <name>',
    '  mothprobe mcp disconnect <name>'
  ].join('\n'));
}

async function runMcpCli(args: string[]) {
  const command = args[0] || 'help';
  if (command === 'help' || command === '--help' || command === '-h') {
    printMcpHelp();
    return;
  }
  await ensureMcpReady();
  let result: unknown;
  if (command === 'list') {
    result = await mcpClientAction('list');
  } else if (command === 'add') {
    const name = args[1];
    if (!name) throw new Error('mcp add requires <name>');
    const sep = args.indexOf('--');
    if (sep >= 0) {
      result = await mcpClientAction('add', {
        name,
        transport: 'stdio',
        command: args[sep + 1],
        args: args.slice(sep + 2)
      });
    } else {
      const url = valueAfter(args, '--url');
      if (!url) throw new Error('mcp add requires --url <url> or -- <command>');
      result = await mcpClientAction('add', {
        name,
        transport: args.includes('--sse') ? 'sse' : 'http',
        url,
        key: valueAfter(args, '--key') || ''
      });
    }
  } else if (['get', 'remove', 'logout', 'connect', 'disconnect'].includes(command)) {
    const name = args[1];
    if (!name) throw new Error(`mcp ${command} requires <name>`);
    result = await mcpClientAction(command, { name });
  } else if (command === 'login') {
    const name = args[1];
    if (!name) throw new Error('mcp login requires <name>');
    result = await mcpClientAction('login', {
      name,
      key: valueAfter(args, '--key') || '',
      auth_header: valueAfter(args, '--auth-header') || ''
    });
  } else {
    throw new Error(`Unknown mcp command: ${command}`);
  }
  console.log(JSON.stringify(result, null, 2));
  stopMcpClient();
}

const rawCliArgs = process.argv.slice(2);
if (rawCliArgs[0] === 'mcp') {
  try {
    await runMcpCli(rawCliArgs.slice(1));
    process.exit(0);
  } catch (error) {
    console.error(error instanceof Error ? error.message : String(error));
    stopMcpClient();
    process.exit(1);
  }
}
if (isQuestionMode(rawCliArgs)) {
  try {
    await runQuestionCli(rawCliArgs.join(' '));
    process.exit(0);
  } catch (error) {
    console.error(error instanceof Error ? error.message : String(error));
    stopMcpClient();
    process.exit(1);
  }
}
const cliOptions = parseCliArgs(rawCliArgs);

if (cliOptions.help) {
  printHelp();
  process.exit(0);
}

if (cliOptions.version) {
  console.log(VERSION);
  process.exit(0);
}

if (rawCliArgs.length === 0 && !process.stdin.isTTY) {
  printHelp();
  process.exit(0);
}

const initialWorkspacePath = path.resolve(cliOptions.workspacePath || process.cwd());
if (!fs.existsSync(initialWorkspacePath) || !fs.statSync(initialWorkspacePath).isDirectory()) {
  console.error(`Workspace path does not exist or is not a directory: ${initialWorkspacePath}`);
  process.exit(1);
}

useStore.setState({
  workspacePath: initialWorkspacePath,
  permissionLevel: cliOptions.permissionLevel || 'default',
  activeAgent: cliOptions.activeAgent === undefined ? null : cliOptions.activeAgent,
  permissions: cliOptions.permissionLevel === 'default' ? [...DEFAULT_SHELL_PERMISSIONS] : useStore.getState().permissions
});

function App() {
  const llmConfig = useStore((state) => state.llmConfig);
  const modelPickerOpen = useStore((state) => state.modelPickerOpen);
  const resumePickerOpen = useStore((state) => state.resumePickerOpen);
  const reasoningPickerOpen = useStore((state) => state.reasoningPickerOpen);
  const theme = useStore((state) => state.theme);
  const themeName = useStore((state) => state.themeName);
  const setLlmConfig = useStore((state) => state.setLlmConfig);
  const setTheme = useStore((state) => state.setTheme);
  const setWorkspacePath = useStore((state) => state.setWorkspacePath);
  const setAgentsList = useStore((state) => state.setAgentsList);
  const setActiveAgent = useStore((state) => state.setActiveAgent);
  const setPermissionLevel = useStore((state) => state.setPermissionLevel);
  const refreshThemes = useStore((state) => state.refreshThemes);
  const toggleLatestThought = useStore((state) => state.toggleLatestThought);
  const interruptActiveAi = useStore((state) => state.interruptActiveAi);
  const hasActiveAi = useStore((state) => state.hasActiveAi);
  const [loadingConfig, setLoadingConfig] = useState(true);
  const chatHistory = useStore((state) => state.chatHistory);
  const permissions = useStore((state) => state.permissions);
  const reasoningMode = useStore((state) => state.reasoningMode);
  const workspacePath = useStore((state) => state.workspacePath);
  const agentsList = useStore((state) => state.agentsList);
  const permissionLevel = useStore((state) => state.permissionLevel);
  const activeAgent = useStore((state) => state.activeAgent);

  useInput((input, key) => {
    if (input === '\u000f' || (key.ctrl && input.toLowerCase() === 'o')) {
      toggleLatestThought();
      return;
    }
    if ((key.escape || input === '\u001b') && hasActiveAi()) {
      const changed = interruptActiveAi();
      if (changed) interruptMcpClient();
    }
  });
  
  useEffect(() => {
    try {
      const dataDir = path.resolve(process.cwd(), 'data');
      const configPath = path.join(dataDir, 'config.json');
      refreshThemes();
      if (fs.existsSync(configPath)) {
        const data = fs.readFileSync(configPath, 'utf8');
        const conf = JSON.parse(data);
        if (conf.theme) {
          setTheme(conf.theme);
        } else {
          setTheme('default');
        }
        if (conf.llmConfig) {
          setLlmConfig(conf.llmConfig);
        } else if (conf.provider) {
          setLlmConfig(conf);
        }
        if (Array.isArray(conf.permissions) && !cliOptions.permissionLevel) {
          useStore.setState({ permissions: conf.permissions });
        }
        if ((conf.permissionLevel === 'default' || conf.permissionLevel === 'full') && !cliOptions.permissionLevel) {
          setPermissionLevel(conf.permissionLevel);
        }
        if (conf.workspacePath && !cliOptions.workspacePath && fs.existsSync(conf.workspacePath)) {
          setWorkspacePath(path.resolve(conf.workspacePath));
        }
        if (Array.isArray(conf.agentsList)) {
          setAgentsList(conf.agentsList as AgentSummary[]);
        }
        if (conf.activeAgent !== undefined && cliOptions.activeAgent === undefined) {
          setActiveAgent(conf.activeAgent);
        }
        if (conf.reasoningMode) {
          useStore.setState({ reasoningMode: conf.reasoningMode as ReasoningMode });
        }
      } else {
        setTheme('default');
      }
    } catch (e) {
      setTheme('default');
    }
    setLoadingConfig(false);

    startMcpClient();
  }, [refreshThemes, setLlmConfig, setTheme]);

  useEffect(() => {
    if (!loadingConfig) {
      try {
        const dataDir = path.resolve(process.cwd(), 'data');
        if (!fs.existsSync(dataDir)) {
          fs.mkdirSync(dataDir);
        }
        fs.writeFileSync(
          path.join(dataDir, 'config.json'), 
          JSON.stringify({
            llmConfig,
            theme: themeName,
            permissions,
            reasoningMode,
            workspacePath,
            agentsList,
            permissionLevel,
            activeAgent
          }, null, 2)
        );
      } catch (e) {}
    }
  }, [activeAgent, agentsList, llmConfig, loadingConfig, permissionLevel, permissions, reasoningMode, themeName, workspacePath]);

  if (loadingConfig) {
    return <Text color={theme.text}>Loading...</Text>;
  }

  const showHome = chatHistory.length === 0;

  return (
    <Box flexDirection="column" minHeight={28} paddingX={2}>
      <Header />
      {modelPickerOpen ? (
        <ModelPickerModal />
      ) : resumePickerOpen ? (
        <ResumeSessionModal />
      ) : reasoningPickerOpen ? (
        <ReasoningModeModal />
      ) : !llmConfig ? (
        <LLMSetupModal />
      ) : (
        <>
          {showHome && <HelpPanel />}
          <ChatArea />
          <InputBar />
        </>
      )}
    </Box>
  );
}

render(<App />);
