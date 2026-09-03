---
id: 59
title: "pi_adapter emits legacy flat delegation payload — ALL critical plan_review fail with CRITICAL_REVIEW_ERROR"
state: done
severity: high
requires: []
validates: "pi_adapter emit includes ownerRunId, nodeId, result per SubagentDelegationRequest; handleResponse parses SubagentDelegationResponse {status,result}"
area: "critical_agent/pi_adapter.ts:227-235 emit, 180-198 handleResponse, 15-29 SubagentExecutorFn type, 274-326 review"
parent: 43
---
# Issue: `pi_adapter` emits legacy flat delegation payload — ALL `critical plan_review` fail

- **Area:** `pi-quest` subagent delegation — `critical_agent/pi_adapter.ts:227-235` (emit), `180-198` (handleResponse), `15-29` (SubagentExecutorFn type), `274-326` (review method)
- **Runs observed:** `1788359911` `execution.log:195,441` — 2× `CRITICAL_REVIEW_ERROR: Legacy prompt-template direct delegation was removed; use workflowScript through the subagent tool or structured delegation.`
- **Severity:** High — blocks all `critical plan_review` calls; quest survived only via synthetic retry subagents (`execution.log:405`)

## Current behavior

`resolveSubagentExecutor:227` emits:
```ts
pi.events!.emit("prompt-template:subagent:request", {
  requestId,
  agent: options?.agent || "reviewer",
  task,
  context: "fresh",
  model: targetModel,
  async: true,
  cwd: resolveSubagentCwd(ctx),
});
```

This is a **flat legacy payload** — missing `ownerRunId`, `nodeId`, `result` per `SubagentDelegationRequest` (`pi-subagents/src/api/delegation.ts:24-39`).

Receiver `prompt-template-bridge.ts:57-64` checks:
```ts
function hasStructuredDelegationMarker(data: unknown): boolean {
  return Object.hasOwn(value,"ownerRunId") || Object.hasOwn(value,"nodeId")
      || Object.hasOwn(value,"result") || Object.hasOwn(value,"version");
}
```
Payload without markers → immediate rejection with error text above.

Second emit `subagent:slash:request:237-247` is legacy/test-mock channel — ignored by current bridge (`pi-subagents@0.63.0`).

`handleResponse:180-198` parses old shape (`data.isError`, `data.contentText`, `data.text`) not new `SubagentDelegationResponse {status, result.text/value}`.

`SubagentExecutorFn:15-29` return type doesn't match new response shape.

## Desired behavior (as implemented 2026-09-03)

1. **Emit** at `219-227` includes `ownerRunId` (`getQuestId(ctx) || getSessionId(ctx)`, quest-first per decision), `nodeId` (**fresh, per-invocation unique** — NOT review-kind, see concurrency note), and `result: { kind: "text" }` per `SubagentDelegationRequest`. The unsupported `async` field is **dropped** (would cause `Unsupported delegation field` parse rejection).
2. **handleResponse** at `180-198` parses `SubagentDelegationResponse {status, result.text/value}` — `status === "completed"` → resolve with `result.text` (or `JSON.stringify(result.value)` for structured); any other present `status` → reject with `error`/`status` (no `isError`/`contentText` on the structured wire).
3. **SubagentExecutorFn** return type (`15-29`) already tolerates the resolved shape — **no change required**.
4. **Removed** dead `subagent:slash:request` emit (`229-241`) and `subagent:slash:response` subscription.
5. Subscriptions now use canonical `prompt-template:subagent:response` (response) + `prompt-template:subagent:update` (activity) with legacy `subagent:*` retained for heartbeat only.

## Concurrency note (important divergence from the original draft)

The draft proposed `nodeId = review kind`. That is **wrong**: the pi-subagents bridge keys `activeOwnedNodes` by `[ownerRunId, nodeId]` and rejects a second active delegate on the same node with `duplicate_node`. `reviewId` reaches the executor as `correlationId = state.currentTurnCorrelationId` (per-*turn*, not per-invocation), so two reviews in one turn would also collide. Correct: emit a **fresh `requestId` and fresh `nodeId` per executor invocation** (unique regardless of `reviewId` reuse). Settle-dedup (`[requestId, ownerRunId, nodeId]`) is then always unique.

## Resolution — implemented 2026-09-03

Files changed:
- `src/critical_agent/pi_adapter.ts` — structured delegation emit + status-based handleResponse + subscription cleanup. See `git diff` for this file.
- `tests/delegation_payload.test.ts` — **new**: drives the real `resolveSubagentExecutor` bridge path (no `setCustomSubagentRunner`), asserts the exact structured shape the validator accepts (`ownerRunId`/`nodeId`/`result:{kind:"text"}`, no `async`, handled via `findProjectRoot` cwd anchor) and uniqueness of `requestId`/`nodeId` across same-turn invocations.
- `tests/critical_agent.test.ts` (steps 37, 38, 40) and `tests/subagent_working_dir.test.ts` (step 3) — migrated from the legacy `subagent:slash:request`/`subagent:slash:response` wire format to `prompt-template:subagent:request`/`:response`.

Behavioral note: read-only enforcement for the reviewer is now delegated to the pi-subagents `reviewer` agent profile (acceptance level `none`, read-only) — structured delegation does not carry a `tools:` allow-list on the wire, so pi-quest no longer injects `[read,grep,find,ls]`. Step 38 updated to assert the new contract.

**Verification:** `deno test --allow-all --node-modules-dir=none tests/` → **66 passed / 355 steps / 0 failed**. Bundle `pi-quest-bundle.zip` created, SHA-256 `f7791810…` (all 231 entries verified).

## History

- `pi-subagents 0.45.0 CHANGELOG:817` replaced versioned contracts with structured owned-leaf API, kept unversioned bridge as temporary fallback.
- `pi-subagents 0.63.0` hardened to unconditional reject — `pi-quest` never migrated.

Related: #13 (suppressed review logging), #52, #55, #57 (all symptom classes of this root cause).
