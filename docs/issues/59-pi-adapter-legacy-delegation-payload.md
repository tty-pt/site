---
id: 59
title: "pi_adapter emits legacy flat delegation payload — ALL critical plan_review fail with CRITICAL_REVIEW_ERROR"
state: ready
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

## Desired behavior

1. **Emit** at `227-235` must include `ownerRunId` (quest sessionId or `getQuestId(ctx)`), `nodeId` (review kind: `"direction-review"` | `"final-review"`), and `result: { kind: "text" }` per `SubagentDelegationRequest`.
2. **handleResponse** at `180-198` must parse `SubagentDelegationResponse {status, result.text/value}` — `data.status === "completed"` → resolve with text, `data.status === "failed"` → reject.
3. **SubagentExecutorFn** return type updated to match `SubagentDelegationResponse`.
4. **Remove** dead `subagent:slash:request` emit (237-247) — legacy channel no longer wired.
5. **Subscribe** to canonical `SUBAGENT_DELEGATION_STARTED_EVENT` / `SUBAGENT_DELEGATION_UPDATE_EVENT` instead of ad-hoc `subagent:activity/started/tool_call/tool_result/turn_start/turn_end` + `prompt-template:subagent:activity`.

## Manual validation in `pi`

1. Trigger `critical plan_review` on any quest → no `CRITICAL_REVIEW_ERROR`; reviewer subagent starts, reports verdict, `handleResponse` resolves.
2. Timeout path works: reviewer exceeds `timeoutMs` → `handleResponse` receives error status → reject with `classifyTimeoutLayer`.

## History

- `pi-subagents 0.45.0 CHANGELOG:817` replaced versioned contracts with structured owned-leaf API, kept unversioned bridge as temporary fallback.
- `pi-subagents 0.63.0` hardened to unconditional reject — `pi-quest` never migrated.

Related: #13 (suppressed review logging), #52, #55, #57 (all symptom classes of this root cause).
