// HIGH_LEVEL: #review and validation communication.
// HIGH_LEVEL: #review request — brief identifies qid, type, target, plan, evidence, criteria.
// HIGH_LEVEL: #review result — exactly one PASS | FAIL with findings.
// HIGH_LEVEL: #stale results — valid only for the named target.
// HIGH_LEVEL: #rebuttal — recorded as a new event, may reopen the question.
// HIGH_LEVEL: #reviewer independence — minimal info, judge and report.
// HIGH_LEVEL: #validator communication — same protocol against plan + amendments.
// HIGH_LEVEL: #no direct mutation — reviewers supply evidence, never transitions.
import type { Qid } from "../domain/qid";
import type { ReviewKind } from "../domain/quest";

export interface ReviewRequest {
  qid: Qid;
  kind: ReviewKind;
  target: string;
  plan?: string;
  evidence: string[];
  criteria: string[];
}

export type Verdict = "PASS" | "FAIL";

export interface ReviewResult {
  verdict: Verdict;
  target: string;
  findings: string;
}

export function isResultValidFor(result: ReviewResult, currentTarget: string): boolean {
  return result.target === currentTarget;
}

export interface Rebuttal {
  target: string;
  finding: string;
  evidence: string;
}
