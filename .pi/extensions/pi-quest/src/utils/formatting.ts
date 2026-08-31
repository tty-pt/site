import { ANSI_SGR, DEFAULT_CEILING_TOKENS, MAX_QUEST_NAME_DISPLAY_LENGTH } from "../constants.ts";

export const displayWidth = (s: string): number => {
	let w = 0;
	for (const ch of s.replace(ANSI_SGR, "")) {
		w += ch.codePointAt(0)! > 0x7f ? 2 : 1;
	}
	return w;
};

export class Text {
	constructor(public text: string, public x: number = 0, public y: number = 0) {}

	render(width: number): string[] {
		if (!this.text || this.text.trim() === "") return [];
		const visible = displayWidth(this.text);
		if (visible < width) {
			return [this.text + " ".repeat(width - visible)];
		}
		let out = "";
		let w = 0;
		for (const ch of this.text.replace(ANSI_SGR, "")) {
			const cw = ch.codePointAt(0)! > 0x7f ? 2 : 1;
			if (w + cw > width) break;
			out += ch;
			w += cw;
		}
		return [out];
	}
}

export function formatTokens(num: number): string {
	if (num >= 1_000_000) {
		const m = num / 1_000_000;
		return `${m % 1 === 0 ? m.toFixed(0) : m.toFixed(1)}M`;
	}
	if (num >= 1_000) {
		const k = num / 1_000;
		return `${k % 1 === 0 ? k.toFixed(0) : k.toFixed(1)}k`;
	}
	return `${num}`;
}

export function parsePercentage(val: unknown): number | null {
	if (typeof val === "number" && !Number.isNaN(val)) {
		if (val > 0 && val <= 1.0) return Math.round(val * 100);
		if (val > 1 && val <= 100) return Math.round(val);
		return null;
	}
	if (typeof val !== "string") return null;
	const s = val.trim().toLowerCase();
	if (!s) return null;
	const match = s.match(/^([\d.]+)\s*%$/);
	if (match) {
		const num = Number.parseFloat(match[1]);
		if (!Number.isNaN(num) && num > 0 && num <= 100) {
			return Math.round(num);
		}
	}
	return null;
}

export function parseTokenAmount(val: unknown, defaultVal: number = DEFAULT_CEILING_TOKENS): number | null {
	if (typeof val === "number" && !Number.isNaN(val)) {
		return val > 0 ? Math.round(val) : 0;
	}
	if (typeof val !== "string") return null;
	const s = val.trim().toLowerCase();
	if (!s) return null;
	if (s === "off" || s === "disable" || s === "disabled" || s === "0") return 0;
	if (s === "default") return defaultVal;

	const cleaned = s.replace(/,/g, "");
	const match = cleaned.match(/^([\d.]+)\s*([km])?(?:\s*tokens)?$/i);
	if (!match) return null;
	const base = Number.parseFloat(match[1]);
	if (Number.isNaN(base) || base <= 0) return null;
	const unit = match[2]?.toLowerCase();
	if (unit === "k") return Math.round(base * 1000);
	if (unit === "m") return Math.round(base * 1000000);
	return Math.round(base);
}

export function truncateQuestName(name: string, maxLen = MAX_QUEST_NAME_DISPLAY_LENGTH): string {
	if (!name) return "";
	if (name.length <= maxLen) return name;
	return name.slice(0, Math.max(1, maxLen - 1)) + "…";
}

export function formatQuestHierarchy(active: string | null, stack?: string[], maxNameLength = MAX_QUEST_NAME_DISPLAY_LENGTH): string {
	if (!active) return "(none)";
	if (!stack || stack.length === 0) return truncateQuestName(active, maxNameLength);

	const cleanStack: string[] = [];
	for (const item of stack) {
		if (item && (cleanStack.length === 0 || cleanStack[cleanStack.length - 1] !== item)) {
			cleanStack.push(item);
		}
	}
	if (!cleanStack.includes(active)) {
		cleanStack.push(active);
	} else if (cleanStack[cleanStack.length - 1] !== active) {
		const idx = cleanStack.lastIndexOf(active);
		cleanStack.splice(idx + 1);
	}

	const depth = cleanStack.length;
	const activeTruncated = truncateQuestName(active, maxNameLength);

	if (depth <= 1) return activeTruncated;
	return `d${depth}: ${activeTruncated}`;
}

export const normalizePath = (p: string) => p.replace(/^\.\//, "").replace(/\\/g, "/");
