export function buildTemplateMetadataSections(): string[] {
	const now = Date.now();
	return [
		`## Plan Version`,
		`1`,
		``,
		`## Research Round`,
		`1`,
		``,
		`## Last Research At`,
		`${now}`,
		``,
		`## Last Plan Revision At`,
		`${now}`,
		``,
	];
}

export interface QuestMeta {
	questId: string;
	planVersion: number;
	researchRound: number;
	lastResearchAt: number;
	lastPlanRevisionAt: number;
	reassessmentVersion: number;
	resolvedReassessmentVersion: number;
	planConfidence: string;
}

export function buildDefaultMeta(questId: string): QuestMeta {
	const now = Date.now();
	return {
		questId,
		planVersion: 1,
		researchRound: 1,
		lastResearchAt: now,
		lastPlanRevisionAt: now,
		reassessmentVersion: 0,
		resolvedReassessmentVersion: 0,
		planConfidence: "low",
	};
}

export function serializeMeta(meta: QuestMeta): string {
	return JSON.stringify(meta, null, 2) + "\n";
}

export function parseMeta(raw: string): QuestMeta | null {
	try {
		const j = JSON.parse(raw);
		if (j && typeof j.questId === "string") return j as QuestMeta;
	} catch {}
	return null;
}

export async function syncMetaJson(qid: string, targetState: any): Promise<void> {
	try {
		const { writeFile } = await import("node:fs/promises");
		const { questMetaPath } = await import("../../paths.ts");
		if (!qid) return;
		const metaPath = questMetaPath(qid);
		const meta: QuestMeta = {
			questId: qid,
			planVersion: targetState.planVersion ?? 1,
			researchRound: targetState.researchRound ?? 1,
			lastResearchAt: targetState.lastResearchAt ?? Date.now(),
			lastPlanRevisionAt: targetState.lastPlanRevisionAt ?? Date.now(),
			reassessmentVersion: targetState.reassessmentVersion ?? 0,
			resolvedReassessmentVersion: targetState.resolvedReassessmentVersion ?? 0,
			planConfidence: targetState.planConfidence ?? "low",
		};
		await writeFile(metaPath, JSON.stringify(meta, null, 2) + "\n", "utf8");
	} catch {}
}
