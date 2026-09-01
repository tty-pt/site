---
id: 29
title: "121 bare catch{} swallow — needs tryLog helper, keep only existsSync guards"
state: blocked
severity: low
requires: [23]
blocked_by: [23]
validates: "grep catch {} <10 (only existsSync)"
area: "29-bare-catch-swallow.md"
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
