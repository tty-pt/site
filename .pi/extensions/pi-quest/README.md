# pi-quest

**Persistent execution and epistemic control for Pi coding agents.**

`pi-quest` is a Quest Journal extension for the
[Pi Coding Agent](https://github.com/badlogic/pi-mono). It is designed for tasks
that are too large, uncertain, or long-running to solve safely in a single model
turn.

Instead of treating an agent run as a disposable conversation, `pi-quest` gives
the work a **persistent, inspectable state**: what the agent is trying to
accomplish, what it currently believes, what it has investigated, what remains
unresolved, what it changed, what still needs verification, and what must happen
next.

The result is a long-running agent that can survive multiple turns, failures,
context compaction, session changes, and interruptions without losing the actual
task.

## Why it exists

An autonomous coding agent has a few recurring failure modes:

- it starts implementing before it understands the problem;
- its plan drifts away from the user's actual request;
- contradictory evidence is rationalized instead of triggering reconsideration;
- a failed attempt is forgotten after context pressure or compaction;
- the model says “done” when important work remains;
- internal enforcement happens in the extension but the model is never told;
- recovery state becomes inconsistent with the durable task state;
- a long task becomes impossible to reconstruct after the fact.

`pi-quest` is built around the opposite assumption:

> **A long-running agent should be treated as a persistent execution process,
> not as a sequence of unrelated prompts.**

## What pi-quest provides

### Persistent quests

Each substantial task has a durable quest record containing the working state of
the effort.

The journal can preserve:

- the original user request;
- the quest goal and refinements;
- research and evidence;
- current understanding;
- assumptions and open questions;
- the plan and plan revisions;
- remaining work;
- changed files;
- verification status;
- reassessment state;
- checkpoint information;
- sub-quest relationships.

This state survives ordinary turns and can be reconstructed when the agent's
conversational context is lost.

### Research and implementation gates

`pi-quest` deliberately separates **understanding a problem** from **changing
the code**.

For substantive quests, the workflow can require research and establishment of a
durable understanding before implementation is permitted.

The implementation gate is enforced at the tool boundary. It is not merely an
instruction in the system prompt.

When an action is blocked, the extension reports the reason and the required
next step back to the agent through the model-facing messaging path.

The goal is not bureaucracy for its own sake. It is to prevent the common
failure mode of confidently modifying a system that the agent has not yet
understood.

### Reassessment when reality disagrees with the plan

A plan is not treated as sacred.

Test failures, contradictory evidence, inconsistent checkpoints, and other
meaningful failures can require the agent to stop implementation and reassess
its assumptions.

The intended loop is:

```text
investigate
    ↓
plan
    ↓
implement
    ↓
verify
    ↓
failure / contradiction
    ↓
reassess
    ↓
new evidence
    ↓
revise understanding / plan
    ↓
continue
```

A failed implementation attempt therefore becomes information for the next
attempt rather than something the agent simply tries to forget.

### Adversarial critical review

For substantive work, `pi-quest` can use an independent read-only reviewer to
challenge the main agent's reasoning.

There are two distinct uses:

**Direction review**

> Are we solving the right problem and moving in the right direction?

**Final acceptance review**

> Did the resulting work actually satisfy the user's original request?

The reviewer is expected to challenge assumptions, look for requirement drift
and premature closure, inspect evidence independently, and attack its own
initial conclusion before returning a verdict.

The final review is intentionally tied to the exact recorded original request
rather than only to the agent's latest plan or summary.

A successful implementation is therefore not automatically a successful task.

### Sub-quests

Large objectives can be decomposed into persistent sub-quests without creating a
second task-management system.

Sub-quests retain their relationship to the parent quest and can be resumed and
completed independently while preserving the overall quest hierarchy.

### Transactional context compaction

Long-running agents eventually encounter context pressure.

`pi-quest` treats compaction as a stateful transaction rather than as an
incidental event.

The compaction machinery includes:

- pressure detection;
- pre-compaction durable checkpoints;
- transaction identity;
- success/failure handling;
- recovery;
- resume obligations;
- reconstruction after context loss.

The important invariant is that **the durable quest state must remain
trustworthy even when compaction fails**.

A failed compaction is not silently treated as a successful one.

### Persistent recovery

The extension is designed around the assumption that failures happen.

Quest state can be reconstructed from durable data rather than relying entirely
on the model remembering what happened earlier in the conversation.

This applies to:

- interrupted execution;
- failed verification;
- reassessment;
- compaction and resume;
- sub-quest transitions;
- session changes;
- persistence problems.

Recovery is part of the execution model, not an afterthought.

### Explicit tool gating

Tools are classified according to what they are allowed to do in the current
quest state.

For example, investigation, implementation, verification, and Quest Journal
operations do not necessarily have the same permissions.

The gate is enforced where tool calls enter the extension's control path, and
model-visible feedback is generated when an action is rejected.

This is intended to make important workflow promises **enforceable**, rather
than merely advisory.

### Execution logging and diagnostics

The execution log is an audit trail rather than a second task database.

It records the important events needed to reconstruct an execution, including
things such as:

- lifecycle transitions;
- tool activity;
- files read and changed;
- commands executed;
- failures;
- enforcement decisions;
- reassessment;
- compaction;
- resume;
- critical-review lifecycle.

The aim is to answer:

> **What actually happened?**

without turning the log into a copy of the entire model transcript.

Completed runs can also be packaged as diagnostic artifacts for later
inspection.

## Design principles

### The journal is durable state, not model memory

The model's context can disappear. The quest state must not.

### Enforcement must reach the agent

An extension-side rejection that the model never sees is not useful enforcement.

When the system requires a different action, that requirement is communicated
through the same model-facing execution path used by the rest of the workflow.

### Evidence outranks confidence

The agent's confidence is not evidence.

Research, repository state, test results, diffs, and durable checkpoints are
treated as evidence that can confirm or invalidate the current direction.

### Failed operations remain failed

Recovery logic must not silently turn a failed transaction into a successful one
merely so execution can continue.

### One source of truth per concept

Quest state, compaction transactions, review state, obligations, and diagnostic
logs have different responsibilities. The architecture aims to keep them
distinct rather than representing the same fact independently in several places.

## Architecture

The implementation is divided into focused subsystems rather than one monolithic
extension:

```text
src/
├── types.ts            Host API types and persisted state models
├── state.ts            Session/quest state and reconstruction state
├── reconstruction.ts   Rebuild state from durable quest records
├── research.ts         Research and reassessment workflow
├── gates.ts            Implementation/lifecycle gate policy
├── validation.ts       Research and consistency validation
├── persistence.ts      Durable quest persistence and verification
├── obligations.ts      Model-facing execution obligations
├── messaging.ts        Internal agent/user messaging
├── tool_gating.ts      Tool and command permission enforcement
├── subquest.ts         Parent/child quest execution
├── lifecycle.ts        Quest creation, activation, completion, archival
├── classification.ts   User-message classification
├── compaction/         Pressure, checkpoints, transactions, recovery, resume
├── critical_agent.ts   Adversarial review workflow
├── diagnostic.ts       Run diagnostics and packaging
├── logging.ts          Execution audit logging
├── hooks.ts            Pi lifecycle integration
├── tools.ts            Quest Journal tools exposed to Pi
├── commands.ts         Slash commands
└── ui.ts               Live quest status
```

The directory structure is intended to separate domain responsibilities while
keeping the execution lifecycle explicit.

## Testing

The project has an extensive test suite covering the stateful execution model,
including:

- lifecycle consistency;
- research and reassessment;
- implementation gates;
- persistence and checkpoint verification;
- compaction and recovery;
- resume behavior;
- sub-quest handling;
- tool enforcement;
- observability;
- diagnostic archives;
- Critical Agent behavior;
- reconstruction and persistence across execution boundaries.

Run the suite with:

```bash
npm test
```

or:

```bash
deno test --allow-all --node-modules-dir=none tests/
```

## Package structure

`pi-quest` is a Pi extension package:

```json
{
  "pi": {
    "extensions": ["index.ts"]
  }
}
```

Install/load the extension in a Pi environment according to Pi's normal
extension mechanism.

For development, the package also provides:

```bash
npm test
```

and a bundle command:

```bash
npm run zip
```

The latter packages the extension together with the diagnostic run artifacts
used for post-run analysis.

## What this project is — and isn't

`pi-quest` is **not** a generic agent framework or a replacement for Pi.

It is an execution-control layer built around Pi's agent lifecycle.

It does not attempt to make the model omniscient. Instead, it makes the parts of
a long-running task that can be represented, persisted, checked, and enforced
explicit.

It is intentionally opinionated about:

- research before implementation;
- durable task state;
- evidence-based reassessment;
- explicit gates;
- recoverable execution;
- independent review;
- and truthful completion.

The central idea is simple:

> **For difficult tasks, the agent should not merely remember what it intended
> to do. The system should remember what the task requires, what has been
> established, what has actually happened, what failed, and what must happen
> next.**
