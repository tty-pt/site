---
id: 49
title: "Future file slug mismatch: probe look-consumer… vs actual task-adversarial…"
state: ready
severity: high
requires: []
validates: "ls future slug == state.activeDraft"
area: "paths.ts:generateSlugFromPrompt + hooks/index.ts:453-456 + future/task-adversarial-plan-review-look-consumer.md:1"
parent: 43
---
# Issue: Future file slug mismatch: probe look-consumer… vs actual task-adversarial…

- **Area:** `pi-quest` paths — `paths.ts:generateSlugFromPrompt` + `hooks/index.ts:453-456` slug generation + `execution.log:34` + disk `future/task-adversarial-plan-review-look-consumer.md:1`
- **Runs observed:** `1788305314` `future/` lists only `task-adversarial-plan-review-look-consumer.md` (78 lines); agent `34` tried `read .pi/quest/future/look-consumer-side-code-lot-complexity.md` (generated from prompt slice `Look at the consumer side code…` len 313 `bdbaf5`) → `ENOENT` → #47 → #50. `draft-prompts.jsonl:1` slice shows same.
- **Severity:** High — `read futureDraftPath(activeDraft)` must succeed at `turn0`; mismatch triggers `SNAPSHOT_FALLBACK`.

Related: #06, #08, #25, #47.
