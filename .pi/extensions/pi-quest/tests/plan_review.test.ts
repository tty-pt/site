import assert from "node:assert";
import { mkdir, readFile, rm, writeFile } from "node:fs/promises";
import plugin, {
	acceptRootConfirmation,
	asyncContext,
	buildCriticalReviewPrompt,
	canImplement,
	canToolExecuteInCriticalReview,
	checkAndTriggerPlanReview,
	classifyToolCall,
	clearActiveReviews,
	ensureQuestId,
	executeUpdateStateTool,
	getActiveReviews,
	getImplementationBlockReason,
	getPendingReviews,
	getQuestLogPath,
	getState,
	isPlanReviewValidForState,
	isSubagentAvailable,
	isSubagentToolRegistered,
	parseCriticalReviewResponse,
	readQuestLog,
	requestPlanReview,
	setCustomSubagentRunner,
	snapshotState,
	QuestErrorCode,
	type ExtensionAPI,
	type ExtensionContext,
	type StoredState,
} from "../index.ts";

function createMockExtensionAPI() {
	const handlers: Record<string, any[]> = {};
	const registeredTools: any[] = [];
	const registeredCommands: any[] = [];
	const agentMessages: Array<{ msg: any; options?: any; customType?: any; display?: any }> = [];
	const userMessages: Array<{ msg: any; options?: any }> = [];
	const appendedEntries: Array<{ type: string; data: any }> = [];
	let configuredTools: any[] = [];
	const eventBusHandlers: Record<string, any[]> = {};

	const events = {
		on: (event: string, handler: any) => {
			if (!eventBusHandlers[event]) eventBusHandlers[event] = [];
			eventBusHandlers[event].push(handler);
			return () => {
				const idx = eventBusHandlers[event].indexOf(handler);
				if (idx >= 0) eventBusHandlers[event].splice(idx, 1);
			};
		},
		emit: (event: string, data: any) => {
			const list = eventBusHandlers[event] || [];
			for (const h of [...list]) {
				try { h(data); } catch {}
			}
		},
	};

	const mockPi: ExtensionAPI = {
		on: (event: string, handler: any) => {
			if (!handlers[event]) handlers[event] = [];
			handlers[event].push(handler);
		},
		registerTool: (tool: any) => {
			registeredTools.push(tool);
		},
		registerCommand: (name: string, cmd: any) => {
			registeredCommands.push({ name, ...cmd });
		},
		sendMessage: (msg: any, options?: any) => {
			agentMessages.push({
				msg: msg?.content || msg,
				options,
				customType: msg?.customType,
				display: msg?.display,
			});
		},
		sendUserMessage: (msg: any, options?: any) => {
			userMessages.push({ msg, options });
		},
		appendEntry: (type: string, data: any) => {
			appendedEntries.push({ type, data });
		},
		registerEntryRenderer: () => {},
		getAllTools: () => configuredTools,
		events,
	};

	return {
		mockPi,
		handlers,
		registeredTools,
		registeredCommands,
		agentMessages,
		userMessages,
		appendedEntries,
		setAllTools: (tools: any[]) => { configuredTools = tools; },
		events,
	};
}

function createMockContext(tokens = 50000, sessionId = `session_${Math.random().toString(36).slice(2)}`): ExtensionContext {
	const branch: any[] = [];
	return {
		cwd: process.cwd(),
		mode: "agent",
		hasUI: true,
		sessionManager: {
			id: sessionId,
			sessionId,
			getBranch: () => branch,
			appendCustomEntry: (_type: string, data: any) => {
				branch.push({ type: "custom", customType: "quest_journal", data });
			},
		},
		getContextUsage: () => ({ tokens, percent: (tokens / 800000) * 100 }),
		ui: {
			notify: () => {},
			setStatus: () => {},
			input: async () => "",
			select: async () => null,
		},
	};
}

Deno.test("Adversarial Plan Review Suite: Comprehensive Verification of Planning Gates & Reviewer Self-Attack", async (t) => {
	const currentDir = ".pi/quest/current";
	await mkdir(currentDir, { recursive: true });

	setCustomSubagentRunner(null);

	// -----------------------------------------------------------------------
	// 1. Plan draft is not complete merely because main agent claims ready
	// -----------------------------------------------------------------------
	await t.step("1. plan draft is not complete merely because main agent claims ready", async () => {
		const { mockPi, setAllTools } = createMockExtensionAPI();
		const ctx = createMockContext(50000, "session_plan_1");
		plugin(mockPi);
		setAllTools([{ name: "subagent", description: "Subagent runner" }]);

		const s = getState(ctx);
		const slug = "audio-chunker-plan";
		s.active = slug;
		s.questId = slug;
		s.stack = [slug];
		s.prompts = ["Build an async audio chunker with ring buffer and backpressure handling."];

		const qPath = `${currentDir}/${slug}/quest.md`;
		await mkdir(`${currentDir}/${slug}`, { recursive: true });
		await writeFile(qPath, `# Quest: ${slug}\n\n## Goal\nAudio chunker\n\n## Original request\n> Build an async audio chunker with ring buffer and backpressure handling.\n\n## Plan\n1. Simple chunking without buffer\n\n## Remaining work\n- [ ] Task 1\n`, "utf8");

		// Reviewer rejects because backpressure / ring buffer is omitted from plan
		let reviewerCalled = false;
		const rejectRunner = async () => {
			reviewerCalled = true;
			return `PASS 1 (Independent Evaluation):
Provisional Judgment: REVISE
Provisional Summary: Plan omitted ring buffer and backpressure.

PASS 2 (Self-Attack & Falsification):
- Assumptions tested: Is ring buffer strictly required?
- Invalidation risk: User explicitly requested ring buffer and backpressure handling.
- Revised Judgment: REVISE

PROMPT-COMPLIANCE:
- Requirement: Async audio chunker -> Plan Handling: Addressed in step 1 -> Status: SATISFIED
- Requirement: Ring buffer -> Plan Handling: Not mentioned in plan -> Status: UNSATISFIED
- Requirement: Backpressure handling -> Plan Handling: Omitted -> Status: UNSATISFIED

VERDICT: REVISE
SEVERITY: CRITICAL

FINDINGS:
- Issue: Plan omits ring buffer and backpressure handling
  Evidence: Step 1 uses naive chunking without ring buffer structure

REQUIRED REVISIONS:
- Add ring buffer data structure step
- Add backpressure callback handling step`;
		};
		setCustomSubagentRunner(rejectRunner);

		// Agent attempts to complete research with plan draft
		await executeUpdateStateTool({
			name: slug,
			plan: ["1. Simple chunking without buffer"],
			planConfidence: "high",
			planConfidenceReason: "Main agent claims high confidence and tested assumptions.",
			researchComplete: true,
		}, mockPi, ctx);

		assert.strictEqual(reviewerCalled, true, "Independent reviewer must be invoked");
		assert.strictEqual(s.researchComplete, false, "researchComplete must NOT be set when reviewer issues REVISE");
		assert.strictEqual(s.researchRequired, true, "researchRequired must remain true");
		assert.strictEqual(canImplement(s, ctx), false, "Implementation gate must remain blocked");

		const blockReason = getImplementationBlockReason(s, ctx);
		assert.strictEqual(blockReason.blocked, true);
		assert.strictEqual(blockReason.code, QuestErrorCode.PLAN_REVIEW_REQUIRED);

		setCustomSubagentRunner(null);
	});

	// -----------------------------------------------------------------------
	// 2. Reviewer prompt receives exact prompt, 11-point criteria & 2-pass instructions
	// -----------------------------------------------------------------------
	await t.step("2. reviewer prompt receives exact prompt, 11-point criteria & 2-pass instructions", async () => {
		const originalPrompt = "Implement zero-copy audio stream muxer with sample-rate conversion";
		const prompt = buildCriticalReviewPrompt("plan_review", "stream-muxer-quest", {
			originalRequest: originalPrompt,
			refinements: ["Must support 48kHz and 96kHz rates"],
			currentUnderstanding: "axil audio muxing architecture",
			keyAssumptions: "Zero-copy requires aligned memory pools",
			openQuestions: "Latency budget < 5ms",
			plan: "1. Memory pool setup\n2. SRC filter\n3. Mux loop",
			planConfidence: "high",
			planRevisions: "Initial plan",
			findings: "SIMD acceleration available for SRC",
			filesModified: "",
			testStatus: "Unit tests ready",
			executionSnapshot: "Research complete",
			exactNextAction: "Begin implementation",
			remainingWork: "- [ ] Step 1\n- [ ] Step 2",
			status: "Research complete",
		});

		assert.ok(prompt.includes(originalPrompt), "Prompt must include verbatim user prompt");
		assert.ok(prompt.includes("WHAT YOU MUST EVALUATE (PLAN REVIEW):"), "Prompt must include 11-point plan evaluation checklist");
		assert.ok(prompt.includes("1. Whether the plan actually addresses the user's objective;"), "Must include criteria 1");
		assert.ok(prompt.includes("10. Whether the plan contains contradictions or internally incompatible steps;"), "Must include criteria 10");
		assert.ok(prompt.includes("11. Whether the plan provides a credible path to satisfying the original request."), "Must include criteria 11");
		assert.ok(prompt.includes("Reviewer Preference (MUST NOT block a plan)"), "Must instruct reviewer not to block on preference");
		assert.ok(prompt.includes("TWO-PASS SELF-ATTACK REQUIREMENT:"), "Must instruct 2-pass self-attack");
		assert.ok(prompt.includes("What requirement could this plan still be missing?"), "Must include self-attack question 1");
		assert.ok(prompt.includes("What would make this plan wrong?"), "Must include self-attack question 4");
		assert.ok(prompt.includes("Do not trust the main agent's summary or claims"), "Must instruct untrusted inspection");
	});

	// -----------------------------------------------------------------------
	// 3. Reviewer self-attack overturning initial PASS to REVISE blocks the plan
	// -----------------------------------------------------------------------
	await t.step("3. reviewer self-attack overturning initial PASS to REVISE blocks the plan", async () => {
		const rawResponse = `PASS 1 (Independent Evaluation):
Provisional Judgment: APPROVE
Provisional Summary: Plan looks comprehensive on first pass.

PASS 2 (Self-Attack & Falsification):
- Assumptions tested: Did the plan account for sample-rate conversion filter latency?
- Invalidation risk: Without latency compensation, multi-track audio will drift out of sync.
- Revised Judgment: REVISE

PROMPT-COMPLIANCE:
- Requirement: Zero-copy muxer -> Plan Handling: Covered in step 1 -> Status: SATISFIED
- Requirement: Sample-rate conversion -> Plan Handling: Covered in step 2 -> Status: SATISFIED
- Requirement: Multi-track sync -> Plan Handling: Latency compensation omitted -> Status: UNSATISFIED

VERDICT: REVISE
SEVERITY: MAJOR

FINDINGS:
- Issue: Missing filter latency compensation causes multi-track drift
  Evidence: Plan step 2 does not buffer delay compensation

REQUIRED REVISIONS:
- Add multi-track latency delay line step to plan`;

		const parsed = parseCriticalReviewResponse(rawResponse);
		assert.strictEqual(parsed.selfCritique?.initialJudgment, "APPROVE");
		assert.strictEqual(parsed.selfCritique?.revisedJudgment, "REVISE");
		assert.strictEqual(parsed.verdict, "REVISE");
		assert.strictEqual(parsed.severity, "MAJOR");
		assert.strictEqual(parsed.findings.length, 1);
		assert.ok(parsed.findings[0].issue.includes("Missing filter latency compensation"));
		assert.strictEqual(parsed.requiredActions.length, 1);
		assert.ok(parsed.requiredActions[0].includes("delay line step"));
	});

	// -----------------------------------------------------------------------
	// 4. Reviewer approval allows planning phase to complete
	// -----------------------------------------------------------------------
	await t.step("4. reviewer approval allows planning phase to complete", async () => {
		const { mockPi, setAllTools } = createMockExtensionAPI();
		const ctx = createMockContext(50000, "session_plan_4");
		plugin(mockPi);
		setAllTools([{ name: "subagent" }]);

		const s = getState(ctx);
		const slug = "approved-muxer-plan";
		s.active = slug;
		s.questId = slug;
		s.stack = [slug];
		s.prompts = ["Implement zero-copy audio stream muxer with sample-rate conversion and delay compensation."];

		const qPath = `${currentDir}/${slug}/quest.md`;
		await mkdir(`${currentDir}/${slug}`, { recursive: true });
		await writeFile(qPath, `# Quest: ${slug}\n\n## Goal\nAudio muxer\n\n## Original request\n> Implement zero-copy audio stream muxer with sample-rate conversion and delay compensation.\n\n## Plan\n1. Memory pool setup\n2. SRC filter with delay line\n3. Mux loop\n\n## Remaining work\n- [ ] Task 1\n`, "utf8");

		const approveRunner = async () => `PASS 1 (Independent Evaluation):
Provisional Judgment: APPROVE
Provisional Summary: Plan satisfies all requirements.

PASS 2 (Self-Attack & Falsification):
- Assumptions tested: Memory alignment verified against libxylem specs.
- Invalidation risk: None found.
- Revised Judgment: APPROVE

PROMPT-COMPLIANCE:
- Requirement: Zero-copy muxer -> Plan Handling: Addressed in step 1 -> Status: SATISFIED
- Requirement: Sample-rate conversion -> Plan Handling: Addressed in step 2 -> Status: SATISFIED
- Requirement: Delay compensation -> Plan Handling: Addressed in step 2 -> Status: SATISFIED

VERDICT: APPROVE
SEVERITY: NONE

FINDINGS:
- None

REQUIRED REVISIONS:
- None`;
		setCustomSubagentRunner(approveRunner);

		await executeUpdateStateTool({
			name: slug,
			plan: ["1. Memory pool setup", "2. SRC filter with delay line", "3. Mux loop"],
			planConfidence: "high",
			planConfidenceReason: "Verified alignment and delay calculations.",
			researchComplete: true,
		}, mockPi, ctx);

		assert.strictEqual(s.researchComplete, true, "researchComplete must be true after APPROVE");
		assert.strictEqual(s.researchRequired, false);
		assert.strictEqual(isPlanReviewValidForState(s), true, "Plan review approval must be valid");
		assert.strictEqual(s.lastPlanReviewApproval?.planVersion, 1);
		assert.ok(s.lastPlanReviewApproval?.reviewId);

		// Awaiting confirmation for root quest
		assert.strictEqual(s.awaitingUserConfirmation, true);

		// Accept user confirmation
		await acceptRootConfirmation(mockPi, ctx);
		assert.strictEqual(canImplement(s, ctx), true, "Implementation gate must open after plan approval + confirmation");

		setCustomSubagentRunner(null);
	});

	// -----------------------------------------------------------------------
	// 5. Main-agent revision loop (P1 REVISE -> P2 APPROVE)
	// -----------------------------------------------------------------------
	await t.step("5. main-agent revision loop (P1 REVISE -> P2 APPROVE)", async () => {
		const { mockPi, setAllTools, agentMessages } = createMockExtensionAPI();
		const ctx = createMockContext(50000, "session_plan_5");
		plugin(mockPi);
		setAllTools([{ name: "subagent" }]);

		const s = getState(ctx);
		const slug = "revision-loop-quest";
		s.active = slug;
		s.questId = slug;
		s.stack = [slug];
		s.prompts = ["Implement HTTP/2 multiplexed stream parser with flow control."];

		const qPath = `${currentDir}/${slug}/quest.md`;
		await mkdir(`${currentDir}/${slug}`, { recursive: true });
		await writeFile(qPath, `# Quest: ${slug}\n\n## Goal\nHTTP/2 parser\n\n## Original request\n> Implement HTTP/2 multiplexed stream parser with flow control.\n\n## Plan\n1. Frame parser\n\n## Remaining work\n- [ ] Task 1\n`, "utf8");

		let callCount = 0;
		const dynamicRunner = async (_task: string) => {
			callCount++;
			if (callCount === 1) {
				// Draft 1 rejected
				return `PASS 1:\nProvisional Judgment: REVISE\nPASS 2:\n- Revised Judgment: REVISE\nPROMPT-COMPLIANCE:\n- Requirement: Flow control -> Plan Handling: Missing -> Status: UNSATISFIED\nVERDICT: REVISE\nSEVERITY: CRITICAL\nFINDINGS:\n- Issue: Flow control missing from plan\n  Evidence: Step 1 lacks WINDOW_UPDATE handling\nREQUIRED REVISIONS:\n- Add WINDOW_UPDATE flow control state machine`;
			}
			// Draft 2 approved
			return `PASS 1:\nProvisional Judgment: APPROVE\nPASS 2:\n- Revised Judgment: APPROVE\nPROMPT-COMPLIANCE:\n- Requirement: Flow control -> Plan Handling: WINDOW_UPDATE added in step 2 -> Status: SATISFIED\nVERDICT: APPROVE\nSEVERITY: NONE\nFINDINGS:\n- None\nREQUIRED REVISIONS:\n- None`;
		};
		setCustomSubagentRunner(dynamicRunner);

		// Draft 1
		await executeUpdateStateTool({
			name: slug,
			plan: ["1. Frame parser"],
			planVersion: 1,
			researchComplete: true,
		}, mockPi, ctx);

		assert.strictEqual(callCount, 1);
		assert.strictEqual(s.researchComplete, false);
		assert.strictEqual(isPlanReviewValidForState(s), false);

		// Verify steer message was delivered to main agent
		const steerMsg = agentMessages.find((m) => m.msg?.includes("ADVERSARIAL PLAN REVIEW REJECTED"));
		assert.ok(steerMsg, "Steer message must be delivered to main agent with findings");
		assert.ok(steerMsg.msg.includes("Flow control missing from plan"));
		assert.ok(steerMsg.msg.includes("Add WINDOW_UPDATE flow control state machine"));

		// Draft 2 addressing findings
		await executeUpdateStateTool({
			name: slug,
			plan: ["1. Frame parser", "2. WINDOW_UPDATE flow control state machine"],
			planRevisions: ["Added flow control state machine per reviewer findings"],
			planVersion: 2,
			researchComplete: true,
		}, mockPi, ctx);

		assert.strictEqual(callCount, 2, "Second review must execute for revised plan version");
		assert.strictEqual(s.researchComplete, true, "Plan version 2 must be approved");
		assert.strictEqual(isPlanReviewValidForState(s), true);
		assert.strictEqual(s.lastPlanReviewApproval?.planVersion, 2);

		setCustomSubagentRunner(null);
	});

	// -----------------------------------------------------------------------
	// 6. Material plan change after approval invalidates previous approval
	// -----------------------------------------------------------------------
	await t.step("6. material plan change after approval invalidates previous approval", async () => {
		const s: StoredState = {
			active: "invalidate-plan-quest",
			questId: "invalidate-plan-quest",
			saveCount: 1,
			compactCount: 0,
			prompts: ["Test prompt"],
			stack: ["invalidate-plan-quest"],
			dirty: false,
			planVersion: 1,
			lastSavedHash: "hash_plan_v1",
			lastPlanReviewApproval: {
				questId: "invalidate-plan-quest",
				planVersion: 1,
				reviewId: "rev_app_1",
				saveHash: "hash_plan_v1",
				saveCount: 1,
				timestamp: Date.now(),
			},
		};

		assert.strictEqual(isPlanReviewValidForState(s), true, "Approval must be valid before modification");

		// Change 1: Plan version increments
		s.planVersion = 2;
		assert.strictEqual(isPlanReviewValidForState(s), false, "Plan version mismatch must invalidate approval");
		s.planVersion = 1;

		// Change 2: Save hash mismatch
		s.lastSavedHash = "hash_plan_v2";
		assert.strictEqual(isPlanReviewValidForState(s), false, "Save hash mismatch must invalidate approval");
		s.lastSavedHash = "hash_plan_v1";

		// Change 3: Dirty state
		s.dirty = true;
		assert.strictEqual(isPlanReviewValidForState(s), false, "Dirty working state must invalidate approval");
	});

	// -----------------------------------------------------------------------
	// 7. Reviewer error, timeout, or UNCERTAIN does not become approval
	// -----------------------------------------------------------------------
	await t.step("7. reviewer error, timeout, or UNCERTAIN does not become approval", async () => {
		const { mockPi, setAllTools } = createMockExtensionAPI();
		const ctx = createMockContext(50000, "session_plan_7");
		plugin(mockPi);
		setAllTools([{ name: "subagent" }]);

		const s = getState(ctx);
		const slug = "error-uncertain-quest";
		s.active = slug;
		s.questId = slug;
		s.stack = [slug];
		s.prompts = ["Build component X"];

		const qPath = `${currentDir}/${slug}/quest.md`;
		await mkdir(`${currentDir}/${slug}`, { recursive: true });
		await writeFile(qPath, `# Quest: ${slug}\n\n## Goal\nX\n\n## Original request\n> Build component X\n\n## Plan\n1. X\n\n## Remaining work\n- [ ] 1\n`, "utf8");

		// Case A: Reviewer throws error
		const errorRunner = async () => { throw new Error("Subagent execution timeout"); };
		setCustomSubagentRunner(errorRunner);

		const errRes = await requestPlanReview(mockPi, ctx, slug);
		assert.strictEqual(errRes.success, false);
		assert.strictEqual(s.researchComplete, false);
		assert.strictEqual(isPlanReviewValidForState(s), false);

		// Case B: Reviewer returns UNCERTAIN
		const uncertainRunner = async () => `PASS 1:\nProvisional Judgment: UNCERTAIN\nPASS 2:\n- Revised Judgment: UNCERTAIN\nPROMPT-COMPLIANCE:\n- Requirement: X -> Plan Handling: Unclear -> Status: UNCERTAIN\nVERDICT: UNCERTAIN\nSEVERITY: MAJOR\nFINDINGS:\n- Issue: Unknown dependency\n  Evidence: Missing header\nREQUIRED REVISIONS:\n- Verify header exists`;
		setCustomSubagentRunner(uncertainRunner);

		const uncRes = await requestPlanReview(mockPi, ctx, slug);
		assert.strictEqual(uncRes.success, false);
		assert.strictEqual(s.researchComplete, false);
		assert.strictEqual(isPlanReviewValidForState(s), false);

		setCustomSubagentRunner(null);
	});

	// -----------------------------------------------------------------------
	// 8. Repeated rejections hit loop bound without automatic approval
	// -----------------------------------------------------------------------
	await t.step("8. repeated rejections hit loop bound without automatic approval", async () => {
		const { mockPi, setAllTools } = createMockExtensionAPI();
		const ctx = createMockContext(50000, "session_plan_8");
		plugin(mockPi);
		setAllTools([{ name: "subagent" }]);

		const s = getState(ctx);
		const slug = "loop-bound-quest";
		s.active = slug;
		s.questId = slug;
		s.stack = [slug];
		s.planVersion = 1;
		s.lastSavedHash = "hash_fixed";
		s.prompts = ["Build component Y"];

		const qPath = `${currentDir}/${slug}/quest.md`;
		await mkdir(`${currentDir}/${slug}`, { recursive: true });
		await writeFile(qPath, `# Quest: ${slug}\n\n## Goal\nY\n\n## Original request\n> Build component Y\n\n## Plan\n1. Y\n\n## Remaining work\n- [ ] 1\n`, "utf8");

		const rejectRunner = async () => `VERDICT: REVISE\nSEVERITY: CRITICAL\nFINDINGS:\n- Persistent flaw\nREQUIRED REVISIONS:\n- Fix`;
		setCustomSubagentRunner(rejectRunner);

		// 3 attempts
		await requestPlanReview(mockPi, ctx, slug);
		await requestPlanReview(mockPi, ctx, slug);
		await requestPlanReview(mockPi, ctx, slug);

		// 4th attempt exceeds bound
		const boundRes = await requestPlanReview(mockPi, ctx, slug);
		assert.strictEqual(boundRes.success, false);
		assert.ok(boundRes.error?.includes("bound"));
		assert.strictEqual(isPlanReviewValidForState(s), false, "Bound limit must NEVER convert to approval");
		assert.strictEqual(canImplement(s, ctx), false, "Implementation gate must remain blocked");

		setCustomSubagentRunner(null);
	});

	// -----------------------------------------------------------------------
	// 9. Plan review events are recorded in execution log
	// -----------------------------------------------------------------------
	await t.step("9. plan review events are recorded in execution log", async () => {
		const { mockPi, setAllTools } = createMockExtensionAPI();
		const ctx = createMockContext(50000, "session_plan_9");
		plugin(mockPi);
		setAllTools([{ name: "subagent" }]);

		const s = getState(ctx);
		const slug = "plan-log-quest";
		s.active = slug;
		s.questId = slug;
		s.stack = [slug];
		s.prompts = ["Build logged feature"];

		const qPath = `${currentDir}/${slug}/quest.md`;
		await mkdir(`${currentDir}/${slug}`, { recursive: true });
		await writeFile(qPath, `# Quest: ${slug}\n\n## Goal\nLogged\n\n## Original request\n> Build logged feature\n\n## Plan\n1. Step 1\n\n## Remaining work\n- [ ] 1\n`, "utf8");

		const passRunner = async () => `PASS 1:\nProvisional Judgment: APPROVE\nPASS 2:\n- Revised Judgment: APPROVE\nPROMPT-COMPLIANCE:\n- Requirement: Logged feature -> Status: SATISFIED\nVERDICT: APPROVE\nSEVERITY: NONE\nFINDINGS:\n- None\nREQUIRED REVISIONS:\n- None`;
		setCustomSubagentRunner(passRunner);

		await requestPlanReview(mockPi, ctx, slug);

		const log = readQuestLog(getQuestLogPath(slug));
		assert.ok(log.includes("PLAN_REVIEW_REQUESTED"), "Must log PLAN_REVIEW_REQUESTED");
		assert.ok(log.includes("PLAN_REVIEW_STARTED"), "Must log PLAN_REVIEW_STARTED");
		assert.ok(log.includes("PLAN_REVIEW_APPROVED"), "Must log PLAN_REVIEW_APPROVED");

		setCustomSubagentRunner(null);
	});

	// -----------------------------------------------------------------------
	// 10. When subagent capability is genuinely not registered, normal Quest Journal behavior is preserved
	// -----------------------------------------------------------------------
	await t.step("10. when subagent capability is genuinely not registered, normal Quest Journal behavior is preserved", async () => {
		const { mockPi } = createMockExtensionAPI();
		const ctx = createMockContext(50000, "session_plan_10");
		plugin(mockPi);
		setCustomSubagentRunner(null);

		assert.strictEqual(isSubagentToolRegistered(mockPi, ctx), false);
		assert.strictEqual(isSubagentAvailable(mockPi, ctx), false);

		const s = getState(ctx);
		const slug = "unregistered-subagent-quest";
		s.active = slug;
		s.questId = slug;
		s.stack = [slug];
		s.dirty = false;
		s.prompts = ["Build simple utility"];

		const qPath = `${currentDir}/${slug}/quest.md`;
		await mkdir(`${currentDir}/${slug}`, { recursive: true });
		await writeFile(qPath, `# Quest: ${slug}\n\n## Goal\nUtility\n\n## Original request\n> Build simple utility\n\n## Plan\n1. Step 1\n\n## Remaining work\n- [ ] 1\n`, "utf8");

		// With subagent tool unregistered, review is gracefully skipped
		const reviewRes = await requestPlanReview(mockPi, ctx, slug);
		assert.strictEqual(reviewRes.available, false);
		assert.strictEqual(reviewRes.skipped, true);
		assert.strictEqual(reviewRes.success, true);
	});

	// -----------------------------------------------------------------------
	// 10b. Draft branch with no registered reviewer returns null AND emits audit
	// log (covers #21: silent return null). The !registered early-return in the
	// draft shard must log REVIEW_DEDUP_HIT + CRITICAL_REVIEW_SUPPRESSED_DUPLICATE
	// -----------------------------------------------------------------------
	await t.step("10b. checkAndTriggerPlanReview draft !registered returns null and logs REVIEW_DEDUP_HIT", async () => {
		const { mockPi } = createMockExtensionAPI();
		const ctx = createMockContext(50000, "session_plan_10b");
		plugin(mockPi);
		setCustomSubagentRunner(null);

		const s = getState(ctx);
		const slug = "draft-unregistered-audit";
		const qid = `qid_10b_${slug}`;
		s.questId = qid;
		s.activeDraft = slug;
		s.active = "";
		s.stack = [];
		s.draftPrompts = ["prompt one"];

		// checkAndTriggerPlanReview reads the future draft to compute the hash
		const futureDir = ".pi/quest/future";
		await mkdir(futureDir, { recursive: true });
		await writeFile(`${futureDir}/${slug}.md`, `# Draft: ${slug}\n\n## Requirements\n- prompt one\n`, "utf8");

		// Ensure the quest log dir exists so logEvent writes to a real file (path derives from s.questId)
		await mkdir(`${currentDir}/${qid}`, { recursive: true });
		const logPath = getQuestLogPath(qid, currentDir);
		await rm(logPath, { force: true });

		// No reviewer registered -> draft !registered branch returns null.
		// Wrap in asyncContext.run so the bare logEvent calls (which resolve the
		// log path from getActiveContext()) route to this session's questId,
		// matching how the framework invokes handlers via withContext (context.ts).
		const result = await asyncContext.run(ctx, () => checkAndTriggerPlanReview(mockPi, ctx));
		assert.strictEqual(result, null, "must return null when no reviewer is registered");

		// The previously-silent return must now emit structured audit events (#21)
		const logContent = await readFile(logPath, "utf8");
		assert.ok(logContent.includes("REVIEW_DEDUP_HIT"), "must log REVIEW_DEDUP_HIT for draft !registered");
		assert.ok(logContent.includes("reason=not_registered"), "REVIEW_DEDUP_HIT must carry reason=not_registered");
		assert.ok(logContent.includes("shard=draft"), "REVIEW_DEDUP_HIT must carry shard=draft");
		assert.ok(logContent.includes("CRITICAL_REVIEW_SUPPRESSED_DUPLICATE"), "must log CRITICAL_REVIEW_SUPPRESSED_DUPLICATE");
	});

	// -----------------------------------------------------------------------
	// 11. Edge-triggered plan review: 10 consecutive quest_update_state calls with unchanged plan
	// -----------------------------------------------------------------------
	await t.step("11. edge-triggered plan review: 10 consecutive quest_update_state calls invoke reviewer exactly once", async () => {
		const { mockPi, setAllTools } = createMockExtensionAPI();
		const ctx = createMockContext(50000, "session_plan_11");
		plugin(mockPi);
		setAllTools([{ name: "subagent" }]);
		clearActiveReviews();

		const s = getState(ctx);
		const slug = "edge-triggered-10-updates-quest";
		s.active = slug;
		s.questId = slug;
		s.stack = [slug];
		s.prompts = ["Implement edge-triggered plan review scheduler."];

		const qPath = `${currentDir}/${slug}/quest.md`;
		await mkdir(`${currentDir}/${slug}`, { recursive: true });
		await writeFile(qPath, `# Quest: ${slug}\n\n## Goal\nEdge triggered scheduler\n\n## Original request\n> Implement edge-triggered plan review scheduler.\n\n## Remaining work\n- [ ] Task 1\n`, "utf8");

		let reviewerInvocations = 0;
		const passRunner = async () => {
			reviewerInvocations++;
			return `PASS 1 (Independent Evaluation):
Provisional Judgment: APPROVE
Provisional Summary: Plan is solid.

PASS 2 (Self-Attack & Falsification):
- Assumptions tested: Is edge trigger sufficient?
- Evidence evaluated: Yes.
- Invalidation risk: None.
- Revised Judgment: APPROVE

PROMPT-COMPLIANCE:
- Requirement: Edge-triggered scheduler -> Status: SATISFIED

VERDICT: APPROVE
SEVERITY: NONE

FINDINGS:
- None

REQUIRED REVISIONS:
- None`;
		};
		setCustomSubagentRunner(passRunner);

		// Call 1: Initial plan draft and research completion
		await executeUpdateStateTool({
			name: slug,
			plan: ["1. Step one", "2. Step two", "3. Step three"],
			planConfidence: "high",
			planConfidenceReason: "Tested and verified assumptions",
			researchComplete: true,
			status: "Initial plan submitted",
		}, mockPi, ctx);

		assert.strictEqual(reviewerInvocations, 1, "Reviewer must be invoked exactly once on initial actionable plan");
		assert.strictEqual(s.researchComplete, true, "Plan should be approved");
		assert.strictEqual(isPlanReviewValidForState(s), true);
		assert.strictEqual(getPendingReviews().size, 0, "No pending reviews should exist after completion");

		// Calls 2 through 10: Pure cognitive and non-plan metadata updates with unchanged plan
		const cognitiveUpdates = [
			{ understanding: "Discovered module X boundaries", status: "Understanding updated" },
			{ assumptions: ["Assumption A verified", "Assumption B verified"], status: "Assumptions updated" },
			{ findings: ["Finding 1: edge trigger is clean", "Finding 2: no extra requests"], status: "Findings logged" },
			{ nextAction: "Begin step 1", exactNextAction: "Begin step 1", status: "Next action updated" },
			{ status: "In progress on step 1", inProgress: ["Step 1 implementation"] },
			{ filesExamined: ["src/tools/update_operation.ts", "src/critical_agent/policy.ts"], status: "Examined files" },
			{ completed: ["Step 1 implementation"], inProgress: ["Step 2 tests"], status: "Step 1 done" },
			{ plan: ["1. Step one", "2. Step two", "3. Step three"], status: "Resent identical plan array" },
			{ plan: "1. Step one\n2. Step two\n3. Step three", status: "Resent identical plan text", nextAction: "Finalizing" },
		];

		for (let i = 0; i < cognitiveUpdates.length; i++) {
			const update = cognitiveUpdates[i];
			const res = await executeUpdateStateTool({
				name: slug,
				...update,
			}, mockPi, ctx);

			assert.strictEqual(res.details?.error, undefined, `Update ${i + 2} must succeed without error`);
			assert.strictEqual(reviewerInvocations, 1, `Update ${i + 2} (${update.status}) must NOT trigger a new reviewer invocation`);
			assert.strictEqual(getPendingReviews().size, 0, `Update ${i + 2} must NOT queue any pending review requests`);
			assert.strictEqual(getActiveReviews().size, 0, `Update ${i + 2} must NOT start any repeated active reviews`);
		}

		assert.strictEqual(reviewerInvocations, 1, "Total reviewer invocations across all 10 calls must be exactly 1");
		assert.strictEqual(isPlanReviewValidForState(s), true, "Plan approval must remain valid across non-plan cognitive updates");

		setCustomSubagentRunner(null);
	});

	// -----------------------------------------------------------------------
	// 12. In-flight review coalescence: non-plan updates do not queue, material change queues exactly 1 pending review
	// -----------------------------------------------------------------------
	await t.step("12. in-flight review coalescence: non-plan updates do not queue, material change queues exactly 1 pending review", async () => {
		const { mockPi, setAllTools } = createMockExtensionAPI();
		const ctx = createMockContext(50000, "session_plan_12");
		plugin(mockPi);
		setAllTools([{ name: "subagent" }]);
		clearActiveReviews();

		const s = getState(ctx);
		const slug = "coalesce-in-flight-quest";
		s.active = slug;
		s.questId = slug;
		s.stack = [slug];
		s.prompts = ["Implement asynchronous audio router."];

		const qPath = `${currentDir}/${slug}/quest.md`;
		await mkdir(`${currentDir}/${slug}`, { recursive: true });
		await writeFile(qPath, `# Quest: ${slug}\n\n## Goal\nAudio router\n\n## Original request\n> Implement asynchronous audio router.\n\n## Remaining work\n- [ ] Task 1\n`, "utf8");

		let reviewerCallCount = 0;
		let resolveV1Review!: (text: string) => void;
		const v1Promise = new Promise<string>((res) => {
			resolveV1Review = res;
		});

		const customRunner = async (task: string, options?: any) => {
			reviewerCallCount++;
			if (reviewerCallCount === 1) {
				// First review hangs until we manually resolve it
				return await v1Promise;
			}
			// Second review (V2) resolves immediately with APPROVE
			return `PASS 1:\nProvisional Judgment: APPROVE\nPASS 2:\n- Revised Judgment: APPROVE\nPROMPT-COMPLIANCE:\n- Requirement: Audio router V2 -> Status: SATISFIED\nVERDICT: APPROVE\nSEVERITY: NONE\nFINDINGS:\n- None\nREQUIRED REVISIONS:\n- None`;
		};
		setCustomSubagentRunner(customRunner);

		// 1. Trigger V1 review (runs asynchronously / in-flight)
		const v1UpdatePromise = executeUpdateStateTool({
			name: slug,
			plan: ["1. Basic audio routing"],
			planConfidence: "high",
			planConfidenceReason: "Initial plan",
			researchComplete: true,
		}, mockPi, ctx);

		// Yield to allow V1 review promise to register and become active
		for (let i = 0; i < 50 && reviewerCallCount === 0; i++) {
			await new Promise((r) => setTimeout(r, 10));
		}

		assert.strictEqual(reviewerCallCount, 1, "V1 review must be started");
		assert.strictEqual(getActiveReviews().size, 1, "Exactly one review must be active");
		assert.strictEqual(getPendingReviews().size, 0, "No pending reviews should exist yet");

		// 2. Perform several non-plan cognitive updates while V1 review is in-flight
		await executeUpdateStateTool({
			name: slug,
			understanding: "Deeper understanding of PCM buffer formats",
			status: "Researching formats",
		}, mockPi, ctx);

		await executeUpdateStateTool({
			name: slug,
			findings: ["Sample rate conversion needed"],
			status: "Documenting findings",
		}, mockPi, ctx);

		await executeUpdateStateTool({
			name: slug,
			nextAction: "Refine audio filter graph",
			status: "Next action planned",
		}, mockPi, ctx);

		// Verify non-plan updates did NOT queue any pending review
		assert.strictEqual(reviewerCallCount, 1, "Non-plan updates must NOT start another reviewer");
		assert.strictEqual(getPendingReviews().size, 0, "Non-plan updates must NOT queue any pending review");

		// 3. Materially change the plan to V2
		await executeUpdateStateTool({
			name: slug,
			plan: ["1. Basic audio routing", "2. Multi-channel mixer", "3. Sample-rate conversion delay compensation"],
			planRevisions: ["Added mixer and SRC delay compensation"],
			planVersion: 2,
			planConfidence: "high",
			planConfidenceReason: "Revised architecture",
			status: "Materially revised plan to V2",
		}, mockPi, ctx);

		// Verify exactly 1 pending review is now queued for V2
		assert.strictEqual(reviewerCallCount, 1, "V2 review must not launch immediately while V1 is in-flight");
		assert.strictEqual(getPendingReviews().size, 1, "Exactly one pending review request must be queued for V2");
		const pending = getPendingReviews().get(slug);
		assert.strictEqual(pending?.planVersion, 2, "Pending review must be for plan version 2");

		// 4. Perform another non-plan update while V1 is still in-flight
		await executeUpdateStateTool({
			name: slug,
			status: "Waiting for V1 review to complete",
			nextAction: "Await V2 review",
		}, mockPi, ctx);

		assert.strictEqual(getPendingReviews().size, 1, "Additional non-plan update must NOT duplicate or alter pending review");

		// 5. Complete V1 review (resolving with APPROVE for V1)
		resolveV1Review(`PASS 1:\nProvisional Judgment: APPROVE\nPASS 2:\n- Revised Judgment: APPROVE\nPROMPT-COMPLIANCE:\n- Requirement: Audio router V1 -> Status: SATISFIED\nVERDICT: APPROVE\nSEVERITY: NONE\nFINDINGS:\n- None\nREQUIRED REVISIONS:\n- None`);

		await v1UpdatePromise;

		// Yield to allow finally block setTimeout(..., 0) to execute the queued V2 review
		await new Promise((r) => setTimeout(r, 50));

		// Verify V2 review was launched as a follow-up
		assert.strictEqual(reviewerCallCount, 2, "Exactly one V2 follow-up review must be started after V1 completes");

		setCustomSubagentRunner(null);
	});

	// Clean up
	await rm(currentDir, { recursive: true, force: true });
});
