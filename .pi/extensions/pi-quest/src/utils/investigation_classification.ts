import { basename } from "node:path";
import { questPath } from "../paths.ts";
import { state } from "../state.ts";
import { InvestigationKind } from "../types.ts";
import { cleanCommandPreamble, hasFileRedirection, splitBashCommandChain, splitBashTokens } from "./shell_parser.ts";

const CODE_SEARCH_BINARIES = new Set([
	"rg",
	"grep",
	"egrep",
	"fgrep",
	"ag",
	"ack",
	"find",
	"fd",
]);

const FILE_READ_BINARIES = new Set([
	"cat",
	"head",
	"tail",
	"less",
	"more",
	"nl",
	"strings",
	"od",
	"hexdump",
	"xxd",
	"wc",
	"ls",
	"stat",
	"file",
	"du",
	"df",
	"tree",
]);

function classifyGitInvestigation(tokens: string[], trimmed: string): { kind: InvestigationKind; target?: string; command?: string } {
	const nonFlagTokens = tokens.slice(1).filter((t) => !t.startsWith("-"));
	const gitSub = (nonFlagTokens[0] || "").toLowerCase();
	if (["diff", "log", "show", "blame", "ls-files", "ls-tree"].includes(gitSub)) {
		return { kind: "code-search", command: trimmed.slice(0, 100) };
	}
	return { kind: "none" };
}

function classifySedInvestigation(tokens: string[], trimmed: string): { kind: InvestigationKind; target?: string; command?: string } {
	const hasInPlace = tokens.some((t) => t === "-i" || t.startsWith("-i") || t.startsWith("--in-place"));
	if (!hasInPlace) {
		const nonFlags = tokens.slice(1).filter((t) => !t.startsWith("-"));
		if (nonFlags.length > 0) {
			return { kind: "file-read", target: nonFlags[nonFlags.length - 1], command: trimmed.slice(0, 100) };
		}
		return { kind: "file-read", command: trimmed.slice(0, 100) };
	}
	return { kind: "none" };
}

export function classifySingleBashInvestigationKind(cmdSegment: string): { kind: InvestigationKind; target?: string; command?: string } {
	const trimmed = cmdSegment.trim();
	if (!trimmed || hasFileRedirection(trimmed)) return { kind: "none" };

	const cleaned = cleanCommandPreamble(trimmed);
	const tokens = splitBashTokens(cleaned);
	if (tokens.length === 0) return { kind: "none" };

	const bin = basename(tokens[0]).toLowerCase();

	if (bin === "xargs") {
		const nonFlagTokens = tokens.slice(1).filter((t) => !t.startsWith("-"));
		return nonFlagTokens.length > 0 ? classifySingleBashInvestigationKind(nonFlagTokens.join(" ")) : { kind: "none" };
	}

	if (CODE_SEARCH_BINARIES.has(bin)) {
		return { kind: "code-search", command: trimmed.slice(0, 100) };
	}

	if (FILE_READ_BINARIES.has(bin)) {
		const nonFlagTokens = tokens.slice(1).filter((t) => !t.startsWith("-"));
		return nonFlagTokens.length > 0
			? { kind: "file-read", target: nonFlagTokens[0], command: trimmed.slice(0, 100) }
			: { kind: "file-read", command: trimmed.slice(0, 100) };
	}

	if (bin === "sed") {
		return classifySedInvestigation(tokens, trimmed);
	}

	if (bin === "git") {
		return classifyGitInvestigation(tokens, trimmed);
	}

	return { kind: "none" };
}

export function classifyBashInvestigationKind(cmdStr: string): { kind: InvestigationKind; target?: string; command?: string } {
	if (!cmdStr || typeof cmdStr !== "string" || !cmdStr.trim()) {
		return { kind: "none" };
	}
	const segments = splitBashCommandChain(cmdStr);
	if (segments.length === 0) return { kind: "none" };

	for (const segment of segments) {
		const res = classifySingleBashInvestigationKind(segment);
		if (res.kind !== "none") {
			return res;
		}
	}
	return { kind: "none" };
}

function classifyReadInvestigation(input: any): { kind: InvestigationKind; target?: string } {
	const rawPath = typeof input === "string" ? input : typeof input?.path === "string" ? input.path : typeof input?.file === "string" ? input.file : "";
	const normPath = rawPath.replace(/^\.\//, "").replace(/\\/g, "/");
	if (state.active && normPath === questPath(state.active)) {
		return { kind: "none" };
	}
	return rawPath ? { kind: "file-read", target: rawPath } : { kind: "none" };
}

function classifyGraphInvestigation(norm: string, input: any): { kind: InvestigationKind; target?: string } {
	const target =
		input?.pattern ||
		input?.name_pattern ||
		input?.function_name ||
		input?.query ||
		input?.qualified_name ||
		input?.path ||
		norm;
	return { kind: "architecture-research", target: `${norm}:${String(target).slice(0, 80)}` };
}

export function classifyInvestigationKind(
	toolName: string,
	input?: any,
): { kind: InvestigationKind; target?: string; command?: string } {
	const norm = (toolName || "").toLowerCase().trim();

	if (norm === "read") {
		return classifyReadInvestigation(input);
	}
	if (norm === "doc_to_md") {
		const target = typeof input === "string" ? input : input?.path || "";
		return target ? { kind: "file-read", target } : { kind: "none" };
	}
	if (norm === "memory_read") {
		const target = typeof input === "string" ? input : input?.target || "memory";
		return { kind: "architecture-research", target: `memory:${target}` };
	}
	if (norm === "memory_search") {
		const target = typeof input === "string" ? input : input?.query || "memory";
		return { kind: "architecture-research", target: `memory_search:${String(target).slice(0, 80)}` };
	}
	if (norm === "search_code") {
		const target = input?.pattern || input?.query || input?.path || "code";
		return { kind: "code-search", target: `search_code:${String(target).slice(0, 80)}` };
	}
	if (
		norm === "search_graph" ||
		norm === "query_graph" ||
		norm === "trace_path" ||
		norm === "get_code_snippet" ||
		norm === "get_architecture" ||
		norm === "get_graph_schema" ||
		norm === "index_status" ||
		norm === "check_index_coverage" ||
		norm === "detect_changes"
	) {
		return classifyGraphInvestigation(norm, input);
	}
	if (
		norm === "web_search" ||
		norm === "source_check" ||
		norm === "fetch_content" ||
		norm === "get_search_content" ||
		norm === "fetch"
	) {
		const target = input?.query || input?.claim || input?.url || input?.path || "external";
		return { kind: "external-research", target: `${norm}:${String(target).slice(0, 80)}` };
	}
	if (
		norm === "bg_delegate" ||
		norm === "fusion_investigate" ||
		norm === "fusion_reason" ||
		norm === "fusion_research" ||
		norm === "fusion_validate"
	) {
		const target = input?.name || input?.objective || input?.prompt || "investigate";
		return { kind: "architecture-research", target: `${norm}:${String(target).slice(0, 80)}` };
	}
	if (norm === "bash" || norm === "user_bash") {
		const cmd = typeof input === "string" ? input : typeof input?.command === "string" ? input.command : typeof input?.cmd === "string" ? input.cmd : "";
		return classifyBashInvestigationKind(cmd);
	}

	return { kind: "none" };
}
