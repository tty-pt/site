---
id: 46
title: "Status bar icon + 3-word nome + #full qid — hide Critical reviewer phrase"
state: done
severity: medium
requires: []
validates: "formatQuestShort == 'icon 3-word-nome #full qid' without Critical reviewer or receiving word"
area: "utils/formatting.ts:125-137 barIcon/formatQuestShort + ui.ts:25-29 + critical_agent/tracker.ts:333-341"
parent: 43
---
# Issue: Status bar icon + 3-word nome + #full qid — hide Critical reviewer phrase

- **Area:** `pi-quest` UI — `utils/formatting.ts:125-137 barIcon/formatQuestShort` + `ui.ts:25-29 updateUIStatus` + `critical_agent/tracker.ts:333-341 formatActiveReviewsUIStatus` + `index.ts:104 renderEntry`
- **Runs observed:** `1788305314` bar `⚖ Critical: reviewer ? UNCERTAIN │ Σ$0.000 ↺ look-consumer-side-code… #1788305314 receiving` (4 slots `critical_review │ cost │ quest │ receiving`) while `quest` slot already `↺ look-consumer-side… #id` (`barIcon:↺` reassess, `truncateQuestName(24)` mid-word). `? UNCERTAIN` meaningless, you want icon alone.
- **Severity:** Medium — bar duplicates reviewer state (`critical_review` `⚖ Critical… ? UNCERTAIN` 15s via `tracker.ts:278,304`) + `quest` `↺ … #id` + Pi native `Σ$0.000` + `receiving` suffix `↺ … receiving` (icon already conveys receiving). Desired you approved: `icon 3-word-nome #full qid` e.g. `📝 Look consumer side #1788305314` (icon `📝 draft → ⏳ review → 💾 dirty → ↺ reassess → 🔍 research → ✅ active`), 3 words `split(/[-_\s]+).slice(0,3)` with `…` if longer, full `#qid` grep-able.
- **Desired:** `tracker.ts:297-329 formatActiveReviewsUIStatus` return `undefined` (hide second `│` segment) when `formatQuestShort` already shows `↺/⏳` — no `⚖ Critical: reviewer …` phrase; `Σ$0.000` left be (Pi cost, not extension); `receiving` kept as Pi native but icon suffices. Single short token per bar: `icon 3-word-nome #full qid` from `formatQuestShort`, `index.ts:104` reuses same.

Related: #05, #16, #17, #40.
