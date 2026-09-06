// HIGH_LEVEL: #review request — re-review briefs carry the plan diff.
const MAX_DIFF_LINES = 40;

// Line diff between the last reviewed plan and the current one: common
// prefix/suffix trimmed, removals marked "- ", additions "+ ". Returns null
// when identical or too large — then the full plan stands alone.
export function diffPlans(previous: string, current: string, maxLines: number = MAX_DIFF_LINES): string | null {
  const oldLines = previous.split("\n");
  const newLines = current.split("\n");
  let start = 0;
  while (start < oldLines.length && start < newLines.length && oldLines[start] === newLines[start]) {
    start += 1;
  }
  let endOld = oldLines.length - 1;
  let endNew = newLines.length - 1;
  while (endOld >= start && endNew >= start && oldLines[endOld] === newLines[endNew]) {
    endOld -= 1;
    endNew -= 1;
  }
  const removed = oldLines.slice(start, endOld + 1);
  const added = newLines.slice(start, endNew + 1);
  if (removed.length === 0 && added.length === 0) return null;
  if (removed.length + added.length > maxLines) return null;
  return [...removed.map((line) => `- ${line}`), ...added.map((line) => `+ ${line}`)].join("\n");
}
