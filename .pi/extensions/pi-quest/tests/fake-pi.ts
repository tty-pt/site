import type { Pi, PiCtx, PiToolSpec, TranscriptEntry, AgentToolResult } from "../src/hooks/events.ts";

export interface SentMessage {
  message: { customType: string; content: unknown };
  options?: { triggerTurn?: boolean; deliverAs?: "steer" | "followUp" | "nextTurn" };
}

export interface ShortcutReg {
  shortcut: string;
  options: { description?: string; handler: (ctx: PiCtx) => Promise<void> | void };
}

export interface CommandReg {
  name: string;
  options: { description?: string; handler: (args: string, ctx: PiCtx) => Promise<void> | void };
}

export interface FakePi extends Pi {
  sent: SentMessage[];
  appended: Array<{ customType: string; data: unknown }>;
  tools: PiToolSpec[];
  commands: CommandReg[];
  shortcuts: ShortcutReg[];
  execCalls: Array<{ command: string; args: string[] }>;
  execCode: number;
  toolNames: string[];
  subscriptions: string[];
  executeToolCalls: Array<{ name: string; params: Record<string, unknown>; signal?: AbortSignal }>;
  executeToolHandler: ((name: string, params: Record<string, unknown>, signal?: AbortSignal) => Promise<AgentToolResult>) | null;
}

export function fakePi(): FakePi {
  const sent: SentMessage[] = [];
  const appended: Array<{ customType: string; data: unknown }> = [];
  const tools: PiToolSpec[] = [];
  const commands: CommandReg[] = [];
  const shortcuts: ShortcutReg[] = [];
  const execCalls: Array<{ command: string; args: string[] }> = [];
  const subscriptions: string[] = [];
  const fake = {
    sent,
    appended,
    tools,
    commands,
    shortcuts,
    execCalls,
    subscriptions,
    execCode: 0,
    toolNames: [] as string[],
    executeToolCalls: [] as Array<{ name: string; params: Record<string, unknown>; signal?: AbortSignal }>,
    executeToolHandler: null as ((name: string, params: Record<string, unknown>, signal?: AbortSignal) => Promise<AgentToolResult>) | null,
    on(event: string): void {
      subscriptions.push(event);
    },
    appendEntry(customType: string, data: unknown): void {
      appended.push({ customType, data });
    },
    registerTool(tool: PiToolSpec): void {
      tools.push(tool);
    },
    registerCommand(name: string, options: CommandReg["options"]): void {
      commands.push({ name, options });
    },
    registerShortcut(shortcut: string, options: ShortcutReg["options"]): void {
      shortcuts.push({ shortcut, options });
    },
    sendMessage(message: SentMessage["message"], options?: SentMessage["options"]): void {
      sent.push({ message, options });
    },
    getAllTools(): Array<{ name: string }> {
      return (fake as FakePi).toolNames.map((name) => ({ name }));
    },
    exec(command: string, args: string[]): Promise<{ stdout: string; stderr: string; code: number }> {
      execCalls.push({ command, args });
      return Promise.resolve({ stdout: "", stderr: "", code: (fake as FakePi).execCode });
    },
    executeTool(name: string, params: Record<string, unknown>, signal?: AbortSignal): Promise<AgentToolResult> {
      (fake as FakePi).executeToolCalls.push({ name, params, signal });
      if ((fake as FakePi).executeToolHandler) {
        return (fake as FakePi).executeToolHandler!(name, params, signal);
      }
      return Promise.resolve({ content: [{ type: "text", text: "mock answer" }] });
    },
    events: {
      on(): () => void {
        return () => {};
      },
      emit(): void {},
    },
  };
  return fake as unknown as FakePi;
}

export interface FakeNotifications {
  calls: Array<{ message: string; type?: string }>;
}

export function fakeCtx(cwd: string, entries: TranscriptEntry[] = [], ui?: Partial<PiCtx["ui"]>): PiCtx & { notifications: FakeNotifications } {
  const notifications: FakeNotifications = { calls: [] };
  return {
    cwd,
    hasUI: false,
    mode: "test",
    notifications,
    sessionManager: { getEntries: () => entries },
    ui: {
      select: async () => undefined,
      input: async () => undefined,
      confirm: async () => false,
      notify: (message: string, type?: string) => { notifications.calls.push({ message, type }); },
      setStatus: () => {},
      setWidget: () => {},
      ...ui,
    },
  };
}

// A Pi with neither a reviewer tool nor an event bridge: the structural "no
// reviewer at all" case that must fall back to a live human.
export function barePi(): FakePi {
  const pi = fakePi();
  (pi as { events: unknown }).events = undefined;
  return pi;
}
