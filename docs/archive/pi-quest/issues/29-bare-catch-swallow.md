---
id: 29
title: "bare catch{} swallow → tryLog helper; keep only IO/existsSync guards"
state: done
severity: low
requires: [23]
validates: "grep catch {} no logging-swallows remain; tryLog rolls out to 12 files; DAG passed"
area: "grep -rn 'catch {}' src | wc -l, try{, grep -P 'catch/s*/{/s*/}'"
parent: 44
---
# Issue: 121 bare `catch{}` swallow — needs `tryLog` helper, keep only `existsSync` guards

- **Area:** `pi-quest` quality/logging — `grep -rn "catch {}" src | wc -l` 121 bare (197 total `try{`) at HEAD, `grep -P "catch\s*\{\s*\}"` 121, not doc `167`; `logging/core.ts:34 PERSISTENCE_DEGRADED`, `logging/safe.ts` not yet created
- **Runs observed:** HEAD 2026-09-01 (`hooks/index.ts 30×25`, `executor.ts 16×13`, `paths.ts 12×8`, `messaging.ts 8×4`, `classification.ts 6×6`, `utils/mutex.ts 2×2` dynamic import, remainder)
- **Severity:** Low — silent `logEvent` degradation indistinguishable from success; violates `AGENTS.md:35` uniform agent-visible communication

Worst at HEAD: `hooks/index.ts` (`CLASSIFICATION_RESULT`, `DRAFT_APPENDED`, `DRAFT_CONVERSATIONAL_IGNORED` unconditional, future draft), `messaging.ts` `SYNTHETIC_FILTERED`, `paths.ts` `cleanDraftIfExists` triple-nested `try{copyFile} catch{try mkdir+copyFile} catch{try copyFileSync}`, `classification.ts`, `executor.ts` `PLAN_REVIEW_SUPPRESSED`, `utils/mutex.ts` `await import` etc. Stale doc said 167 bare / 197 total `try`; corrected `grep -P "catch\s*\{\s*\}"` 121 bare (120 literal +1), per-file corrected `hooks 30×25`, `executor 16×13`, `paths 12×8`, `messaging 8×4` — over by 5/3 originally. Most `try{logEvent} catch{}` intentionally non-fatal per `AGENTS.md:35` but swallows `logging` degradation (only `logging/core.ts:PERSISTENCE_DEGRADED` surfaces).

Undetailed fix (1 helper, not per-file table now):

- Introduce `src/logging/safe.ts: tryLog(event,msg,ctx)` that `try{logEvent} catch(e){logError("logging failed", e, ctx, QuestErrorCode.PERSISTENCE_FAILURE)}` so `execution.log` write failure surfaces via `reportAgentError`, not silent.
- Keep swallow only where `existsSync` guard acceptable: `persistence.ts:futureDraftExists catch{return false}`, `diagnostic/packaging.ts:future entries .catch(()=>[])`, `packaging zip exists` `existsSync` guards, `reconstruction.ts:file fallback try{readFileSync FUTURE_DIR}` (no file when `activeDraft==null` before any quest) — `§2.5:149`.
- Replace 121 bare with helper where `logEvent` only; flatten `paths.ts` triple-nested to `try{copyFile} catch{await mkdir}` single level — 1 helper replaces all bare `logEvent` swallows.

Doc lies: `167 bare` high by 46 (actual 121), `167→<10` only for `existsSync` guards.

Verification: `grep -rn "catch {}" src | wc -l` `121→<10` (only `existsSync` guards) + `grep PERSISTENCE_DEGRADED .pi/quest/current/*/execution.log` on injected `logEvent` failure + `deno test` still `54→60 passed`. Hash helper already `diagnostic/packaging.ts:computeFileSha256` reuse, file-already rule §11.5 preserved.

Related: #23, #27, #28, #33.

## RESOLUTION (2026-09-03)

**Reality check during implementation:** the true bare `catch {}` count at HEAD was **214** (issue's `121`/`167` both stale). A precise scan showed only **~64/214 (30%)** wrapped a logging call, and only **44** were *clean* log-only swallows. The remaining ~147 are legitimate best-effort IO/import/defensive guards (existsSync, readFile/writeFile, persist, dynamic `await import`, mutex lock-touch, JSON.parse, etc.) that must stay.

**User decision:** fix all logging swallows, keep IO guards. Not feasible to reach the issue's original `<10` target without unsafe restructuring.

**Delivered:**
- New `src/logging/safe.ts` with `tryLog(type, message, context?, ctx?)` that calls `logEvent` inside a try and, on failure, surfaces via `ui.notify` (logging-local, mirrors `core.ts:41`) — **never re-enters logEvent, no messaging import, no DAG edge**.
- `src/logging/index.ts` += `export * from "./safe.ts"`.
- Converted all **pure logging swallows** to `tryLog` across 12 src files (hooks, policy, executor, classification, research, pi_adapter, subquest_operation, messaging, handlers, pending_coalesce); flattened `paths.ts` triple-nested copy (kept dynamic import to avoid the `paths↔logging` DAG cycle — `logging/summary/helpers.ts:3` already imports `../paths.ts`).
- Result: `catch {}` **214 → 155** (remainder all IO/defensive guards), **zero pure logging swallows left**.
- New tests: `tests/logging_safe.test.ts` (5: success write, non-throw on success, non-throw on blocked-write, repeated failure) and `tests/bare_catch_audit.test.ts` (2: no pure logging swallow remains; count held < 170 baseline-regression guard).
- Gate: `check-pi-quest-dag.ts` **passed** (131 files, 702 edges, 1 allowlisted cycle); `deno test` **83 passed / 0 failed**; `as any` stays **4**; `deno fmt --check` clean; `npm run zip` **Verification PASSED (234 entries)**, SHA-256 `82e21675…`, Content SHA `fc8f712f…`.

