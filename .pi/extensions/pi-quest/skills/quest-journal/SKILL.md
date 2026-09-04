---
name: quest-journal
description: "Quest Journal v2: drafting / implementing / validating modes per quest. Stub until slice 3 lands the full skill text; v1 rules do NOT apply."
---

# Quest Journal v2 (stub)

v2 is a layered rebuild (see `REBUILD_PLAN.md`). Standing rules until the
full skill text lands — for the main agent only (reviewers get the review
brief, never this skill):

- Quest state lives in the session transcript as `quest_journal`
  snapshots; `quest.md` is a generated read-only view, never truth.
- Every quest uses a short alphanumeric quest id (`future/<qid>.md`,
  `current/<qid>/`); there are no slugs.
- Drafting: the agent may write exactly one file (the draft); all else is
  blocked. Draft edits re-run review (PASS auto-promotes); reviewer
  verdicts are PASS/FAIL.
- Implementing is unrestricted: record setbacks and amendments with
  evidence; nothing blocks. Amendments adjust the plan toward reality and
  never change scope.
- Validation PASS archives (quest view + session reference + manifest);
  FAIL demotes with findings. Archived is final.
- Sub-quests run implement-then-validate with their own id; one quest
  active at a time; depth cap 3.
- `quest_recover` rebuilds state from the transcript, including earlier
  sessions. The three commands are `/quest`, `/quests`, `/quest-del`.
- No `quest_mark_saved` exists. No gate ever waits for the user: questions
  carry a recommended default with a one-minute configurable timeout; late
  answers apply retroactively.
- Approval is never required; the user may approve at any moment.
