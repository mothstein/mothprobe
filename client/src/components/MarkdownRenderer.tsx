import React from "react";
import { Box, Text } from "ink";
import type { Theme } from "../themeManager.js";

function inline(text: string, theme: Theme) {
  const parts = text.split(/(\[.*?\]\(.*?\)|\*\*[^*]+\*\*|~~[^~]+~~|`[^`]+`|_[^_]+_|\*[^*]+\*)/g);

  return (
    <Text color={theme.text}>
      {parts.map((part, index) => {
        if (!part) return null;

        const linkMatch = part.match(/^\[(.*?)\]\((.*?)\)$/);
        if (linkMatch) {
          return (
            <Text key={index}>
              <Text color={theme.user} underline>{linkMatch[1]}</Text>
              <Text color={theme.muted}> ({linkMatch[2]})</Text>
            </Text>
          );
        }
        if (part.startsWith("**") && part.endsWith("**")) {
          return <Text key={index} bold color={theme.accent}>{part.slice(2, -2)}</Text>;
        }
        if (part.startsWith("*") && part.endsWith("*")) {
          return <Text key={index} italic color={theme.text}>{part.slice(1, -1)}</Text>;
        }
        if (part.startsWith("_") && part.endsWith("_")) {
          return <Text key={index} underline color={theme.text}>{part.slice(1, -1)}</Text>;
        }
        if (part.startsWith("~~") && part.endsWith("~~")) {
          return <Text key={index} strikethrough color={theme.muted}>{part.slice(2, -2)}</Text>;
        }
        if (part.startsWith("`") && part.endsWith("`")) {
          return <Text key={index} backgroundColor={theme.codeBackground} color={theme.warning}> {part.slice(1, -1)} </Text>;
        }
        return <Text key={index}>{part}</Text>;
      })}
    </Text>
  );
}

function codeLine(line: string, theme: Theme) {
  const keyword = /^(const|let|var|function|return|if|else|for|while|class|import|from|export|def|true|false|null|undefined|auto|std|namespace|include|public|private|protected|interface|type|extends|implements|new|this|super|async|await)$/;
  const tokens = line.match(/(".*?"|'.*?'|`.*?`|\b[A-Za-z_][A-Za-z0-9_]*\b|\b\d+\b|\s+|[^\s]+)/g) ?? [line];

  return (
    <Text>
      {tokens.map((token, index) => {
        let color = theme.text;
        if (/^["'`]/.test(token)) color = theme.success;
        else if (keyword.test(token)) color = theme.accent;
        else if (/^\d+$/.test(token)) color = theme.warning;
        else if (/^[A-Z][a-zA-Z0-9_]*$/.test(token)) color = theme.user;
        else if (/[{}[\](),.:;=+\-*/<>!]/.test(token)) color = theme.muted;
        return <Text key={index} color={color}>{token}</Text>;
      })}
    </Text>
  );
}


export function MarkdownRenderer({ content, theme }: { content: string; theme: Theme }) {
  const rows: React.ReactNode[] = [];
  const lines = content.split(/\r?\n/);
  let inCode = false;
  let codeRows: string[] = [];
  let codeLang = "";

  const flushCode = () => {
    if (codeRows.length === 0 && !codeLang) return;
    rows.push(
      <Box key={`code-${rows.length}`} flexDirection="column" marginY={1}>
        {codeRows.map((line, index) => (
          <Text key={index}>
            {codeLine(line, theme)}
          </Text>
        ))}
      </Box>
    );
    codeRows = [];
    codeLang = "";
  };

  for (const line of lines) {
    if (line.startsWith("```")) {
      if (inCode) flushCode();
      else codeLang = line.slice(3).trim();
      inCode = !inCode;
      continue;
    }

    if (inCode) {
      codeRows.push(line);
      continue;
    }

    const trimmed = line.trimStart();
    const indentMatch = line.match(/^(\s+)/);
    const indent = indentMatch ? indentMatch[1].length : 0;

    if (!trimmed) {
      rows.push(<Box key={rows.length} height={1} />);
      continue;
    }

    const headerMatch = trimmed.match(/^(#{1,6})\s+(.*)$/);
    if (headerMatch) {
      const level = headerMatch[1].length;
      const text = headerMatch[2];
      rows.push(
        <Box key={rows.length} marginY={level <= 2 ? 1 : 0} marginLeft={indent}>
          <Text bold color={level <= 2 ? theme.accent : theme.text} underline={level === 1}>
            {level === 1 ? text.toUpperCase() : text}
          </Text>
        </Box>
      );
      continue;
    }

    if (/^(---|\*\*\*|___)$/.test(trimmed)) {
      rows.push(
        <Box key={rows.length} marginY={1}>
          <Text color={theme.border}>{"-".repeat(50)}</Text>
        </Box>
      );
      continue;
    }

    if (trimmed.startsWith("- ") || trimmed.startsWith("* ")) {
      rows.push(
        <Box key={rows.length} marginLeft={indent}>
          <Text color={theme.accent}>- </Text>
          {inline(trimmed.slice(2), theme)}
        </Box>
      );
      continue;
    }

    const orderedMatch = trimmed.match(/^(\d+)\.\s+(.*)$/);
    if (orderedMatch) {
      rows.push(
        <Box key={rows.length} marginLeft={indent}>
          <Text color={theme.accent}>{orderedMatch[1]}. </Text>
          {inline(orderedMatch[2], theme)}
        </Box>
      );
      continue;
    }

    if (trimmed.startsWith(">")) {
      rows.push(
        <Box key={rows.length} marginLeft={indent} paddingLeft={1}>
          <Text color={theme.muted}>| </Text>
          {inline(trimmed.slice(1).trimStart(), theme)}
        </Box>
      );
      continue;
    }

    rows.push(<Box key={rows.length} marginLeft={indent}>{inline(trimmed, theme)}</Box>);
  }

  if (inCode) flushCode();
  return <Box flexDirection="column">{rows}</Box>;
}
