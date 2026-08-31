import assert from "node:assert";
import { mkdir, rm, writeFile } from "node:fs/promises";
import plugin, {
	canImplement,
	createOrGetCompactionTransaction,
	dispatchCompactionResume,
	getImplementationBlockReason,
	getState,
	QuestErrorCode,
	recordObservedInvestigation,
	snapshotState,
} from "../index.ts";

export {
	assert,
	canImplement,
	createOrGetCompactionTransaction,
	dispatchCompactionResume,
	getImplementationBlockReason,
	getState,
	mkdir,
	plugin,
	QuestErrorCode,
	recordObservedInvestigation,
	rm,
	snapshotState,
	writeFile,
};

export function createMockExtensionAPI() {
	const handlers: Record<string, any[]> = {};
	const registeredTools: any[] = [];
	const registeredCommands: any[] = [];
	const agentMessages: Array<{ msg: any; options?: any; customType?: any; display?: any }> = [];
	const userMessages: Array<{ msg: any; options?: any }> = [];
	const appendedEntries: Array<{ type: string; data: any }> = [];
	let throwOnSend = false;

	const mockPi = {
		setThrowOnSend(val: boolean) {
			throwOnSend = val;
		},
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
			if (throwOnSend) {
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
			if (throwOnSend) {
				throw new Error("Simulated sendUserMessage transport failure");
			}
			userMessages.push({ msg, options });
		},
		appendEntry: (type: string, data: any) => {
			appendedEntries.push({ type, data });
		},
		registerEntryRenderer: () => {},
	};

	return { mockPi, handlers, registeredTools, registeredCommands, agentMessages, userMessages, appendedEntries };
}

export function createMockContext(tokens = 50000, sessionId = `session_${Math.random().toString(36).slice(2)}`) {
	const branch: any[] = [];
	return {
		cwd: process.cwd(),
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
			notify: () => {},
			setStatus: () => {},
			input: async () => "",
			select: async () => "",
		},
	};
}

export function setupCompactionTestHarness(tokens = 10000, sessionId = `session_${Math.random().toString(36).slice(2)}`) {
	const api = createMockExtensionAPI();
	plugin(api.mockPi as any);
	const ctx = createMockContext(tokens, sessionId);
	const commands: Record<string, any> = {};
	for (const cmd of api.registeredCommands) commands[cmd.name] = cmd;
	const tools: Record<string, any> = {};
	for (const tool of api.registeredTools) tools[tool.name] = tool;
	const getAllMessages = () => [
		...api.agentMessages.map((m) => String(m.msg)),
		...api.userMessages.map((m) => String(m.msg)),
	];
	return { api, ctx, commands, tools, getAllMessages };
}

export function getAllMessages(api: ReturnType<typeof createMockExtensionAPI>): string[] {
	return [...api.agentMessages.map((m) => String(m.msg)), ...api.userMessages.map((m) => String(m.msg))];
}

export async function writeTestQuestFile(name: string, goal = name, parent = ""): Promise<string> {
	const path = `.pi/quest/current/${name}.md`;
	const parentSec = parent ? `\n\n## Parent Quest\n[[${parent}]]\n` : "";
	await writeFile(path, `# Quest: ${name}\n\n## Goal\n${goal}${parentSec}\n`, "utf8");
	return path;
}
