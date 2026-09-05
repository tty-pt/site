import { check } from "../check.ts";
import { mkdirSync, mkdtempSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { getState, replaceState } from "../../src/app/store.ts";
import { createDraft, createQuest, IDLE_STATE } from "../../src/domain/quest.ts";
import type { Qid } from "../../src/domain/qid.ts";
import { draftPath } from "../../src/domain/paths.ts";
import type { OverlayFactory, PiCtx, PiOverlayComponent, PiTheme } from "../../src/hooks/events.ts";
import { installCommands } from "../../src/surface/commands/index.ts";
import { viewActivePlan } from "../../src/surface/commands/plan.ts";
import { fakeCtx, fakePi } from "../fake-pi.ts";

const QID = "abc123" as Qid;
const DRAFT = "## Requirements\n- first\n\n## Evidence\n- found it\n\n## Implementation Plan\nDo step one.\nThen step two.\n";

function draftingCwd(): string {
  const cwd = mkdtempSync(join(tmpdir(), "pi-quest-plan-"));
  mkdirSync(join(cwd, ".pi", "quest", "future"), { recursive: true });
  writeFileSync(join(cwd, draftPath(QID)), DRAFT);
  replaceState(createDraft(createQuest("work", QID), "work"));
  return cwd;
}

Deno.test("plan viewer reports when no quest is active", async () => {
  replaceState(IDLE_STATE);
  const notes: string[] = [];
  await viewActivePlan(fakeCtx("/tmp", [], { notify: (m: string) => notes.push(m) }));
  check(notes.length === 1 && notes[0].includes("No active quest"), "oriented");
});

Deno.test("plan viewer reports a missing draft file", async () => {
  const cwd = mkdtempSync(join(tmpdir(), "pi-quest-plan-"));
  replaceState(createDraft(createQuest("work", QID), "work"));
  const notes: string[] = [];
  await viewActivePlan(fakeCtx(cwd, [], { notify: (m: string) => notes.push(m) }));
  check(notes.length === 1 && notes[0].includes(QID), "qid named");
  replaceState(IDLE_STATE);
});

Deno.test("non-tui viewers fall back to a capped toast", async () => {
  const cwd = draftingCwd();
  const notes: string[] = [];
  const pi = fakePi();
  await viewActivePlan(fakeCtx(cwd, [], { notify: (m: string) => notes.push(m) }));
  check(notes.length === 1, "one toast");
  check(notes[0].includes("Do step one."), "plan excerpt shown");
  check(notes[0].includes(draftPath(QID)), "file pointed at");
  check(pi.sent.length === 0, "zero inference: no model message");
  replaceState(IDLE_STATE);
});

Deno.test("tui viewers open the overlay with the plan lines", async () => {
  const cwd = draftingCwd();
  let captured: { factory: OverlayFactory<void>; overlay: boolean } | null = null;
  const ctx: PiCtx = fakeCtx(cwd, [], {
    notify: () => {},
    custom: <T>(factory: OverlayFactory<T>, options?: { overlay?: boolean }): Promise<T> => {
      captured = { factory: factory as OverlayFactory<void>, overlay: options?.overlay === true };
      return Promise.resolve(undefined as unknown as T);
    },
  });
  await viewActivePlan({ ...ctx, mode: "tui" });
  check(captured !== null, "overlay opened");
  check((captured as unknown as { overlay: boolean }).overlay, "floating overlay requested");
  const theme: PiTheme = { fg: (_c: string, s: string) => s };
  let closed = false;
  const produced: PiOverlayComponent | Promise<PiOverlayComponent> = (captured as unknown as {
    factory: OverlayFactory<void>;
  }).factory({}, theme, {}, () => {
    closed = true;
  });
  const view = produced instanceof Promise ? await produced : produced;
  const out = view.render(60).join("\n");
  check(out.includes("Do step one.") && out.includes("Then step two."), "plan body rendered");
  check(out.includes("## Implementation Plan"), "full draft markdown, not just the section");
  view.handleInput?.("\x1b");
  check(closed, "escape closes");
  replaceState(IDLE_STATE);
});

Deno.test("f2 shortcut opens the plan viewer", async () => {
  const cwd = draftingCwd();
  const pi = fakePi();
  installCommands(pi);
  const entry = pi.shortcuts.find((s) => s.shortcut === "f2");
  check(entry !== undefined, "f2 registered");
  if (entry === undefined) throw new Error("f2 shortcut missing");
  let opened = false;
  const ctx: PiCtx = fakeCtx(cwd, [], {
    notify: () => {},
    custom: <T>(): Promise<T> => {
      opened = true;
      return Promise.resolve(undefined as unknown as T);
    },
  });
  await entry.options.handler({ ...ctx, mode: "tui" });
  check(opened, "shortcut opens the viewer");
  replaceState(IDLE_STATE);
});
