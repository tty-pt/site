import type { Pi, PiCtx, PiToolSpec, TranscriptEntry } from "../src/hooks/events.ts";

export interface SentMessage {
  message: { customType: string; content: unknown };
  options?: { triggerTurn?: boolean; deliverAs?: "steer" | "followUp" | "nextTurn" };
}

export interface FakePi extends Pi {
  sent: SentMessage[];
  appended: Array<{ customType: string; data: unknown }>;
  tools: PiToolSpec[];
  commands: Array<{ name: string }>;
  execCalls: Array<{ command: string; args: string[] }>;
  execCode: number;
  toolNames: string[];
  subscriptions: string[];
}

export function fakePi(): FakePi {
  const sent: SentMessage[] = [];
  const appended: Array<{ customType: string; data: unknown }> = [];
  const tools: PiToolSpec[] = [];
  const commands: Array<{ name: string }> = [];
  const execCalls: Array<{ command: string; args: string[] }> = [];
  const subscriptions: string[] = [];
  const fake = {
    sent,
    appended,
    tools,
    commands,
    execCalls,
    subscriptions,
    execCode: 0,
    toolNames: [] as string[],
    on(event: string): void {
      subscriptions.push(event);
    },
    appendEntry(customType: string, data: unknown): void {
      appended.push({ customType, data });
    },
    registerTool(tool: PiToolSpec): void {
      tools.push(tool);
    },
    registerCommand(name: string): void {
      commands.push({ name });
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

export function fakeCtx(cwd: string, entries: TranscriptEntry[] = [], ui?: Partial<PiCtx["ui"]>): PiCtx {
  return {
    cwd,
    hasUI: false,
    mode: "test",
    sessionManager: { getEntries: () => entries },
    ui: {
      select: async () => undefined,
      input: async () => undefined,
      confirm: async () => false,
      notify: () => {},
      setStatus: () => {},
      setWidget: () => {},
      ...ui,
    },
  };
}
