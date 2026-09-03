import assert from "node:assert";
import { mkdtemp, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { getPeriodicLogTail } from "../src/compaction/execution.ts";
import { pinQuestLog, resetPinnedQuestLogs } from "../src/logging/paths.ts";
import { state } from "../src/state.ts";
import { buildPeriodicCheckpointPrompt } from "../src/compaction/checkpoint.ts";

const ORIG = { questId: state.questId, active: state.active };

async function withQuest(t: () => Promise<void>) {
  state.questId = "log-tail-test-quest";
  state.active = "log-tail-test-quest";
  const dir = await mkdtemp(join(tmpdir(), "pi-quest-logtail-"));
  try {
    await t();
  } finally {
    state.questId = ORIG.questId;
    state.active = ORIG.active;
    resetPinnedQuestLogs();
    await rm(dir, { recursive: true, force: true });
  }
}

Deno.test(
  "getPeriodicLogTail returns last 10 non-empty lines and truncates to 1200",
  async () => {
    await withQuest(async () => {
      const logPath = join(
        await mkdtemp(join(tmpdir(), "pi-quest-logtail-")),
        "execution.log",
      );
      const lines = Array.from(
        { length: 25 },
        (_, i) => `[T] line ${i} of the quest execution log`,
      );
      await writeFile(logPath, lines.join("\n"), "utf8");
      pinQuestLog("log-tail-test-quest", logPath);

      const tail = getPeriodicLogTail();
      assert.ok(tail, "should return a tail");
      const tailLines = tail!.split(/\r?\n/).filter((l) => l.trim().length > 0);
      assert.ok(tail!.length <= 1200, `tail ${tail!.length} should be <= 1200`);
      if (tailLines.length === 10) {
        assert.match(
          tailLines[0],
          /line 15/,
          "first returned line should be the 16th source line (15 lines dropped)",
        );
      }
      await rm(logPath, { recursive: true, force: true });
    });
  },
);

Deno.test(
  "getPeriodicLogTail returns undefined when no quest is active",
  async () => {
    const origQ = state.questId;
    const origA = state.active;
    try {
      state.questId = null;
      state.active = null;
      assert.strictEqual(
        getPeriodicLogTail(),
        undefined,
        "no active quest should yield undefined",
      );
    } finally {
      state.questId = origQ;
      state.active = origA;
    }
  },
);

Deno.test(
  "getPeriodicLogTail returns undefined when log is missing or empty",
  async () => {
    await withQuest(async () => {
      const logPath = join(
        await mkdtemp(join(tmpdir(), "pi-quest-logtail-")),
        "execution.log",
      );
      pinQuestLog("log-tail-test-quest", logPath);
      assert.strictEqual(
        getPeriodicLogTail(),
        undefined,
        "missing log should yield undefined",
      );
      await writeFile(logPath, "\n   \n", "utf8");
      assert.strictEqual(
        getPeriodicLogTail(),
        undefined,
        "whitespace-only log should yield undefined",
      );
      await rm(logPath, { recursive: true, force: true });
    });
  },
);

Deno.test(
  "buildPeriodicCheckpointPrompt includes logTail block and truncates to 1200",
  () => {
    state.questId = "prompt-test";
    state.active = "prompt-test";
    try {
      const longTail = Array.from(
        { length: 100 },
        (_, i) =>
          `[T] verbose execution log line ${i} padded to extend the tail well beyond the 1200 character cap`,
      ).join("\n");
      const withTail = buildPeriodicCheckpointPrompt("prompt-test", {
        turnsSinceCheckpoint: 6,
        logTail: longTail,
      });
      assert.match(
        withTail,
        /Recent execution log tail \(last 10\)/,
        "should include log tail block",
      );
      const block = withTail.match(/\n```\n([\s\S]*?)\n```/);
      assert.ok(block, "should contain a fenced code block with the tail");
      assert.ok(
        (block![1]?.length || 0) <= 1200,
        `tail block should be <= 1200, got ${block![1]?.length}`,
      );
      assert.match(
        withTail,
        /Periodic Durable Checkpoint/,
        "should still be a checkpoint prompt",
      );
    } finally {
      state.questId = ORIG.questId;
      state.active = ORIG.active;
    }
  },
);

Deno.test(
  "buildPeriodicCheckpointPrompt omits logTail block when none provided",
  () => {
    state.questId = "prompt-test-nolog";
    state.active = "prompt-test-nolog";
    try {
      const noTail = buildPeriodicCheckpointPrompt("prompt-test-nolog", {
        turnsSinceCheckpoint: 6,
      });
      assert.ok(
        !noTail.includes("Recent execution log tail"),
        "no block when logTail omitted",
      );
    } finally {
      state.questId = ORIG.questId;
      state.active = ORIG.active;
    }
  },
);
