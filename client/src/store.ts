import { create } from 'zustand';
import { fallbackTheme, listThemes, loadTheme, normalizeThemeName, type Theme } from './themeManager.js';

export const DEFAULT_SHELL_PERMISSIONS = ["dir", "ls", "echo", "whoami", "hostname", "ver"] as const;

export type ChatMessage = {
  id: string;
  type: 'user' | 'ai' | 'system' | 'mcp' | 'shell';
  content: string;
  reasoning?: string;
  pendingContent?: string;
  isTyping?: boolean;
  isLoading?: boolean;
  thoughtExpanded?: boolean;
  thinkingStartedAt?: number;
  thinkingEndedAt?: number;
  thoughtDurationMs?: number;
  interrupted?: boolean;
  mode?: string;
};

export type AppMode = 'chat' | 'command' | 'shell' | 'file';

export type LlmConfig = {
  provider: string;
  model?: string;
};

export type ReasoningMode = 'default' | 'advanced' | 'fast';
export type PermissionLevel = 'default' | 'full';

export type AgentSummary = {
  name: string;
  title?: string;
  description?: string;
};

export type ToolApprovalRequest = {
  command: string;
  reason: string;
  resolve: (response: string) => void;
};

interface AppState {
  chatHistory: ChatMessage[];
  mode: AppMode;
  isConnected: boolean;
  workspacePath: string;
  llmConfig: LlmConfig | null;
  modelPickerOpen: boolean;
  resumePickerOpen: boolean;
  reasoningPickerOpen: boolean;
  activeSessionId: string | null;
  reasoningMode: ReasoningMode;
  agentsList: AgentSummary[];
  activeAgent: string | null;
  permissionLevel: PermissionLevel;
  themeName: string;
  theme: Theme;
  availableThemes: string[];
  permissions: string[];
  toolApprovalRequest: ToolApprovalRequest | null;
  addMessage: (msg: Omit<ChatMessage, 'id'>) => string;
  setChatHistory: (history: ChatMessage[]) => void;
  setWorkspacePath: (workspacePath: string) => void;
  setActiveSessionId: (sessionId: string | null) => void;
  setReasoningMode: (mode: ReasoningMode) => void;
  setAgentsList: (agentsList: AgentSummary[]) => void;
  setActiveAgent: (activeAgent: string | null) => void;
  setPermissionLevel: (permissionLevel: PermissionLevel) => void;
  resetDefaultPermissions: () => void;
  grantPermission: (cmd: string) => void;
  revokePermission: (cmd: string) => void;
  updateMessage: (id: string, content: string, reasoning?: string) => void;
  updateLastMessage: (content: string, reasoning?: string) => void;
  startTypingMessage: (id: string, content: string, reasoning?: string) => void;
  startTypingLastMessage: (content: string, reasoning?: string) => void;
  typeNextChunk: (id: string, chunkSize: number) => void;
  toggleLatestThought: () => void;
  interruptActiveAi: () => boolean;
  hasActiveAi: () => boolean;
  setMode: (mode: AppMode) => void;
  setConnected: (connected: boolean) => void;
  setLlmConfig: (config: LlmConfig) => void;
  setModelPickerOpen: (open: boolean) => void;
  setResumePickerOpen: (open: boolean) => void;
  setReasoningPickerOpen: (open: boolean) => void;
  setTheme: (themeName: string) => boolean;
  refreshThemes: () => void;
  setToolApprovalRequest: (req: ToolApprovalRequest | null) => void;
}

export const useStore = create<AppState>((set, get) => ({
  chatHistory: [],
  mode: 'chat',
  isConnected: false,
  workspacePath: process.cwd(),
  llmConfig: null,
  modelPickerOpen: false,
  resumePickerOpen: false,
  reasoningPickerOpen: false,
  activeSessionId: null,
  reasoningMode: 'default',
  agentsList: [],
  activeAgent: null,
  permissionLevel: 'default',
  themeName: 'default',
  theme: fallbackTheme,
  availableThemes: listThemes(),
  permissions: [...DEFAULT_SHELL_PERMISSIONS],
  toolApprovalRequest: null,
  setChatHistory: (history) => set({ chatHistory: history }),
  setWorkspacePath: (workspacePath) => set({ workspacePath }),
  setActiveSessionId: (activeSessionId) => set({ activeSessionId }),
  setReasoningMode: (reasoningMode) => set({ reasoningMode }),
  setAgentsList: (agentsList) => set({ agentsList }),
  setActiveAgent: (activeAgent) => set({ activeAgent }),
  setPermissionLevel: (permissionLevel) => set({ permissionLevel }),
  resetDefaultPermissions: () => set({ permissions: [...DEFAULT_SHELL_PERMISSIONS], permissionLevel: 'default' }),
  grantPermission: (cmd) => set((state) => ({
    permissions: state.permissions.includes(cmd.toLowerCase()) ? state.permissions : [...state.permissions, cmd.toLowerCase()]
  })),
  revokePermission: (cmd) => set((state) => ({
    permissions: state.permissions.filter((p) => p !== cmd.toLowerCase())
  })),
  addMessage: (msg) => {
    const id = Date.now().toString() + Math.random().toString();
    set((state) => ({
      chatHistory: [...state.chatHistory, {
        ...msg,
        id,
        thinkingStartedAt: msg.isLoading ? Date.now() : msg.thinkingStartedAt
      }]
    }));
    return id;
  },
  updateMessage: (id, content, reasoning) => set((state) => ({
    chatHistory: state.chatHistory.map((message) => {
      if (message.id !== id) return message;
      const endedAt = message.isLoading ? Date.now() : message.thinkingEndedAt;
      return {
        ...message,
        content,
        reasoning: reasoning !== undefined ? reasoning : message.reasoning,
        pendingContent: undefined,
        isTyping: false,
        isLoading: false,
        thinkingEndedAt: endedAt,
        thoughtDurationMs: message.thinkingStartedAt && endedAt
          ? endedAt - message.thinkingStartedAt
          : message.thoughtDurationMs
      };
    })
  })),
  updateLastMessage: (content, reasoning) => set((state) => {
    const newHistory = [...state.chatHistory];
    if (newHistory.length > 0) {
      const previous = newHistory[newHistory.length - 1];
      const endedAt = previous.isLoading ? Date.now() : previous.thinkingEndedAt;
      newHistory[newHistory.length - 1] = {
        ...previous,
        content,
        reasoning: reasoning !== undefined ? reasoning : newHistory[newHistory.length - 1].reasoning,
        pendingContent: undefined,
        isTyping: false,
        isLoading: false,
        thinkingEndedAt: endedAt,
        thoughtDurationMs: previous.thinkingStartedAt && endedAt
          ? endedAt - previous.thinkingStartedAt
          : previous.thoughtDurationMs
      };
    }
    return { chatHistory: newHistory };
  }),
  startTypingLastMessage: (content, reasoning) => set((state) => {
    const newHistory = [...state.chatHistory];
    if (newHistory.length > 0) {
      const previous = newHistory[newHistory.length - 1];
      const endedAt = previous.isLoading ? Date.now() : previous.thinkingEndedAt;
      newHistory[newHistory.length - 1] = {
        ...previous,
        content: content,
        reasoning: reasoning !== undefined ? reasoning : newHistory[newHistory.length - 1].reasoning,
        pendingContent: undefined,
        isTyping: false,
        isLoading: false,
        thinkingEndedAt: endedAt,
        thoughtDurationMs: previous.thinkingStartedAt && endedAt
          ? endedAt - previous.thinkingStartedAt
          : previous.thoughtDurationMs
      };
    }
    return { chatHistory: newHistory };
  }),
  startTypingMessage: (id, content, reasoning) => set((state) => ({
    chatHistory: state.chatHistory.map((message) => {
      if (message.id !== id) return message;
      const endedAt = message.isLoading ? Date.now() : message.thinkingEndedAt;
      return {
        ...message,
        content: content,
        reasoning: reasoning !== undefined ? reasoning : message.reasoning,
        pendingContent: undefined,
        isTyping: false,
        isLoading: false,
        thinkingEndedAt: endedAt,
        thoughtDurationMs: message.thinkingStartedAt && endedAt
          ? endedAt - message.thinkingStartedAt
          : message.thoughtDurationMs
      };
    })
  })),
  typeNextChunk: (id, chunkSize) => set((state) => ({
    chatHistory: state.chatHistory.map((message) => {
      if (message.id !== id || !message.isTyping || !message.pendingContent) return message;
      const chunk = message.pendingContent.slice(0, chunkSize);
      const pendingContent = message.pendingContent.slice(chunkSize);
      return {
        ...message,
        content: message.content + chunk,
        pendingContent: pendingContent || undefined,
        isTyping: pendingContent.length > 0
      };
    })
  })),
  toggleLatestThought: () => set((state) => {
    const index = [...state.chatHistory].reverse().findIndex((message) => message.type === 'ai' && Boolean(message.reasoning));
    if (index < 0) return state;
    const targetIndex = state.chatHistory.length - 1 - index;
    return {
      chatHistory: state.chatHistory.map((message, itemIndex) => itemIndex === targetIndex
        ? { ...message, thoughtExpanded: !message.thoughtExpanded }
        : message)
    };
  }),
  interruptActiveAi: () => {
    let changed = false;
    set((state) => ({
      chatHistory: state.chatHistory.map((message) => {
        if (message.type !== 'ai' || (!message.isLoading && !message.isTyping)) return message;
        changed = true;
        return {
          ...message,
          content: message.content || 'Interrupted by user.',
          pendingContent: undefined,
          isLoading: false,
          isTyping: false,
          interrupted: true,
          thinkingEndedAt: Date.now(),
          thoughtDurationMs: message.thinkingStartedAt ? Date.now() - message.thinkingStartedAt : message.thoughtDurationMs
        };
      })
    }));
    return changed;
  },
  hasActiveAi: () => get().chatHistory.some((message) =>
    message.type === 'ai' && (message.isLoading || message.isTyping)
  ),
  setMode: (mode) => set({ mode }),
  setConnected: (isConnected) => set({ isConnected }),
  setLlmConfig: (llmConfig) => set({ llmConfig }),
  setModelPickerOpen: (modelPickerOpen) => set({ modelPickerOpen }),
  setResumePickerOpen: (resumePickerOpen) => set({ resumePickerOpen }),
  setReasoningPickerOpen: (reasoningPickerOpen) => set({ reasoningPickerOpen }),
  setTheme: (themeName) => {
    const normalized = normalizeThemeName(themeName);
    const available = listThemes();
    if (!available.includes(normalized)) return false;
    const theme = loadTheme(normalized);
    set({ themeName: normalized, theme, availableThemes: available });
    return true;
  },
  refreshThemes: () => set({ availableThemes: listThemes() }),
  setToolApprovalRequest: (toolApprovalRequest) => set({ toolApprovalRequest }),
}));
