// HIGH_LEVEL: #storage — one quest file for the whole life; archive staging is ephemeral.
import type { Qid } from "./qid";

export const QUEST_ROOT = ".pi/quest";
export const FUTURE_DIR = `${QUEST_ROOT}/future`;
export const ARCHIVE_DIR = `${QUEST_ROOT}/archive`;
export const STAGING_DIR = `${QUEST_ROOT}/.staging`;

// The quest document for the entire life: the drafting workspace before
// promotion and the editable plan+status doc through implementing and
// validating. Removed only when the quest archives.
export function draftPath(qid: Qid): string {
  return `${FUTURE_DIR}/${qid}.md`;
}

// Ephemeral archive staging: materialized, zipped, and removed. Never
// surfaced to the agent and never read back.
export function stageDir(qid: Qid): string {
  return `${STAGING_DIR}/${qid}`;
}

export function archivePath(qid: Qid): string {
  return `${ARCHIVE_DIR}/${qid}.zip`;
}

export function isQuestPath(path: string): boolean {
  return path === QUEST_ROOT || path.startsWith(`${QUEST_ROOT}/`);
}
