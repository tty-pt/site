# Project Extensions (`.pi/extensions/`)

This repository includes custom TypeScript extensions located in `.pi/extensions/` that extend Pi's functionality for persistent multi-axis memory and project workflows.

---

## 1. Overview

Project-local extensions are loaded automatically by Pi when opening this repository (after trust confirmation).

- **Location**: `.pi/extensions/` (currently `.pi/extensions/pi-mm/`)
- **Hot-reload**: Execute `/reload` in Pi at any time to reload modified extensions, prompt files, and skills without restarting Pi.
- **Retired Extension (`pi-quest`)**: `.pi/extensions/pi-quest/` was deactivated and removed in Phase 5 of `docs/CODEBASE_IMPROVEMENT_ROADMAP.md`. Multi-step session tracking uses Pi's built-in `todo` tool, worktrees use `scripts/worktree.sh`, and cross-session memory uses `pi-mm`.

---

## 2. Persistent Memory Mipmaps (`.pi/extensions/pi-mm/`)

### Purpose
Provides multi-resolution, hierarchical episodic memory (L0 raw tool events $\rightarrow$ L1 task episodes $\rightarrow$ L2 project invariants $\rightarrow$ L3 system principles) plus live codebase AST/call-graph indexing powered by `libcorm`, `libsepal`, `liblemma`, `libislet`, `libjoint`, and `axil-auren`. Automatically captures engineering breakthroughs and standing directives across sessions, reinforces active constraints in prompt context, and enables fast semantic and keyword recall.

### Commands

| Command | Usage | Description |
|---------|-------|-------------|
| `/mm doctor` | `/mm doctor` | Displays live status for decision judge, vector endpoints, storage footprint, and consolidation pipeline health (L2/L3 synthesis status, L0/L1 queue depths, drain rate, and backlog alerts). |
| `/mm overview` | `/mm overview` | Synthesizes an all-time structured timeline across active principles (L3), invariants (L2), and milestones (L1). |
| `/mm timeline` | `/mm timeline` | Chronological grouping of memory capture epochs and historical activity. |

### Tools

- **`memory_recall`**: Multi-resolution retrieval across persistent lore (`scope: "persistent"`), active/parallel/past session transcripts (`scope: "session"`), and live codebase symbols & call graphs (`scope: "code"`, `symbol`, `trace`, `diff`, `diagnose`, `guardrail`, `overview`, `zoomRef`).
- **`memory_record`**: Record structured task episodes (`problem`, `approaches`, `diffs`, `findings`, `outcome`, `verification`). Automatically elevates standing operational rules (`standing: true`) to persistent L2 invariants.
- **`memory_review`**: Inspect memory evidence hierarchy (`action: "inspect"`) or transition factual lifecycle status (`action: "update_lifecycle"`: tested, supported, contradicted, superseded).

### Key Architectural Invariants & Mechanics

1. **Continuous Directive Reinforcement**: Developer corrections and standing negative constraints are detected via `POST /v1/systemone`, elevated to L2 invariants, and reinforced newest-first in the agent's prompt across every turn.
2. **Unwedged Consolidation & Idle Backlog Drain**: Fresh turn events are prioritized first, with remaining backlogs ordered newest-first (`orderPendingL0Refs`). Idle turns drain backlogged L0 records within a configurable wall-clock budget (`consolidateDrainMs`, default 250ms).
3. **Zero-Config Local Chat Synthesis**: Automatically hooks local `axil-auren` OpenAI-compatible chat endpoints (`/v1/chat/completions`) using `smollm` for L2/L3 abstraction synthesis.
4. **Targeted Recall & Episode Exemption**: Hybrid recall surfaces relevant L0 memories when requested (`level: "L0"` or untiered searches) while strictly exempting structured technical episodes from code artifact pruning.

---

## 3. Standing Project Notes (`.pi/context.md`)

You can create or edit `.pi/context.md` at any time to give Pi standing facts or rules that should be injected into every request payload (e.g., "Always test with `make watch` after editing UX modules").

```markdown
# Standing Project Notes
- Run `make watch` after updating C UX modules.
- Ensure WASM bridge exports match `htdocs/*.wasm`.
```

---

## 4. Summary Table

| Extension Path | Primary Hooks | Key Commands / Tools | Responsibility |
|----------------|---------------|----------------------|----------------|
| `.pi/extensions/pi-mm/` | `before_agent_start`, `turn_end`, `agent_settled`, `session_start`, `session_shutdown`, `message_end`, `tool_result` | `/mm doctor`, `/mm overview`, `/mm timeline`, `memory_recall`, `memory_record`, `memory_review` | Hierarchical memory mipmaps, live codebase symbol/call-graph index, epistemic lifecycle verification, standing directive reinforcement, automated L0–L3 consolidation |

---

## 5. Adding New Extensions

To create a new project-local extension:
1. Add a TypeScript file in `.pi/extensions/my-extension.ts`.
2. Export a default function taking `ExtensionAPI`:
   ```typescript
   import type { ExtensionAPI } from "@earendil-works/pi-coding-agent";

   export default function (pi: ExtensionAPI) {
     pi.on("before_agent_start", async (event, ctx) => {
       // ...
     });
   }
   ```
3. Run `/reload` in Pi to load the new extension.
