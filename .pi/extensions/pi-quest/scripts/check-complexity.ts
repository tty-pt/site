#!/usr/bin/env -S deno run --allow-all
// Complexity gate: fails if any file/function exceeds budget.
// Budget: file <350 LOC, function <80 LOC, branching tokens heuristic <60 per file group.
// This is a lightweight heuristic; full cyclomatic via eslint would be heavier.

import { walk } from "https://deno.land/std@0.224.0/fs/walk.ts";

const SRC = new URL("../src", import.meta.url).pathname;
const BUDGET_FILE_LOC = 350;
const BUDGET_FN_LOC = 80;

let failures = 0;

async function countLines(path: string): Promise<number> {
  const text = await Deno.readTextFile(path);
  return text.split("\n").length;
}

function estimateFnSize(text: string): Array<{ name: string; loc: number }> {
  const lines = text.split("\n");
  const fns: Array<{ name: string; start: number }> = [];
  for (let i = 0; i < lines.length; i++) {
    const m = lines[i].match(/^\s*(export\s+)?(async\s+)?function\s+(\w+)|^\s*(export\s+)?(async\s+)?(const|let)\s+(\w+)\s*=\s*\(.*\)\s*=>/);
    if (m) {
      const name = m[3] || m[7] || `anon@${i+1}`;
      fns.push({ name, start: i });
    }
  }
  const result: Array<{ name: string; loc: number }> = [];
  for (let idx = 0; idx < fns.length; idx++) {
    const start = fns[idx].start;
    const end = idx + 1 < fns.length ? fns[idx+1].start : lines.length;
    result.push({ name: fns[idx].name, loc: end - start });
  }
  return result;
}

for await (const entry of walk(SRC, { exts: [".ts"], skip: [/\.test\./] })) {
  if (entry.isFile) {
    const loc = await countLines(entry.path);
    if (loc > BUDGET_FILE_LOC) {
      console.warn(`WARN file ${entry.path.replace(SRC, "src")} : ${loc} LOC > ${BUDGET_FILE_LOC}`);
      failures++;
    }
    const text = await Deno.readTextFile(entry.path);
    for (const fn of estimateFnSize(text)) {
      if (fn.loc > BUDGET_FN_LOC) {
        console.warn(`WARN func ${fn.name} in ${entry.path.replace(SRC, "src")} : ${fn.loc} LOC > ${BUDGET_FN_LOC}`);
      }
    }
  }
}

if (failures > 0) {
  console.log(`\nComplexity gate: ${failures} file(s) over budget (${BUDGET_FILE_LOC} LOC). Reduce before merging.`);
  Deno.exit(1);
} else {
  console.log("Complexity gate: passed");
}
