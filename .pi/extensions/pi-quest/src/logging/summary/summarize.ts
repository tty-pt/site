import { parseLogEntry } from "../formatters.ts";
import { QuestLogEntry, QuestRunSummary } from "../types.ts";
import { mapEventTypeToMajorPhase } from "./phase_map.ts";
import { parseQuestLogEntries } from "./helpers.ts";
import { createInitialState, SummaryState } from "./state.ts";
import { routeEntry } from "./reducers.ts";

// helpers extracted from original summary.ts to keep file <350
function buildFormattedLines(s: SummaryState, formattedExtra: { lastTestStatus: string; lastFailureReason?: string; lastPassedCommand?: string; lastFailedCommand?: string; failureCount: number; unrecoveredFailures: any[]; failures: any[]; hasUnresolvedError: boolean; lastError?: string; implementationAllowedCount: number; }): string[] {
  const quests = Array.from(s.quests);
  const majorPhases = Array.from(s.majorPhases);
  const blockedGates = Array.from(s.blockedGates);
  const compactionCount = s.compactionsMap.size;
  let successfulCompactions = 0, failedCompactions = 0, inconsistentCompactions = 0;
  for (const cmp of s.compactionsMap.values()) { if (cmp.success) successfulCompactions++; if (cmp.failed) failedCompactions++; if (cmp.inconsistent) inconsistentCompactions++; }
  const resumeCount = s.resumesMap.size;
  let resumeSuccessCount = 0, resumeFailedCount = 0, resumePendingCount = 0;
  for (const res of s.resumesMap.values()) { if (res.success) resumeSuccessCount++; else if (res.failed) resumeFailedCount++; else if (!res.obsolete) resumePendingCount++; }

  let terminalVerdictReason = "";
  if (s.hasUnresolvedError) terminalVerdictReason = `Unresolved error: ${s.lastError || "unknown"}`;
  else if (s.hasCriticalReviewFailure) terminalVerdictReason = `Critical review failed without approval`;
  else if (formattedExtra.lastTestStatus === "FAILED") terminalVerdictReason = `Unrecovered test failure: ${formattedExtra.lastFailureReason || formattedExtra.lastFailedCommand || "test failure"}`;
  else if (formattedExtra.unrecoveredFailures.length > 0) terminalVerdictReason = `Unrecovered failure: ${formattedExtra.unrecoveredFailures[0].type} (${formattedExtra.unrecoveredFailures[0].reason || "unknown"})`;
  else if (formattedExtra.lastTestStatus === "PASSED") terminalVerdictReason = `All tests verified clean (${formattedExtra.lastPassedCommand || "tests passed"}), zero unresolved errors`;
  else if (s.implementationAllowedCount > 0) terminalVerdictReason = `Implementation completed with zero unresolved errors`;
  else terminalVerdictReason = `Run executed cleanly without errors`;

  const lines: string[] = [
    `=== Quest Journal Run Summary ===`,
    `Quests Tracked (${quests.length}): ${quests.join(", ") || "(none)"}`,
    `Phases Observed: ${majorPhases.join(", ") || "(none)"}`,
    `Research Rounds: ${s.researchCycles}`,
    `Reassessment Cycles: ${s.reassessmentCycles}`,
    `Implementation Attempts: ${s.implementationAttempts} (allowed: ${s.implementationAllowedCount}, blocked: ${s.implementationBlockedCount})`,
    `Blocked Gates: ${blockedGates.join(", ") || "(none)"}`,
    `Compactions (${compactionCount}): Total ${compactionCount} (successful: ${successfulCompactions}, failed: ${failedCompactions}, inconsistent/external: ${inconsistentCompactions})`,
    `Resumes (${resumeCount}): Total ${resumeCount} (successful: ${resumeSuccessCount}, failed: ${resumeFailedCount}, pending: ${resumePendingCount})`,
    `Total Failures: ${formattedExtra.failureCount} (unrecovered: ${formattedExtra.unrecoveredFailures.length})`,
    `Tests: ${formattedExtra.lastTestStatus === "PASSED" ? `PASSED (${formattedExtra.lastPassedCommand || "verified"})` : formattedExtra.lastTestStatus === "FAILED" ? `FAILED (${formattedExtra.lastFailureReason || formattedExtra.lastFailedCommand})` : "NOT_RUN"}`,
  ];
  if (s.deadlockWarnings.length > 0) { lines.push(`Flow Warnings / Deadlocks (${s.deadlockWarnings.length}):`); for (const w of s.deadlockWarnings.slice(0, 5)) lines.push(`  - ${w}`); }
  if (s.failures.length > 0) {
    lines.push(`Recorded Failures (${s.failures.length}):`);
    for (const f of s.failures.slice(0, 5)) {
      const recStr = f.recovered ? ` [RECOVERED via ${f.recoveryAction || "subsequent action"}]` : " [UNRECOVERED]";
      lines.push(`  - [${f.type}] ${f.code ? `(${f.code}) ` : ""}${f.reason || "unknown"}${recStr}`);
    }
    if (s.failures.length > 5) lines.push(`  ... and ${s.failures.length - 5} more failures`);
  }
  if (s.hasUnresolvedError) lines.push(`Status: UNRESOLVED ERROR (${s.lastError || "unknown"})`);
  else if (formattedExtra.unrecoveredFailures.length > 0 || formattedExtra.lastTestStatus === "FAILED") lines.push(`Status: UNRECOVERED FAILURES (${terminalVerdictReason})`);
  else lines.push(`Status: CLEAN / RECOVERED (${terminalVerdictReason})`);
  return lines;
}

export function summarizeQuestJournalLog(rawOrQid: string): QuestRunSummary {
  const entries = parseQuestLogEntries(rawOrQid);
  const s = createInitialState();

  // pre-scan for major phases, modified files, causal chain, recovery linkages
  for (const entry of entries) {
    const qName = entry.quest || entry.context.root || entry.context.rootQuest;
    if (qName && qName !== "(none)") s.quests.add(qName);
    const majorPhase = mapEventTypeToMajorPhase(entry.type, entry.context);
    if (majorPhase) s.majorPhases.add(majorPhase);
    if (entry.context.filesModified) {
      for (const f of entry.context.filesModified.split(",")) { const t = f.trim(); if (t) s.modifiedFilesSet.add(t); }
    } else if (entry.context.path && (entry.context.operation === "success" || entry.type === "IMPLEMENTATION_COMPLETED")) {
      if (entry.context.tool === "edit" || entry.context.tool === "write" || entry.type === "IMPLEMENTATION_COMPLETED") s.modifiedFilesSet.add(entry.context.path);
    }
    if (entry.context.completed) s.completedTasksSet.add(entry.context.completed);
    if (entry.type === "TURN_START" || entry.type === "TURN_END" || entry.type === "TOOL_ACTIVITY" || entry.type === "TEST_FAILED" || entry.type === "TEST_PASSED" || entry.type === "BUILD_FAILED" || entry.type === "BUILD_PASSED" || entry.type === "GATE_BLOCKED" || entry.type === "GATE_OPENED" || entry.type === "REASSESSMENT_REQUIRED" || entry.type === "REASSESSMENT_COMPLETED" || entry.type === "RESEARCH_COMPLETED" || entry.type === "STATE_UPDATE_ACCEPTED") {
      s.causalChain.push({ turn: entry.context.turn, correlationId: entry.context.correlationId, phase: entry.context.phase || majorPhase || undefined, intent: entry.context.intent, action: entry.context.action || entry.context.command || entry.context.tool || entry.type, result: entry.context.result || entry.context.operation || entry.context.outcome, consequence: entry.context.consequence || (entry.type === "GATE_BLOCKED" ? "OPERATION_BLOCKED" : entry.type === "REASSESSMENT_REQUIRED" ? "REASSESSMENT_TRIGGERED" : undefined), recoveryFor: entry.context.recoveryFor, failureId: entry.context.failureId, timestamp: entry.timestamp });
    }
    if (entry.context.recoveryFor) {
      const priorIdx = s.failureIdMap.get(entry.context.recoveryFor);
      if (priorIdx !== undefined && s.failures[priorIdx]) { s.failures[priorIdx].recovered = true; s.failures[priorIdx].recoveryAction = `${entry.type} (${entry.context.tool || entry.context.command || entry.message})`; }
    }
    routeEntry(entry, s);
  }

  for (const f of s.failures) if (!f.recovered) s.unrecoveredFailures.push({ type: f.type, code: f.code, reason: f.reason, failureId: f.failureId });

  const compactionCount = s.compactionsMap.size;
  let successfulCompactions = 0, failedCompactions = 0, inconsistentCompactions = 0;
  const compactions: Array<{ id?: string; status: string; phase?: string }> = [];
  for (const cmp of s.compactionsMap.values()) { if (cmp.success) successfulCompactions++; if (cmp.failed) failedCompactions++; if (cmp.inconsistent) inconsistentCompactions++; compactions.push({ id: cmp.id, status: cmp.status, phase: Array.from(cmp.phases).pop() }); }
  const resumeCount = s.resumesMap.size;
  let resumeSuccessCount = 0, resumeFailedCount = 0, resumePendingCount = 0;
  for (const res of s.resumesMap.values()) { if (res.success) resumeSuccessCount++; else if (res.failed) resumeFailedCount++; else if (!res.obsolete) resumePendingCount++; }

  let terminalVerdictReason = "";
  if (s.hasUnresolvedError) terminalVerdictReason = `Unresolved error: ${s.lastError || "unknown"}`;
  else if (s.hasCriticalReviewFailure) terminalVerdictReason = `Critical review failed without approval`;
  else if (s.lastTestStatus === "FAILED") terminalVerdictReason = `Unrecovered test failure: ${s.lastFailureReason || s.lastFailedCommand || "test failure"}`;
  else if (s.unrecoveredFailures.length > 0) terminalVerdictReason = `Unrecovered failure: ${s.unrecoveredFailures[0].type} (${s.unrecoveredFailures[0].reason || "unknown"})`;
  else if (s.lastTestStatus === "PASSED") terminalVerdictReason = `All tests verified clean (${s.lastPassedCommand || "tests passed"}), zero unresolved errors`;
  else if (s.implementationAllowedCount > 0) terminalVerdictReason = `Implementation completed with zero unresolved errors`;
  else terminalVerdictReason = `Run executed cleanly without errors`;

  const formattedLines = buildFormattedLines(s, { lastTestStatus: s.lastTestStatus, lastFailureReason: s.lastFailureReason, lastPassedCommand: s.lastPassedCommand, lastFailedCommand: s.lastFailedCommand, failureCount: s.failureCount, unrecoveredFailures: s.unrecoveredFailures, failures: s.failures, hasUnresolvedError: s.hasUnresolvedError, lastError: s.lastError, implementationAllowedCount: s.implementationAllowedCount });

  return {
    quests: Array.from(s.quests),
    majorPhases: Array.from(s.majorPhases),
    researchCycles: s.researchCycles,
    reassessmentCycles: s.reassessmentCycles,
    implementationAttempts: s.implementationAttempts,
    implementationAllowedCount: s.implementationAllowedCount,
    implementationBlockedCount: s.implementationBlockedCount,
    blockedGates: Array.from(s.blockedGates),
    failureCount: s.failureCount,
    failures: s.failures,
    unrecoveredFailures: s.unrecoveredFailures,
    compactionCount,
    successfulCompactions,
    failedCompactions,
    inconsistentCompactions,
    compactions,
    resumeCount,
    resumeSuccessCount,
    resumeFailedCount,
    resumePendingCount,
    hasUnresolvedError: s.hasUnresolvedError,
    hasCriticalReviewFailure: s.hasCriticalReviewFailure,
    criticalReviewPassed: s.criticalReviewPassed,
    lastError: s.lastError,
    deadlockWarnings: s.deadlockWarnings,
    testVerification: { status: s.lastTestStatus, lastTestCommand: s.lastTestCommand, lastPassedCommand: s.lastPassedCommand, lastFailedCommand: s.lastFailedCommand, lastFailureReason: s.lastFailureReason, totalTestsRun: s.totalTestsRun, testsPassed: s.testsPassedCount, testsFailed: s.testsFailedCount },
    implementationSummary: { totalAttempts: s.implementationAttempts, allowed: s.implementationAllowedCount, blocked: s.implementationBlockedCount, blockedGates: Array.from(s.blockedGates), completedTasks: Array.from(s.completedTasksSet), modifiedFiles: Array.from(s.modifiedFilesSet) },
    causalChain: s.causalChain,
    terminalVerdictReason,
    formattedSummary: formattedLines.join("\n"),
  };
}
