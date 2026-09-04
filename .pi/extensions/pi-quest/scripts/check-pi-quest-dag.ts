// DAG gate (re-derived, self-contained): source modules must form a strict
// directed acyclic graph over relative imports, and `src/domain/` (once it
// exists) may import nothing outside itself — no pi packages, no adapters.

async function* walkTs(dir: string): AsyncGenerator<string> {
  for await (const entry of Deno.readDir(dir)) {
    const p = `${dir}/${entry.name}`;
    if (entry.isDirectory) {
      yield* walkTs(p);
    } else if (entry.isFile && entry.name.endsWith(".ts")) {
      yield p;
    }
  }
}

const SRC = new URL("../src", import.meta.url).pathname;
const files: string[] = [];
try {
  for await (const f of walkTs(SRC)) files.push(f);
} catch {
  console.log("dag: no src tree, nothing to check");
  Deno.exit(0);
}

const relOf = (p: string) => p.slice(SRC.length + 1);
const exists = new Set(files.map((f) => relOf(f)));

const importRe = /^\s*import\s[^;]*?from\s*["']([^"']+)["']/gm;
const edges = new Map<string, string[]>();
for (const f of files) {
  const text = await Deno.readTextFile(f);
  const deps: string[] = [];
  let m: RegExpExecArray | null;
  importRe.lastIndex = 0;
  while ((m = importRe.exec(text)) !== null) {
    const spec = m[1];
    if (!spec.startsWith(".")) continue;
    const base = relOf(f).split("/").slice(0, -1).join("/");
    const parts = (base ? base + "/" : "") + spec;
    const norm = parts.split("/").reduce<string[]>((acc, seg) => {
      if (seg === "." || seg === "") return acc;
      if (seg === "..") acc.pop();
      else acc.push(seg);
      return acc;
    }, []).join("/");
    for (const cand of [`${norm}.ts`, `${norm}/index.ts`]) {
      if (exists.has(cand)) {
        deps.push(cand);
        break;
      }
    }
  }
  edges.set(relOf(f), deps);
}

let failures = 0;

// Cycle detection (iterative DFS).
const WHITE = 0, GRAY = 1, BLACK = 2;
const color = new Map<string, number>();
const visit = (start: string, trail: string[]): void => {
  const stack: Array<{ node: string; iter: number }> = [
    { node: start, iter: 0 },
  ];
  const localTrail = [...trail];
  while (stack.length > 0) {
    const top = stack[stack.length - 1];
    if (color.get(top.node) === BLACK) {
      stack.pop();
      localTrail.pop();
      continue;
    }
    if (color.get(top.node) !== GRAY) {
      color.set(top.node, GRAY);
      localTrail.push(top.node);
    }
    const deps = edges.get(top.node) ?? [];
    if (top.iter < deps.length) {
      const next = deps[top.iter];
      top.iter++;
      const c = color.get(next) ?? WHITE;
      if (c === GRAY) {
        console.error(
          `FAIL dag cycle: ${[...localTrail, next].join(" -> ")}`,
        );
        failures++;
      } else if (c === WHITE) {
        stack.push({ node: next, iter: 0 });
      }
    } else {
      color.set(top.node, BLACK);
      stack.pop();
      localTrail.pop();
    }
  }
};
for (const f of edges.keys()) {
  if ((color.get(f) ?? WHITE) === WHITE) visit(f, []);
}

// Domain purity: files under src/domain/ may only import within src/domain/.
for (const [from, deps] of edges) {
  if (!from.startsWith("domain/")) continue;
  for (const d of deps) {
    if (!d.startsWith("domain/")) {
      console.error(`FAIL domain purity: ${from} imports ${d}`);
      failures++;
    }
  }
}
try {
  for (const f of files) {
    if (!f.includes("/domain/")) continue;
    const text = await Deno.readTextFile(f);
    const extRe = /^\s*import\s[^;]*?from\s*["']([^"']+)["']/gm;
    let m: RegExpExecArray | null;
    extRe.lastIndex = 0;
    while ((m = extRe.exec(text)) !== null) {
      const spec = m[1];
      if (
        !spec.startsWith(".") &&
        !spec.startsWith("node:") &&
        !spec.startsWith("deno:")
      ) {
        console.error(`FAIL domain purity: ${relOf(f)} imports ${spec}`);
        failures++;
      }
    }
  }
} catch {
  // No domain dir yet — rule activates with S1.
}

if (failures > 0) {
  console.error(`dag: ${failures} violation(s)`);
  Deno.exit(1);
}
console.log(`dag: ok (${edges.size} modules, acyclic)`);
