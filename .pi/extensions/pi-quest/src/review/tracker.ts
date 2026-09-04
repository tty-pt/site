// HIGH_LEVEL: #drafting — every content-changing save boots a fresh reviewer.
// HIGH_LEVEL: #stale results — a result is valid only for its target revision.
// HIGH_LEVEL: #tools (other agents) — reviewer sessions are tracked for the read-only gate.
// SPEC: B1.3 (supersede rule).
interface InFlight {
  target: string;
  abort: () => void;
  childSessionId?: string;
}

const inFlight = new Map<string, InFlight>();

export function trackReview(qid: string, target: string, abort: () => void): void {
  cancelReview(qid);
  inFlight.set(qid, { target, abort });
}

export function noteReviewerSession(qid: string, childSessionId: string): void {
  const current = inFlight.get(qid);
  if (current) inFlight.set(qid, { ...current, childSessionId });
}

export function isReviewerSession(sessionId: string): boolean {
  for (const review of inFlight.values()) {
    if (review.childSessionId === sessionId) return true;
  }
  return false;
}

export function cancelReview(qid: string): void {
  const current = inFlight.get(qid);
  if (current === undefined) return;
  inFlight.delete(qid);
  try {
    current.abort();
  } catch {
    // Abort is best-effort; the entry is already gone.
  }
}

export function supersedeReviewThenBootFresh(qid: string, target: string, boot: () => void): void {
  cancelReview(qid);
  trackReview(qid, target, () => {});
  boot();
}

export function isCurrentReview(qid: string, target: string): boolean {
  return inFlight.get(qid)?.target === target;
}

export function hasInFlight(qid: string): boolean {
  return inFlight.has(qid);
}

export function settleReview(qid: string, target: string): boolean {
  if (!isCurrentReview(qid, target)) return false;
  inFlight.delete(qid);
  return true;
}
