export type InputKind = "chat" | "command" | "shell" | "file";

export interface ParsedInput {
  kind: InputKind;
  payload: string;
  raw: string;
}

export interface ChatMessage {
  role: "user" | "system" | "shell" | "mcp";
  text: string;
}

export interface JsonRpcRequest {
  jsonrpc: "2.0";
  id?: number;
  method: string;
  params?: unknown;
}

export interface JsonRpcResponse {
  jsonrpc?: "2.0";
  id?: number;
  result?: unknown;
  error?: {
    code: number;
    message: string;
    data?: unknown;
  };
}
