import { syncImplementationPermission } from "./gates.ts";
import {
  logEvent,
  logReassessmentTransition,
  logResearchTransition,
  tryLog,
} from "./logging.ts";
import { questPath } from "./paths.ts";
import { classifyInvestigationKind } from "./utils.ts";
import { InvestigationReceipt, StoredState } from "./types.ts";

export function startResearchEpoch(
  targetState: StoredState,
  epochType: "research" | "reassessment" = "research",
): InvestigationReceipt {
  targetState.investigationEpoch = (targetState.investigationEpoch || 0) + 1;
  const receipt: InvestigationReceipt = {
    epoch: targetState.investigationEpoch,
    epochType,
    startedAt: Date.now(),
    toolCalls: 0,
    readTargets: [],
    searchTargets: [],
    commands: [],
    evidenceCount: 0,
  };
  targetState.currentReceipt = receipt;
  if (epochType === "research") {
    logResearchTransition("RESEARCH_REQUIRED", "research required", {
      quest: targetState.active || "",
      round: targetState.researchRound || 1,
    });
  }
  return receipt;
}

export function recordObservedInvestigation(
  targetState: StoredState,
  toolName: string,
  input: any,
  output?: any,
  isError = false,
): boolean {
  if (isError) return false;
  const normName = (toolName || "").toLowerCase().trim();
  if (!normName) return false;

  // Classify investigation value: only operations with kind !== "none" count as research evidence!
  const investigation = classifyInvestigationKind(normName, input);
  if (investigation.kind === "none") {
    return false;
  }

  if (
    !targetState.currentReceipt ||
    targetState.currentReceipt.epoch !== (targetState.investigationEpoch || 1)
  ) {
    targetState.currentReceipt = {
      epoch: targetState.investigationEpoch || 1,
      epochType: targetState.reassessmentRequired ? "reassessment" : "research",
      startedAt: Date.now(),
      toolCalls: 0,
      readTargets: [],
      searchTargets: [],
      commands: [],
      evidenceCount: 0,
    };
  } else if (
    targetState.reassessmentRequired &&
    targetState.currentReceipt.epochType !== "reassessment"
  ) {
    // A3: re-stamp receipt when reassessmentRequired flips mid-epoch (rejection path)
    // Carry existing evidence (same-turn synthesis) so a read done in same turn
    // as the trigger/rejection is not lost to epoch staleness.
    targetState.currentReceipt.epochType = "reassessment";
  }

  const receipt = targetState.currentReceipt;

  if (investigation.kind === "file-read") {
    const target = investigation.target ||
      (typeof input === "string" ? input : input?.path || input?.file || "");
    if (target) {
      if (!receipt.readTargets.includes(target)) {
        receipt.readTargets.push(target);
      }
    }
    if (investigation.command) {
      if (!receipt.commands.includes(investigation.command)) {
        receipt.commands.push(investigation.command);
      }
    }
    receipt.evidenceCount++;
    receipt.toolCalls++;
    receipt.lastEvidenceAt = Date.now();

    logResearchTransition(
      "RESEARCH_EVIDENCE",
      `investigation evidence recorded (${investigation.kind})`,
      {
        quest: targetState.active || "",
        kind: investigation.kind,
        target: investigation.target || target,
        round: targetState.researchRound || 1,
        reads: receipt.readTargets.length,
        searches: receipt.searchTargets.length,
        evidence: receipt.evidenceCount,
      },
    );

    // 42: after sufficient evidence while drafting, clear NO_PROGRESS so gate can open via quest_update_state
    if (
      receipt.evidenceCount >= 5 && !targetState.active &&
      targetState.activeDraft
    ) {
      targetState.substantiveTurnsSinceCheckpoint = 0;
      if (!targetState.researchComplete) {
        targetState.researchComplete = true;
        targetState.researchRequired = false;
        try {
          syncImplementationPermission(targetState);
        } catch {}
        tryLog(
          "RESEARCH_COMPLETED",
          `research completed after ${receipt.evidenceCount} evidences`,
          {
            quest: targetState.activeDraft || "",
            round: targetState.researchRound || 1,
            evidence: receipt.evidenceCount,
          },
        );
      }
    }

    return true;
  }

  if (
    investigation.kind === "code-search" ||
    investigation.kind === "architecture-research" ||
    investigation.kind === "external-research"
  ) {
    if (investigation.target) {
      if (!receipt.searchTargets.includes(investigation.target)) {
        receipt.searchTargets.push(investigation.target);
      }
    }
    if (investigation.command) {
      if (!receipt.commands.includes(investigation.command)) {
        receipt.commands.push(investigation.command);
      }
    }
    receipt.evidenceCount++;
    receipt.toolCalls++;
    receipt.lastEvidenceAt = Date.now();

    logResearchTransition(
      "RESEARCH_EVIDENCE",
      `investigation evidence recorded (${investigation.kind})`,
      {
        quest: targetState.active || "",
        kind: investigation.kind,
        target: investigation.target || undefined,
        round: targetState.researchRound || 1,
        reads: receipt.readTargets.length,
        searches: receipt.searchTargets.length,
        evidence: receipt.evidenceCount,
      },
    );

    // 42: after sufficient evidence while drafting, clear NO_PROGRESS so gate can open via quest_update_state
    if (
      receipt.evidenceCount >= 5 && !targetState.active &&
      targetState.activeDraft
    ) {
      targetState.substantiveTurnsSinceCheckpoint = 0;
      if (!targetState.researchComplete) {
        targetState.researchComplete = true;
        targetState.researchRequired = false;
        try {
          syncImplementationPermission(targetState);
        } catch {}
        tryLog(
          "RESEARCH_COMPLETED",
          `research completed after ${receipt.evidenceCount} evidences`,
          {
            quest: targetState.activeDraft || "",
            round: targetState.researchRound || 1,
            evidence: receipt.evidenceCount,
          },
        );
      }
    }

    return true;
  }

  return false;
}

export function hasSufficientInvestigation(
  targetState: StoredState,
  expectedType: "research" | "reassessment" = "research",
): { sufficient: boolean; reason?: string; receipt?: InvestigationReceipt } {
  const r = targetState.currentReceipt;
  const requiredEpoch = targetState.investigationEpoch || 1;

  if (!r) {
    return {
      sufficient: false,
      reason:
        `No observed investigation receipt exists for epoch ${requiredEpoch}.`,
    };
  }

  if (r.isHistorical) {
    return {
      sufficient: false,
      reason:
        `Historical receipt cannot satisfy new investigation gate in active session.`,
      receipt: r,
    };
  }

  if (r.epoch !== requiredEpoch) {
    return {
      sufficient: false,
      reason:
        `Investigation receipt is from epoch ${r.epoch}, but current required epoch is ${requiredEpoch}.`,
      receipt: r,
    };
  }

  if (
    expectedType === "reassessment" && targetState.reassessmentRequired &&
    r.epochType !== "reassessment"
  ) {
    return {
      sufficient: false,
      reason:
        `Investigation receipt was for initial research (epoch ${r.epoch}), but a fresh post-trigger investigation is required for reassessment epoch ${requiredEpoch}. Run a read/code-search now; the extension records a fresh receipt automatically while reassessment is pending.`,
      receipt: r,
    };
  }

  const hasRead = Array.isArray(r.readTargets) && r.readTargets.length > 0;
  const hasSearch = Array.isArray(r.searchTargets) &&
    r.searchTargets.length > 0;
  const hasCmd = Array.isArray(r.commands) && r.commands.length > 0;
  const totalEvidence = r.evidenceCount || 0;

  if (totalEvidence === 0 || (!hasRead && !hasSearch && !hasCmd)) {
    const phaseLabel = expectedType === "reassessment"
      ? `Reassessment v${targetState.reassessmentVersion || 1}`
      : `Research Round ${targetState.researchRound || 1}`;
    return {
      sufficient: false,
      reason:
        `Zero investigation tool calls or commands observed since ${phaseLabel} began.`,
      receipt: r,
    };
  }

  return { sufficient: true, receipt: r };
}

export function formatInvestigationEvidenceSummary(
  receipt: InvestigationReceipt,
): string {
  if (receipt.isHistorical) {
    return `Historical verified research`;
  }
  const items: string[] = [];
  if (receipt.readTargets && receipt.readTargets.length > 0) {
    items.push(
      `read [${receipt.readTargets.slice(0, 3).join(", ")}${
        receipt.readTargets.length > 3 ? "..." : ""
      }]`,
    );
  }
  if (receipt.searchTargets && receipt.searchTargets.length > 0) {
    items.push(
      `search [${receipt.searchTargets.slice(0, 3).join(", ")}${
        receipt.searchTargets.length > 3 ? "..." : ""
      }]`,
    );
  }
  if (receipt.commands && receipt.commands.length > 0) {
    items.push(
      `commands [${receipt.commands.slice(0, 3).join(", ")}${
        receipt.commands.length > 3 ? "..." : ""
      }]`,
    );
  }
  const detailsStr = items.length > 0
    ? items.join(", ")
    : "verified inspection";
  const dateStr = receipt.completedAt
    ? new Date(receipt.completedAt).toISOString()
    : new Date().toISOString();
  return `Epoch ${receipt.epoch} (${receipt.epochType}): ${detailsStr} (evidence count: ${receipt.evidenceCount}, completed: ${dateStr})`;
}

export function triggerReassessment(
  targetState: StoredState,
  reason: string,
  evidence?: string | null,
) {
  // A3: capture same-turn evidence from the current receipt before bumping epoch.
  // Only carry if evidence is recent (lastEvidenceAt within ~2s) — stale evidence from prior turns must not satisfy fresh gate.
  const cur = targetState.currentReceipt;
  const isRecent = cur?.lastEvidenceAt &&
    (Date.now() - cur.lastEvidenceAt) < 3000;
  const prevReceipt =
    cur && cur.epoch === (targetState.investigationEpoch || 1) &&
      (cur.evidenceCount || 0) > 0 && isRecent
      ? {
        ...cur,
        readTargets: [...(cur.readTargets || [])],
        searchTargets: [...(cur.searchTargets || [])],
        commands: [...(cur.commands || [])],
      }
      : null;
  targetState.reassessmentVersion = (targetState.reassessmentVersion || 0) + 1;
  targetState.reassessmentRequired = true;
  targetState.reassessmentReason = reason;
  targetState.reassessmentEvidence = evidence ?? null;
  targetState.researchRequired = true;
  targetState.researchComplete = false;
  // A1: preserve stated confidence — do NOT force planConfidence="low" on trigger.
  targetState.researchRound = (targetState.researchRound || 1) + 1;
  targetState.dirty = true;
  targetState.awaitingUserConfirmation = false;
  if (Array.isArray(targetState.confirmedQuests) && targetState.active) {
    targetState.confirmedQuests = targetState.confirmedQuests.filter((q) =>
      q !== targetState.active
    );
  }
  const newReceipt = startResearchEpoch(targetState, "reassessment");
  // A3: carry same-turn evidence into the new reassessment epoch so a read done
  // in the same turn as the trigger satisfies the fresh-investigation gate.
  if (prevReceipt) {
    newReceipt.readTargets = prevReceipt.readTargets || [];
    newReceipt.searchTargets = prevReceipt.searchTargets || [];
    newReceipt.commands = prevReceipt.commands || [];
    newReceipt.evidenceCount = prevReceipt.evidenceCount || 0;
    newReceipt.toolCalls = prevReceipt.toolCalls || 0;
    newReceipt.lastEvidenceAt = prevReceipt.lastEvidenceAt;
  }
  syncImplementationPermission(targetState);

  logReassessmentTransition(
    "REASSESSMENT_REQUIRED",
    `reassessment triggered: ${reason}`,
    {
      quest: targetState.active || "",
      reason,
      reassessmentVersion: targetState.reassessmentVersion,
      version: targetState.reassessmentVersion,
      round: targetState.researchRound,
      failureId: targetState.lastFailureId || undefined,
      consequence: "GATE_BLOCKED_REASSESSMENT_PENDING",
    },
  );
}

export function buildReassessmentPrompt(
  activeQuest: string,
  reason: string,
  evidence?: string | null,
  planVersion?: number,
): string {
  const evidenceBlock = evidence
    ? `\n\n**Contradictory Evidence & Observed Output**:\n\`\`\`\n${
      evidence.slice(0, 1500)
    }\n\`\`\``
    : "";
  const versionStr = typeof planVersion === "number"
    ? ` (current plan: v${planVersion})`
    : "";

  return `⚡ **Quest Reassessment Required**: Evidence has been encountered that challenges or invalidates the current plan for **${activeQuest}**${versionStr}.

**Trigger Reason**: ${reason}${evidenceBlock}

**Reassessment & Falsification Directive**:
Do not blindly push forward with the current plan, and do not merely declare the plan valid without investigation.

1. **Recover Prior Belief**: What did we assume or expect to happen?
2. **Inspect New Evidence**: What specific contradiction, test failure, unexpected execution path, or child finding occurred?
3. **Perform Targeted Fresh Investigation**: Use \`read\` / \`search_graph\` to investigate the specific contradiction. Determine where the mental model diverged from reality.
4. **Validate or Falsify Assumptions**: Determine whether previous assumptions were invalid or need reformulation.
5. **Decide Plan Validity & Revise**: Decide whether the plan survives or requires revision. If revising, record the previous plan, invalidating evidence, and new plan under \`## Plan Revisions\` and \`## Rejected Approaches\`.
6. **Persist & Explicitly Complete Reassessment**:
   Do not call \`reassessmentComplete\` merely because the quest file was updated.
   Reassessment is complete only after you have:
   - investigated the triggering contradiction;
   - established what was actually true;
   - validated, invalidated, or reformulated the relevant assumptions;
   - determined whether the current plan survives;
   - revised the plan if necessary;
   - persisted the new evidence and reasoning in \`${questPath(activeQuest)}\`;
   - provided a justified confidence level and concrete next action via \`quest_update_state({ reassessmentComplete: true, ... })\`.
7. **Continue Execution**: Proceed autonomously with the revised Exact Next Action.`;
}

export function buildResearchCheckpointPrompt(
  activeQuest: string,
  planVersion?: number,
  planConfidence?: string,
): string {
  const verStr = typeof planVersion === "number" ? `v${planVersion}` : "v1";
  const confStr = planConfidence || "unspecified";

  return `⚡ **Periodic Durable Memory & Reasoning Checkpoint**: Substantial execution has occurred on **${activeQuest}** (Current Plan: ${verStr}, Confidence: ${confStr}).

**Reasoning & Decision Questions**:
1. **Discoveries & Learnings**: What concrete facts or data paths have been established in recent turns?
2. **Assumption Verification**: Have any key assumptions been disproved or weakened?
3. **Plan Evaluation & Epistemic Action**: Choose the most justified path and act accordingly:
   - **If current understanding is well supported** -> Save durable state and continue execution.
   - **If an important uncertainty exists** -> Perform targeted research pass before continuing to write code.
   - **If evidence contradicts current model** -> Enter reassessment and update \`## Rejected Approaches\`.
   - **If plan changed** -> Update plan and record the revision in \`## Plan Revisions\` (auto-increments planVersion).
4. **Exact Next Action**: What is the most justified immediate execution step?

**Action Required**:
Update \`${
    questPath(activeQuest)
  }\` (using \`quest_update_state\` or edit + \`quest_mark_saved\`), then proceed autonomously.`;
}
