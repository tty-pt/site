import {
  canonicalizeFilePath,
  extractFileNames,
  extractIdentifiers,
  extractLines,
  isModificationStatement,
  pastToPresentVerbs,
} from "../helpers.ts";
import { isPlaceholderOrEmpty } from "../../utils.ts";

function checkNextAction(
  sections: {
    completedBody: string;
    reassessmentBody: string;
    nextActionBody: string;
  },
  issues: string[],
): void {
  const { completedBody, reassessmentBody, nextActionBody } = sections;
  const completedLines = extractLines(completedBody);
  const reassessmentLines = extractLines(reassessmentBody);
  const nextActionText = nextActionBody.replace(/^>\s*/gm, "").trim();
  if (!nextActionText || isPlaceholderOrEmpty(nextActionText)) return;
  const nextActionLower = nextActionText.toLowerCase();
  const nextActionFiles = extractFileNames(nextActionText);
  const nextActionIdents = extractIdentifiers(nextActionText);
  const completedStatements = [...completedLines, ...reassessmentLines];
  for (const stmt of completedStatements) {
    const stmtLower = stmt.toLowerCase();
    const stmtFiles = extractFileNames(stmt);
    const stmtIdents = extractIdentifiers(stmt);
    const sharedFiles = stmtFiles.filter((f) => nextActionFiles.includes(f));
    const sharedIdents = stmtIdents.filter((id) =>
      nextActionIdents.includes(id)
    );
    let verbConflict = false;
    for (const [pastVerb, presVerbs] of Object.entries(pastToPresentVerbs)) {
      if (stmtLower.includes(pastVerb)) {
        for (const presVerb of presVerbs) {
          if (
            nextActionLower.startsWith(presVerb) ||
            nextActionLower.includes(` ${presVerb} `)
          ) {
            verbConflict = true;
            break;
          }
        }
      }
      if (verbConflict) break;
    }
    // C2: require shared file/ident for substring match and expand verb-strip to all past verbs
    const pastVerbsPattern = Object.keys(pastToPresentVerbs).join("|");
    const cleanedStmt = stmtLower.replace(
      new RegExp(`^(${pastVerbsPattern})\\s+`),
      "",
    );
    const substringOverlap = stmtLower.length > 15 && cleanedStmt.length > 5 &&
      nextActionLower.includes(cleanedStmt);
    if (
      (verbConflict && (sharedFiles.length > 0 || sharedIdents.length > 0)) ||
      (substringOverlap && (sharedFiles.length > 0 || sharedIdents.length > 0))
    ) {
      issues.push(
        `Exact Next Action ('${
          nextActionText.slice(0, 80)
        }') repeats work already recorded as completed in Latest Reassessment / Completed ('${
          stmt.slice(0, 80)
        }'). Advance Exact Next Action to the live next step (e.g. rerun unit tests, verify integration, or proceed to next stage).`,
      );
      break;
    }
  }
}

function checkFilesModified(
  sections: {
    completedBody: string;
    reassessmentBody: string;
    filesModifiedBody: string;
  },
  options: any,
  hasFilesModified: boolean,
  issues: string[],
): void {
  const { completedBody, reassessmentBody, filesModifiedBody } = sections;
  const completedLines = extractLines(completedBody);
  const reassessmentLines = extractLines(reassessmentBody);
  const filesMentionedInCompleted: string[] = [];
  for (const stmt of [...completedLines, ...reassessmentLines]) {
    if (!isModificationStatement(stmt)) continue;
    for (const f of extractFileNames(stmt)) {
      if (!filesMentionedInCompleted.includes(f)) {
        filesMentionedInCompleted.push(f);
      }
    }
  }
  const sessionFiles = (options?.recentModifiedFiles || []).map((f: string) =>
    canonicalizeFilePath(f)
  );
  const allExpectedFiles = Array.from(
    new Set([...filesMentionedInCompleted, ...sessionFiles]),
  );
  if (allExpectedFiles.length === 0) return;
  if (!hasFilesModified) {
    issues.push(
      `Files Modified is empty or placeholder, but substantive changes to [${
        allExpectedFiles.join(", ")
      }] were recorded in Latest Reassessment / Completed / tool history. List modified files under Files Modified.`,
    );
  } else {
    const modFilesList = extractFileNames(filesModifiedBody);
    const lowerBody = filesModifiedBody.toLowerCase();
    const isSameFile = (a: string, b: string) => {
      const ca = canonicalizeFilePath(a);
      const cb = canonicalizeFilePath(b);
      return ca === cb || ca.endsWith("/" + cb) || cb.endsWith("/" + ca);
    };
    const missingFiles = allExpectedFiles.filter((f) =>
      !modFilesList.some((mf) => isSameFile(mf, f)) &&
      !lowerBody.includes(canonicalizeFilePath(f))
    );
    if (
      missingFiles.length > 0 &&
      filesMentionedInCompleted.some((f) => missingFiles.includes(f))
    ) {
      issues.push(
        `Files Modified omits file(s) [${
          missingFiles.join(", ")
        }] that were modified according to Completed / Reassessment. Include all modified files in Files Modified.`,
      );
    }
  }
}

function checkPlanVersion(
  planVersionBody: string,
  planRevisionsBody: string,
  issues: string[],
): void {
  const planVersionNum =
    Number.parseInt(planVersionBody.replace(/\D/g, ""), 10) || 1;
  if (planVersionNum > 1) {
    const revBody = planRevisionsBody.trim();
    const isOnlyInitial = isPlaceholderOrEmpty(revBody) ||
      revBody === "- Initial plan formulated." ||
      revBody === "Initial plan formulated." ||
      revBody === "- Initial plan formulated" || revBody === "-";
    if (isOnlyInitial) {
      issues.push(
        `Plan Version is ${planVersionNum} but Plan Revisions only lists initial plan formulation. Plan Revisions must explain what triggered the revision (Previous plan -> Evidence/contradiction -> New understanding -> Revised plan).`,
      );
    }
  }
}

function checkStatusAndRemaining(
  sections: {
    uncertaintiesBody: string;
    assumptionsBody: string;
    testStatusBody: string;
    filesModifiedBody: string;
    completedBody: string;
    remainingBody: string;
    currentStatusBody: string;
  },
  hasFilesModified: boolean,
  hasTestStatus: boolean,
  hasCompleted: boolean,
  hasRemaining: boolean,
  options: any,
  issues: string[],
): void {
  const {
    uncertaintiesBody,
    assumptionsBody,
    testStatusBody,
    filesModifiedBody,
    completedBody,
    remainingBody,
    currentStatusBody,
  } = sections;
  // uncertainties vs assumptions
  const uncLower = uncertaintiesBody.toLowerCase();
  if (
    uncLower.includes("none remaining - user confirmed") ||
    uncLower.includes("no uncertainties remaining - user confirmed") ||
    uncLower.includes("none - user confirmed") ||
    uncLower.includes("user confirmed language defaults") ||
    uncLower.includes("user confirmed negotiation strategy")
  ) {
    const hasUncheckedAssumptions = assumptionsBody.includes("- [ ]") ||
      assumptionsBody.includes("unverified");
    if (hasUncheckedAssumptions) {
      issues.push(
        "Open Questions & Uncertainties claims no uncertainties based solely on user requirement confirmation, but unverified engineering assumptions remain in Key Assumptions. User requirement decisions must be separated from verified engineering facts.",
      );
    }
  }
  // test status — research-only quests must not be forced to fill Test / Build Status
  if (!options?.isResearchOnly) {
    if (
      (hasFilesModified ||
        (options?.recentModifiedFiles &&
          options.recentModifiedFiles.length > 0)) && !hasTestStatus
    ) {
      issues.push(
        "Files were modified but Test / Build Status is empty. Test / Build Status must explicitly state whether tests have been run or are pending rerun after changes (e.g. '- Unit tests pending rerun after file edits').",
      );
    }
  }
  // completed vs remaining
  if (hasCompleted && hasRemaining) {
    const completedLines = extractLines(completedBody);
    const remainingLines = extractLines(remainingBody);
    for (const comp of completedLines) {
      const compClean = comp.toLowerCase().replace(
        /^(added|implemented|created|fixed|defined)\s+/,
        "",
      );
      for (const rem of remainingLines) {
        const remClean = rem.toLowerCase().replace(
          /^(add|implement|create|fix|define)\s+/,
          "",
        );
        if (
          (compClean === remClean ||
            (compClean.length > 10 && remClean.includes(compClean)) ||
            (remClean.length > 10 && compClean.includes(remClean))) &&
          compClean.length > 5
        ) {
          issues.push(
            `Remaining Work still lists item ('${
              rem.slice(0, 60)
            }') that is already recorded in Completed. Remove it from Remaining Work or mark it completed.`,
          );
        }
      }
    }
  }
  // status vs remaining
  const statusLower = currentStatusBody.toLowerCase();
  const isStatusTemplatePlaceholder =
    statusLower.includes("research pending ·") ||
    isPlaceholderOrEmpty(currentStatusBody);
  if (
    !isStatusTemplatePlaceholder &&
    (statusLower.startsWith("- [x] done") || statusLower === "done" ||
      statusLower.startsWith("done") || statusLower === "completed" ||
      statusLower.startsWith("- [x] completed"))
  ) {
    if (remainingBody.includes("- [ ]")) {
      issues.push(
        "Current Status is marked done/completed, but Remaining Work still contains unchecked items.",
      );
    }
  }
}

function checkCompletedEmpty(
  hasCompleted: boolean,
  hasReassessment: boolean,
  reassessmentLines: string[],
  issues: string[],
): void {
  if (!hasCompleted && hasReassessment) {
    const actionVerbs = [
      "added",
      "implemented",
      "created",
      "fixed",
      "defined",
      "updated",
      "modified",
      "configured",
    ];
    const hasCompletedAction = reassessmentLines.some((l) =>
      actionVerbs.some((v) => l.toLowerCase().includes(v))
    );
    if (hasCompletedAction) {
      issues.push(
        "Completed section is empty despite completed implementation recorded in Latest Reassessment. Move completed work into Completed and remove from Remaining Work.",
      );
    }
  }
}

export {
  checkCompletedEmpty,
  checkFilesModified,
  checkNextAction,
  checkPlanVersion,
  checkStatusAndRemaining,
};
