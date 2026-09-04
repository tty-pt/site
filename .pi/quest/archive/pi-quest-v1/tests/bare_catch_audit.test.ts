import assert from "node:assert";
import { walk } from "https://deno.land/std@0.224.0/fs/walk.ts";
import { resolve } from "https://deno.land/std@0.224.0/path/mod.ts";

const SRC = resolve(import.meta.dirname ?? ".", "../src");

// A logging call that, if swallowed by a bare catch, would hide an observability
// failure (the whole point of issue #29 / tryLog).
const LOG_RE =
  /\b(logEvent|log\w+Transition|logUserInteraction|logAgentMessage|logTool\w*|logError|logDebug)\s*\(/;
const BARE_CATCH_RE = /\}\s*catch\s*\{\s*\}$/;
// Marker for a legitimate non-logging operation in the same guarded block, which
// makes the guard a defensible best-effort I/O/defensive guard (kept per #29).
const IO_RE =
  /\b(existsSync|readdirSync?|readFileSync|statSync|unlinkSync|mkdirSync|copyFileSync|writeFileSync|utimesSync|writeSync|closeSync|openSync|renameSync|rmSync|readdir|readFile|stat\b|unlink|mkdir|copyFile|writeFile|persist|await import|import\(|\.catch\(|JSON\.parse|syncImpl\w+|syncMetaJson|summarizeQuestJournalLog|remove\w+|create\w+|release\w+|touch\w+|writeSessionLiveness|listActiveQuestRecords|getCachedSettingsJson|getAllTools|\.delete\(|\.set\(|\.get\(|\.abort\(|notify|sendInternalAgentMessage|sendUserMessage|auditQuestConsistency|commitTerminalState|validatePhasedPlan|questPath|getActiveReviews)\b/;

interface Hit {
  file: string;
  line: number;
  block: string;
  hasLog: boolean;
  hasIo: boolean;
}

function innerBlock(lines: string[], catchLineIdx: number): string {
  let depth = 0;
  let j = catchLineIdx - 1;
  while (j >= 0) {
    depth += (lines[j].match(/\{/g)?.length ?? 0) -
      (lines[j].match(/\}/g)?.length ?? 0);
    if (depth >= 1) break;
    j--;
  }
  return lines.slice(j + 1, catchLineIdx).join("\n");
}

async function collectHits(): Promise<Hit[]> {
  const hits: Hit[] = [];
  for await (const entry of walk(SRC, { exts: [".ts"] })) {
    if (!entry.isFile) continue;
    const content = await Deno.readTextFile(entry.path);
    const lines = content.split("\n");
    for (let i = 0; i < lines.length; i++) {
      if (!BARE_CATCH_RE.test(lines[i])) continue;
      const block = innerBlock(lines, i);
      hits.push({
        file: entry.path,
        line: i + 1,
        block,
        hasLog: LOG_RE.test(block),
        hasIo: IO_RE.test(block),
      });
    }
  }
  return hits;
}

Deno.test("bare_catch_audit: no pure logging swallow remains (tryLog rolled out)", async () => {
  const hits = await collectHits();
  const pure = hits.filter((h) => h.hasLog && !h.hasIo);
  const details = pure
    .map((h) =>
      `${h.file}:${h.line} -> ${h.block.replace(/\s+/g, " ").slice(0, 90)}`
    )
    .join("\n");
  assert.strictEqual(
    pure.length,
    0,
    `${pure.length} bare catch{} still swallow a logging call with no IO guard:\n${details}`,
  );
});

Deno.test("bare_catch_audit: total bare catch count held below 214 baseline", async () => {
  // Baseline before #29 was 214. All logging-swallows were replaced with tryLog;
  // the remainder are legitimate IO/defensive guards. This guards against a future
  // regression that re-introduces logging swallows and drives the count back up.
  const hits = await collectHits();
  assert.ok(
    hits.length <= 170,
    `bare catch count ${hits.length} regressed toward the 214 baseline`,
  );
});
