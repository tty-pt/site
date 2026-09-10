import type { Pi, PiCtx, PiToolSpec, TranscriptEntry } from "../src/hooks/events.ts";

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
  eventHandlers: Record<string, Array<(event: unknown, ctx: PiCtx) => unknown>>;
}

export function fakePi(): FakePi {
  const sent: SentMessage[] = [];
  const appended: Array<{ customType: string; data: unknown }> = [];
  const tools: PiToolSpec[] = [];
  const commands: CommandReg[] = [];
  const shortcuts: ShortcutReg[] = [];
  const execCalls: Array<{ command: string; args: string[] }> = [];
  const subscriptions: string[] = [];
  const eventHandlers: Record<string, Array<(event: unknown, ctx: PiCtx) => unknown>> = {};
  const fake = {
    sent,
    appended,
    tools,
    commands,
    shortcuts,
    execCalls,
    subscriptions,
    eventHandlers,
    execCode: 0,
    toolNames: [] as string[],
    on(event: string, handler: (event: unknown, ctx: PiCtx) => unknown): void {
      subscriptions.push(event);
      if (!eventHandlers[event]) eventHandlers[event] = [];
      eventHandlers[event].push(handler);
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

export type TerminalInputHandler = (data: string) => { consume?: boolean; data?: string } | undefined;

export function fakeCtx(cwd: string, entries: TranscriptEntry[] = [], ui?: Partial<PiCtx["ui"]>): PiCtx & { notifications: FakeNotifications; inputHandlers: TerminalInputHandler[] } {
  const notifications: FakeNotifications = { calls: [] };
  const inputHandlers: TerminalInputHandler[] = [];
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
      onTerminalInput: (handler: TerminalInputHandler) => { inputHandlers.push(handler); return () => {}; },
      ...ui,
    },
    inputHandlers,
  };
}

// A Pi with neither a reviewer tool nor an event bridge: the structural "no
// reviewer at all" case that must fall back to a live human.
export function barePi(): FakePi {
  const pi = fakePi();
  (pi as { events: unknown }).events = undefined;
  return pi;
}
