import React, { useEffect, useState } from 'react';
import { render, Box, Text, useInput } from 'ink';
import { Header } from './components/Header.js';
import { HelpPanel } from './components/HelpPanel.js';
import { ChatArea } from './components/ChatArea.js';
import { InputBar } from './components/InputBar.js';
import { LLMSetupModal } from './components/LLMSetupModal.js';
import { ModelPickerModal } from './components/ModelPickerModal.js';
import { useStore } from './store.js';
import { interruptMcpClient, startMcpClient } from './mcpClient.js';
import fs from 'fs';
import path from 'path';

function App() {
  const llmConfig = useStore((state) => state.llmConfig);
  const modelPickerOpen = useStore((state) => state.modelPickerOpen);
  const theme = useStore((state) => state.theme);
  const themeName = useStore((state) => state.themeName);
  const setLlmConfig = useStore((state) => state.setLlmConfig);
  const setTheme = useStore((state) => state.setTheme);
  const refreshThemes = useStore((state) => state.refreshThemes);
  const toggleLatestThought = useStore((state) => state.toggleLatestThought);
  const interruptActiveAi = useStore((state) => state.interruptActiveAi);
  const hasActiveAi = useStore((state) => state.hasActiveAi);
  const [loadingConfig, setLoadingConfig] = useState(true);
  const chatHistory = useStore((state) => state.chatHistory);
  const setChatHistory = useStore((state) => state.setChatHistory);
  const permissions = useStore((state) => state.permissions);
  const reasoningEnabled = useStore((state) => state.reasoningEnabled);

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
      const historyPath = path.join(dataDir, 'history.json');
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
        if (conf.permissions) {
          useStore.setState({ permissions: conf.permissions });
        }
        if (typeof conf.reasoningEnabled === 'boolean') {
          useStore.setState({ reasoningEnabled: conf.reasoningEnabled });
        }
      } else {
        setTheme('default');
      }

      if (fs.existsSync(historyPath)) {
        try {
          const histData = fs.readFileSync(historyPath, 'utf8');
          const history = JSON.parse(histData);
          if (Array.isArray(history)) {
            // Clean up transient states
            const cleanedHistory = history.map(msg => ({
              ...msg,
              isTyping: false,
              isLoading: false,
              pendingContent: undefined
            }));
            setChatHistory(cleanedHistory);
          }
        } catch (e) {}
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
          JSON.stringify({ llmConfig, theme: themeName, permissions, reasoningEnabled }, null, 2)
        );
      } catch (e) {}
    }
  }, [llmConfig, loadingConfig, themeName, permissions, reasoningEnabled]);

  useEffect(() => {
    if (!loadingConfig) {
      try {
        const dataDir = path.resolve(process.cwd(), 'data');
        if (!fs.existsSync(dataDir)) {
          fs.mkdirSync(dataDir);
        }
        // Don't save if it's completely empty to avoid overwriting a potentially corrupted load, but empty clear should be fine
        fs.writeFileSync(path.join(dataDir, 'history.json'), JSON.stringify(chatHistory, null, 2));
      } catch (e) {}
    }
  }, [chatHistory, loadingConfig]);

  if (loadingConfig) {
    return <Text color={theme.text}>Loading...</Text>;
  }

  const showHome = chatHistory.length === 0;

  return (
    <Box flexDirection="column" minHeight={28} paddingX={2}>
      <Header />
      {modelPickerOpen ? (
        <ModelPickerModal />
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
