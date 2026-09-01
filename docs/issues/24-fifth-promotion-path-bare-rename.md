---
id: 24
title: "5th promotion path commands/quest.ts bare rename without archive (extends #04)"
state: blocked
severity: high
requires: [04]
blocked_by: [04]
validates: "grep DRAFT_DISCARDED src/commands/quest.ts 1 hit + future-archive in zip"
area: "24-fifth-promotion-path-bare-rename.md"
---
# Issue: 5th promotion path `commands/quest.ts:55` bare `rename` without archive (extends #04)

- **Area:** `pi-quest` lifecycle/promotion — `commands/quest.ts:49-65` (`55`), `lifecycle.ts:99-110`, `paths.ts:335-352`, `diagnostic/packaging.ts:157-196`
- **Runs observed:** HEAD 2026-09-01 still bare; docs `§1 ALL 4 paths now archive-before-unlink` true for A-D, false that ALL paths covered
- **Severity:** High — irretrievable draft loss before `packaging.ts:158` copies `future/`, `run/future-archive/<slug>.md` lost

Four canon paths A-D are fixed at HEAD (verified `executor.ts:90-123` `copyFileSync`+`DRAFT_DISCARDED`, `lifecycle.ts:104-108`, `promote.ts:58-66`, `paths.ts:340-348` triple-nested): `04-promotion-paths-delete-without-archive.md` gray table. **But unlisted E still violates** invariant `never unlink without archive`:

```ts
// src/commands/quest.ts:49-65
54 readFile(futurePath)
55 await rename(futurePath,path) // bare, 0 copyFile, 0 DRAFT_DISCARDED
...
64 cleanDraftIfExists(name) // finds no file after move → no future-archive copy
```

`future/<slug>.md` is moved to `current/<qid>/quest.md` without first copying to `future-archive/`. `diagnostic/packaging.ts:170-178` then has nothing to copy to `current-run/future-archive/`, `verifyDiagnosticZip:339` not asserting, `hierarchy futureCount` under-reports (#25).

Fix canon (as `lifecycle.ts` copy-before-rename): copy-before-rename with dual fallback.

```ts
import {createHash} from "node:crypto";
import {copyFileSync, mkdirSync, existsSync} from "node:fs";
import {join, basename} from "node:path";
import {logEvent} from "../logging.ts";

const content = await readFile(futurePath, "utf8");
try {
  const qid = targetQid;
  const archDir = join(questDirPath(qid), "future-archive");
  mkdirSync(archDir, {recursive:true});
  const destArch = join(archDir, basename(futurePath));
  try { copyFileSync(futurePath, destArch); } catch { try { await copyFile(futurePath, destArch); } catch {} }
  const h = createHash("sha256").update(content).digest("hex").slice(0,12);
  try { logEvent("DRAFT_DISCARDED", `draft discarded`, {quest: slug, slug, hash: h, dest: destArch, reason:"quest"}); } catch {}
} catch {}
await rename(futurePath, path)
```

Legacy `HIGH_LEVEL_PLAN_V2_GAPFILL_DETAILED` flagged as B, now `lifecycle.ts` fixed but `commands/quest.ts:55` reintroduces gap (history note). DAG `commands/quest.ts → logging.ts` leaf + `paths.ts` leaf, +0 (already `commands/promote.ts → logging.ts`). `questDirPath(null)→""` edge → guard `future-archive` relative write to `./future-archive` if `getQuestId(ctx)` null before `questId` established.

Tests: `lifecycle_consistency.test.ts` + `diagnostic_zip.test.ts` `unzip -l | rg future-archive` includes `quest.ts` path slug.

Verification: `rg DRAFT_DISCARDED src/commands/quest.ts` 1 hit after; `unzip -l pi-quest-bundle.zip | rg future-archive` includes slug.

Related: #04, #06, #22, #25, #26, #33.

Related: #04, #06, #22, #25, #26.
