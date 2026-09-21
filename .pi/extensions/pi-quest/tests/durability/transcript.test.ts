import { check } from "../check.ts";
import { mkdtempSync, mkdirSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { createQuest, type QuestState } from "../../src/domain/quest.ts";
import type { Qid } from "../../src/domain/qid.ts";
import type { TranscriptEntry } from "../../src/hooks/events.ts";
import { encodeSnapshot } from "../../src/durability/snapshots.ts";
import {
  boundTranscript,
  extractQuestTranscript,
  messageText,
  TRANSCRIPT_OMITTED_MARK,
} from "../../src/durability/transcript.ts";
import { fakeCtx } from "../fake-pi.ts";

const QID = "abc123";

Deno.test("messageText extracts role-prefixed user/assistant text", () => {
  check(messageText({ role: "user", content: "Do the work." }) === "[user] Do the work.", "user string");
  check(
    messageText({ role: "assistant", content: [{ type: "text", text: "notes" }, { type: "tool_use", name: "bash" }] }) === "[assistant] notes",
    "assistant text blocks only",
  );
  check(messageText({ role: "tool", content: "stdout" }) === null, "tool spam excluded");
  check(messageText({ role: "system", content: "be careful" }) === null, "system spam excluded");
  check(messageText({ message: { role: "user", content: "nested" } }) === "[user] nested", "nested message wrapper");
  check(messageText("bare string") === "bare string", "bare string data");
  check(messageText(42) === null, "non-message data degrades");
  check(messageText({ role: "user", content: "   " }) === null, "blank content degrades");
});

Deno.test("extractQuestTranscript slims the current session to a user/assistant brief", async () => {
  const entries: TranscriptEntry[] = [
    { customType: "quest_journal", data: { v: 1 } },
    { data: { role: "user", content: "Original request: fix alloc." } },
    { data: { role: "assistant", content: [{ type: "text", text: "Traced alloc.c:77." }, { type: "tool_use", name: "read" }] } },
    { data: { role: "tool", content: "won't show" } },
  ];
  const ctx = fakeCtx(join(tmpdir(), "pi-quest-x"), entries);
  const out = await extractQuestTranscript(ctx, QID, { sessionsDir: join(tmpdir(), "no-such-sessions-dir") });
  check(out.includes("[user] Original request: fix alloc."), "user text kept");
  check(out.includes("Traced alloc.c:77."), "assistant text kept");
  check(!out.includes("won't show"), "tool text excluded");
  check(!out.includes("quest_journal"), "snapshot excluded");
});

Deno.test("boundTranscript keeps the tail and marks the omitted front", () => {
  check(boundTranscript(["a"], 10) === "a\n", "fits unchanged");
  const many = Array.from({ length: 200 }, (_, i) => `message ${i} `.repeat(80));
  const out = boundTranscript(many, 800);
  check(out.length <= 800, "bounded to the budget");
  check(out.startsWith(TRANSCRIPT_OMITTED_MARK), "omitted mark leads");
  check(!out.includes("message 0"), "oldest dropped");
  check(out.includes("message 199"), "newest kept");
});

Deno.test("extractQuestTranscript pulls child-research sibling sessions", async () => {
  const sessions = join(mkdtempSync(join(tmpdir(), "pi-quest-x-")), "sessions");
  const project = join(sessions, "site");
  mkdirSync(project, { recursive: true });
  const parentQid = QID as Qid;
  const child = { ...createQuest("child research", "kid001" as Qid), parentQid } as QuestState;
  const related = [
    JSON.stringify({ customType: "quest_journal", data: encodeSnapshot(child) }),
    JSON.stringify({ type: "user", data: { role: "user", content: "Measure the alloc path." } }),
    JSON.stringify({ type: "assistant", data: { role: "assistant", content: "Buffer grows unbounded." } }),
  ].join("\n") + "\n";
  writeFileSync(join(project, "related.jsonl"), related);
  const unrelated = [
    JSON.stringify({ customType: "quest_journal", data: encodeSnapshot(createQuest("other work", "other00" as Qid)) }),
    JSON.stringify({ type: "assistant", data: { role: "assistant", content: "irrelevant chat" } }),
  ].join("\n") + "\n";
  writeFileSync(join(project, "unrelated.jsonl"), unrelated);
  const ctx = fakeCtx(tmpdir(), []);
  const out = await extractQuestTranscript(ctx, parentQid, { sessionsDir: sessions });
  check(out.includes("Buffer grows unbounded."), "child research contributes");
  check(!out.includes("irrelevant chat"), "unrelated session excluded");
});

Deno.test("extractQuestTranscript degrades to an empty transcript", async () => {
  const ctx = fakeCtx(tmpdir(), []);
  const out = await extractQuestTranscript(ctx, QID, { sessionsDir: join(tmpdir(), "missing-sessions") });
  check(out === "", "empty thread");
});