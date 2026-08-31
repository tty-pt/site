import assert from "node:assert";
import { mkdir, rm, writeFile } from "node:fs/promises";
import plugin, { canImplement, getState, type StoredState } from "../index.ts";
import { questPath } from "../src/paths.ts";

function createMockExtensionAPI() {
	const handlers: Record<string, any[]> = {};
	const registeredTools: any[] = [];
	const registeredCommands: any[] = [];
	const userMessages: Array<{ msg: any; options?: any; customType?: any; display?: any }> = [];

	const mockPi = {
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
			userMessages.push({
				msg: msg?.content || msg,
				options,
				customType: msg?.customType,
				display: msg?.display,
			});
		},
		sendUserMessage: (msg: any, options?: any) => {
			userMessages.push({ msg, options });
		},
		registerEntryRenderer: () => {},
	};

	return { mockPi, handlers, registeredTools, registeredCommands, userMessages };
}

function createMockContext(tokens = 50000, sessionId = `session_${Math.random().toString(36).slice(2)}`) {
	const branch: any[] = [];
	let compactInvocationCount = 0;
	let lastCompactOptions: any = null;

	const ctx = {
		mode: "agent",
		hasUI: true,
		sessionManager: {
			id: sessionId,
			getBranch: () => branch,
			appendCustomEntry: (_type: string, data: any) => {
				branch.push({ type: "custom", customType: "quest_journal", data });
			},
		},
		getContextUsage: () => ({ tokens, percent: (tokens / 800000) * 100 }),
		setTokens: (t: number) => {
			tokens = t;
		},
		ui: {
			notify: () => {},
			setStatus: () => {},
			input: async () => "",
			select: async () => "",
		},
		compact: (options: any) => {
			compactInvocationCount++;
			lastCompactOptions = options;
		},
		getCompactInvocationCount: () => compactInvocationCount,
		getLastCompactOptions: () => lastCompactOptions,
	};
	return ctx;
}

Deno.test("quest_journal_subquest_launch_compaction_resume: subquest launch, compaction, and child resumption lifecycle", async (t) => {
	const QUEST_DIR = ".pi/quest/current";
	await mkdir(QUEST_DIR, { recursive: true });

	// -----------------------------------------------------------------------
	// 1. Subquest launch under high context triggers launch compaction and resumes child
	// -----------------------------------------------------------------------
	await t.step("1. Subquest launch with launch compaction consumes pendingSubquestResume and resumes child", async () => {
		const { mockPi, handlers, registeredTools, registeredCommands, userMessages } = createMockExtensionAPI();
		plugin(mockPi as any);

		const mockCtx = createMockContext(75000, "session_launch_compaction_1");
		const tools: Record<string, any> = {};
		for (const tool of registeredTools) tools[tool.name] = tool;
		const commands: Record<string, any> = {};
		for (const cmd of registeredCommands) commands[cmd.name] = cmd;

		const parentSlug = "parent-launch-quest";
		const childSlug = "child-launch-subquest";

		// 1. Parent quest is active
		await commands["quest"].handler(parentSlug, mockCtx);
		const parentPath = questPath(getState(mockCtx as any).questId);

		const preState = getState(mockCtx as any);
		assert.strictEqual(preState.active, parentSlug, "Parent quest must be active initially");

		// 2. Current tokens >= subquest launch threshold (75,000 >= 60,000)
		userMessages.length = 0;

		// 3. quest_subquest({ name: childSlug, goal: "...", switchNow: true })
		await tools["quest_subquest"].execute(
			"call_subquest_launch",
			{
				name: childSlug,
				goal: "Investigate separate caching subsystem",
				switchNow: true,
			},
			null,
			null,
			mockCtx,
		);

		const stateAfterLaunch = getState(mockCtx as any);
		const childPath = questPath(stateAfterLaunch.questId);
		// 4. Assert state.active === "child"
		assert.strictEqual(stateAfterLaunch.active, childSlug, "Child must be active immediately after launch");
		// 5. Assert state.pendingSubquestResume === "child"
		assert.strictEqual(stateAfterLaunch.pendingSubquestResume, childSlug, "pendingSubquestResume must record the child slug");
		// 6. Assert state.subquestLaunchCompactionPending === true
		assert.strictEqual(stateAfterLaunch.subquestLaunchCompactionPending, true, "subquestLaunchCompactionPending must be set");

		// 7. Trigger deferred compaction via turn_end
		for (const cb of handlers["turn_end"] || []) {
			await cb({ toolResults: [{ toolName: "quest_subquest" }] }, mockCtx);
		}

		await new Promise((resolve) => setTimeout(resolve, 80));
		assert.strictEqual(mockCtx.getCompactInvocationCount(), 1, "Compaction must be scheduled via ctx.compact");
		const compactOpts = mockCtx.getLastCompactOptions();
		assert.ok(compactOpts?.customInstructions?.includes(childSlug), "Compaction instructions must target the child");

		// 8. Simulate successful session_compact
		userMessages.length = 0;
		for (const cb of handlers["session_compact"] || []) {
			await cb({}, mockCtx);
		}

		const stateAfterCompact = getState(mockCtx as any);
		// 9. Assert pendingSubquestResume is consumed
		assert.strictEqual(stateAfterCompact.pendingSubquestResume, null, "pendingSubquestResume must be consumed (null)");
		assert.strictEqual(stateAfterCompact.subquestLaunchCompactionPending, false, "subquestLaunchCompactionPending must be cleared");

		// 10. Assert exactly one child continuation is emitted
		assert.strictEqual(userMessages.length, 1, "Exactly one resume directive must be emitted upon compaction completion");
		const resumeEntry = userMessages[0];
		const resumeText = typeof resumeEntry.msg === "string" ? resumeEntry.msg : (resumeEntry.msg?.text || "");

		// 11. Assert its target is "child" and contains required invariants
		assert.ok(resumeText.includes(`sub-quest \`${childSlug}\``) || resumeText.includes(`sub-quest **${childSlug}**`), "Continuation must target child slug");
		assert.ok(resumeText.includes(childPath), "Continuation must reference child quest file");
		assert.ok(resumeText.includes("Independently verify inherited context"), "Must include inherited context verification invariant");
		assert.ok(resumeText.includes("Perform the child's required research"), "Must include research requirement invariant");
		assert.ok(resumeText.includes("Once the child research gate is satisfied, continue autonomously"), "Must include autonomous continuation invariant");
		assert.ok(resumeText.includes("Do not return to the parent until this sub-quest is actually complete"), "Must include parent return invariant");

		// 12. Assert deliverAs === "followUp"
		assert.strictEqual(resumeEntry.options?.deliverAs, "followUp", "Resumption directive must use deliverAs: 'followUp'");

		// 13. Assert parent is NOT resumed at this point
		assert.ok(!resumeText.includes(`You are working on active quest **${parentSlug}**`), "Parent quest must not be resumed");

		// Cleanup
		await rm(parentPath, { force: true });
	});

	// -----------------------------------------------------------------------
	// 2. Subquest launch without compaction (below threshold)
	// -----------------------------------------------------------------------
	await t.step("2. Subquest launch below compaction threshold sends immediate entry directive without pending state", async () => {
		const { mockPi, registeredTools, registeredCommands, userMessages } = createMockExtensionAPI();
		plugin(mockPi as any);

		const mockCtx = createMockContext(30000, "session_launch_nocompact_2");
		const tools: Record<string, any> = {};
		for (const tool of registeredTools) tools[tool.name] = tool;
		const commands: Record<string, any> = {};
		for (const cmd of registeredCommands) commands[cmd.name] = cmd;

		const parentSlug = "parent-nocompact-quest";
		const childSlug = "child-nocompact-subquest";

		await commands["quest"].handler(parentSlug, mockCtx);
		const parentPath = questPath(getState(mockCtx as any).questId);

		userMessages.length = 0;

		await tools["quest_subquest"].execute(
			"call_sub_nocompact",
			{
				name: childSlug,
				goal: "Investigate child task without compaction",
				switchNow: true,
			},
			null,
			null,
			mockCtx,
		);

		const state = getState(mockCtx as any);
		assert.strictEqual(state.active, childSlug, "Child must be active");
		assert.strictEqual(state.pendingSubquestResume, null, "pendingSubquestResume must remain null when no compaction is needed");
		assert.strictEqual(state.subquestLaunchCompactionPending, false, "subquestLaunchCompactionPending must be false");

		// Entry directive is sent immediately
		assert.strictEqual(userMessages.length, 1, "Child entry directive must be sent immediately");
		const entry = userMessages[0];
		const text = typeof entry.msg === "string" ? entry.msg : (entry.msg?.text || "");
		assert.ok(text.includes(`sub-quest **${childSlug}**`), "Entry directive must target child");
		assert.strictEqual(entry.options?.deliverAs, "followUp", "Entry directive must use deliverAs: 'followUp'");

		// Cleanup
		await rm(parentPath, { force: true });
	});

	// -----------------------------------------------------------------------
	// 3. Subquest launch compaction failure fallback
	// -----------------------------------------------------------------------
	await t.step("3. Compaction failure fallback immediately resumes the pending child", async () => {
		const { mockPi, handlers, registeredTools, registeredCommands, userMessages } = createMockExtensionAPI();
		plugin(mockPi as any);

		const mockCtx = createMockContext(75000, "session_launch_fallback_3");
		const tools: Record<string, any> = {};
		for (const tool of registeredTools) tools[tool.name] = tool;
		const commands: Record<string, any> = {};
		for (const cmd of registeredCommands) commands[cmd.name] = cmd;

		const parentSlug = "parent-fallback-quest";
		const childSlug = "child-fallback-subquest";

		await commands["quest"].handler(parentSlug, mockCtx);
		const parentPath = questPath(getState(mockCtx as any).questId);

		// Launch child with launch compaction pending
		await tools["quest_subquest"].execute(
			"call_sub_fallback",
			{
				name: childSlug,
				goal: "Investigate child task with compaction error fallback",
				switchNow: true,
			},
			null,
			null,
			mockCtx,
		);

		assert.strictEqual(getState(mockCtx as any).pendingSubquestResume, childSlug);

		userMessages.length = 0;

		// Simulate compaction failure via session_compact_failed
		for (const cb of handlers["session_compact_failed"] || []) {
			await cb({}, mockCtx);
		}

		const stateAfterFailed = getState(mockCtx as any);
		assert.strictEqual(stateAfterFailed.pendingSubquestResume, null, "pendingSubquestResume must be consumed by fallback handler");
		assert.ok(userMessages.length >= 1, "Fallback handler must dispatch messages");
		const allTexts = userMessages.map((m) => typeof m.msg === "string" ? m.msg : (m.msg?.text || "")).join("\n\n");
		assert.ok(allTexts.includes("COMPACTION_FAILURE") || allTexts.includes("Session context compaction failed"), "Agent must receive COMPACTION_FAILURE");
		assert.ok(allTexts.includes(`sub-quest \`${childSlug}\``) || allTexts.includes(`sub-quest **${childSlug}**`) || allTexts.includes(childSlug), "Fallback continuation must target child");

		// Cleanup
		await rm(parentPath, { force: true });
	});

	// -----------------------------------------------------------------------
	// 4. Preserves child research gate post-compaction
	// -----------------------------------------------------------------------
	await t.step("4. Child remains gated by research after compaction until researchComplete is marked", async () => {
		const { mockPi, handlers, registeredTools, registeredCommands, userMessages } = createMockExtensionAPI();
		plugin(mockPi as any);

		const mockCtx = createMockContext(75000, "session_launch_gate_4");
		const tools: Record<string, any> = {};
		for (const tool of registeredTools) tools[tool.name] = tool;
		const commands: Record<string, any> = {};
		for (const cmd of registeredCommands) commands[cmd.name] = cmd;

		const parentSlug = "parent-gate-quest";
		const childSlug = "child-gate-subquest";

		await commands["quest"].handler(parentSlug, mockCtx);
		const parentPath = questPath(getState(mockCtx as any).questId);

		await tools["quest_subquest"].execute(
			"call_sub_gate",
			{
				name: childSlug,
				goal: "Investigate child task gating",
				switchNow: true,
			},
			null,
			null,
			mockCtx,
		);

		// Trigger launch compaction
		for (const cb of handlers["turn_end"] || []) {
			await cb({ toolResults: [{ toolName: "quest_subquest" }] }, mockCtx);
		}
		await new Promise((resolve) => setTimeout(resolve, 80));

		// Complete compaction
		for (const cb of handlers["session_compact"] || []) {
			await cb({}, mockCtx);
		}

		const state = getState(mockCtx as any);
		const childPath = questPath(state.questId);
		// Assert that child is NOT allowed to implement before research
		assert.strictEqual(canImplement(state), false, "Child implementation must remain blocked by research gate post-compaction");
		assert.strictEqual(state.researchComplete, false, "researchComplete must be false initially");

		// Now child performs research and marks researchComplete
		await writeFile(
			childPath,
			`# Quest: ${childSlug}\n\n## Goal\nInvestigate child task gating\n\n## Current Understanding\nDiscovered that subsystem requires module X.\n\n## Key Assumptions\n- Assumption 1: Module X is thread safe\n\n## Research Findings\n- Subsystem initializes cleanly.\n\n## Open Questions & Uncertainties\n- None at this stage.\n\n## Plan\n1. Modify module X\n2. Run tests\n\n## Plan Confidence\nHigh\n\n## Exact Next Action\nImplement module X change\n`,
			"utf8",
		);

		for (const cb of handlers["tool_call"] || []) {
			await cb({ toolName: "read", input: { path: "mods/child/module.c" } }, mockCtx);
		}
		for (const cb of handlers["tool_result"] || []) {
			await cb({ toolName: "read", input: { path: "mods/child/module.c" }, output: "module code", isError: false }, mockCtx);
		}

		await tools["quest_update_state"].execute(
			"call_update_child_research",
			{
				name: childSlug,
				researchComplete: true,
				planConfidence: "high",
				exactNextAction: "Implement module X change",
			},
			null,
			null,
			mockCtx,
		);

		const updatedState = getState(mockCtx as any);
		assert.strictEqual(updatedState.researchComplete, true, "researchComplete must now be true");
		// Child sub-quest is autonomous (no user confirmation required)
		assert.strictEqual(canImplement(updatedState), true, "Child implementation gate must now be open autonomously");

		// Cleanup
		await rm(parentPath, { force: true });
	});
});
