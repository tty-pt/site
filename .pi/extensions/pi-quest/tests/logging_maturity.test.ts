import assert from "node:assert";
import { createHash } from "node:crypto";
import { existsSync } from "node:fs";
import { mkdir, mkdtemp, readFile, rm, writeFile } from "node:fs/promises";
import { join } from "node:path";
import { tmpdir } from "node:os";
import { formatContextFields, parseLogEntry, sanitizeLogString } from "../src/logging/formatters.ts";
import { getQuestLogPath, logEvent, summarizeQuestJournalLog } from "../src/logging.ts";
import { asyncContext, createDefaultState, getState, sessionStates, setSessionState, state } from "../src/state.ts";
import { QUEST_CURRENT_DIR, FUTURE_DIR } from "../src/constants.ts";
import { createDiagnosticZip, findExtensionDir, findProjectRoot } from "../src/diagnostic.ts";
import { ExtensionContext } from "../src/types.ts";

function mockCtx(sid: string, cwd: string): ExtensionContext {
	return { cwd, sessionManager: { id: sid }, ui: { notify: () => {} }, hasUI: false } as any;
}

Deno.test("logging_maturity: B2/B2.5/B3 observability", async (t) => {
	const tempRoot = await mkdtemp(join(tmpdir(), "pi-logging-maturity-"));
	const currentDir = join(tempRoot, QUEST_CURRENT_DIR);
	const futureDir = join(tempRoot, FUTURE_DIR);
	await mkdir(currentDir, { recursive: true });
	await mkdir(futureDir, { recursive: true });
	const realRoot = findProjectRoot();
	const realExt = findExtensionDir(realRoot);
	try {
		await t.step("P0 draft early capture DRAFT_* hash only", async () => {
			const qid = "log_mat_draft_1";
			const ctx = mockCtx("sess-draft", tempRoot);
			const s = createDefaultState();
			s.questId = qid; s.activeDraft = "my-feature"; s.draftPrompts = ["prompt one"];
			setSessionState(ctx, s);
			const logPath = getQuestLogPath(qid, currentDir);
			await asyncContext.run(ctx, async () => {
				const h = createHash("sha256").update("prompt one").digest("hex").slice(0,12);
				logEvent("DRAFT_APPENDED", "draft appended", { logPath, questId: qid, slug: "my-feature", hash: h, draftPromptsCount: 1 } as any);
				logEvent("DRAFT_APPEND_DEDUPED", "deduped", { logPath, questId: qid, slug: "my-feature", hash: h, draftPromptsCount: 1 } as any);
				logEvent("DRAFT_CONVERSATIONAL_IGNORED", "ignored", { logPath, questId: qid, slug: "my-feature", hash: h } as any);
				logEvent("DRAFT_DISCARDED", "discarded", { logPath, questId: qid, slug: "my-feature", hash: h, reviewId: "rev_1", boundaryKey: "bk123" } as any);
			logEvent("DRAFT_PROMOTED", "promoted", { logPath, questId: qid, slug: "my-feature", hash: h, dest: "/arch/my-feature.md", reason: "promote" } as any);
			});
			const content = await readFile(logPath, "utf8");
			assert.ok(content.includes("DRAFT_APPENDED"));
			assert.ok(content.includes("DRAFT_APPEND_DEDUPED"));
			assert.ok(content.includes("DRAFT_CONVERSATIONAL_IGNORED"));
			assert.ok(content.includes("DRAFT_DISCARDED"));
			assert.ok(content.includes("DRAFT_PROMOTED"));
			// formatters priority includes new keys
			const ctxStr = formatContextFields({ draftPromptsCount: 1, hash: "abc", attemptKey: "k", syntheticPrefix: "pre" } as any);
			assert.ok(ctxStr.includes("draftPromptsCount="));
			// future dir copy in bundle: create a future file
			await writeFile(join(futureDir, "my-feature.md"), "# Quest: my-feature\n## Requirements\n- prompt one\n", "utf8");
			await mkdir(join(currentDir, qid), { recursive: true });
			await writeFile(join(currentDir, qid, "quest.md"), "# Quest: my-feature\n", "utf8");
			await writeFile(logPath, content, "utf8");
		});

		await t.step("P1 pending/attempt/requireConfirm observability", async () => {
			const qid = "log_mat_pending_1";
			const ctx = mockCtx("sess-pending", tempRoot);
			const s = createDefaultState(); s.questId = qid; s.active = "q1"; setSessionState(ctx, s);
			const lp = getQuestLogPath(qid, currentDir);
			await asyncContext.run(ctx, async () => {
				logEvent("PENDING_COALESCED_DROPPED", "dropped", { logPath: lp, questId: qid, pendingCount: 10 } as any);
				logEvent("PENDING_COALESCED_RESOLVED", "resolved", { logPath: lp, questId: qid, chosenKind: "plan_review", staleCount: 2, candidateCount: 3 } as any);
				logEvent("ATTEMPT_INCREMENTED", "attempt", { logPath: lp, questId: qid, attemptKey: "q1:plan_review", attempts: 1 } as any);
				logEvent("REQUIRE_CONFIRM_DECISION", "requireConfirm", { logPath: lp, questId: qid, requireConfirm: false } as any);
				logEvent("FIRST_PLAN_REVIEW_ALREADY_FIRED", "already fired", { logPath: lp, questId: qid } as any);
				logEvent("REVIEW_DEDUP_HIT", "dedup", { logPath: lp, questId: qid, shard: "draft" } as any);
			});
			const c = await readFile(lp, "utf8");
			assert.ok(c.includes("PENDING_COALESCED_DROPPED"));
			assert.ok(c.includes("ATTEMPT_INCREMENTED"));
			assert.ok(c.includes("REQUIRE_CONFIRM_DECISION"));
		});

		await t.step("B2.5 provisional-stall SAVE_FAILED+ future_draft_exists", async () => {
			const qid = "log_mat_prov_1";
			const lp = getQuestLogPath(qid, currentDir);
			const ctx = mockCtx("sess-prov", tempRoot);
			const s = createDefaultState(); s.questId = qid; setSessionState(ctx, s);
			await asyncContext.run(ctx, async () => {
				logEvent("SAVE_FAILED", "Save verification failed: Quest file not found or unreadable at `.pi/quest/current/1/quest.md`. Draft exists in `.pi/quest/future/my-feature.md` — call quest_update_state", { logPath: lp, questId: qid, path: `.pi/quest/current/${qid}/quest.md`, reason: "file_not_found+future_draft_exists", requiredAction: "quest_update_state" } as any);
				logEvent("GATE_BLOCKED", "gate blocked", { logPath: lp, questId: qid, gate: "PROVISIONAL_RESEARCH_PENDING", requiredAction: "quest_update_state" } as any);
				logEvent("REASSESSMENT_REQUIRED", "reassessment", { logPath: lp, questId: qid, round: 2 } as any);
			});
			const content = await readFile(lp, "utf8");
			assert.ok(content.includes("file_not_found+future_draft_exists"));
			assert.ok(content.includes("PROVISIONAL_RESEARCH_PENDING"));
		});

		await t.step("B3 dedup+user+semantic INITIAL_PROMPT USER_PROMPT SEMANTIC_SNAPSHOT", async () => {
			const qid = "log_mat_b3_1";
			const ctx = mockCtx("sess-b3", tempRoot);
			const s = createDefaultState(); s.questId = qid; s.active = "q-b3"; setSessionState(ctx, s);
			const lp = getQuestLogPath(qid, currentDir);
			await asyncContext.run(ctx, async () => {
				const intent = "Look at the consumer side code lot complexity";
				const h = createHash("sha256").update(intent).digest("hex").slice(0,12);
				logEvent("INITIAL_PROMPT", "initial prompt", { logPath: lp, questId: qid, hash: h, intentLen: intent.length, ref: "run/initial-prompt.txt", opencodeSessionId: "sess-b3", elapsedMs: 5 } as any);
				logEvent("TURN_START", "turn start", { logPath: lp, questId: qid, intentHash: h, intentLen: intent.length, slice: intent.slice(0,80), elapsedMs: 10, opencodeSessionId: "sess-b3" } as any);
				logEvent("USER_PROMPT", "user prompt", { logPath: lp, questId: qid, slice: "my refinement", hash: h, classification: "REFINEMENT_OR_REQUIREMENT", intentHash: h } as any);
				logEvent("SYNTHETIC_FILTERED", "filtered", { logPath: lp, questId: qid, syntheticPrefix: "⚡", slice: "synthetic", hash: h } as any);
				logEvent("SEMANTIC_SNAPSHOT", "research->awaiting-review", { logPath: lp, questId: qid, from: "research", to: "awaiting-review", planVersion: 1, activeGate: "RESEARCH_PENDING", elapsedMs: 20, opencodeSessionId: "sess-b3" } as any);
			});
			const cnt = await readFile(lp, "utf8");
			assert.ok(cnt.includes("INITIAL_PROMPT"));
			assert.ok(cnt.includes("USER_PROMPT"));
			assert.ok(cnt.includes("SEMANTIC_SNAPSHOT"));
			assert.ok(cnt.includes("intentHash="));
		});

		await t.step("P3b mutex/orphan/snapshot/resume", async () => {
			const qid = "log_mat_p3b_1";
			const ctx = mockCtx("sess-p3b", tempRoot);
			const s = createDefaultState(); s.questId = qid; s.active = "q-p3b"; setSessionState(ctx, s);
			const lp = getQuestLogPath(qid, currentDir);
			await asyncContext.run(ctx, async () => {
				logEvent("MUTEX_WAIT", "wait", { logPath: lp, questId: qid, lockKey: "global:review:sess-p3b", waitMs: 2 } as any);
				logEvent("MUTEX_ACQUIRED", "acquired", { logPath: lp, questId: qid, lockKey: "global:review:sess-p3b", holdMs: 5, waitMs: 2, contention: true } as any);
				logEvent("CRITICAL_REVIEW_ORPHAN_CLEARED", "orphan cleared", { logPath: lp, questId: qid, reason: "orphan_awaiting_pending_requeue", reviewId: "rev_1" } as any);
				logEvent("SNAPSHOT_FALLBACK", "fallback", { logPath: lp, questId: qid, reason: "git_diff_failed" } as any);
				logEvent("RESUME_DIRECTIVE_SENT", "resume", { logPath: lp, questId: qid, hash: "abc123", compactionId: "cmp1" } as any);
			});
			const c2 = await readFile(lp, "utf8");
			assert.ok(c2.includes("MUTEX_WAIT"));
			assert.ok(c2.includes("CRITICAL_REVIEW_ORPHAN_CLEARED"));
			assert.ok(c2.includes("SNAPSHOT_FALLBACK"));
			assert.ok(c2.includes("RESUME_DIRECTIVE_SENT"));
		});

		await t.step("formatters + reducers + summarize not crash", async () => {
			const qid = "log_mat_sum_1";
			const lp = getQuestLogPath(qid, currentDir);
			await writeFile(lp, "2026-09-01T00:00:00.000Z | DRAFT_APPENDED | quest=q1 draftPromptsCount=1 hash=abc | draft\n2026-09-01T00:00:01.000Z | PENDING_COALESCED_DROPPED | quest=q1 pendingCount=1 | drop\n", "utf8");
			const summary = summarizeQuestJournalLog(lp);
			assert.ok(summary);
			const line = "2026-09-01T00:00:00.000Z | DRAFT_APPENDED | quest=q1 draftPromptsCount=1 hash=abc | msg";
			const parsed = parseLogEntry(line);
			assert.ok(parsed);
			assert.strictEqual(parsed?.type, "DRAFT_APPENDED");
		});
	} finally {
		await rm(tempRoot, { recursive: true, force: true });
	}
});
