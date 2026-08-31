import { readFileSync } from "node:fs";
import { NOTES_FILE } from "./constants.ts";
import { asyncContext, getActiveContext } from "./state.ts";
import { ExtensionContext } from "./types.ts";

export function withContext<T extends (...args: any[]) => any>(fn: T): T {
	return ((...args: any[]) => {
		let ctx: ExtensionContext | undefined;
		for (const arg of args) {
			if (arg && typeof arg === "object" && ("sessionManager" in arg || "cwd" in arg || "ui" in arg)) {
				ctx = arg as ExtensionContext;
				break;
			}
		}
		if (ctx) {
			return asyncContext.run(ctx, () => fn(...args));
		}
		return fn(...args);
	}) as T;
}

export function calculateCurrentTokens(ctx?: ExtensionContext): number | null {
	const c = getActiveContext(ctx);
	const usage = typeof c?.getContextUsage === "function" ? c.getContextUsage() : undefined;
	return usage?.tokens ?? (usage?.percent && usage?.contextWindow ? Math.round((usage.percent * usage.contextWindow) / 100) : null);
}

export function usagePercent(ctx: ExtensionContext): number {
	const u = typeof ctx.getContextUsage === "function" ? ctx.getContextUsage() : undefined;
	if (u && typeof u.percent === "number" && Number.isFinite(u.percent)) return u.percent;
	return 0;
}

export function gitBranch(): string | null {
	try {
		const head = readFileSync(".git/HEAD", "utf8").trim();
		const m = head.match(/^ref:\s*refs\/heads\/(.+)$/);
		return m ? m[1] : head.slice(0, 40);
	} catch {
		return null;
	}
}

export function standingNotes(): string | null {
	try {
		const t = readFileSync(NOTES_FILE, "utf8").trim();
		return t || null;
	} catch {
		return null;
	}
}

export function projectGuidelines(): string | null {
	const candidates = ["AGENTS.md", "CLAUDE.md", "SYSTEM.md"];
	for (const file of candidates) {
		try {
			const content = readFileSync(file, "utf8").trim();
			if (!content) continue;

			const match = content.match(/(##\s+(?:Guidelines|Rules|Invariants|Mandatory Guidelines)[\s\S]*?)(?=\n##\s+|$)/i);
			if (match && match[1].trim()) {
				return `# MANDATORY PROJECT INVARIANTS & GUIDELINES (Strictly Enforced from \`${file}\`)\nCRITICAL: The following guidelines are absolute architectural invariants. You MUST strictly adhere to them across all turns without exception.\n\n${match[1].trim()}`;
			}

			if (content.length <= 2500) {
				return `# MANDATORY PROJECT INVARIANTS & GUIDELINES (Strictly Enforced from \`${file}\`)\nCRITICAL: The following guidelines are absolute architectural invariants. You MUST strictly adhere to them across all turns without exception.\n\n${content}`;
			}
		} catch {
			// file unreadable or missing
		}
	}
	return null;
}
