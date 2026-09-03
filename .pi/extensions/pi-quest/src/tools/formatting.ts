import { questPath } from "../paths.ts";

export function formatMarkSavedResponse(
  targetName: string,
  res: {
    count: number;
    hash?: string;
    consistency?: { consistent: boolean; issues: string[] };
  },
): { content: Array<{ type: "text"; text: string }>; details: any } {
  let auditWarning = "";
  if (
    res.consistency && !res.consistency.consistent &&
    res.consistency.issues.length > 0
  ) {
    auditWarning =
      `\n\n⚠️ Consistency Audit Notice: Stale or contradictory state detected in '${
        questPath(targetName)
      }':\n${
        res.consistency.issues.map((i: string) => `  - ${i}`).join("\n")
      }\nTo fix: ensure Files Modified lists every file mentioned in Completed/Latest Reassessment (e.g. quest_update_state({ filesModified: ["path/to/file.c", ...] })). This is advisory only — saves still succeed.`;
  }

  return {
    content: [
      {
        type: "text",
        text: `Quest file '${
          questPath(targetName)
        }' verified and marked as saved in the journal (gen #${res.count}, hash: ${
          res.hash || ""
        }).${auditWarning}`,
      },
    ],
    details: {
      hash: res.hash || "",
      generation: res.count,
      consistency: res.consistency,
    },
  };
}

export function formatUpdateStateResponse(
  targetName: string,
  path: string,
  params: any,
  saveRes: {
    count: number;
    hash?: string;
    consistency?: { consistent: boolean; issues: string[] };
  },
  planVersion: number,
  researchNote: string,
  reassessmentNote: string,
  reassessmentRequired?: boolean,
  researchComplete?: boolean,
): { content: Array<{ type: "text"; text: string }>; details: any } {
  let consistencyNote = "";
  if (
    saveRes.consistency && !saveRes.consistency.consistent &&
    saveRes.consistency.issues.length > 0
  ) {
    consistencyNote = `\n\n⚠️ Consistency Audit Notice:\n${
      saveRes.consistency.issues.map((i: string) => `  - ${i}`).join("\n")
    }\nTo fix: pass filesModified: [...] listing the missing files on the next quest_update_state / quest_mark_saved. This is advisory only — saves still succeed.`;
  }

  return {
    content: [
      {
        type: "text",
        text:
          `Successfully updated quest state for **${targetName}** at \`${path}\` (gen #${saveRes.count}, hash: ${
            saveRes.hash || ""
          }, plan v${
            planVersion || 1
          }).${researchNote}${reassessmentNote}${consistencyNote}`,
      },
    ],
    details: {
      quest: targetName,
      path,
      status: params?.status,
      hash: saveRes.hash || "",
      generation: saveRes.count,
      planVersion,
      researchComplete: Boolean(researchComplete),
      reassessmentRequired: Boolean(reassessmentRequired),
      consistency: saveRes.consistency,
    },
  };
}

export function formatSubquestResponse(
  name: string,
  path: string,
  parentName: string,
  isExisting: boolean,
  switchNow: boolean,
): { content: Array<{ type: "text"; text: string }>; details: any } {
  const msg = isExisting
    ? `Sub-quest '${name}' already exists at \`${path}\`.${
      parentName ? ` Verified link in parent '${parentName}'.` : ""
    }${switchNow ? " Switched active quest to this sub-quest." : ""}`
    : `Created sub-quest **${name}** at \`${path}\`${
      parentName ? ` (parent: **${parentName}**)` : ""
    }.${
      switchNow
        ? " Switched active quest to this sub-quest."
        : " Kept parent quest active; sub-quest added to tracker."
    }`;

  return {
    content: [{ type: "text", text: msg }],
    details: { subquest: name, path, parent: parentName, switched: switchNow },
  };
}

export function formatArchiveResponse(
  targetName: string,
  res: { message: string; dest?: string; nextActive?: string | null },
  shouldCompact: boolean,
): { content: Array<{ type: "text"; text: string }>; details: any } {
  return {
    content: [
      {
        type: "text",
        text: `${res.message}${
          shouldCompact ? " Context compaction queued for turn end." : ""
        }`,
      },
    ],
    details: {
      archived: targetName,
      dest: res.dest,
      compacted: shouldCompact,
      nextActive: res.nextActive,
    },
  };
}
