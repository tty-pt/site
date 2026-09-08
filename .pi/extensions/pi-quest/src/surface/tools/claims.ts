// HIGH_LEVEL: #plan revision — save-time citation spot check.
// Pure-enough disk probe for the agent's plan writes: every file:line the
// plan cites must resolve, so a save surfaces drift before a review spends
// minutes re-deriving facts the plan got wrong.
import { readFile, stat } from "node:fs/promises";
import { readdirSync } from "node:fs";
import { join } from "node:path";
import type { PiCtx } from "../../hooks/events";
import { citeLocations } from "../../drafting/plan-text";

async function resolveCitableFile(cwd: string, ref: string): Promise<string | null> {
  const candidates: string[] = [join(cwd, ref)];
  if (!ref.includes("/")) {
    try {
      for (const top of readdirSync(join(cwd, "mods"))) {
        candidates.push(join(cwd, "mods", top, ref));
      }
    } catch {
      // No mods dir to probe.
    }
  } else {
    candidates.push(join(cwd, "mods", ref));
  }
  for (const candidate of candidates) {
    try {
      const info = await stat(candidate);
      if (info.isFile()) return candidate;
    } catch {
      // Keep probing candidates.
    }
  }
  return null;
}

export async function checkPlanCitations(ctx: PiCtx, planText: string): Promise<string> {
  const cites = citeLocations(planText);
  if (cites.length === 0) return "";
  const problems: string[] = [];
  for (const cite of cites) {
    const resolved = await resolveCitableFile(ctx.cwd, cite.file);
    if (resolved === null) {
      problems.push(`${cite.file}:${cite.lineStart} not found`);
      continue;
    }
    const lineCount = (await readFile(resolved, "utf8")).split("\n").length;
    if (cite.lineStart > lineCount) {
      problems.push(`${cite.file}:${cite.lineStart} beyond ${lineCount} lines`);
    } else if (cite.lineEnd !== undefined && cite.lineEnd > lineCount) {
      problems.push(`${cite.file}:${cite.lineStart}-${cite.lineEnd} beyond ${lineCount} lines`);
    }
  }
  if (problems.length === 0) return `claims check: ${cites.length} citations resolve`;
  const shown = problems.slice(0, 8).join("; ");
  const more = problems.length > 8 ? ` (+${problems.length - 8} more)` : "";
  return `claims check: ${cites.length - problems.length}/${cites.length} citations resolve — verify: ${shown}${more}`;
}