# AGENTS.md — pi-quest v2 package guidelines

`pi-quest` v2 is the Quest Journal & Epistemic Workflow Extension for the
Pi Coding Agent harness: drafting / implementing / validating modes per
quest, enforced by gates and adversarial reviews. Product authority is
`HIGH_LEVEL.md` (repo root); behavioral elaboration is `docs/PRODUCT_SPEC_v2.md`
Part II; build plan is `REBUILD_PLAN.md` (repo root).

## Location & loading

The package lives at `.pi/extensions/pi-quest/` and loads via pi's
project-local auto-discovery (`*/index.ts`). No build step: pi loads the
TypeScript directly. `/reload` picks up changes. Every pi session runs
this extension from the skeleton onward — **every commit must leave it
loadable-and-graceful** (resilience rule): handlers never throw on missing
state, no imports of not-yet-existing modules.

## Architecture (A1–A3, REBUILD_PLAN.md §5)

- **A1 — reducer.** pi events flow `hooks/tools → reduce(state, event) →
  {state, effects[]} → interpreter`. Domain (`src/domain/`) is pure.
- **A2 — review transport behind `ReviewRunner`.** Default impl uses the
  `subagent` tool; abort via AbortController + cancel bridge.
- **A3 — wordings in `src/messaging/`.** Model/user-facing text lives as
  reviewable prose, referenced by key — never inline strings in logic.
- **Vertical facades.** `src/index.ts` calls one installer per product
  area (`drafting/`, `implementing/`, `validation/`, `subquests/`,
  `absence/`, `durability/`, `surface/` — each a directory with
  `index.ts`, imported bare). Facades are thin entry points over the
  layers; nothing imports a facade except the main file.
- Dependency direction: adapters → domain, never the reverse (DAG-gated;
  `domain/` may import nothing outside itself).

## Readability contract (RD1–RD3)

1. **Spec mapping is machine-checked** (`scripts/check-spec-map.ts`, run
   with the other gates). HIGH_LEVEL `#`/`##` headers starting lowercase
   each need ≥1 `// HIGH_LEVEL: #name` code tag; `###` item headers are
   covered by their parent section. Permanently exempt (not behaviors):
   `intro`, `dependencies`. Not yet wired: explicitly PENDING with the
   owning slice — never fake tags. Every tag must name an existing
   section.
2. **Budgets fail the build:** file <350 LOC, function <80 LOC.
3. **Tag format:** `// HIGH_LEVEL: #section` (why) + `// SPEC: Bx.y`
   (exactly what), one tag per line. `src/index.ts` mirrors HIGH_LEVEL
   section order as declarative calls; domain names come from the spec
   glossary (`qid`, never new abbreviations).

## HIGH_LEVEL.md edit policy

`HIGH_LEVEL.md` product text is frozen. Append or complete only on
explicit user direction; never reword, "fix," or normalize existing
lines (typos included). Structural additions (new sections) also require
explicit direction. Keep the file git-tracked so third-party edits surface
as reviewable diffs instead of silent changes.

## Verification & testing

```bash
deno test --allow-all --sloppy-imports --node-modules-dir=none tests/
sh scripts/check-pi-quest-dag.sh
deno run --allow-read scripts/check-pi-quest-dag.ts
deno run --allow-all scripts/check-complexity.ts
deno run --allow-read scripts/check-spec-map.ts
```

or via npm (`test`, and `zip` below). Tests mirror `src/`; the bulk is
`tests/domain/*` — pure functions, no pi mocks. Boundaries are tested
against fakes.

## Packaging

```bash
npm --prefix .pi/extensions/pi-quest run zip
```

writes `pi-quest-bundle.zip` at the project root (source tree + docs;
quest-state rendering lands with the views slice).
