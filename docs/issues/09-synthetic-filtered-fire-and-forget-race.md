---
id: 09
title: "SYNTHETIC_FILTERED is fire-and-forget and racy"
state: ready
severity: medium
requires: []
validates: "SYNTHETIC_FILTERED sync with hash slice12"
area: "messaging.ts:286-327 shouldCapturePrompt, logging/types.ts, logging/summary/reducers.ts:135"
parent: 43
---
# Issue: `SYNTHETIC_FILTERED` is fire-and-forget and racy

- **Area:** `pi-quest` messaging — `messaging.ts:286-327 shouldCapturePrompt`, `logging/types.ts`, `logging/summary/reducers.ts:135`, `hooks/index.ts:253`, `utils/investigation_classification.ts:16`
- **Runs observed:** `1788280759` (many `shouldCapturePrompt` calls, `filteredCount` stuck 0 in `manifest.txt`)
- **Severity:** Medium — `manifest.txt filteredCount` and `CLASSIFICATION_RESULT` under-count

`shouldCapturePrompt` handles 1 `t.startsWith("/")` case, 1 `INTERNAL_MESSAGE_PREFIX` case, N `SYNTHETIC_PROMPT_PREFIXES` prefixes, and 6 contains-phrase checks. Each `return false` branch currently does `void (async()=>{ const {logEvent}=await import("./logging/core.ts"); logEvent("SYNTHETIC_FILTERED",...) })()` — dynamic `await import` never awaited. If `before_agent_start` returns before the microtask runs, `SYNTHETIC_FILTERED {syntheticPrefix, slice(0,80)}` is lost and `reducers.ts:135 filteredCount` never increments. The type `logging/types.ts: SYNTHETIC_FILTERED` exists but the emitter is missing in the hot path. No `hash` or `classification:"SYNTHETIC"` is emitted per §11.5.

Related: #10.
