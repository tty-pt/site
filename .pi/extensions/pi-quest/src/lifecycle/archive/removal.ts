import { existsSync } from "node:fs";
import { mkdir, rm, writeFile } from "node:fs/promises";
import { resolve } from "node:path";
import { QUEST_CURRENT_DIR } from "../../constants.ts";
import { pinQuestLog } from "../../logging.ts";
import { cleanDraftIfExists, questDirPath } from "../../paths.ts";
import { onLifecycleStageTransition } from "../../lifecycle.ts";

export async function pinLogToFinalized(targetQid: string, verifiedLogContent: string, projectRoot: string): Promise<void> {
	const finalizedLogDir = resolve(projectRoot, ".pi/quest/finalized_logs");
	await mkdir(finalizedLogDir, { recursive: true });
	const pinnedLogPath = resolve(finalizedLogDir, `${targetQid}.log`);
	if (verifiedLogContent) await writeFile(pinnedLogPath, verifiedLogContent, "utf8");
	pinQuestLog(targetQid, pinnedLogPath);
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
