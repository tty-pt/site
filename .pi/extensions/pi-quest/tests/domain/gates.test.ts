import { check } from "../check.ts";
import type { Qid } from "../../src/domain/qid.ts";
import type { QuestState } from "../../src/domain/quest.ts";
import { createDraft, createQuest, IDLE_STATE } from "../../src/domain/quest.ts";
import { draftPath } from "../../src/domain/paths.ts";
import type { ToolRef } from "../../src/domain/gates.ts";
import { decide, reasonText } from "../../src/domain/gates.ts";

const QID = "abc123" as Qid;

function ref(toolName: string, toolClass: ToolRef["toolClass"], path?: string): ToolRef {
  return path === undefined ? { toolName, toolClass } : { toolName, toolClass, path };
}

const EDIT_OTHER = ref("edit", "write", "src/impl.ts");
const READ = ref("read", "read", "src/impl.ts");
const JOURNAL = ref("quest_update_state", "journal");
const ASK = ref("ask_questions", "ask");
const OTHER = ref("mystery_tool", "other");
const LAUNCH = ref("subagent", "launch");

function drafting() {
  return createDraft(createQuest("req", QID), "thing");
}

Deno.test("gate exempts the draft file in every state", () => {
  const draftWrite = ref("edit", "write", draftPath(QID));
  const revision = { ...drafting(), draft: { ...drafting().draft!, outstandingFindings: true } };
  check(decide(revision, draftWrite).allowed, "exempt under revision");
  const midReview = { ...drafting(), activeReview: { kind: "draft" as const, target: "h1" } };
  check(decide(midReview, draftWrite).allowed, "exempt mid-review");
  check(decide(midReview, draftWrite, { isReviewerSession: true }).allowed, "exemption beats reviewer row");
});

Deno.test("gate keeps reviewers read-only", () => {
  const d = decide({ ...drafting(), phase: "implementing" }, READ, { isReviewerSession: true });
  check(!d.allowed && d.code === "IMPLEMENTATION_BLOCKED", "reviewer blocked");
  if (!d.allowed) {
    check(d.phaseName === "REVIEWER_READ_ONLY", "reviewer state name");
    check(reasonText(d).includes("REVIEWER_READ_ONLY"), "reason carries state");
  }
});

Deno.test("gate always allows reads, journal ops, and questions", () => {
  for (const r of [READ, JOURNAL, ASK]) {
    check(decide(drafting(), r).allowed, `${r.toolName} allowed in drafting`);
  }
  check(decide(createQuest("req", QID), READ).allowed, "read allowed in provisional");
});

Deno.test("gate blocks implementation in provisional", () => {
  const d = decide(createQuest("req", QID), EDIT_OTHER);
  check(!d.allowed && d.code === "RESEARCH_REQUIRED", "provisional blocked");
  if (!d.allowed) check(d.phaseName === "PROVISIONAL_RESEARCH_PENDING", "provisional name");
});

Deno.test("gate blocks unauthored and revision drafts", () => {
  const pending = decide(drafting(), EDIT_OTHER);
  check(!pending.allowed && pending.code === "DRAFT_REVIEW_REQUIRED", "unauthored blocked");
  if (!pending.allowed) check(pending.phaseName === "DRAFT_PENDING", "pending name");
  const s = drafting();
  const revision = { ...s, draft: { ...s.draft!, outstandingFindings: true } };
  const d = decide(revision, EDIT_OTHER);
  check(!d.allowed, "revision blocked");
  if (!d.allowed) check(d.phaseName === "DRAFT_REVISION_PENDING", "revision name");
});

Deno.test("gate leaves idle and archived open", () => {
  check(decide(IDLE_STATE, EDIT_OTHER).allowed, "idle open");
  check(decide(IDLE_STATE, LAUNCH).allowed, "idle launch open");
  check(decide({ ...IDLE_STATE, phase: "archived" }, EDIT_OTHER).allowed, "archived open");
});

Deno.test("gate blocks writes while a review runs", () => {
  const s = drafting();
  const authored = { ...s, draft: { ...s.draft!, planAuthored: true } };
  const midReview = { ...authored, activeReview: { kind: "draft" as const, target: "h1" } };
  const d = decide(midReview, EDIT_OTHER);
  check(!d.allowed && d.code === "PLAN_REVIEW_REQUIRED", "review blocks writes");
  check(decide(midReview, READ).allowed, "review allows reads");
});

Deno.test("gate opens implementation and blocks unknown tools while drafting", () => {
  const open: QuestState = { ...drafting(), phase: "implementing" };
  check(decide(open, EDIT_OTHER).allowed, "implementing open");
  check(decide(open, LAUNCH).allowed, "implementing launch open");
  const d = decide(drafting(), OTHER);
  check(!d.allowed, "unknown tool blocked while drafting");
  if (!d.allowed) {
    const text = reasonText(d);
    check(text.includes(d.code) && text.includes(d.action), "reason carries code and action");
  }
});
