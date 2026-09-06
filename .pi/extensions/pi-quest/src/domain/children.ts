// Sub-quest links: children are full-lifecycle units; the parent records,
// settles, and explicitly continues past them. Pure reducer helpers over
// QuestState; the ChildLink shape stays in quest.ts with the state.
import type { Qid } from "./qid";
import { markChanged, type ChildStatus, type ChildLink, type QuestState } from "./quest";

export function addChild(state: QuestState, link: ChildLink): QuestState {
  if (state.children.some((c) => c.qid === link.qid)) throw new Error(`child ${link.qid} already linked`);
  return markChanged(state, { children: [...state.children, link] });
}

export function settleChild(state: QuestState, qid: Qid, status: ChildStatus, findings: string | null): QuestState {
  return markChanged(state, {
    children: state.children.map((c) => c.qid === qid ? { ...c, status, findings } : c),
  });
}

export function acknowledgeChild(state: QuestState, qid: Qid): QuestState {
  const link = state.children.find((c) => c.qid === qid);
  if (!link) throw new Error(`no linked child ${qid}`);
  if (link.status === "running") throw new Error(`child ${qid} has not returned yet`);
  return markChanged(state, {
    children: state.children.map((c) => c.qid === qid ? { ...c, acknowledged: true } : c),
  });
}

export function unfinishedChildren(state: QuestState): ChildLink[] {
  return state.children.filter((c) => c.status === "running");
}
