import assert from "node:assert";
import { mkdir, rm, writeFile } from "node:fs/promises";
import plugin, {
	classifyBashCommand,
	classifyToolCall,
	getState,
	reportAgentError,
	sendInternalAgentMessage,
	formatAgentErrorMessage,
	QuestErrorCode,
	type StoredState,
	type ToolPermission,
} from "../index.ts";

function createMockExtensionAPI() {
	const handlers: Record<string, any[]> = {};
	const registeredTools: any[] = [];
	const registeredCommands: any[] = [];
	const agentMessages: Array<{ msg: any; options?: any; customType?: any; display?: any }> = [];
	const userMessages: Array<{ msg: any; options?: any }> = [];
	const appendedEntries: Array<{ type: string; data: any }> = [];

	let shouldFailSendMessage = false;
	let shouldFailSendUserMessage = false;
	let shouldFailAppendEntry = false;

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
			if (shouldFailSendMessage) {
				throw new Error("Simulated sendMessage transport failure");
			}
			agentMessages.push({
				msg: msg?.content || msg,
				options,
				customType: msg?.customType,
				display: msg?.display,
			});
		},
		sendUserMessage: (msg: any, options?: any) => {
			if (shouldFailSendUserMessage) {
				throw new Error("Simulated sendUserMessage fallback failure");
			}
			userMessages.push({ msg, options });
		},
		appendEntry: (type: string, data: any) => {
			if (shouldFailAppendEntry) {
				throw new Error("Simulated appendEntry database failure");
			}
			appendedEntries.push({ type, data });
		},
		registerEntryRenderer: () => {},
	};

	return {
		mockPi,
		handlers,
		registeredTools,
		registeredCommands,
		agentMessages,
		userMessages,
		appendedEntries,
		setFailSendMessage: (val: boolean) => { shouldFailSendMessage = val; },
		setFailSendUserMessage: (val: boolean) => { shouldFailSendUserMessage = val; },
		setFailAppendEntry: (val: boolean) => { shouldFailAppendEntry = val; },
	};
}

function createMockContext(tokens = 50000, sessionId = `session_${Math.random().toString(36).slice(2)}`) {
	const branch: any[] = [];
	const uiNotifications: Array<{ msg: string; type: string }> = [];
	return {
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
		ui: {
			notify: (msg: string, type: string) => {
				uiNotifications.push({ msg, type });
			},
			setStatus: () => {},
			input: async () => "",
			select: async () => "",
		},
		uiNotifications,
	};
}

Deno.test("quest_journal_agent_error_messaging: uniform model-visible error and enforcement delivery", async (t) => {
	const QUEST_DIR = ".pi/quest/current";
	await mkdir(QUEST_DIR, { recursive: true });

	// Helper to extract all delivered model messages across both sendMessage and sendUserMessage
	const getAllModelMessages = (api: ReturnType<typeof createMockExtensionAPI>) => {
		const combined: string[] = [];
		for (const m of api.agentMessages) combined.push(String(m.msg));
		for (const m of api.userMessages) combined.push(String(m.msg));
		return combined;
	};

	// -----------------------------------------------------------------------
	// 1. Central agent error API: reportAgentError and formatAgentErrorMessage
	// -----------------------------------------------------------------------
	await t.step("1. reportAgentError produces structured model-visible message with code and next action", () => {
		const api = createMockExtensionAPI();
		const ctx = createMockContext();

		const delivered = reportAgentError(
			api.mockPi as any,
			ctx as any,
			"Operation failed because prerequisite was not met.",
			{
				code: QuestErrorCode.RESEARCH_REQUIRED,
				deliverAs: "steer",
				requiredNextAction: "Investigate architecture and update quest state.",
				details: { Tool: "edit", State: "RESEARCH_PENDING" },
			},
		);

		assert.strictEqual(delivered, true, "reportAgentError must return true on successful delivery");
		assert.strictEqual(api.agentMessages.length, 1, "Must deliver message via primary agent transport");

		const msgText = String(api.agentMessages[0].msg);
		assert.ok(msgText.includes(`[Quest Journal] ${QuestErrorCode.RESEARCH_REQUIRED}`), "Must include standardized error code");
		assert.ok(msgText.includes("Operation failed because prerequisite was not met."), "Must explain what happened");
		assert.ok(msgText.includes("Tool: edit"), "Must include details");
		assert.ok(msgText.includes("Required next action:"), "Must include required next action header");
		assert.ok(msgText.includes("Investigate architecture and update quest state."), "Must include action text");
	});

	// -----------------------------------------------------------------------
	// 2. Transport fallback and failure semantics (no recursion)
	// -----------------------------------------------------------------------
	await t.step("2. Transport gracefully falls back to sendUserMessage and reports false on total failure without infinite recursion", () => {
		const api = createMockExtensionAPI();
		const ctx = createMockContext();

		// Primary fails -> fallback succeeds
		api.setFailSendMessage(true);
		const fallbackDelivered = sendInternalAgentMessage(api.mockPi as any, "Directive text", "followUp");
		assert.strictEqual(fallbackDelivered, true, "sendInternalAgentMessage must succeed via fallback");
		assert.strictEqual(api.userMessages.length, 1, "Fallback must deliver to userMessages");

		// Both fail -> returns false cleanly
		api.setFailSendUserMessage(true);
		const totalFailure = sendInternalAgentMessage(api.mockPi as any, "Directive text", "followUp");
		assert.strictEqual(totalFailure, false, "sendInternalAgentMessage must return false when all transports fail");
	});

	// -----------------------------------------------------------------------
	// 3. Blocked edit tool produces agent-visible message
	// -----------------------------------------------------------------------
	await t.step("3. Blocked edit tool produces model-visible RESEARCH_REQUIRED message", async () => {
		const api = createMockExtensionAPI();
		plugin(api.mockPi as any);

		const ctx = createMockContext(10000, "session_agent_msg_edit");
		const commands: Record<string, any> = {};
		for (const cmd of api.registeredCommands) commands[cmd.name] = cmd;

		const slug = "test-agent-msg-edit-quest";
		await commands["quest"].handler(slug, ctx);

		api.agentMessages.length = 0;
		api.userMessages.length = 0;

		for (const cb of api.handlers["tool_call"] || []) {
			const res = await cb({ toolName: "edit", input: { path: "mods/song/song.c" } }, ctx);
			assert.strictEqual(res?.block, true, "Tool gate must block mutating edit");
		}

		const messages = getAllModelMessages(api);
		assert.ok(messages.length >= 1, "Agent must receive at least one model-visible message on blocked edit");
		const joined = messages.join("\n\n");
		assert.ok(joined.includes(QuestErrorCode.RESEARCH_REQUIRED), "Message must contain RESEARCH_REQUIRED error code");
		assert.ok(joined.includes("Tool: edit"), "Message must identify blocked tool");
		assert.ok(joined.includes("Required next action:"), "Message must instruct agent on required next action");
	});

	// -----------------------------------------------------------------------
	// 4. Blocked bash command produces agent-visible message
	// -----------------------------------------------------------------------
	await t.step("4. Blocked mutating bash command produces model-visible message", async () => {
		const api = createMockExtensionAPI();
		plugin(api.mockPi as any);

		const ctx = createMockContext(10000, "session_agent_msg_bash");
		const commands: Record<string, any> = {};
		for (const cmd of api.registeredCommands) commands[cmd.name] = cmd;

		const slug = "test-agent-msg-bash-quest";
		await commands["quest"].handler(slug, ctx);

		api.agentMessages.length = 0;
		api.userMessages.length = 0;

		for (const cb of api.handlers["tool_call"] || []) {
			const res = await cb({ toolName: "bash", input: { command: "sed -i 's/a/b/g' file.c" } }, ctx);
			assert.strictEqual(res?.block, true, "Tool gate must block mutating bash command");
		}

		const messages = getAllModelMessages(api);
		assert.ok(messages.length >= 1, "Agent must receive model-visible message on blocked bash");
		const joined = messages.join("\n\n");
		assert.ok(joined.includes("Tool: bash"), "Message must identify bash tool");
		assert.ok(joined.includes("Required next action:"), "Message must provide required next action");
	});

	// -----------------------------------------------------------------------
	// 5. Blocked unknown tool produces UNKNOWN_TOOL_BLOCKED message
	// -----------------------------------------------------------------------
	await t.step("5. Blocked unknown tool produces model-visible UNKNOWN_TOOL_BLOCKED message", async () => {
		const api = createMockExtensionAPI();
		plugin(api.mockPi as any);

		const ctx = createMockContext(10000, "session_agent_msg_unknown");
		const commands: Record<string, any> = {};
		for (const cmd of api.registeredCommands) commands[cmd.name] = cmd;

		const slug = "test-agent-msg-unknown-quest";
		await commands["quest"].handler(slug, ctx);

		api.agentMessages.length = 0;
		api.userMessages.length = 0;

		for (const cb of api.handlers["tool_call"] || []) {
			const res = await cb({ toolName: "unrecognized_custom_tool", input: { foo: "bar" } }, ctx);
			assert.strictEqual(res?.block, true, "Unknown tool must be blocked default-deny");
		}

		const messages = getAllModelMessages(api);
		assert.ok(messages.length >= 1, "Agent must receive message on unknown tool block");
		const joined = messages.join("\n\n");
		assert.ok(joined.includes(QuestErrorCode.UNKNOWN_TOOL_BLOCKED), "Message must contain UNKNOWN_TOOL_BLOCKED code");
		assert.ok(joined.includes("Tool: unrecognized_custom_tool"), "Message must identify tool name");
	});

	// -----------------------------------------------------------------------
	// 6. Reassessment gate produces REASSESSMENT_REQUIRED message
	// -----------------------------------------------------------------------
	await t.step("6. Blocked mutating tool during reassessment produces REASSESSMENT_REQUIRED message", async () => {
		const api = createMockExtensionAPI();
		plugin(api.mockPi as any);

		const ctx = createMockContext(10000, "session_agent_msg_reassess");
		const commands: Record<string, any> = {};
		for (const cmd of api.registeredCommands) commands[cmd.name] = cmd;

		const slug = "test-agent-msg-reassess-quest";
		await commands["quest"].handler(slug, ctx);

		// Trigger test failure in turn_end to enter reassessment
		for (const cb of api.handlers["turn_end"] || []) {
			await cb({
				toolResults: [
					{
						toolName: "bash",
						command: "make test",
						content: "FAIL: test_player_stream_chunk assertion failed",
					},
				],
			}, ctx);
		}

		const state = getState(ctx as any);
		assert.strictEqual(state.reassessmentRequired, true, "Must be in reassessment");

		api.agentMessages.length = 0;
		api.userMessages.length = 0;

		for (const cb of api.handlers["tool_call"] || []) {
			const res = await cb({ toolName: "edit", input: { path: "mods/song/song.c" } }, ctx);
			assert.strictEqual(res?.block, true);
		}

		const messages = getAllModelMessages(api);
		assert.ok(messages.length >= 1, "Agent must receive message when edit is blocked during reassessment");
		const joined = messages.join("\n\n");
		assert.ok(joined.includes(QuestErrorCode.REASSESSMENT_REQUIRED), "Message must contain REASSESSMENT_REQUIRED code");
		assert.ok(joined.includes("State: REASSESSMENT_PENDING"), "Message must specify REASSESSMENT_PENDING state");
	});

	// -----------------------------------------------------------------------
	// 7. Confirmation gate produces CONFIRMATION_REQUIRED message
	// -----------------------------------------------------------------------
	await t.step("7. Blocked mutating tool during confirmation pending produces CONFIRMATION_REQUIRED message", async () => {
		const api = createMockExtensionAPI();
		plugin(api.mockPi as any);

		const ctx = createMockContext(10000, "session_agent_msg_confirm");
		const commands: Record<string, any> = {};
		for (const cmd of api.registeredCommands) commands[cmd.name] = cmd;
		const tools: Record<string, any> = {};
		for (const t of api.registeredTools) tools[t.name] = t;

		const slug = "test-agent-msg-confirm-quest";
		await commands["quest"].handler(slug, ctx);

		for (const cb of api.handlers["tool_call"] || []) {
			await cb({ toolName: "read", input: { path: "mods/song/song.c" } }, ctx);
		}
		for (const cb of api.handlers["tool_result"] || []) {
			await cb({ toolName: "read", input: { path: "mods/song/song.c" }, output: "song code", isError: false }, ctx);
		}

		// Satisfy research
		await tools["quest_update_state"].execute("call_1", {
			findings: ["Architecture discovered"],
			understanding: "Verified flow",
			assumptions: ["Assumption valid"],
			openQuestions: ["None remaining - all verified"],
			plan: ["Stage 1"],
			planConfidence: "high",
			planConfidenceReason: "All verified",
			nextAction: "Execute stage 1",
			researchComplete: true,
		}, {}, () => {}, ctx);

		const state = getState(ctx as any);
		assert.strictEqual(state.awaitingUserConfirmation, true, "Must be awaiting user confirmation");

		api.agentMessages.length = 0;
		api.userMessages.length = 0;

		for (const cb of api.handlers["tool_call"] || []) {
			const res = await cb({ toolName: "edit", input: { path: "mods/song/song.c" } }, ctx);
			assert.strictEqual(res?.block, true);
		}

		const messages = getAllModelMessages(api);
		assert.ok(messages.length >= 1, "Agent must receive message when edit is blocked awaiting confirmation");
		const joined = messages.join("\n\n");
		assert.ok(joined.includes(QuestErrorCode.CONFIRMATION_REQUIRED), "Message must contain CONFIRMATION_REQUIRED code");
		assert.ok(joined.includes("State: CONFIRMATION_PENDING"), "Message must specify CONFIRMATION_PENDING state");
	});

	// -----------------------------------------------------------------------
	// 8. Persistence failure produces PERSISTENCE_FAILURE message
	// -----------------------------------------------------------------------
	await t.step("8. Persistence failure produces model-visible PERSISTENCE_FAILURE message", async () => {
		const api = createMockExtensionAPI();
		plugin(api.mockPi as any);

		const ctx = createMockContext(10000, "session_agent_msg_persist");
		const commands: Record<string, any> = {};
		for (const cmd of api.registeredCommands) commands[cmd.name] = cmd;

		const slug = "test-agent-msg-persist-quest";

		// Simulate appendEntry throwing a fatal database error
		api.setFailAppendEntry(true);
		api.agentMessages.length = 0;
		api.userMessages.length = 0;

		await commands["quest"].handler(slug, ctx);

		const messages = getAllModelMessages(api);
		assert.ok(messages.length >= 1, "Agent must receive message when persistence fails");
		const joined = messages.join("\n\n");
		assert.ok(joined.includes(QuestErrorCode.PERSISTENCE_FAILURE), "Message must contain PERSISTENCE_FAILURE code");
		assert.ok(joined.includes("Do not assume the current state will survive compaction"), "Message must warn agent of durability loss");
		assert.ok(joined.includes("Required next action:"), "Message must specify recovery action");
	});

	// -----------------------------------------------------------------------
	// 9. Save verification failure produces SAVE_VERIFICATION_FAILURE message
	// -----------------------------------------------------------------------
	await t.step("9. Save verification failure produces model-visible SAVE_VERIFICATION_FAILURE message", async () => {
		const api = createMockExtensionAPI();
		plugin(api.mockPi as any);

		const ctx = createMockContext(10000, "session_agent_msg_save_fail");
		const tools: Record<string, any> = {};
		for (const t of api.registeredTools) tools[t.name] = t;

		api.agentMessages.length = 0;
		api.userMessages.length = 0;

		// Attempt to mark saved when quest file does not exist on disk
		const res = await tools["quest_mark_saved"].execute("call_save_missing", { questName: "non-existent-quest-xyz" }, {}, () => {}, ctx);
		assert.strictEqual(res.details.success, false, "Mark saved must fail for missing quest file");

		const messages = getAllModelMessages(api);
		assert.ok(messages.length >= 1, "Agent must receive model-visible message on save verification failure");
		const joined = messages.join("\n\n");
		assert.ok(joined.includes(QuestErrorCode.SAVE_VERIFICATION_FAILURE), "Message must contain SAVE_VERIFICATION_FAILURE code");
	});

	// -----------------------------------------------------------------------
	// 10. Checkpoint/compaction gate produces CHECKPOINT_REQUIRED message
	// -----------------------------------------------------------------------
	await t.step("10. Critical compaction threshold without save produces CHECKPOINT_REQUIRED message on edit", async () => {
		const api = createMockExtensionAPI();
		plugin(api.mockPi as any);

		// Tokens exceed threshold (450k > 400k threshold)
		const ctx = createMockContext(450000, "session_agent_msg_checkpoint");
		const commands: Record<string, any> = {};
		for (const cmd of api.registeredCommands) commands[cmd.name] = cmd;
		const tools: Record<string, any> = {};
		for (const t of api.registeredTools) tools[t.name] = t;

		const slug = "test-agent-msg-checkpoint-quest";
		await commands["quest"].handler(slug, ctx);

		for (const cb of api.handlers["tool_call"] || []) {
			await cb({ toolName: "read", input: { path: "mods/song/song.c" } }, ctx);
		}

		// Satisfy research
		await tools["quest_update_state"].execute("call_1", {
			findings: ["Discovered"],
			understanding: "Verified",
			assumptions: ["Valid"],
			openQuestions: ["None remaining"],
			plan: ["Stage 1"],
			planConfidence: "high",
			planConfidenceReason: "Verified",
			nextAction: "Execute step 1",
			researchComplete: true,
		}, {}, () => {}, ctx);

		const state = getState(ctx as any);
		state.confirmedQuests = [slug];
		state.awaitingUserConfirmation = false;
		state.dirty = true; // Mark dirty so compaction is not ready
		state.saveGeneration = null;

		api.agentMessages.length = 0;
		api.userMessages.length = 0;

		for (const cb of api.handlers["tool_call"] || []) {
			const res = await cb({ toolName: "edit", input: { path: "mods/song/song.c" } }, ctx);
			assert.strictEqual(res?.block, true, "Must block edit when checkpoint is required in CRITICAL");
		}

		const messages = getAllModelMessages(api);
		assert.ok(messages.length >= 1, "Agent must receive CHECKPOINT_REQUIRED message");
		const joined = messages.join("\n\n");
		assert.ok(joined.includes(QuestErrorCode.CHECKPOINT_REQUIRED), "Message must contain CHECKPOINT_REQUIRED code");
		assert.ok(joined.includes("State: CRITICAL_COMPACTION_CHECKPOINT_REQUIRED"), "Message must indicate CRITICAL state");
	});

	// Cleanup test artifacts
	try {
		await rm(QUEST_DIR, { recursive: true, force: true });
	} catch {}
});
