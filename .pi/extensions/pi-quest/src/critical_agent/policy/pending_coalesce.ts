import { logEvent } from "../../logging.ts";
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
  const firstPlanReviewFired = (targetState as any).firstPlanReviewFired || (targetState as any).planReviewAlreadyFired || !!(targetState as any).lastPlanReviewApproval;
  // 38: keep draft follow-up pending even after first approval — live draft hash drift supersedes old boundary
  const hasDraftPending = pendings.some((p: any) => String(p.boundaryKey || "").startsWith("draft:") || String(p.kind || "") === "plan_review" && targetState.activeDraft);
  if (firstPlanReviewFired && !hasDraftPending && pendings.some((p: any) => p.kind === "plan_review")) {
    logEvent("PENDING_COALESCED_DROPPED", `pending coalesced dropped (first plan review already fired)`, {
      quest: slug,
      shard: "plan_review",
      staleCount: pendings.length,
      candidateCount: 0,
    });
    return null;
  }
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
      // 38: draft hash drift counts as boundary change even when lastPlanReviewBoundaryKey stale
      const pendingIsDraft = String(pending.boundaryKey || "").startsWith("draft:");
      const snapshotIsDraft = String(snapshot?.boundaryKey || "").startsWith("draft:");
      const draftDrift = pendingIsDraft && snapshotIsDraft && pending.boundaryKey !== snapshot.boundaryKey;
      const boundaryChanged = draftDrift || (snapshot.boundaryKey ? (latestBoundaryKey !== snapshot.boundaryKey) : stateChanged);
      needsFollowUp = (!isPlanReviewValidForStateLocal(targetState) || boundaryChanged || draftDrift);
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
  logEvent("PENDING_COALESCED_RESOLVED", `pending coalesced resolved (chosen=${chosen.kind})`, {
    quest: slug,
    chosenKind: chosen.kind,
    staleCount: stale.length,
    candidateCount: candidates.length,
  });
  clearPendingReview(slug, chosen.kind);
  // Also clear other stale candidates? keep one chosen; clear remaining candidates that are not chosen but still stale? They are not stale, but we coalesce to one.
  // Remove remaining candidates for same slug to avoid duplicate launches
  for (const c of candidates.slice(1)) clearPendingReview(slug, c.kind);
  return chosen;
}
