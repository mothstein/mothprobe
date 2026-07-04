#!/usr/bin/env node

const VERSION = '0.1.0-alpha';

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

const args = process.argv.slice(2);

if (args.includes('-h') || args.includes('--help')) {
  printHelp();
  process.exit(0);
}

if (args.includes('-v') || args.includes('--version')) {
  console.log(VERSION);
  process.exit(0);
}

if (args.length === 0 && !process.stdin.isTTY) {
  printHelp();
  process.exit(0);
}

await import('./main.js');
