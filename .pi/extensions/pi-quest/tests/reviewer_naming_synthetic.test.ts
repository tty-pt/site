import assert from "node:assert";
import { mkdir, rm, writeFile } from "node:fs/promises";
import {
	clearActiveReviews,
	formatActiveReviewsUIStatus,
	getActiveReviews,
	getState,
	readQuestLog,
	getQuestLogPath,
	registerActiveReview,
	runCriticalReview,
	setCustomSubagentRunner,
} from "../src/index.ts";
import { shouldCapturePrompt } from "../src/messaging.ts";
import { classifyUserMessage } from "../src/classification.ts";
import { GLOBAL_REVIEW_CAP } from "../src/constants.ts";

function createMockAPI() {
	const handlers: Record<string, any[]> = {};
	let configuredTools: any[] = [];
	const eventBus: Record<string, any[]> = {};
	const events = {
		on: (e: string, h: any) => {
			if (!eventBus[e]) eventBus[e] = [];
			eventBus[e].push(h);
			return () => {};
		},
		emit: (e: string, d: any) => {
			for (const h of (eventBus[e] || [])) h(d);
		},
	};
	const mockPi: any = {
		on: (e: string, h: any) => { if (!handlers[e]) handlers[e] = []; handlers[e].push(h); },
		registerTool: () => {},
		registerCommand: () => {},
		sendMessage: () => {},
		sendUserMessage: () => {},
		appendEntry: () => {},
		registerEntryRenderer: () => {},
		getAllTools: () => configuredTools,
		events,
	};
	return { mockPi, setAllTools: (t: any[]) => { configuredTools = t; }, events };
}

function createMockCtx(sessionId = `session_${Math.random().toString(36).slice(2)}`): any {
	const branch: any[] = [];
	const status: Record<string, any> = {};
	return {
		cwd: process.cwd(),
		mode: "agent",
		hasUI: true,
		sessionManager: { id: sessionId, sessionId, getBranch: () => branch, appendCustomEntry: (_t: string, d: any) => branch.push({ type: "custom", customType: "quest_journal", data: d }) },
		getContextUsage: () => ({ tokens: 50000, percent: 6 }),
		ui: { notify: () => {}, setStatus: (k: string, v: any) => { if (v === undefined) delete status[k]; else status[k] = v; }, getStatus: (k: string) => status[k], getAllStatus: () => ({ ...status }), input: async () => "", select: async () => null },
	};
}

Deno.test("Reviewer naming and synthetic isolation (T40-T44)", async (t) => {
	const currentDir = ".pi/quest/current";
	await t.step("T40 synthetic isolation: post-compaction directive not captured", () => {
		assert.strictEqual(shouldCapturePrompt("⚡ **Post-Compaction Autonomous Resumption Directive** test"), false);
		assert.strictEqual(shouldCapturePrompt("⚡ **Pre-Compaction Exhaustive Context Preservation Protocol**"), false);
	});

	await t.step("T44 synthetic false-positive: user discussion of compaction is captured", () => {
		assert.strictEqual(shouldCapturePrompt("my compaction warning about context is user discussing compaction tradeoffs"), true);
		assert.strictEqual(shouldCapturePrompt("context compaction warning: hello").valueOf(), false);
	});

	await t.step("classification defense: post-compaction never becomes refinement", () => {
		const c = classifyUserMessage("⚡ **Post-Compaction Autonomous Resumption Directive** please add feature");
		assert.strictEqual(c as unknown as string, "CONVERSATIONAL_ACK");
	});

	await t.step("T41 naming: UI shows kind/triggerReason and logs contain triggerReason", async () => {
		clearActiveReviews();
		const { mockPi, setAllTools } = createMockAPI();
		setAllTools([{ name: "subagent" }]);
		const ctx = createMockCtx("session_naming_41");
		const s = getState(ctx);
		s.active = "naming-quest-41";
		s.questId = "naming-quest-41";
		s.planVersion = 1;
		s.researchComplete = true;
		await mkdir(`${currentDir}/${s.questId}`, { recursive: true });
		await writeFile(`${currentDir}/${s.questId}/quest.md`, `# Quest: ${s.questId}\n\n## Goal\nTest\n\n## Original request\n> Test\n`, "utf8");
		let captured: any = null;
		setCustomSubagentRunner(async (_task: string, opts?: any) => {
			captured = opts;
			return `VERDICT: PASS\nSEVERITY: NONE\nFINDINGS:\n- None\nREQUIRED ACTIONS:\n- None`;
		});
		await runCriticalReview(mockPi, ctx, { kind: "direction", questSlug: s.active, triggerReason: "no_progress" } as any);
		assert.strictEqual(captured.agent, "reviewer");
		assert.strictEqual(captured.triggerReason, "no_progress");
		assert.strictEqual(captured.reviewKind, "direction");
		const log = readQuestLog(getQuestLogPath(s.questId));
		assert.ok(log.includes("triggerReason=no_progress"), "log must contain triggerReason");
		assert.ok(log.includes("CRITICAL_REVIEW_REQUESTED"));
		clearActiveReviews();
		setCustomSubagentRunner(null);
		await rm(`${currentDir}/${s.questId}`, { recursive: true, force: true });
	});

	await t.step("UI status contains reviewer and trigger label", () => {
		clearActiveReviews();
		const ctx = createMockCtx("session_ui_41");
		const snap: any = {
			questId: "ui-quest-41", sessionId: "session_ui_41", reviewId: "rev_ui_1", reviewKind: "direction",
			planVersion: 1, saveGeneration: 1, stateHash: "h1", originalUserRequest: "Req", currentUnderstanding: "", assumptions: "", plan: "", planRevisions: "", findings: "", filesChanged: "", relevantDiff: "", testStatus: "", nextAction: "", createdAt: Date.now(),
		};
		registerActiveReview("rev_ui_1", "ui-quest-41", "session_ui_41", "direction", snap, undefined, "no_progress");
		const status = formatActiveReviewsUIStatus();
		// 46: critical_review slot hidden — quest slot icon already conveys status
		assert.strictEqual(status, undefined, `status must be hidden, got: ${status}`);
		assert.ok(getActiveReviews().has("rev_ui_1"), "review still tracked internally");
		clearActiveReviews();
	});

	await t.step("GLOBAL_REVIEW_CAP is 1 by default", () => {
		assert.strictEqual(GLOBAL_REVIEW_CAP, 1);
	});

	await t.step("draft vs root serialization with cap 1", async () => {
		clearActiveReviews();
		const { mockPi, setAllTools } = createMockAPI();
		setAllTools([{ name: "subagent" }]);
		const ctx = createMockCtx("session_cap_43");
		const s = getState(ctx);
		s.active = "cap-quest-43";
		s.questId = "cap-quest-43";
		s.planVersion = 1;
		s.lastSavedHash = "h1";
		s.researchComplete = true;
		await mkdir(`${currentDir}/${s.questId}`, { recursive: true });
		await writeFile(`${currentDir}/${s.questId}/quest.md`, `# Quest: ${s.questId}\n\n## Goal\nTest\n`, "utf8");
		let callCount = 0;
		let resolveFirst: any;
		const firstWait = new Promise((r) => { resolveFirst = r; });
		setCustomSubagentRunner(async () => { callCount++; if (callCount === 1) await firstWait; return `VERDICT: PASS\nSEVERITY: NONE\nFINDINGS:\n- None\nREQUIRED ACTIONS:\n- None`; });
		const p1 = runCriticalReview(mockPi, ctx, { kind: "plan_review", questSlug: s.active, triggerReason: "draft" } as any);
		await new Promise((r) => setTimeout(r, 10));
		assert.strictEqual(callCount, 1);
		const p2 = runCriticalReview(mockPi, ctx, { kind: "direction", questSlug: s.active, triggerReason: "no_progress" } as any);
		const p2Res = await p2;
		assert.strictEqual(p2Res.inProgress, true, "second must be queued due to cap 1");
		assert.strictEqual(getActiveReviews().size, 1);
		resolveFirst(undefined);
		await p1;
		clearActiveReviews();
		setCustomSubagentRunner(null);
		await rm(`${currentDir}/${s.questId}`, { recursive: true, force: true });
	});

	await rm(".pi/quest/current/naming-quest-41", { recursive: true, force: true }).catch(() => {});
	await rm(".pi/quest/current/cap-quest-43", { recursive: true, force: true }).catch(() => {});
});
