import assert from "node:assert";
import { mkdir, mkdtemp, readFile, rm, writeFile } from "node:fs/promises";
import { existsSync, readdirSync } from "node:fs";
import { join, resolve } from "node:path";
import { tmpdir } from "node:os";
import {
  findProjectRoot,
  generateRunManifest,
  resolveActiveRunHierarchy,
} from "../src/diagnostic.ts";
import { QUEST_CURRENT_DIR } from "../src/constants.ts";
import { resetPinnedQuestLogs } from "../src/logging/paths.ts";
import { state } from "../src/state.ts";

Deno.test("diagnostic_archive_cluster: futureCount disk-authoritative + future-archive", async (t) => {
  const tempRoot = await mkdtemp(join(tmpdir(), "pi-diag-cluster-future-"));
  try {
    const futureDir = join(tempRoot, ".pi/quest/future");
    const currentDir = join(tempRoot, ".pi/quest/current");
    const archiveDir = join(tempRoot, ".pi/quest/archive");
    await mkdir(futureDir, { recursive: true });
    await mkdir(currentDir, { recursive: true });
    await mkdir(archiveDir, { recursive: true });

    // Create 3 future drafts on disk
    await writeFile(join(futureDir, "draft-a.md"), "# draft a\n", "utf8");
    await writeFile(join(futureDir, "draft-b.md"), "# draft b\n", "utf8");
    await writeFile(join(futureDir, "draft-c.md"), "# draft c\n", "utf8");

    // Create a quest with future-archive containing 2 more
    const qid = "a1b2c3d400";
    const qDir = join(currentDir, qid);
    await mkdir(join(qDir, "future-archive"), { recursive: true });
    await writeFile(
      join(qDir, "future-archive", "arch-1.md"),
      "# arch 1\n",
      "utf8",
    );
    await writeFile(
      join(qDir, "future-archive", "arch-2.md"),
      "# arch 2\n",
      "utf8",
    );
    await writeFile(
      join(qDir, "quest.md"),
      `# Quest: test-future\n\n## Goal\ntest\n\n<!-- questId: ${qid} -->\n`,
      "utf8",
    );
    await writeFile(
      join(qDir, "execution.log"),
      `2026-08-31T01:00:00.000Z | QUEST_CREATED | questId=${qid} quest=test-future | start\n`,
      "utf8",
    );

    // Ensure state has draftPrompts shadow ([] should not suppress disk count)
    const origActiveDraft = state.activeDraft;
    const origDraftPrompts = state.draftPrompts;
    const origQuestId = state.questId;
    state.activeDraft = null;
    state.draftPrompts = [];
    state.questId = qid;

    const hierarchy = await resolveActiveRunHierarchy(tempRoot, {
      questId: qid,
    });
    assert.strictEqual(
      hierarchy.futureCount,
      5,
      `futureCount must be 3+2=5 (disk authoritative), got ${hierarchy.futureCount}`,
    );

    // Restore
    state.activeDraft = origActiveDraft;
    state.draftPrompts = origDraftPrompts;
    state.questId = origQuestId;
  } finally {
    await rm(tempRoot, { recursive: true, force: true });
    resetPinnedQuestLogs();
  }
});

Deno.test("diagnostic_archive_cluster: manifest 9 fields even on cold start", async (t) => {
  const tempRoot = await mkdtemp(join(tmpdir(), "pi-diag-cluster-manifest-"));
  try {
    const currentDir = join(tempRoot, ".pi/quest/current");
    await mkdir(currentDir, { recursive: true });
    await mkdir(join(tempRoot, ".pi/quest/archive"), { recursive: true });
    const qid = "b2c3d4e500";
    const qDir = join(currentDir, qid);
    await mkdir(qDir, { recursive: true });
    await writeFile(
      join(qDir, "quest.md"),
      `# Quest: cold-manifest\n\n## Goal\ncold\n\n<!-- questId: ${qid} -->\n`,
      "utf8",
    );
    // No execution.log — cold start

    const origQuestId = state.questId;
    state.questId = qid;
    const hierarchy = await resolveActiveRunHierarchy(tempRoot, {
      questId: qid,
    });
    const manifest = generateRunManifest(hierarchy, undefined, tempRoot, {});
    // Must contain all 9 fields unconditionally
    assert.ok(
      manifest.includes("draftCaptured:"),
      "manifest must contain draftCaptured",
    );
    assert.ok(
      manifest.includes("futureCount:"),
      "manifest must contain futureCount",
    );
    assert.ok(
      manifest.includes("semanticSummaryEnabled:"),
      "manifest must contain semanticSummaryEnabled",
    );
    assert.ok(
      manifest.includes("thoughtLoggingEnabled:"),
      "manifest must contain thoughtLoggingEnabled",
    );
    assert.ok(
      manifest.includes("filteredCount:"),
      "manifest must contain filteredCount even on cold start",
    );
    assert.ok(
      manifest.includes("opencodeSessionId:"),
      "manifest must contain opencodeSessionId even on cold start",
    );
    assert.ok(
      manifest.includes("startMs:"),
      "manifest must contain startMs even on cold start",
    );
    assert.ok(
      manifest.includes("elapsedMaxMs:"),
      "manifest must contain elapsedMaxMs even on cold start",
    );
    // questHash optional, compactionResumeHash optional
    state.questId = origQuestId;
  } finally {
    await rm(tempRoot, { recursive: true, force: true });
    resetPinnedQuestLogs();
  }
});

Deno.test("diagnostic_archive_cluster: compactionResumeHash disk fallback", async (t) => {
  const tempRoot = await mkdtemp(join(tmpdir(), "pi-diag-cluster-comp-"));
  try {
    const currentDir = join(tempRoot, ".pi/quest/current");
    await mkdir(currentDir, { recursive: true });
    await mkdir(join(tempRoot, ".pi/quest/archive"), { recursive: true });
    const qid = "c3d4e5f600";
    const qDir = join(currentDir, qid);
    await mkdir(qDir, { recursive: true });
    await writeFile(
      join(qDir, "quest.md"),
      `# Quest: comp-fallback\n\n## Goal\ncomp\n\n<!-- questId: ${qid} -->\n`,
      "utf8",
    );
    await writeFile(
      join(qDir, "compaction-resume.txt"),
      "compaction-resume-content\n",
      "utf8",
    );

    const origPending = state.pendingResume;
    const origTx = state.activeTransaction;
    const origQid = state.questId;
    state.pendingResume = null;
    state.activeTransaction = null;
    state.questId = qid;

    const hierarchy = await resolveActiveRunHierarchy(tempRoot, {
      questId: qid,
    });
    assert.ok(
      hierarchy.compactionResumeHash,
      "compactionResumeHash must be set from disk fallback when state is empty",
    );

    state.pendingResume = origPending;
    state.activeTransaction = origTx;
    state.questId = origQid;
  } finally {
    await rm(tempRoot, { recursive: true, force: true });
    resetPinnedQuestLogs();
  }
});

Deno.test("diagnostic_archive_cluster: findProjectRoot skips extensions mirror", async () => {
  const realRoot = findProjectRoot();
  // Mirror path under .pi/extensions/pi-quest/.pi/quest/current/...
  const fromMirror = findProjectRoot(
    join(realRoot, ".pi/extensions/pi-quest/.pi/quest/current/abc"),
  );
  assert.strictEqual(
    fromMirror,
    realRoot,
    `findProjectRoot must skip extensions mirror, got ${fromMirror}`,
  );
  const fromExtDeep = findProjectRoot(
    join(realRoot, ".pi/extensions/pi-quest/src/nested"),
  );
  assert.strictEqual(fromExtDeep, realRoot);
});

Deno.test("diagnostic_archive_cluster: 3-dir invariant", async () => {
  const realRoot = findProjectRoot();
  const questRoot = resolve(realRoot, ".pi/quest");
  const entries = readdirSync(questRoot, { withFileTypes: true })
    .filter((e) => e.isDirectory())
    .map((e) => e.name)
    .sort();
  assert.deepStrictEqual(
    entries,
    ["archive", "current", "future"],
    `/.pi/quest must contain exactly 3 dirs, got ${entries.join(", ")}`,
  );
  assert.ok(!existsSync(resolve(questRoot, "runs")), "runs/ must not exist");
  assert.ok(
    !existsSync(resolve(questRoot, "finalized_logs")),
    "finalized_logs/ must not exist",
  );
});
