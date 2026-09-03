import { readFileSync, statSync } from "node:fs";
import { createHash } from "node:crypto";
import type { MarkdownSection } from "../types.ts";

export type FileFingerprint = { hash: string; mtimeMs: number; size: number };

const fpCache = new Map<string, FileFingerprint>();
const mdCache = new Map<string, Map<string, MarkdownSection>>();
const MAX_MD_CACHE = 32;

let settingsCache: { mtime: number; rawHash: string; parsed: any | null } = { mtime: 0, rawHash: "", parsed: null };
let lastWorkflowCache: { saveHash: string; pressure: string; prompt: string } | null = null;
let resumeCache: { saveHash: string; value: string } = { saveHash: "", value: "" };

function hashContent(content: string | Uint8Array): string {
	return createHash("sha256").update(content).digest("hex");
}

export async function memoFileFingerprint(p: string): Promise<FileFingerprint | null> {
	try {
		const st = statSync(p);
		const cached = fpCache.get(p);
		if (cached && cached.mtimeMs === st.mtimeMs && cached.size === st.size) return cached;
		const raw = readFileSync(p);
		const hash = hashContent(raw);
		const fp: FileFingerprint = { hash, mtimeMs: st.mtimeMs, size: st.size };
		fpCache.set(p, fp);
		return fp;
	} catch {
		return null;
	}
}

export function memoParseMarkdown(content: string, hash: string, parser: (c: string) => Map<string, MarkdownSection>): Map<string, MarkdownSection> {
	const cached = mdCache.get(hash);
	if (cached) return cached;
	const parsed = parser(content);
	if (mdCache.size >= MAX_MD_CACHE) {
		const firstKey = mdCache.keys().next().value;
		if (firstKey) mdCache.delete(firstKey);
	}
	mdCache.set(hash, parsed);
	return parsed;
}

export function memoSettings(mtimeMs: number, raw: string, parser: (raw: string) => any): any {
	const h = hashContent(raw);
	if (settingsCache.mtime === mtimeMs && settingsCache.rawHash === h && settingsCache.parsed !== null) return settingsCache.parsed;
	const parsed = parser(raw);
	settingsCache = { mtime: mtimeMs, rawHash: h, parsed };
	return parsed;
}

export function memoResumeContext(saveHash: string, extracted: string[]): string {
	if (resumeCache.saveHash === saveHash && resumeCache.value) return resumeCache.value;
	const val = extracted.join("\n\n");
	resumeCache = { saveHash, value: val };
	return val;
}

export function getCachedWorkflow(saveHash: string, pressureKey: string): string | null {
	if (lastWorkflowCache && lastWorkflowCache.saveHash === saveHash && lastWorkflowCache.pressure === pressureKey) return lastWorkflowCache.prompt;
	return null;
}

export function setCachedWorkflow(saveHash: string, pressureKey: string, prompt: string): void {
	lastWorkflowCache = { saveHash, pressure: pressureKey, prompt };
}

let settingsJsonCache: { path: string; mtime: number; hash: string; parsed: any | null } = { path: "", mtime: 0, hash: "", parsed: null };

export function getCachedSettingsJson(path: string): any | null {
	try {
		const st = statSync(path);
		const mtime = st.mtimeMs;
		if (settingsJsonCache.path === path && settingsJsonCache.mtime === mtime && settingsJsonCache.parsed !== null) return settingsJsonCache.parsed;
		const raw = readFileSync(path, "utf8");
		const h = hashContent(raw).slice(0, 16);
		if (settingsJsonCache.path === path && settingsJsonCache.hash === h && settingsJsonCache.parsed !== null) {
			settingsJsonCache.mtime = mtime;
			return settingsJsonCache.parsed;
		}
		const json = JSON.parse(raw);
		settingsJsonCache = { path, mtime, hash: h, parsed: json };
		return json;
	} catch {
		return null;
	}
}

export function clearCache(): void {
	fpCache.clear();
	mdCache.clear();
	settingsCache = { mtime: 0, rawHash: "", parsed: null };
	settingsJsonCache = { path: "", mtime: 0, hash: "", parsed: null };
	lastWorkflowCache = null;
	resumeCache = { saveHash: "", value: "" };
}
