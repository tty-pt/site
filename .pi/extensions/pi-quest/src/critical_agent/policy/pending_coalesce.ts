import { clearPendingReview, getAllPendingForSlug, getPendingReview, getPendingReviews } from "../tracker.ts";
import { state } from "../../state.ts";

function isPlanReviewValidForStateLocal(targetState?: any): boolean {
  const s = targetState || state;
  if (s.dirty) return false;
  const approval = s.lastPlanReviewApproval;
  if (!approval) return false;
  const currentPlanVersion = s.planVersion || 1;
  if (approval.planVersion !== currentPlanVersion) return false;
  if (approval.boundaryKey) {
    const currentBoundaryKey = s.lastPlanReviewBoundaryKey;
    if (currentBoundaryKey && approval.boundaryKey !== currentBoundaryKey) return false;
    return true;
  }
  const currentHash = s.lastSavedHash || (s.saveGeneration ? s.saveGeneration.hash : null);
  if (currentHash && approval.saveHash && approval.saveHash !== currentHash) return false;
  if (s.saveCount && approval.saveCount && approval.saveCount !== s.saveCount) return false;
  return true;
}

export function dequeuePendingIfNeeded(
  slug: string,
  currentPlanVersion: number,
  currentHash: string | null,
  snapshot: any,
  targetState: any,
  originalKind: any,
): any | null {
  const pendings = getAllPendingForSlug(slug);
  if (pendings.length === 0) return null;
  const latestPlanVersion = targetState.planVersion || 1;
  const latestHash = targetState.lastSavedHash || (targetState.saveGeneration ? targetState.saveGeneration.hash : null);
  const latestBoundaryKey = targetState.lastPlanReviewBoundaryKey;
  const stateChanged = (latestPlanVersion !== currentPlanVersion) || (latestHash !== currentHash);
  // Evaluate each pending per-kind; collect those that need follow-up
  const candidates: any[] = [];
  const stale: any[] = [];
  for (const pending of pendings) {
    const isPlanRev = pending.kind === "plan_review";
    let needsFollowUp = false;
    if (isPlanRev) {
      const boundaryChanged = snapshot.boundaryKey ? (latestBoundaryKey !== snapshot.boundaryKey) : stateChanged;
      needsFollowUp = (!isPlanReviewValidForStateLocal(targetState) || boundaryChanged);
    } else {
      needsFollowUp = stateChanged;
    }
    if (needsFollowUp) candidates.push(pending);
    else stale.push(pending);
  }
  // Clear stale ones
  for (const s of stale) clearPendingReview(slug, s.kind);
  if (candidates.length === 0) return null;
  // Prefer most recent requestedAt; if tie, last
  candidates.sort((a, b) => (b.requestedAt || 0) - (a.requestedAt || 0));
  const chosen = candidates[0];
  clearPendingReview(slug, chosen.kind);
  // Also clear other stale candidates? keep one chosen; clear remaining candidates that are not chosen but still stale? They are not stale, but we coalesce to one.
  // Remove remaining candidates for same slug to avoid duplicate launches
  for (const c of candidates.slice(1)) clearPendingReview(slug, c.kind);
  return chosen;
}
