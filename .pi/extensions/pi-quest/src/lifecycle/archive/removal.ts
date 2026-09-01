import { existsSync } from "node:fs";
import { mkdir, rm, writeFile } from "node:fs/promises";
import { resolve } from "node:path";
import { QUEST_CURRENT_DIR } from "../../constants.ts";
import { pinQuestLog } from "../../logging.ts";
import { cleanDraftIfExists, questDirPath } from "../../paths.ts";
import { onLifecycleStageTransition } from "../../lifecycle.ts";

export async function pinLogToFinalized(targetQid: string, verifiedLogContent: string, projectRoot: string): Promise<void> {
	const currentLogPath = resolve(projectRoot, QUEST_CURRENT_DIR, targetQid, "execution.log");
	const finalizedLogDir = resolve(projectRoot, ".pi/quest/finalized_logs");
	const finalizedLogPath = resolve(finalizedLogDir, `${targetQid}.log`);
	try {
		await mkdir(resolve(projectRoot, QUEST_CURRENT_DIR, targetQid), { recursive: true });
		if (verifiedLogContent) await writeFile(currentLogPath, verifiedLogContent, "utf8");
	} catch {}
	try {
		await mkdir(finalizedLogDir, { recursive: true });
		if (verifiedLogContent) await writeFile(finalizedLogPath, verifiedLogContent, "utf8");
	} catch {}
	pinQuestLog(targetQid, finalizedLogPath);
}

export async function removeActiveDirectory(targetQid: string, questName: string, projectRoot: string, ctx: any, checkActiveDirExists: () => boolean, checkZipExists: () => boolean): Promise<void> {
	await rm(resolve(projectRoot, QUEST_CURRENT_DIR, targetQid), { recursive: true, force: true });
	if (existsSync(questDirPath(targetQid))) await rm(questDirPath(targetQid), { recursive: true, force: true });
	await cleanDraftIfExists(questName, ctx);

	onLifecycleStageTransition?.("active_removal", {
		questId: targetQid,
		questName,
		activeDirExists: checkActiveDirExists(),
		zipExists: checkZipExists(),
	});
}
