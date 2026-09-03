import { state } from "../state.ts";

export function populateCoreEpistemicUpdates(
  params: any,
  updates: Map<string, string>,
): void {
  if (params.goal) updates.set("goal", params.goal);
  if (params.status) updates.set("current status", params.status);

  if (params.understanding || params.currentUnderstanding) {
    const val = params.understanding || params.currentUnderstanding;
    const text = Array.isArray(val)
      ? val.map((u: string) => (u.startsWith("- ") ? u : `- ${u}`)).join("\n")
      : String(val);
    updates.set("current understanding", text);
  }

  if (params.assumptions || params.keyAssumptions) {
    const val = params.assumptions || params.keyAssumptions;
    const text = Array.isArray(val)
      ? val.map((
        a: string,
      ) => (a.startsWith("- [") || a.startsWith("- ") ? a : `- [ ] ${a}`)).join(
        "\n",
      )
      : String(val);
    updates.set("key assumptions", text);
  }

  if (params.openQuestions || params.uncertainties) {
    const val = params.openQuestions || params.uncertainties;
    const text = Array.isArray(val)
      ? val.map((
        q: string,
      ) => (q.startsWith("- [") || q.startsWith("- ") ? q : `- [ ] ${q}`)).join(
        "\n",
      )
      : String(val);
    updates.set("open questions & uncertainties", text);
  }

  const findingsList = params.findings || params.researchFindings ||
    params.importantFindings;
  if (Array.isArray(findingsList) && findingsList.length > 0) {
    const findingsText = findingsList.map((
      f: string,
    ) => (f.startsWith("- ") ? f : `- ${f}`)).join("\n");
    updates.set("research findings", findingsText);
    updates.set("in-depth analysis & findings", findingsText);
  }
}

export function populatePlanAndReassessmentUpdates(
  params: any,
  updates: Map<string, string>,
  targetState?: any,
): void {
  const s = targetState || state;
  if (params.plan || params.executionPlan) {
    const val = params.plan || params.executionPlan;
    const text = Array.isArray(val)
      ? val.map((
        p: string,
        i: number,
      ) => (/^\d+\./.test(p) ? p : `${i + 1}. ${p}`)).join("\n")
      : String(val);
    updates.set("plan", text);
    updates.set("detailed multi-stage execution plan", text);
  }

  if (params.planConfidence) {
    const confStr = String(params.planConfidence);
    const reasonStr = params.planConfidenceReason
      ? `\nReason:\n${params.planConfidenceReason}`
      : "";
    updates.set("plan confidence", `${confStr}${reasonStr}`);
    const lowerConf = confStr.toLowerCase();
    if (lowerConf.includes("high")) s.planConfidence = "high";
    else if (lowerConf.includes("medium")) s.planConfidence = "medium";
    else if (lowerConf.includes("low")) s.planConfidence = "low";
  }

  if (params.planRevisions || params.revisions) {
    const val = params.planRevisions || params.revisions;
    const text = Array.isArray(val)
      ? val.map((r: string) => (r.startsWith("- ") ? r : `- ${r}`)).join("\n")
      : String(val);
    updates.set("plan revisions", text);
    if (text.trim() !== (s.lastPlanRevisionsText || "").trim()) {
      s.planVersion = Math.max(
        typeof params.planVersion === "number" ? params.planVersion : 0,
        (s.planVersion || 1) + 1,
      );
      s.lastPlanRevisionAt = Date.now();
      s.lastPlanRevisionsText = text.trim();
    }
  } else if (
    typeof params.planVersion === "number" &&
    params.planVersion > (s.planVersion || 1)
  ) {
    s.planVersion = params.planVersion;
    s.lastPlanRevisionAt = Date.now();
  }

  if (params.rejectedApproaches) {
    const val = params.rejectedApproaches;
    const text = Array.isArray(val)
      ? val.map((r: string) => (r.startsWith("- ") ? r : `- ${r}`)).join("\n")
      : String(val);
    updates.set("rejected approaches", text);
  }

  if (params.reassessmentConclusion) {
    updates.set("latest reassessment", params.reassessmentConclusion);
  }
  if (Array.isArray(params.decisions) && params.decisions.length > 0) {
    updates.set(
      "decisions made",
      params.decisions.map((d: string) => (d.startsWith("- ") ? d : `- ${d}`))
        .join("\n"),
    );
  }
  if (Array.isArray(params.constraints) && params.constraints.length > 0) {
    updates.set(
      "constraints & rules",
      params.constraints.map((c: string) => (c.startsWith("- ") ? c : `- ${c}`))
        .join("\n"),
    );
  }
}

export function populateProgressAndArtifactUpdates(
  params: any,
  updates: Map<string, string>,
): void {
  if (Array.isArray(params.filesExamined) && params.filesExamined.length > 0) {
    updates.set(
      "files examined",
      params.filesExamined.map((
        f: string,
      ) => (f.startsWith("- ") ? f : `- ${f}`)).join("\n"),
    );
  }
  if (params.completed) {
    const val = params.completed;
    updates.set(
      "completed",
      Array.isArray(val)
        ? val.map((c: string) => (c.startsWith("- ") ? c : `- ${c}`)).join("\n")
        : String(val),
    );
  }
  if (params.inProgress) {
    const val = params.inProgress;
    updates.set(
      "in progress",
      Array.isArray(val)
        ? val.map((ip: string) => (ip.startsWith("- ") ? ip : `- ${ip}`)).join(
          "\n",
        )
        : String(val),
    );
  }

  const filesTouchedList = params.filesTouched || params.filesModified;
  if (Array.isArray(filesTouchedList) && filesTouchedList.length > 0) {
    const filesText = filesTouchedList.map((
      f: string,
    ) => (f.startsWith("- ") ? f : `- ${f}`)).join("\n");
    updates.set("files touched", filesText);
    updates.set("files modified", filesText);
  } else if (typeof filesTouchedList === "string" && filesTouchedList.trim()) {
    updates.set("files touched", filesTouchedList.trim());
    updates.set("files modified", filesTouchedList.trim());
  }

  if (params.testStatus) {
    updates.set(
      "test / build status",
      typeof params.testStatus === "string"
        ? params.testStatus
        : JSON.stringify(params.testStatus),
    );
  }
  if (Array.isArray(params.remaining) && params.remaining.length > 0) {
    updates.set(
      "remaining work",
      params.remaining.map((
        r: string,
      ) => (r.startsWith("- [") ? r : `- [ ] ${r}`)).join("\n"),
    );
  }
  const snapshot = params.executionSnapshot || params.snapshot;
  if (snapshot) updates.set("execution snapshot", snapshot);

  const nextStep = params.nextStep || params.nextAction ||
    params.exactNextAction;
  if (nextStep) {
    updates.set("exact next action", nextStep);
    updates.set("next recommended step", nextStep);
  }
  const resumeContext = params.resumeContext || params.resumePrompt;
  if (resumeContext) updates.set("resume prompt", resumeContext);
}

export function populateEpistemicUpdates(
  params: any,
  updates: Map<string, string>,
  targetState?: any,
): void {
  populateCoreEpistemicUpdates(params, updates);
  populatePlanAndReassessmentUpdates(params, updates, targetState);
  populateProgressAndArtifactUpdates(params, updates);
}
