export interface PiToolSpec {
  name: string;
  label: string;
  description: string;
  parameters: unknown;
  execute: (
    toolCallId: string,
    params: Record<string, unknown>,
    signal: AbortSignal | undefined,
    onUpdate: unknown,
    ctx: PiCtx,
  ) => Promise<PiAgentToolResult>;
}

export interface PiAgentToolResult {
  content: Array<{ type: string; text?: string }>;
  details?: unknown;
}

export interface PiExecResult {
  stdout: string;
  stderr: string;
  code: number;
}

export interface PiCtx {
  cwd: string;
  hasUI: boolean;
  mode: string;
  ui?: unknown;
}

export interface Pi {
  on(event: string, handler: (...args: unknown[]) => unknown): void;
  registerTool(tool: PiToolSpec): void;
  exec(
    command: string,
    args: string[],
    options?: { cwd?: string; env?: Record<string, string> },
  ): Promise<PiExecResult>;
}