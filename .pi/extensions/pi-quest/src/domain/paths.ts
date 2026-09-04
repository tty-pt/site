// HIGH_LEVEL: #storage — qid in all paths; generated files are views, transcript is truth.
import type { Qid } from "./qid";

export const QUEST_ROOT = ".pi/quest";
export const FUTURE_DIR = `${QUEST_ROOT}/future`;
export const CURRENT_DIR = `${QUEST_ROOT}/current`;
export const ARCHIVE_DIR = `${QUEST_ROOT}/archive`;

export function draftPath(qid: Qid): string {
  return `${FUTURE_DIR}/${qid}.md`;
}

export function questDir(qid: Qid): string {
  return `${CURRENT_DIR}/${qid}`;
}

export function archivePath(qid: Qid): string {
  return `${ARCHIVE_DIR}/${qid}.zip`;
}

export function isQuestPath(path: string): boolean {
  return path === QUEST_ROOT || path.startsWith(`${QUEST_ROOT}/`);
}
