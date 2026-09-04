// Spec-map gate (new, RD1): every HIGH_LEVEL.md section header (levels #
// and ##, lowercase start — skips the title; deeper ### item headers are
// elaborations covered by their parent section) must be referenced by >=1
// `// HIGH_LEVEL: #name` code tag under src/, and every such tag must name
// an existing section. Violations fail the build. Tag names are lowercase
// words, spaces, and hyphens; matching stops at the first other character
// so trailing commentary never leaks into the name.

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

const PKG = new URL("..", import.meta.url).pathname;
const SPEC = `${PKG}/../../../HIGH_LEVEL.md`;
const SRC = `${PKG}/src`;

const specText = await Deno.readTextFile(SPEC);
const sections = new Set<string>();
for (const line of specText.split("\n")) {
  const m = line.match(/^#{1,2}\s+([a-z].*?)\s*$/);
  if (m) sections.add(m[1]);
}

const tagRe = /HIGH_LEVEL:\s*#([a-z][a-z0-9 ()-]*)/gm;

// Sections whose wiring lands in a later slice (honest pending, not debt):
// each must name its slice, and the slice must clear it.
const PENDING: Record<string, string> = {
  "tools (main agent)": "S3",
  "tools (other agents)": "S3",
  "commands": "S3",
  "skill": "S3",
  "configurations": "S2-S3",
  "interfaces": "S2-S3",
  "review and validation communication": "S2",
  "review request": "S2",
  "review result": "S2",
  "stale results": "S2",
  "rebuttal": "S2",
  "reviewer independence": "S2",
  "validator communication": "S2",
  "no direct mutation": "S2",
  "quest creation": "S1",
  "storage": "S1",
  "independent review contexts": "S2",
  "review independence": "S2",
};
const hits = new Map<string, string[]>();
for await (const path of walkTs(SRC)) {
  const text = await Deno.readTextFile(path);
  tagRe.lastIndex = 0;
  let m: RegExpExecArray | null;
  while ((m = tagRe.exec(text)) !== null) {
    const name = m[1].trim();
    if (!hits.has(name)) hits.set(name, []);
    hits.get(name)!.push(path.slice(SRC.length + 1));
  }
}

let failures = 0;
// Document preamble / config-only sections, not behaviors — permanently
// exempt, not pending.
const EXEMPT = new Set(["intro", "dependencies"]);
for (const s of sections) {
  if (hits.has(s)) continue;
  if (EXEMPT.has(s)) continue;
  if (PENDING[s]) {
    console.log(`spec-map: pending #${s} (${PENDING[s]})`);
    continue;
  }
  console.error(`FAIL spec-map: HIGH_LEVEL #${s} has no code reference`);
  failures++;
}
for (const t of hits.keys()) {
  if (!sections.has(t)) {
    console.error(
      `FAIL spec-map: code references unknown HIGH_LEVEL #${t} (${
        hits.get(t)!.join(", ")
      })`,
    );
    failures++;
  }
}

if (failures > 0) {
  console.error(`spec-map: ${failures} violation(s)`);
  Deno.exit(1);
}
console.log(
  `spec-map: ok (${sections.size} sections, ${hits.size} referenced)`,
);
