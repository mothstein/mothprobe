import { spawn, ChildProcess } from 'child_process';
import { useStore } from './store.js';
import path from 'path';
import fs from 'fs';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

let mcpProcess: ChildProcess | null = null;
let currentExePath = '';
let rpcId = 1;
let interrupting = false;

const REQUEST_TIMEOUT_MS = 120_000;

const pendingRequests = new Map<number, {
  resolve: (res: any) => void;
  reject: (err: any) => void;
  timer: NodeJS.Timeout;
}>();

export type LlmMessage = {
  role: 'system' | 'user' | 'assistant';
  content: string;
};

export type McpErrorData = {
  provider?: string;
  kind?: string;
  http_status?: number;
  retryable?: boolean;
  finish_reason?: string;
  truncated?: boolean;
};

export class McpRequestError extends Error {
  code?: number;
  data?: McpErrorData;

  constructor(message: string, code?: number, data?: McpErrorData) {
    super(message);
    this.name = 'McpRequestError';
    this.code = code;
    this.data = data;
  }
}

export type LlmResponse = {
  text: string;
  provider: string;
  model?: string;
  finishReason?: string;
  truncated: boolean;
  reasoning?: string;
  reasoningMode?: string;
  session?: any;
};

export type ReasoningMode = 'default' | 'fast' | 'advanced';

export type ChatSessionSummary = {
  session_id: string;
  title: string;
  updated_at: string;
  message_count: number;
};

export type ChatSessionPayload = {
  summary: ChatSessionSummary;
  messages: any[];
};

export type PromptPayload = {
  name: string;
  description: string;
  messages: LlmMessage[];
};

export type LlmProviderModel = {
  name: string;
  configured: boolean;
  requires_api_key?: boolean;
  api_key_configured?: boolean;
  current_model: string;
  models: string[];
  max_tokens: number;
};

export type LlmModelFetchResult = {
  provider: string;
  source: string;
  models: string[];
};

export function startMcpClient() {
  const store = useStore.getState();
  
  const buildDir = path.resolve(__dirname, '../../build');
  const possiblePaths = [
    path.join(buildDir, 'mothprobe_mcp.exe'),
    path.join(buildDir, 'Debug', 'mothprobe_mcp.exe'),
    path.join(buildDir, 'Release', 'mothprobe_mcp.exe'),
  ];
  
  let exePath = '';
  for (const p of possiblePaths) {
    if (fs.existsSync(p)) {
      exePath = p;
      currentExePath = p;
      break;
    }
  }

  if (!exePath) {
    store.addMessage({ type: 'system', content: `[MCP Error] Could not find mothprobe_mcp.exe in ${buildDir}` });
    return;
  }

  mcpProcess = spawn(exePath, [], { stdio: ['pipe', 'pipe', 'pipe'] });

  mcpProcess.on('spawn', async () => {
    store.setConnected(true);
    store.addMessage({ type: 'mcp', content: 'Connected to MCP daemon.' });
    
    try {
      await sendMcpRequest('initialize', {
        protocolVersion: '2025-11-25',
        capabilities: {}
      });
      if (mcpProcess && mcpProcess.stdin) {
        mcpProcess.stdin.write(JSON.stringify({ jsonrpc: "2.0", method: "notifications/initialized" }) + '\n');
      }
    } catch (e) {
      store.addMessage({ type: 'system', content: `[MCP Error] Auto-initialize failed: ${e instanceof Error ? e.message : String(e)}` });
    }
  });

  let buffer = '';

  mcpProcess.stdout?.on('data', (chunk) => {
    buffer += chunk.toString();
    const lines = buffer.split('\n');
    buffer = lines.pop() || '';

    for (const line of lines) {
      if (!line.trim()) continue;
      try {
        const msg = JSON.parse(line);
        if (msg.id !== undefined && pendingRequests.has(msg.id)) {
          const promiseHandlers = pendingRequests.get(msg.id)!;
          clearTimeout(promiseHandlers.timer);
          if (msg.error) {
            promiseHandlers.reject(new McpRequestError(msg.error.message || 'MCP request failed', msg.error.code, msg.error.data));
          } else {
            promiseHandlers.resolve(msg.result);
          }
          pendingRequests.delete(msg.id);
        } else {
          store.addMessage({ type: 'mcp', content: `Event: ${JSON.stringify(msg)}` });
        }
      } catch (e) {
        store.addMessage({ type: 'mcp', content: line });
      }
    }
  });

  mcpProcess.stderr?.on('data', (chunk) => {
    const text = chunk.toString().trim();
    if (text) store.addMessage({ type: 'mcp', content: text });
  });

  mcpProcess.on('close', (code) => {
    store.setConnected(false);
    if (interrupting) {
      interrupting = false;
      setTimeout(() => startMcpClient(), 150);
      return;
    }
    rejectPending(new McpRequestError(`MCP daemon exited before replying (exit code: ${code})`, -32001, {
      kind: 'mcp_disconnected',
      retryable: true
    }));
    store.addMessage({ type: 'system', content: `MCP disconnected (exit code: ${code}).` });
  });

  mcpProcess.on('error', (err) => {
    store.setConnected(false);
    rejectPending(new McpRequestError(`MCP daemon error: ${err.message}`, -32001, {
      kind: 'mcp_disconnected',
      retryable: true
    }));
    store.addMessage({ type: 'system', content: `MCP error: ${err.message}` });
  });
}

export function restartMcpClient() {
  if (mcpProcess) {
    mcpProcess.kill();
    mcpProcess = null;
  }
  startMcpClient();
}

export function getMcpStatus() {
  return {
    connected: useStore.getState().isConnected,
    pid: mcpProcess?.pid || null,
    executable: currentExePath,
    pendingRequests: pendingRequests.size
  };
}

export function interruptMcpClient() {
  interrupting = true;
  for (const [id, req] of pendingRequests.entries()) {
    clearTimeout(req.timer);
    req.reject(new McpRequestError(`Request ${id} interrupted`, -32800, { kind: 'interrupted' }));
  }
  pendingRequests.clear();
  if (mcpProcess?.stdin) {
    mcpProcess.stdin.write(JSON.stringify({ jsonrpc: '2.0', method: 'cancel' }) + '\n');
  }
}

export function stopMcpClient() {
  if (mcpProcess) {
    mcpProcess.kill();
    mcpProcess = null;
  }
}

export async function ensureMcpReady() {
  if (!getMcpStatus().connected) {
    startMcpClient();
    await new Promise(r => setTimeout(r, 2000));
  }
}

export async function mcpClientAction(action: string, args: any = {}) {
  return await sendMcpRequest(`cli/${action}`, args);
}

function rejectPending(error: McpRequestError) {
  for (const [, pending] of pendingRequests) {
    clearTimeout(pending.timer);
    pending.reject(error);
  }
  pendingRequests.clear();
}

export function sendMcpRequest(method: string, params: any = {}): Promise<any> {
  return new Promise((resolve, reject) => {
    if (!mcpProcess || !useStore.getState().isConnected) {
      reject(new McpRequestError("MCP is not connected", -32001, {
        kind: 'mcp_disconnected',
        retryable: true
      }));
      return;
    }
    
    const id = rpcId++;
    const payload = JSON.stringify({ jsonrpc: "2.0", id, method, params });
    
    const timer = setTimeout(() => {
      pendingRequests.delete(id);
      reject(new McpRequestError(`MCP request timed out after ${REQUEST_TIMEOUT_MS / 1000}s`, -32002, {
        kind: 'timeout',
        retryable: true
      }));
    }, REQUEST_TIMEOUT_MS);

    pendingRequests.set(id, { resolve, reject, timer });
    
    if (!mcpProcess.stdin) {
      clearTimeout(timer);
      pendingRequests.delete(id);
      reject(new McpRequestError("MCP stdin is not available", -32001, {
        kind: 'mcp_disconnected',
        retryable: true
      }));
      return;
    }

    mcpProcess.stdin.write(payload + '\n', (error) => {
      if (!error) return;
      const pending = pendingRequests.get(id);
      if (pending) {
        clearTimeout(pending.timer);
        pendingRequests.delete(id);
      }
      reject(new McpRequestError(`Failed to write MCP request: ${error.message}`, -32001, {
        kind: 'mcp_disconnected',
        retryable: true
      }));
    });
  });
}

export async function getPrompt(name: string): Promise<PromptPayload> {
  return await sendMcpRequest('prompts/get', { name }) as PromptPayload;
}

export async function setReasoningMode(mode: string): Promise<any> {
  return await sendMcpRequest('chat/reasoning', { mode });
}

export async function loadChatSession(sessionId: string): Promise<any> {
  return await sendMcpRequest('chat/load', { session_id: sessionId });
}

export async function listChatSessions(): Promise<ChatSessionSummary[]> {
  const res = await sendMcpRequest('chat/sessions');
  return res.sessions || [];
}

export async function clearChatSession(): Promise<any> {
  return await sendMcpRequest('chat/clear');
}

export async function listLlmProviders(): Promise<LlmProviderModel[]> {
  const result = await sendMcpRequest('llm/list_providers');
  return Array.isArray(result?.providers) ? result.providers : [];
}

export async function fetchModelsForProvider(provider: string): Promise<LlmModelFetchResult> {
  const result = await sendMcpRequest('llm/fetch_models', { provider });
  return {
    provider: result?.provider || provider,
    source: result?.source || 'provider',
    models: Array.isArray(result?.models) ? result.models : []
  };
}

export async function configureProviderApiKey(provider: string, apiKey: string): Promise<void> {
  await sendMcpRequest('llm/configure_provider', {
    provider,
    api_key: apiKey
  });
}

export async function callLLM(messages: LlmMessage[], provider: string, model?: string, includeReasoning?: boolean): Promise<LlmResponse> {
  const result = await sendMcpRequest('llm/chat', {
    provider,
    model,
    messages,
    stream: false,
    include_reasoning: includeReasoning
  });
  if (!result || typeof result.text !== 'string') {
    throw new Error('MCP returned an invalid llm/chat response');
  }
  return {
    text: result.text,
    provider: result.provider || provider,
    model: result.model || model,
    finishReason: result.finish_reason,
    truncated: Boolean(result.truncated),
    reasoning: result.reasoning || undefined
  };
}
