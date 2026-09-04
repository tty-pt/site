// HIGH_LEVEL: #drafting — content-changing save supersedes review, boots fresh.
// SPEC: B1.3.
import { getState, updateState } from "../app/store";
import { draftPath } from "../domain/paths";

export function handleDraftEdit(path: string, contentHash: string): boolean {
  try {
    const state = getState();
    if (state.qid === null || state.draft === null) return false;
    if (!path.endsWith(draftPath(state.qid))) return false;
    if (state.draft.contentHash === contentHash) return false;
    updateState((s) =>
      s.draft === null
        ? s
        : { ...s, draft: { ...s.draft, contentHash }, snapshotPending: true }
    );
    return true;
  } catch {
    // Passive path: never break the agent.
    return false;
  }
}
