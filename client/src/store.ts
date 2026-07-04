import { create } from 'zustand';
import { fallbackTheme, listThemes, loadTheme, normalizeThemeName, type Theme } from './themeManager.js';

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

export type Session = {
  id: string;
  updatedAt: number;
  history: ChatMessage[];
};

interface AppState {
  chatHistory: ChatMessage[];
  mode: AppMode;
  isConnected: boolean;
  llmConfig: LlmConfig | null;
  modelPickerOpen: boolean;
  themeName: string;
  theme: Theme;
  availableThemes: string[];
  permissions: string[];
  reasoningEnabled: boolean;
  addMessage: (msg: Omit<ChatMessage, 'id'>) => void;
  setChatHistory: (history: ChatMessage[]) => void;
  grantPermission: (cmd: string) => void;
  revokePermission: (cmd: string) => void;
  toggleReasoning: () => void;
  updateLastMessage: (content: string, reasoning?: string) => void;
  startTypingLastMessage: (content: string, reasoning?: string) => void;
  typeNextChunk: (id: string, chunkSize: number) => void;
  toggleLatestThought: () => void;
  interruptActiveAi: () => boolean;
  hasActiveAi: () => boolean;
  setMode: (mode: AppMode) => void;
  setConnected: (connected: boolean) => void;
  setLlmConfig: (config: LlmConfig) => void;
  setModelPickerOpen: (open: boolean) => void;
  setTheme: (themeName: string) => boolean;
  refreshThemes: () => void;
}

export const useStore = create<AppState>((set, get) => ({
  chatHistory: [],
  mode: 'chat',
  isConnected: false,
  llmConfig: null,
  modelPickerOpen: false,
  themeName: 'default',
  theme: fallbackTheme,
  availableThemes: listThemes(),
  permissions: ["dir", "ls", "echo", "whoami", "hostname", "ver"],
  reasoningEnabled: true,
  setChatHistory: (history) => set({ chatHistory: history }),
  grantPermission: (cmd) => set((state) => ({ 
    permissions: state.permissions.includes(cmd) ? state.permissions : [...state.permissions, cmd] 
  })),
  revokePermission: (cmd) => set((state) => ({
    permissions: state.permissions.filter((p) => p !== cmd)
  })),
  toggleReasoning: () => set((state) => ({ reasoningEnabled: !state.reasoningEnabled })),
  addMessage: (msg) => set((state) => ({ 
    chatHistory: [...state.chatHistory, {
      ...msg,
      id: Date.now().toString() + Math.random().toString(),
      thinkingStartedAt: msg.isLoading ? Date.now() : msg.thinkingStartedAt
    }] 
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
        content: '',
        reasoning: reasoning !== undefined ? reasoning : newHistory[newHistory.length - 1].reasoning,
        pendingContent: content,
        isTyping: true,
        isLoading: false,
        thinkingEndedAt: endedAt,
        thoughtDurationMs: previous.thinkingStartedAt && endedAt
          ? endedAt - previous.thinkingStartedAt
          : previous.thoughtDurationMs
      };
    }
    return { chatHistory: newHistory };
  }),
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
  setTheme: (themeName) => {
    const normalized = normalizeThemeName(themeName);
    const available = listThemes();
    if (!available.includes(normalized)) return false;
    const theme = loadTheme(normalized);
    set({ themeName: normalized, theme, availableThemes: available });
    return true;
  },
  refreshThemes: () => set({ availableThemes: listThemes() }),
}));
