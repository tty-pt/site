import { readFile, stat } from "node:fs/promises";
import { basename, dirname } from "node:path";
import { fileExists } from "../../paths.ts";
import { ParsedRunLog } from "../types.ts";

export interface DetailedLogEvent {
  timestamp: string;
  event: string;
  quest: string | null;
  parent: string | null;
  child: string | null;
  dest: string | null;
  summary: string;
}

export async function parseDetailedLogEvents(
  logPath: string,
): Promise<DetailedLogEvent[]> {
  if (!(await fileExists(logPath))) return [];
  try {
    const content = await readFile(logPath, "utf8");
    const lines = content.split(/\r?\n/).filter((l) => l.trim().length > 0);
    const events: DetailedLogEvent[] = [];

    for (const line of lines) {
      const parts = line.split(" | ");
      if (parts.length < 3) continue;

      const timestamp = parts[0];
      const event = parts[1];
      const ctxStr = parts[2];
      const summary = parts.slice(3).join(" | ");

      let quest: string | null = null;
      let parent: string | null = null;
      let child: string | null = null;
      let dest: string | null = null;

      const tokens = ctxStr.split(/\s+/);
      for (const token of tokens) {
        if (token.startsWith("quest=")) {
          const val = token.slice("quest=".length);
          if (val && val !== "(none)") quest = val;
        } else if (token.startsWith("parent=")) {
          const val = token.slice("parent=".length);
          if (val && val !== "(none)") parent = val;
        } else if (token.startsWith("child=")) {
          const val = token.slice("child=".length);
          if (val && val !== "(none)") child = val;
        } else if (token.startsWith("dest=")) {
          dest = token.slice("dest=".length);
        }
      }

      events.push({
        timestamp,
        event,
        quest,
        parent,
        child,
        dest,
        summary,
      });
    }

    return events;
  } catch {}
  return [];
}

export async function parseRunLogFile(
  logPath: string,
): Promise<ParsedRunLog | null> {
  if (!(await fileExists(logPath))) return null;
  try {
    const content = await readFile(logPath, "utf8");
    const lines = content.split(/\r?\n/).filter((l) => l.trim().length > 0);
    if (lines.length === 0) {
      const s = await stat(logPath);
      const fallbackQid =
        basename(dirname(logPath)) !== "current" && !logPath.endsWith(".log")
          ? basename(dirname(logPath))
          : basename(logPath).replace(/\.log$/, "");
      return {
        questId: fallbackQid,
        path: logPath,
        size: s.size,
        mtime: s.mtimeMs,
        subquests: [],
        eventCount: 0,
      };
    }

    let questId = basename(dirname(logPath)) !== "current" &&
        basename(logPath) === "execution.log"
      ? basename(dirname(logPath))
      : basename(logPath).replace(/\.log$/, "");
    let rootQuest: string | undefined;
    let activeQuest: string | undefined;
    const subquestsSet = new Set<string>();
    let startTime: string | undefined;
    let endTime: string | undefined;
    let hasRootCompletion = false;

    for (const line of lines) {
      const parts = line.split(" | ");
      if (parts.length < 3) continue;

      const ts = parts[0];
      const eventType = parts[1];

      if (!startTime) startTime = ts;
      endTime = ts;

      const allTokens = `${parts[2] || ""} ${parts[3] || ""}`.split(/\s+/);
      let lineQuestId: string | null = null;
      let lineRoot: string | null = null;
      let lineQuest: string | null = null;
      let lineParent: string | null = null;
      let lineChild: string | null = null;

      for (const tok of allTokens) {
        if (tok.startsWith("questId=")) {
          lineQuestId = tok.slice(tok.indexOf("=") + 1);
        } else if (tok.startsWith("root=") || tok.startsWith("rootQuest=")) {
          const val = tok.slice(tok.indexOf("=") + 1);
          if (val && val !== "(none)") lineRoot = val;
        } else if (tok.startsWith("quest=")) {
          const val = tok.slice("quest=".length);
          if (val && val !== "(none)") lineQuest = val;
        } else if (tok.startsWith("parent=")) {
          const val = tok.slice("parent=".length);
          if (val && val !== "(none)") lineParent = val;
        } else if (tok.startsWith("child=")) {
          const val = tok.slice("child=".length);
          if (val && val !== "(none)") lineChild = val;
        }
      }

      if (lineQuestId && lineQuestId !== "(none)") {
        questId = lineQuestId;
      }
      if (lineRoot) {
        rootQuest = lineRoot;
      } else if (
        !rootQuest && lineQuest &&
        (eventType === "QUEST_CREATED" || eventType === "QUEST_START")
      ) {
        rootQuest = lineQuest;
      }

      if (eventType === "ARCHIVE") {
        if (lineParent) {
          activeQuest = lineParent;
        }
      } else if (lineQuest) {
        activeQuest = lineQuest;
        if (rootQuest && lineQuest !== rootQuest) {
          subquestsSet.add(lineQuest);
        }
      }
      if (lineChild && rootQuest && lineChild !== rootQuest) {
        subquestsSet.add(lineChild);
      }

      if (
        eventType === "COMPLETION" ||
        (eventType === "ARCHIVE" && lineQuest && lineQuest === rootQuest)
      ) {
        hasRootCompletion = true;
      }
    }

    const s = await stat(logPath);
    return {
      questId,
      path: logPath,
      size: s.size,
      mtime: s.mtimeMs,
      startTime,
      endTime,
      rootQuest,
      activeQuest,
      subquests: Array.from(subquestsSet),
      eventCount: lines.length,
      hasRootCompletion,
    };
  } catch {
    return null;
  }
}
