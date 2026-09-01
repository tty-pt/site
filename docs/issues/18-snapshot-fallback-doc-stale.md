---
id: 18
title: "SNAPSHOT_FALLBACK doc stale — captureSnapshot gone, 2 emitters now exist"
state: ready
severity: low
requires: []
validates: "grep SNAPSHOT_FALLBACK 2 emitters at snapshot.ts"
area: "critical_agent/snapshot.ts:37,57, logging/types.ts:163, logging/summary/reducers.ts:137,162"
parent: 45
---
# Issue: `SNAPSHOT_FALLBACK` doc stale — `captureSnapshot:49` gone, 2 emitters now exist

- **Area:** `pi-quest` logging/critical-agent — `critical_agent/snapshot.ts:37,57`, `logging/types.ts:163`, `logging/summary/reducers.ts:137,162`
- **Runs observed:** `1788280759` + HEAD 2026-09-01 re-audit
- **Severity:** Low — observability/doc drift, not data loss

`logging/types.ts:163` declares `SNAPSHOT_FALLBACK` and `reducers.ts:137,162` handles it. `REMAINING_WORK.md §2.1:33` claimed `0 emitter` at `snapshot.ts:49 captureSnapshot():Snapshot|null { if(!snap) return null }` silent. That function does not exist at HEAD.

At HEAD `src/critical_agent/snapshot.ts:1-76` is `export async function createReviewSnapshot(...):Promise<ReviewSnapshot>` always returns object `75 return{...}` with **2 emitters** already landed:

- `37 logEvent("SNAPSHOT_FALLBACK","snapshot fallback: git diff failed",{reason:"git_diff_failed"})`
- `57 logEvent("SNAPSHOT_FALLBACK","snapshot fallback: draft boundary compute failed",{reason:"draft_boundary_fallback",boundaryKey})`

`§1:21` `17/20 DONE` understates reality: 23/24 B2+B3 types now emitted (only `DRAFT_PROMOTED` 0). `§2.1` ordered edit proposing patching the deleted `captureSnapshot` is stale (Bunch 2 A′: file has no `captureSnapshot`, only `createReviewSnapshot`). Reducer `reducers.ts:137` handles `SNAPSHOT_FALLBACK` as `coalesceCount` (doc mis-attributed as `filteredCount`).

Remaining work: keep doc consistent, verify 2 emitters stay, add null guard only if a `Snapshot|null` path is reintroduced. Also retain at `reconstruction.ts` the existing widener `hasDraftOnDisk` via `readdirSync(FUTURE_DIR)` disk check (see #26) and keep `REASSESSMENT_REQUIRED round=2` already done. No new DAG edge (`snapshot.ts → logging.ts` leaf, +0, `check-pi-quest-dag.ts` stays `passed`; hash not required — reason string only).

Ordered-edit context (preserved for audit, line-free):

```ts
import { logEvent } from "../../logging.ts";
export function captureSnapshot(): Snapshot | null {
  const snap = tryCapture();
  if (!snap) {
    try { logEvent("SNAPSHOT_FALLBACK", `snapshot fallback`, { reason: "capture_null", phase: latest?.activeTransaction?.phase || "unknown", questId: getState().questId || undefined }); } catch {}
    return null;
  }
}
```

Tests: `tests/logging_maturity.test.ts:P3b` add step `logEvent("SNAPSHOT_FALLBACK", "...", {reason:"capture_null"})` + assert `c.includes("SNAPSHOT_FALLBACK")` (reducers already count via `reducers.ts:137`, not failure).

Verification: `grep -rn SNAPSHOT_FALLBACK .pi/extensions/pi-quest/src --include="*.ts" -n` → 2 emitters + types+reducers; `grep SNAPSHOT_FALLBACK .pi/quest/current/*/execution.log` appears on git-diff / draft-boundary fallback.

Related: #22, #26, #27, #33.
