import { existsSync } from "node:fs";
import { mkdir, readFile, stat, writeFile } from "node:fs/promises";
import { join } from "node:path";
import { CUSTOM_TYPE, FUTURE_DIR, QUEST_CURRENT_DIR } from "./constants.ts";
import { invalidatePreparedCompactionTransaction } from "./compaction/transaction.ts";
import { logPersistenceTransition } from "./logging.ts";
import { logError, reportAgentError } from "./messaging.ts";
import { questPath, resolveQuestRecordBySlug } from "./paths.ts";
import { getState, setSessionState, snapshotState, state } from "./state.ts";
import { ConsistencyAuditResult, ExtensionAPI, ExtensionContext, QuestErrorCode, StoredState } from "./types.ts";
import { computeFileFingerprint } from "./utils.ts";
import { memoFileFingerprint, type FileFingerprint } from "./utils/cache.ts";
import { auditQuestConsistency } from "./validation.ts";
import { withReviewFileLock } from "./utils/mutex.ts";

export function persist(pi: ExtensionAPI, ctx?: ExtensionContext): boolean {
	try {
		if (typeof pi?.appendEntry === "function") {
			const snapshot = snapshotState(ctx);
			pi.appendEntry<StoredState>(CUSTOM_TYPE, snapshot);
		}
		setSessionState(ctx, getState(ctx));
		return true;
	} catch (err: any) {
		logPersistenceTransition("PERSISTENCE_DEGRADED", `persistence append failed: ${err?.message || String(err)}`, {
			quest: state.active || "",
			code: "PERSISTENCE_FAILURE",
			reason: err?.message || String(err),
		});
		reportAgentError(
			pi,
			ctx,
			`The quest state could not be durably persisted: ${err?.message || String(err)}.\n\nDo not assume the current state will survive compaction or session reconstruction.`,
			{
				code: QuestErrorCode.PERSISTENCE_FAILURE,
				deliverAs: "followUp",
				requiredNextAction: "Restore persistence, then perform and verify a fresh durable quest save before crossing a durability boundary.",
				details: {
					Quest: state.active || "(none)",
					ActiveFile: state.questId ? questPath(state.questId) : undefined,
				},
			},
		);
		setSessionState(ctx, getState(ctx));
		return false;
	}
}

export async function verifyAndMarkSaved(
	pi: ExtensionAPI,
	ctx?: ExtensionContext,
	expectedSlug?: string,
): Promise<{ success: boolean; hash?: string; count: number; error?: string; consistency?: ConsistencyAuditResult }> {
	const s = getState(ctx);
	const targetSlug = expectedSlug || s.active || state.active;
	let targetQid: string | null = null;
	if (expectedSlug) {
		const record = await resolveQuestRecordBySlug(expectedSlug);
		const cand = record ? record.qid : ((s.active === expectedSlug || state.active === expectedSlug) && (s.questId || state.questId) ? (s.questId || state.questId) : expectedSlug);
		targetQid = cand || null;
	} else {
		targetQid = s.questId || state.questId || null;
	}
	if (targetQid) {
		s.questId = targetQid;
		state.questId = targetQid;
	}
	logPersistenceTransition("SAVE_STARTED", `verifying save for '${targetSlug || "(none)"}'`, {
		quest: targetSlug || "",
	});
	if (!targetSlug && !targetQid) {
		const errMsg = "Save verification failed: No active quest is set.";
		logPersistenceTransition("SAVE_REJECTED", errMsg, { quest: "", reason: "no_active_quest" });
		reportAgentError(
			pi,
			ctx,
			errMsg,
			{
				code: QuestErrorCode.SAVE_VERIFICATION_FAILURE,
				requiredNextAction: "Set an active quest with /quest <name> before attempting to save.",
			},
		);
		return { success: false, count: s.saveCount, error: errMsg };
	}
	const doVerify = async (): Promise<{ success: boolean; hash?: string; count: number; error?: string; consistency?: ConsistencyAuditResult }> => {
		const p = questPath(targetQid);
		let fp = await memoFileFingerprint(p);
		if (!fp) fp = (await computeFileFingerprint(p)) as FileFingerprint | null;
		if (!fp) {
			const futureDraftExists = (() => { try { const slug = targetSlug || targetQid || ""; return slug ? existsSync(join(FUTURE_DIR, `${slug}.md`)) : false; } catch { return false; } })();
			const reason = futureDraftExists ? "file_not_found+future_draft_exists" : "file_not_found";
			const requiredAction = futureDraftExists ? "quest_update_state" : undefined;
			const draftHint = futureDraftExists ? ` Draft exists in \`${join(FUTURE_DIR, `${targetSlug || targetQid}.md`)}\` — call quest_update_state (not quest_mark_saved or bash mkdir) with researchComplete:true` : "";
			const errMsg = `Save verification failed: Quest file not found or unreadable at \`${p}\`.${draftHint}`;
			logPersistenceTransition("SAVE_FAILED", errMsg, { quest: targetSlug || targetQid || "", path: p, reason, ...(requiredAction ? { requiredAction } : {}) });
			reportAgentError(
				pi,
				ctx,
				errMsg,
				{
					code: QuestErrorCode.SAVE_VERIFICATION_FAILURE,
					requiredNextAction: futureDraftExists ? `Draft exists in ${join(FUTURE_DIR, `${targetSlug || targetQid}.md`)} — call quest_update_state (not quest_mark_saved or bash mkdir) with researchComplete:true to create the durable quest file.` : `Ensure the file is written to disk at \`${p}\` before calling quest_mark_saved.`,
					details: {
						Quest: targetSlug || targetQid || "(none)",
						Path: p,
					},
				},
			);
			return {
				success: false,
				count: s.saveCount,
				error: `Quest file not found or unreadable at \`${p}\`.${draftHint} Ensure the file is written to disk before marking as saved.`,
			};
		}

		let audit: ConsistencyAuditResult | undefined;
		if (fp.hash !== s.lastSavedHash) {
			try {
				const content = await readFile(p, "utf8");
				audit = auditQuestConsistency(content, { recentModifiedFiles: s.sessionModifiedFiles || state.sessionModifiedFiles });
				if (!audit.consistent) {
					logError(`Consistency audit issues in ${p}: ${audit.issues.join("; ")}`, undefined, ctx, QuestErrorCode.SAVE_VERIFICATION_FAILURE);
				}
			} catch {}
		}

		const isSameAsLastSave =
			s.saveGeneration &&
			s.saveGeneration.path === p &&
			s.saveGeneration.hash === fp.hash &&
			s.saveCount > s.compactCount;

		if (isSameAsLastSave && !s.dirty) {
			if (s.activeTransaction && s.activeTransaction.phase === "resume-delivered") {
				s.activeTransaction = null;
				s.activeCompactionId = null;
				persist(pi, ctx);
			}
			s.lastPromptAt = Date.now();
			if (state !== s) Object.assign(state, s);
			return { success: true, hash: fp.hash, count: s.saveCount, consistency: audit };
		}

		// Invalidate any prepared compaction transaction capturing the prior checkpoint
		invalidatePreparedCompactionTransaction(s, "new_save_verified");

		s.saveCount = Math.max(s.saveCount + 1, s.compactCount + 1);
		s.lastSavedHash = fp.hash;
		s.saveGeneration = {
			count: s.saveCount,
			path: p,
			hash: fp.hash,
			savedAt: Date.now(),
		};
		s.dirty = false;
		s.preCompactionCheckpointPending = false;
		s.preCompactionSaveRequestPending = false;
		if (s.activeTransaction && s.activeTransaction.phase === "resume-delivered") {
			s.activeTransaction = null;
			s.activeCompactionId = null;
		}
		s.lastPromptAt = Date.now();
		// Clear files modified after successful save to silence audit noise for next research-only cycles (B hygiene)
		s.sessionModifiedFiles = [];
		if (state !== s) {
			Object.assign(state, s);
			state.sessionModifiedFiles = [];
		} else {
			state.sessionModifiedFiles = [];
		}
		persist(pi, ctx);

		logPersistenceTransition("SAVE_VERIFIED", `save verified for '${targetSlug || targetQid}' (gen ${s.saveCount})`, {
			quest: targetSlug || targetQid || "",
			gen: s.saveCount,
			hash: fp.hash.slice(0, 8),
		});

		return { success: true, hash: fp.hash, count: s.saveCount, consistency: audit };
	};
	if (targetQid) {
		return await withReviewFileLock(targetQid, doVerify);
	} else {
		return await doVerify();
	}
}

export async function markSaved(pi: ExtensionAPI, ctx?: ExtensionContext) {
	return await verifyAndMarkSaved(pi, ctx);
}
