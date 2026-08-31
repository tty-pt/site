import { QuestLogEntry } from "../types.ts";
import { SummaryState } from "./state.ts";

export function handleResearchEntry(entry: QuestLogEntry, s: SummaryState): void {
  if (entry.context.round) {
    const r = parseInt(entry.context.round, 10);
    if (!isNaN(r) && r > s.researchCycles) s.researchCycles = r;
  } else if (entry.message.includes("complete") || entry.message.includes("round") || entry.type === "RESEARCH_COMPLETED") {
    s.researchCycles++;
  }
}

export function handleReassessmentEntry(entry: QuestLogEntry, s: SummaryState): void {
  if (entry.context.version || entry.context.reassessmentVersion) {
    const v = parseInt(entry.context.reassessmentVersion || entry.context.version || "1", 10);
    if (!isNaN(v) && v > s.reassessmentCycles) s.reassessmentCycles = v;
  } else {
    s.reassessmentCycles++;
  }
}

export function handleImplementationEntry(entry: QuestLogEntry, s: SummaryState): void {
  if (entry.type === "IMPLEMENTATION_ATTEMPT") {
    s.implementationAttempts++;
    if (entry.context.allowed === "true" || entry.message.startsWith("allowed")) s.implementationAllowedCount++;
  } else if (entry.type === "IMPLEMENTATION_ALLOWED") {
    if (s.implementationAllowedCount === 0 || s.implementationAttempts > s.implementationAllowedCount) s.implementationAllowedCount++;
  } else if (entry.type === "GATE_BLOCKED" || entry.type === "IMPLEMENTATION_BLOCKED") {
    s.implementationBlockedCount++;
    if (entry.context.gate) s.blockedGates.add(entry.context.gate);
    else if (entry.message.includes("RESEARCH_PENDING")) s.blockedGates.add("RESEARCH_PENDING");
  } else if (entry.type === "GATE_OPENED") {
    if (entry.context.from) s.blockedGates.delete(entry.context.from);
  }
}

export function handleTestEntry(entry: QuestLogEntry, s: SummaryState): void {
  if (entry.type === "TEST_STARTED" || entry.type === "BUILD_STARTED") {
    s.totalTestsRun++;
    s.lastTestCommand = entry.context.command;
  } else if (entry.type === "TEST_PASSED" || entry.type === "BUILD_PASSED") {
    s.totalTestsRun++;
    s.testsPassedCount++;
    s.lastPassedCommand = entry.context.command;
    s.lastTestCommand = entry.context.command;
    s.lastTestStatus = "PASSED";
    for (let i = s.failures.length - 1; i >= 0; i--) {
      if (s.failures[i].type === "TEST_FAILED" || s.failures[i].type === "BUILD_FAILED" || s.failures[i].type === "TEST_FAILURE") {
        s.failures[i].recovered = true;
        s.failures[i].recoveryAction = `Passing test: ${entry.context.command || "test passed"}`;
      }
    }
  } else if (entry.type === "TEST_FAILED" || entry.type === "BUILD_FAILED" || entry.type === "TEST_FAILURE") {
    s.totalTestsRun++;
    s.testsFailedCount++;
    s.lastFailedCommand = entry.context.command;
    s.lastTestCommand = entry.context.command;
    s.lastFailureReason = entry.context.reason || entry.message;
    s.lastTestStatus = "FAILED";
    s.failureCount++;
    const fIdx = s.failures.length;
    s.failures.push({ type: entry.type, code: entry.context.code, reason: entry.context.reason || entry.message, failureId: entry.context.failureId, recovered: false });
    if (entry.context.failureId) s.failureIdMap.set(entry.context.failureId, fIdx);
  }
}

export function handleToolEntry(entry: QuestLogEntry, s: SummaryState): void {
  if (entry.type === "TOOL_FAILURE" || entry.type === "TOOL_TIMEOUT" || entry.type === "TOOL_CANCELLED") {
    s.failureCount++;
    const fIdx = s.failures.length;
    s.failures.push({ type: entry.type, code: entry.context.code, reason: entry.context.reason || entry.message, failureId: entry.context.failureId, recovered: false });
    if (entry.context.failureId) s.failureIdMap.set(entry.context.failureId, fIdx);
  } else if (entry.type === "ERROR" || entry.type === "SAVE_FAILED" || entry.type === "RESUME_FAILED" || entry.type === "RECOVERY_FAILED") {
    s.failureCount++;
    s.hasUnresolvedError = true;
    s.lastError = entry.message;
    s.failures.push({ type: entry.type, code: entry.context.code, reason: entry.context.reason || entry.message, failureId: entry.context.failureId, recovered: false });
  }
}

export function handleCompactionEntry(entry: QuestLogEntry, s: SummaryState): void {
  if (entry.type === "COMPACTION_FAILED") {
    s.failureCount++;
    s.hasUnresolvedError = true;
    s.lastError = entry.message;
    s.failures.push({ type: entry.type, code: entry.context.code, reason: entry.context.reason || entry.message, failureId: entry.context.failureId, recovered: false });
  }
  if (entry.type === "COMPACTION_BLOCKED" || entry.type === "COMPACTION_INVALIDATED") return;
  const cmpId = entry.context.compactionId || (entry.quest && entry.quest !== "(none)" ? `cmp_${entry.quest}` : `cmp_anon_${++s.anonCompactionCounter}`);
  if (!s.compactionsMap.has(cmpId)) {
    s.compactionsMap.set(cmpId, { id: cmpId, status: entry.message, phases: new Set([entry.type]), success: entry.type === "COMPACTION_COMPLETED", failed: entry.type === "COMPACTION_FAILED", inconsistent: entry.type === "COMPACTION_INCONSISTENT" || entry.type === "COMPACTION_EXTERNAL" });
  } else {
    const existing = s.compactionsMap.get(cmpId)!;
    existing.phases.add(entry.type);
    existing.status = entry.message;
    if (entry.type === "COMPACTION_COMPLETED") existing.success = true;
    if (entry.type === "COMPACTION_FAILED") existing.failed = true;
    if (entry.type === "COMPACTION_INCONSISTENT" || entry.type === "COMPACTION_EXTERNAL") existing.inconsistent = true;
  }
}

export function handleResumeEntry(entry: QuestLogEntry, s: SummaryState): void {
  const resId = entry.context.compactionId || entry.context.obligationId || entry.context.id || entry.context.child || (entry.quest && entry.quest !== "(none)" ? `res_${entry.quest}` : `res_anon_${++s.anonResumeCounter}`);
  if (!s.resumesMap.has(resId)) {
    s.resumesMap.set(resId, { id: resId, success: entry.type === "RESUME_DELIVERED", failed: entry.type === "RESUME_FAILED", retried: entry.type === "RESUME_RETRIED" ? 1 : 0, obsolete: entry.type === "RESUME_OBSOLETED" });
  } else {
    const existing = s.resumesMap.get(resId)!;
    if (entry.type === "RESUME_DELIVERED") { existing.success = true; existing.failed = false; }
    else if (entry.type === "RESUME_FAILED") { if (!existing.success) existing.failed = true; }
    else if (entry.type === "RESUME_RETRIED") existing.retried++;
    else if (entry.type === "RESUME_OBSOLETED") existing.obsolete = true;
  }
  if (s.hasUnresolvedError && entry.type === "RESUME_DELIVERED") s.hasUnresolvedError = false;
}

export function handleCriticalReviewEntry(entry: QuestLogEntry, s: SummaryState): void {
  if (entry.type === "CRITICAL_REVIEW_PASSED") { s.hasCriticalReviewFailure = false; s.criticalReviewPassed = true; }
  else if (entry.type === "CRITICAL_REVIEW_FAILED" || entry.type === "CRITICAL_REVIEW_UNCERTAIN" || entry.type === "CRITICAL_REVIEW_ERROR") {
    s.hasCriticalReviewFailure = true; s.criticalReviewPassed = false; s.failureCount++;
    s.failures.push({ type: entry.type, code: entry.context.code || entry.type, reason: entry.context.reason || entry.message, recovered: false });
  } else if (entry.type === "DIRECTION_REVIEW_THROTTLED" || entry.type === "GLOBAL_REVIEW_CAP_HIT" || entry.type === "CRITICAL_REVIEW_SUPPRESSED_DUPLICATE" || entry.type === "CRITICAL_REVIEW_COALESCED") {
    // Count throttling/cap as verification activity, not failure
  }
}

export function routeEntry(entry: QuestLogEntry, s: SummaryState): void {
  switch (entry.type) {
    case "RESEARCH_REQUIRED": case "RESEARCH_EVIDENCE": case "RESEARCH_COMPLETED": handleResearchEntry(entry, s); break;
    case "REASSESSMENT_REQUIRED": case "REASSESSMENT_EVIDENCE": case "REASSESSMENT_COMPLETED": handleReassessmentEntry(entry, s); break;
    case "IMPLEMENTATION_ATTEMPT": case "IMPLEMENTATION_ALLOWED": case "GATE_BLOCKED": case "IMPLEMENTATION_BLOCKED": case "GATE_OPENED": handleImplementationEntry(entry, s); break;
    case "TEST_STARTED": case "BUILD_STARTED": case "TEST_PASSED": case "BUILD_PASSED": case "TEST_FAILED": case "BUILD_FAILED": case "TEST_FAILURE": handleTestEntry(entry, s); break;
    case "TOOL_FAILURE": case "TOOL_TIMEOUT": case "TOOL_CANCELLED": case "ERROR": case "SAVE_FAILED": case "RESUME_FAILED": case "RECOVERY_FAILED": handleToolEntry(entry, s); break;
    case "COMPACTION_PREPARED": case "COMPACTION_INVALIDATED": case "COMPACTION_STARTED": case "COMPACTION_COMPLETED": case "COMPACTION_FAILED": case "COMPACTION_INCONSISTENT": case "COMPACTION_EXTERNAL": case "COMPACTION_BLOCKED": handleCompactionEntry(entry, s); break;
    case "RESUME_OBLIGATION_CREATED": case "RESUME_ATTEMPTED": case "RESUME_DELIVERED": case "RESUME_FAILED": case "RESUME_RETRIED": case "RESUME_RECONCILIATION_REQUIRED": case "RESUME_OBSOLETED": handleResumeEntry(entry, s); break;
    case "CRITICAL_REVIEW_PASSED": case "CRITICAL_REVIEW_FAILED": case "CRITICAL_REVIEW_UNCERTAIN": case "CRITICAL_REVIEW_ERROR": case "DIRECTION_REVIEW_THROTTLED": case "GLOBAL_REVIEW_CAP_HIT": case "CRITICAL_REVIEW_SUPPRESSED_DUPLICATE": case "CRITICAL_REVIEW_COALESCED": handleCriticalReviewEntry(entry, s); break;
    case "NO_PROGRESS": case "REPEATED_BLOCK": case "REPEATED_FAILURE": s.deadlockWarnings.push(`${entry.type}: ${entry.message}`); break;
    default: break;
  }
}
