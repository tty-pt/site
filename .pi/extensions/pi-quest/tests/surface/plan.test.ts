import { check } from "../check.ts";
import { mkdirSync, mkdtempSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { getState, replaceState } from "../../src/app/store.ts";
import { createDraft, createQuest, IDLE_STATE } from "../../src/domain/quest.ts";
import type { Qid } from "../../src/domain/qid.ts";
import { draftPath } from "../../src/domain/paths.ts";
import type { OverlayFactory, OverlayShowOptions, PiCtx, PiOverlayComponent, PiTheme } from "../../src/hooks/events.ts";
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

function withTerminalSize(rows: number, cols: number, run: () => Promise<void>): Promise<void> {
  const stdout = process.stdout as unknown as { rows?: number; columns?: number };
  const prevRows = Object.getOwnPropertyDescriptor(process.stdout, "rows");
  const prevCols = Object.getOwnPropertyDescriptor(process.stdout, "columns");
  Object.defineProperties(process.stdout, {
    rows: { value: rows, configurable: true },
    columns: { value: cols, configurable: true },
  });
  return (async () => {
    try {
      await run();
    } finally {
      if (prevRows) Object.defineProperty(process.stdout, "rows", prevRows);
      else delete stdout.rows;
      if (prevCols) Object.defineProperty(process.stdout, "columns", prevCols);
      else delete stdout.columns;
    }
  })();
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

Deno.test("tui viewers open a full-size, top-anchored overlay", async () => {
  const cwd = draftingCwd();
  let captured: { factory: OverlayFactory<void>; overlay: boolean; overlayOptions: unknown } | null = null;
  await withTerminalSize(40, 200, async () => {
    const ctx: PiCtx = fakeCtx(cwd, [], {
      notify: () => {},
      custom: <T>(factory: OverlayFactory<T>, options?: OverlayShowOptions): Promise<T> => {
        captured = {
          factory: factory as OverlayFactory<void>,
          overlay: options?.overlay === true,
          overlayOptions: options?.overlayOptions,
        };
        return Promise.resolve(undefined as unknown as T);
      },
    });
    await viewActivePlan({ ...ctx, mode: "tui" });
  });
  check(captured !== null, "overlay opened");
  const capturedView = captured as unknown as {
    overlay: boolean;
    overlayOptions: { width: number; maxHeight: number; anchor: string; offsetX: number; offsetY: number };
  };
  check(capturedView.overlay, "floating overlay requested");
  check(capturedView.overlayOptions.width === 200 - 2, "full-screen box minus its two border cells");
  check(capturedView.overlayOptions.maxHeight === 40 - 2, "full-screen height minus two rows");
  check(capturedView.overlayOptions.anchor === "top-left", "pinned to the top edge so height changes never move it");
  check(capturedView.overlayOptions.offsetX === 1 && capturedView.overlayOptions.offsetY === 1, "one-cell margin on top and left");
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

Deno.test("plan floater never tops out below its own chrome floor", async () => {
  const cwd = draftingCwd();
  let overlayOptions: { width?: number; maxHeight?: number; anchor?: string } | undefined;
  const ctx: PiCtx = fakeCtx(cwd, [], {
    notify: () => {},
    custom: <T>(_factory: OverlayFactory<T>, options?: OverlayShowOptions): Promise<T> => {
      overlayOptions = options?.overlayOptions as { width?: number; maxHeight?: number; anchor?: string };
      return Promise.resolve(undefined as unknown as T);
    },
  });
  await withTerminalSize(6, 30, async () => {
    await viewActivePlan({ ...ctx, mode: "tui" });
  });
  check(overlayOptions?.maxHeight === 8, "maxHeight stays above the 7-row chrome frame");
  check(overlayOptions?.anchor === "top-left", "anchored to the top edge");
  replaceState(IDLE_STATE);
});

Deno.test("ctrl+q via onTerminalInput opens the plan viewer", async () => {
  const cwd = draftingCwd();
  let opened = false;
  const ctx = fakeCtx(cwd, [], {
    notify: () => {},
    custom: <T>(): Promise<T> => {
      opened = true;
      return Promise.resolve(undefined as unknown as T);
    },
  });
  ctx.mode = "tui";
  const { default: install } = await import("../../src/index.ts");
  const pi = fakePi();
  install(pi);
  // Fire the session_start handler to register onTerminalInput.
  const handlers = pi.eventHandlers["session_start"] ?? [];
  check(handlers.length >= 1, "session_start handler registered");
  for (const h of handlers) h({ type: "session_start", reason: "test" }, ctx);
  // The onTerminalInput handler should now be registered.
  check(ctx.inputHandlers.length >= 1, "onTerminalInput registered");
  const handler = ctx.inputHandlers[ctx.inputHandlers.length - 1];
  const result = handler("\x11"); // raw ctrl+Q
  check(result?.consume === true, "ctrl+Q consumed");
  await new Promise((r) => setTimeout(r, 50)); // let async viewActivePlan settle (file I/O)
  check(opened, "viewer opened on ctrl+Q");
  // Non-ctrl+Q passes through.
  opened = false;
  const pass = handler("j");
  check(pass === undefined, "other keys pass through");
  check(!opened, "viewer not opened for other keys");
  replaceState(IDLE_STATE);
});
