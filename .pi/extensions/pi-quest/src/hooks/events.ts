// HIGH_LEVEL: #surviving — pi events feed reduce(); handlers never throw.
// Structural subset of pi's ExtensionAPI (only what slices use).
// Never import pi packages: no new dependency, no version coupling.
export interface TranscriptEntry {
  customType?: string;
  data?: unknown;
}

export interface PiSessionManager {
  getEntries(): TranscriptEntry[];
}

export interface PiEventBus {
  on(event: string, handler: (data: unknown) => void): () => void;
  emit(event: string, data: unknown): void;
}

export interface PiDialogOptions {
  signal?: AbortSignal;
  timeout?: number;
}

export interface PiUI {
  select(title: string, options: string[], opts?: PiDialogOptions): Promise<string | undefined>;
  input(title: string, placeholder?: string, opts?: PiDialogOptions): Promise<string | undefined>;
  confirm(title: string, message: string, opts?: PiDialogOptions): Promise<boolean>;
  notify(message: string, type?: "info" | "warning" | "error"): void;
  setStatus(key: string, text: string | undefined): void;
  setWidget(key: string, content: string[] | undefined): void;
}

export interface PiToolInfo {
  name: string;
}

export interface PiCtx {
  cwd: string;
  hasUI: boolean;
  mode: string;
  sessionManager: PiSessionManager;
  ui: PiUI;
  childSessionId?: unknown;
  parentSessionId?: unknown;
}

export interface SessionStartEvent {
  type: "session_start";
  reason: string;
}

export interface TurnEvent {
  type: string;
  turnIndex: number;
}

export interface BeforeCompactEvent {
  type: "session_before_compact";
}

export interface ToolCallEvent {
  type: "tool_call";
  toolCallId: string;
  toolName: string;
  input: Record<string, unknown>;
}

export interface ToolResultEvent {
  type: "tool_result";
  toolCallId: string;
  toolName: string;
  input: Record<string, unknown>;
  content: Array<{ type: string; text?: string }>;
  isError: boolean;
  details?: unknown;
}

export interface InputEvent {
  type: "input";
  text: string;
}

export interface BeforeAgentStartEvent {
  type: "before_agent_start";
  prompt: string;
}

export interface BlockVerdict {
  block?: boolean;
  reason?: string;
}

export interface AgentStartInjection {
  message?: {
    customType: string;
    content: unknown;
    display?: unknown;
    details?: unknown;
  };
}

export interface AgentToolResult {
  content: Array<{ type: string; text?: string }>;
  details?: unknown;
}

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
  ) => Promise<AgentToolResult>;
}

export interface PiCommandOptions {
  description?: string;
  getArgumentCompletions?: (prefix: string) => Array<{ value: string; label: string }> | Promise<Array<{ value: string; label: string }>>;
  handler: (args: string, ctx: PiCtx) => Promise<void> | void;
}

export interface PiExecResult {
  stdout: string;
  stderr: string;
  code: number;
}

export interface Pi {
  on(event: "session_start", handler: (e: SessionStartEvent, ctx: PiCtx) => unknown): void;
  on(event: "turn_start" | "turn_end", handler: (e: TurnEvent, ctx: PiCtx) => unknown): void;
  on(event: "session_before_compact", handler: (e: BeforeCompactEvent, ctx: PiCtx) => unknown): void;
  on(event: "tool_call", handler: (e: ToolCallEvent, ctx: PiCtx) => BlockVerdict | undefined): void;
  on(event: "tool_result", handler: (e: ToolResultEvent, ctx: PiCtx) => unknown): void;
  on(event: "input", handler: (e: InputEvent, ctx: PiCtx) => unknown): void;
  on(event: "before_agent_start", handler: (e: BeforeAgentStartEvent, ctx: PiCtx) => AgentStartInjection | undefined): void;
  appendEntry(customType: string, data: unknown): void;
  registerTool(tool: PiToolSpec): void;
  registerCommand(name: string, options: PiCommandOptions): void;
  sendMessage(
    message: { customType: string; content: unknown; display?: unknown; details?: unknown },
    options?: { triggerTurn?: boolean; deliverAs?: "steer" | "followUp" | "nextTurn" },
  ): void;
  getAllTools(): PiToolInfo[];
  exec(command: string, args: string[], options?: { cwd?: string }): Promise<PiExecResult>;
  events: PiEventBus;
}

export function onSessionStart(pi: Pi, handler: (e: SessionStartEvent, ctx: PiCtx) => void): void {
  pi.on("session_start", (event, ctx) => {
    handler(event, ctx);
  });
}

export function onTurnStart(pi: Pi, handler: (e: TurnEvent, ctx: PiCtx) => void): void {
  pi.on("turn_start", (event, ctx) => {
    handler(event, ctx);
  });
}

export function onTurnEnd(pi: Pi, handler: (e: TurnEvent, ctx: PiCtx) => void): void {
  pi.on("turn_end", (event, ctx) => {
    handler(event, ctx);
  });
}

export function onBeforeCompact(pi: Pi, handler: (e: BeforeCompactEvent, ctx: PiCtx) => void): void {
  pi.on("session_before_compact", (event, ctx) => {
    handler(event, ctx);
  });
}

export function onToolCall(pi: Pi, handler: (e: ToolCallEvent, ctx: PiCtx) => BlockVerdict | undefined): void {
  pi.on("tool_call", (event, ctx) => handler(event, ctx));
}

export function onToolResult(pi: Pi, handler: (e: ToolResultEvent, ctx: PiCtx) => void): void {
  pi.on("tool_result", (event, ctx) => {
    handler(event, ctx);
  });
}

export function onUserMessage(pi: Pi, handler: (text: string, ctx: PiCtx) => void): void {
  pi.on("input", (event, ctx) => {
    handler(event.text, ctx);
  });
}

export function onBeforeAgentStart(
  pi: Pi,
  handler: (e: BeforeAgentStartEvent, ctx: PiCtx) => AgentStartInjection | undefined,
): void {
  pi.on("before_agent_start", (event, ctx) => handler(event, ctx));
}

export function toolNames(pi: Pi): string[] {
  try {
    const tools = pi.getAllTools();
    if (Array.isArray(tools)) return tools.map((t) => t.name);
  } catch {
    // Capability detection is best-effort; absence degrades, never crashes.
  }
  return [];
}
