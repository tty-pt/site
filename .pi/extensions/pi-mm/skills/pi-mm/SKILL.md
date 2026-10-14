---
name: pi-mm
description: "Memory Mipmaps (pi-mm): persistent multi-resolution memory space over local libcorm recall kernel and axes. Tools: memory_scan/think/store/traverse/lifecycle/feedback/consolidate/forget/reset."
---

# pi-mm — Memory Mipmaps

Persistent, evidence-preserving, multi-resolution memory space over local `libcorm` recall kernel and axes (`joint` time, `stoma` lexical FTS, `sepal` semantic ANN, `islet` spatial context). No daemon, zero network required. Memory lives in `.pi/mm/mem.db` (primary `CM_AINDEX:string` + roster `joint,stoma`; + `sepal` when embeddings are configured; + `islet` for spatial coordinates).

## Boundary rule (with pi-quest)

- **Quest** = current active task state (goals, findings, decisions, plan).
- **MM** = persistent cross-session episodic history & multiresolution memory space (what happened, failed vs successful attempts, and why).
- Never duplicate the same ephemeral fact in both.

## Mipmap Hierarchy (IDEA.md §2)

| Level | Name | Description | Key Contents |
|-------|------|-------------|--------------|
| **L3** | General Knowledge | Cross-project recurring lessons and patterns | Generalized architectural heuristics |
| **L2** | Project Understanding | Architectural decisions, invariants, conventions | Invariants, conventions, rules |
| **L1** | Investigations | Hypotheses, attempted fixes, causal explanations | Tested theories, root causes |
| **L0** | Experience | Original observations, tool results, diffs, tests | Raw episodes, commands, diffs |

Every higher-level abstraction remains connected to its underlying evidence via `evidence: number[]` refs. Never replace source evidence with a summary.

## Memory Lifecycle (IDEA.md §3)

Memories progress through explicit lifecycle states:
- `observed` — noted in a tool result, command output, or conversation.
- `hypothesized` — proposed explanation or candidate theory.
- `tested` — verified or tested in an experiment.
- `supported` — active truth currently favored by evidence.
- `contradicted` — evidence conflicts with this claim (requires `reason` or `evidenceRef`).
- `superseded` — replaced by a newer conclusion; terminal state preserving historical auditable records.

## Tools Reference

- `memory_store(options)` — stores a raw text memory or structured mipmap envelope (`level`, `summary`, `episode`, `context`, `evidence`, `supersedes`, `lifecycle`, `tags`). When `supersedes` is set, marks the prior ref as `superseded` and records `supersededBy`. Returns `{ref, key, level, lifecycle, summary}`.
- `memory_scan(topic, options?)` — multidimensional search across time (`joint`), text (`stoma`), semantics (`sepal`), and space (`islet`). Options: `level` (`L0`..`L3` or legacy 0..2), `lifecycle` (`supported`, `all`, etc.), `project`, `subsystem`, `bbox`, `until`, `embed`, `limit`. Returns ranked `{records: [{ref, score?, level, lifecycle, summary, record, envelope}]}`.
- `memory_traverse(ref, direction?="down", depth?=2)` — walks the memory hierarchy bidirectionally with cycle detection:
  - `down` — drills down from invariants/investigations into supporting L0 episodes and diffs (flags `missingEvidence` if broken).
  - `up` — discovers higher-level abstractions citing this memory as evidence.
- `memory_consolidate(options)` — consolidates lower-level memories into higher-level abstractions (L1 Investigation, L2 Project Knowledge, L3 General Lesson). Enforces explicit scope on L2 and is strictly idempotent (zero duplicate records).
- `memory_lifecycle(ref, lifecycle, reason?, evidenceRef?)` — transitions a memory's state, recording the transition rationale and history.
- `memory_feedback(ref, utility, note?)` — records retrieval utility (`useful`, `irrelevant`, `obsolete`) to calibrate recall usefulness.
- `memory_think(key, extract?="whole")` — recalls the payload for a numeric ref or top-1 topic. `extract` ∈ `date|text|whole|episode|evidence|context|lifecycle`.
- `memory_forget(key)` — deletes a memory across primary and all roster axes (idempotent).
- `memory_reset()` — enumerates and deletes all memories.

## Operational Guidelines for Agents

1. **Recall before repeating investigations:**
   When encountering a failure, test error, or complex architecture question, call `memory_scan` with the exact error string or symbol. If a past episode attempted fixes, check what failed and what worked to avoid repeating mistakes.
2. **Inspect evidence behind conclusions:**
   When recalling an invariant or rule, use `memory_traverse(ref, "down")` to examine the original diffs and test results before trusting an abstraction.
3. **Record failed and successful approaches:**
   When storing an episode (`level: "L0"`), populate `approaches` with items marked `"failed"` or `"worked"` and specific reasons.
4. **Distinguish hypotheses from findings:**
   Do not store unverified theories as `supported`. Start candidate explanations as `hypothesized`, test them, and only mark `supported` once verified with evidence.
5. **Consolidate incrementally:**
   Use `memory_consolidate` after completing an investigation to derive an L1 investigation or L2 invariant linked to the episode refs.
6. **Contradict or supersede without erasing history:**
   When new evidence disproves an old conclusion, use `memory_store` with `supersedes: oldRef` or `memory_lifecycle` with `lifecycle: "contradicted"`. Never delete historical records that other conclusions depend on.

## Examples

```ts
// 1. Capture an L0 episode with failed vs worked approaches
memory_store({
  level: "L0",
  type: "episode",
  summary: "Fixed corm_open handle aliasing bug",
  episode: {
    problem: "Investigate fd leak on duplicate corm_open",
    approaches: [
      { approach: "Increase file descriptor limit", outcome: "failed", reason: "Masked the leak rather than fixing root cause" },
      { approach: "Add live handle table alias check", outcome: "worked", reason: "Reuses existing handle on identical (file, map)" }
    ],
    observations: ["duplicate corm_open was allocating fresh fd"],
    diffs: ["--- a/libcorm.c\n+++ b/libcorm.c\n+ alias check"],
    outcome: "success",
  },
  context: { project: "site", subsystem: "libcorm", module: "corm" }
}); // -> ref 1

// 2. Consolidate into an L2 project invariant backed by evidence
memory_consolidate({
  targetLevel: "L2",
  sourceRefs: [1],
  summary: "Invariant: corm_open must alias live handles on identical (file, map)",
  context: { project: "site", subsystem: "libcorm" }
}); // -> ref 2

// 3. Inspect why the invariant exists (downward traversal)
memory_traverse(2, "down");
// -> [L2] Ref 2: Invariant: corm_open must alias live handles (supported)
//      └─ [L0] Ref 1: Fixed corm_open handle aliasing bug (supported)

// 4. Multidimensional scan with ranking and lifecycle filtering
memory_scan("corm_open", { level: "L2", lifecycle: "supported" });

// 5. Record retrieval utility
memory_feedback(2, "useful", "Prevented file descriptor duplication");
```
