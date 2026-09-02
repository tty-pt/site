import assert from "node:assert";
import { mkdir, rm } from "node:fs/promises";
import plugin, {
	asyncContext,
	getState,
	hasSufficientInvestigation,
	handleReassessmentCompletion,
	recordObservedInvestigation,
	triggerReassessment,
	syncImplementationPermission,
	type StoredState,
} from "../index.ts";

function createMockExtensionAPI() {
	const handlers: Record<string, any[]> = {};
	const agentMessages: Array<{ msg: any; options?: any }> = [];
	const mockPi: any = {
		on(event: string, handler: any) {
			if (!handlers[event]) handlers[event] = [];
			handlers[event].push(handler);
		},
		appendEntry() {},
		registerEntryRenderer() {},
		registerTool() {},
		registerCommand() {},
		sendMessage(msg: any, options?: any) {
			agentMessages.push({ msg: msg?.content || msg, options });
		},
		sendUserMessage() {},
		getAllTools: () => [],
		events: { on: () => () => {}, emit: () => {} },
	};
	return { mockPi, handlers, agentMessages };
}

function createMockContext(sessionId: string): any {
	return {
		cwd: process.cwd(),
		mode: "agent",
		hasUI: true,
		sessionManager: { id: sessionId, sessionId, getBranch: () => [], appendCustomEntry: () => {} },
		getContextUsage: () => ({ tokens: 50000, percent: 6.25 }),
		ui: { notify: () => {}, setStatus: () => {}, input: async () => "", select: async () => null },
	};
}

const VALID_EPISTEMIC = [
	`# Quest: Reassessment Contract`,
	``,
	`## Goal`,
	`Verify aggregated reassessment message`,
	``,
	`## Current Understanding`,
	`The queue dispatcher requires a memory barrier.`,
	``,
	`## Key Assumptions`,
	`- [x] Memory barrier prevents stale reads`,
	``,
	`## Research Findings`,
	`- Identified missing atomic fence in ring_pop()`,
	``,
	`## Open Questions & Uncertainties`,
	`- [ ] none`,
	``,
	`## Plan`,
	`1. Add memory barrier`,
	`2. Run stress tests`,
	``,
	`## Plan Confidence`,
	`low`,
	``,
	`## Exact Next Action`,
	`Implement the barrier`,
	``,
].join("\n");

// Used as an argument that is definitively absent (placeholder conclusion).
const EMPTY_UPDATES = new Map<string, string>();
const EMPTY_SECTIONS = new Map<string, any>();

Deno.test("quest_journal_reassessment_contract: aggregated completion message plus actionable freshness guidance", async (t) => {
	const currentDir = ".pi/quest/current";
	await rm(currentDir, { recursive: true, force: true });
	await mkdir(currentDir, { recursive: true });
	plugin(createMockExtensionAPI().mockPi);

	const run = <T>(ctx: any, fn: () => T): T => {
		return asyncContext.run(ctx, fn);
	};

	await t.step("1. incomplete reassessment reports ALL missing contract fields in ONE message", async () => {
		const { mockPi, agentMessages } = createMockExtensionAPI();
		plugin(mockPi);
		const ctx = createMockContext("session_reassess_1");
		const s = getState(ctx);
		s.active = "Reassessment Contract";
		s.questId = "reass-z-contract";
		s.investigationEpoch = 1;

		// Build a fresh reassessment-epoch receipt with zero evidence and reassessmentRequired true
		triggerReassessment(s, "stress test failed after 1M iterations");

		// Force a stale RESEARCH-epoch receipt on the current epoch so the
		// "initial research" freshness branch is the one that fires.
		s.currentReceipt = {
			epoch: (s.investigationEpoch || 1),
			epochType: "research",
			startedAt: Date.now() - 5000,
			toolCalls: 3,
			readTargets: ["mods/queue/ring_pop.c"],
			searchTargets: [],
			commands: [],
			evidenceCount: 3,
		};

		// Incomplete attempt: no conclusion, low confidence w/o allowLowConfidence, AND stale receipt.
		const note = run(ctx, () =>
			handleReassessmentCompletion(
				{
					reassessmentComplete: true,
					planConfidence: "low",
				},
				VALID_EPISTEMIC,
				EMPTY_UPDATES,
				EMPTY_SECTIONS,
				"Reassessment Contract",
				mockPi,
				ctx,
			),
		);

		assert.ok(note.includes("refused"), "must refuse incomplete reassessment");
		// Placeholder-conclusion guidance
		assert.ok(note.includes("reassessmentConclusion"), "must call out the missing reassessmentConclusion");
		// Low-confidence guidance
		assert.ok(note.includes("allowLowConfidence"), "must explain low-confidence requirement");
		assert.ok(note.includes("planConfidenceReason"), "must explain planConfidenceReason requirement");
		// Freshness guidance
		assert.ok(note.includes("read") || note.includes("search"), "must instruct a fresh read/search");
		assert.ok(note.toLowerCase().includes("epoch"), "must reference the research/reassessment epoch");

		assert.ok(s.reassessmentRequired, "reassessmentRequired stays true on rejection");
		assert.strictEqual(s.researchComplete, false, "researchComplete stays false on rejection");
	});

	await t.step("2. a fresh post-trigger read + complete payload completes reassessment (<2 turns)", async () => {
		const { mockPi } = createMockExtensionAPI();
		plugin(mockPi);
		const ctx = createMockContext("session_reassess_2");
		const s = getState(ctx);
		s.active = "Reassessment Contract";
		s.questId = "reass-z-contract";
		triggerReassessment(s, "stress test failed after 1M iterations");

		// Fresh investigation AFTER the trigger (reassessment epoch)
		recordObservedInvestigation(s, "read", { path: "mods/queue/ring_pop.c" });

		const check = hasSufficientInvestigation(s, "reassessment");
		assert.strictEqual(check.sufficient, true, "a fresh read must satisfy the reassessment evidence gate");

		const note = run(ctx, () =>
			handleReassessmentCompletion(
				{
					reassessmentComplete: true,
					planConfidence: "low",
					allowLowConfidence: true,
					planConfidenceReason: "Stress test passed 10k iterations after the memory barrier was added.",
					reassessmentConclusion: "The missing atomic fence was confirmed and a barrier added; verified with 10k iterations.",
				},
				VALID_EPISTEMIC,
				EMPTY_UPDATES,
				EMPTY_SECTIONS,
				"Reassessment Contract",
				mockPi,
				ctx,
			),
		);

		assert.ok(note.includes("complete"), "must report completion");
		assert.strictEqual(s.reassessmentRequired, false, "reassessmentRequired clears on completion");
		assert.strictEqual(s.researchComplete, true, "researchComplete set on completion");
		assert.strictEqual(s.consecutiveFailures, 0, "consecutiveFailures reset on completion");
		assert.strictEqual(s.resolvedReassessmentVersion, s.reassessmentVersion, "resolved version matches");

		// Gate should now permit implementation
		const perm = syncImplementationPermission(s);
		assert.strictEqual(perm, true, "implementation permission must be granted after reassessment resolves");
		assert.strictEqual(s.implementationAllowed, true, "implementationAllowed must be true after reassessment resolves");
	});

	await t.step("3. standalone freshness reason on a stale research-epoch receipt is actionable", async () => {
		const { mockPi } = createMockExtensionAPI();
		plugin(mockPi);
		const ctx = createMockContext("session_reassess_3");
		const s = getState(ctx);
		s.active = "Reassessment Contract";
		s.questId = "reass-z-contract";
		triggerReassessment(s, "test failed");

		s.currentReceipt = {
			epoch: (s.investigationEpoch || 1),
			epochType: "research",
			startedAt: Date.now() - 5000,
			toolCalls: 3,
			readTargets: ["mods/queue/ring_pop.c"],
			searchTargets: [],
			commands: [],
			evidenceCount: 3,
		};

		const check = hasSufficientInvestigation(s, "reassessment");
		assert.strictEqual(check.sufficient, false, "research-epoch receipt must not satisfy reassessment");
		assert.ok(check.reason, "reason must be present");
		assert.ok(check.reason.includes("read") || check.reason.includes("search"), "reason must tell the agent to investigate");
		assert.ok(check.reason.toLowerCase().includes("epoch"), "reason must reference the epoch");
	});
});
