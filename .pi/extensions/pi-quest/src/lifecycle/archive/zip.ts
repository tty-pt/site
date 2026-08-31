import { logError } from "../../messaging.ts";
import { createRunArchive } from "../../diagnostic.ts";
import { questArchivePath } from "../../paths.ts";
import { onLifecycleStageTransition } from "../../lifecycle.ts";

export async function createArchiveZip(
	projectRoot: string,
	questId: string,
	questName: string,
	authoritativeTerminalStatus: "COMPLETED" | "FAILED",
	hierarchy: any,
	verifiedLogContent: string,
	verifiedQuestContent: string,
	checkActiveDirExists: () => boolean,
	checkZipExists: () => boolean,
	ctx: any,
): Promise<string> {
	let finalArchiveZipPath = questArchivePath(questId);
	try {
		const archiveRes = await createRunArchive({
			projectRoot,
			questId,
			status: authoritativeTerminalStatus,
			hierarchy,
			finalizedLogContent: verifiedLogContent,
			finalizedQuestContent: verifiedQuestContent,
		});
		finalArchiveZipPath = archiveRes.zipPath;
	} catch (err: any) {
		logError(`Automatic run archive generation failed for quest '${questName}'`, err, ctx);
	}

	onLifecycleStageTransition?.("zip_creation", {
		questId,
		questName,
		zipPath: finalArchiveZipPath,
		activeDirExists: checkActiveDirExists(),
		zipExists: checkZipExists(),
	});
	return finalArchiveZipPath;
}
