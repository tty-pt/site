import assert from "node:assert";
import { mkdir, mkdtemp, readFile, writeFile } from "node:fs/promises";
import { existsSync } from "node:fs";
import { join } from "node:path";
import { tmpdir } from "node:os";
import { tryLog } from "../src/logging.ts";
import { pinQuestLog, resetPinnedQuestLogs } from "../src/logging/paths.ts";

const QID = "logsafetest";

interface Harness {
  ctx: {
    cwd: string;
    sessionManager: object;
    hasUI: boolean;
    ui: { notify: (m: string) => void };
  };
  ui: { notified: boolean; last?: string };
  logPath: string;
  read: () => Promise<string>;
  cleanup: () => Promise<void>;
}

async function harness(opts?: { blockDir?: boolean }): Promise<Harness> {
  const root = await mkdtemp(join(tmpdir(), "pi-logsafe-"));
  const questDir = join(root, ".pi/quest/current", QID);
  if (opts?.blockDir) {
    // A FILE at the quest-dir path forces mkdir-recursive + write to fail.
    await mkdir(join(questDir, ".."), { recursive: true });
    await writeFile(questDir, "block", "utf8");
  }
  const logPath = join(
    questDir,
    opts?.blockDir ? "nested/execution.log" : "execution.log",
  );
  // Pin so getQuestLogPath returns our synthetic path deterministically.
  pinQuestLog(QID, logPath);
  const ui = {
    notified: false,
    last: undefined as string | undefined,
    notify: (m: string) => {
      ui.notified = true;
      ui.last = m;
    },
  };
  const ctx = { cwd: root, sessionManager: {}, hasUI: true, ui };
  const cleanup = async () => {
    resetPinnedQuestLogs();
    await import("node:fs/promises").then(async ({ rm }) => {
      await rm(root, { recursive: true, force: true });
    });
  };
  return {
    ctx,
    ui,
    logPath,
    read:
      async () => (existsSync(logPath) ? await readFile(logPath, "utf8") : ""),
    cleanup,
  };
}

Deno.test("logging_safe: tryLog writes the event on success", async () => {
  const h = await harness();
  try {
    tryLog(
      "TURN_START",
      "safe turn start",
      { quest: "test", questId: QID },
      h.ctx,
    );
    const content = await h.read();
    assert.ok(content.includes("TURN_START"), "event type should be written");
    assert.ok(content.includes("safe turn start"), "message should be written");
  } finally {
    await h.cleanup();
  }
});

Deno.test("logging_safe: tryLog does not throw on success", async () => {
  const h = await harness();
  try {
    assert.doesNotThrow(() =>
      tryLog("TURN_END", "no throw", { quest: "test", questId: QID }, h.ctx)
    );
  } finally {
    await h.cleanup();
  }
});

Deno.test("logging_safe: tryLog never throws when the underlying log write fails", async () => {
  // Block the log destination: a file sits where the quest dir must be, so the
  // recursive mkdir + write must fail. tryLog's contract is that it never throws
  // on a logging failure (non-fatal, surfaced to UI via the existing degradation).
  const h = await harness({ blockDir: true });
  try {
    assert.doesNotThrow(() =>
      tryLog("ERROR", "boom", { quest: "test", questId: QID }, h.ctx)
    );
  } finally {
    await h.cleanup();
  }
});

Deno.test("logging_safe: tryLog swallows repeated write failures without throwing", async () => {
  const h = await harness({ blockDir: true });
  try {
    assert.doesNotThrow(() =>
      tryLog("ERROR", "boom", { quest: "test", questId: QID }, h.ctx)
    );
    assert.doesNotThrow(() =>
      tryLog("ERROR", "boom2", { quest: "test", questId: QID }, h.ctx)
    );
  } finally {
    await h.cleanup();
  }
});
