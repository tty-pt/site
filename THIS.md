# Draft → Reviewer → Auto-Promote Workflow — Plan

## 1. Requirements

1. The plan reviewer must launch **immediately** when a draft is created or
   updated — not gated on a user refinement message threshold.
2. If the draft is revised while a plan_review is running, the running reviewer
   must be **cancelled** (killed) and a fresh one launched against the new draft
   hash.
3. When the reviewer reaches an **APPROVE** verdict on an actionable draft, the
   draft must **auto-promote** to a current (active) quest — without waiting for
   the user to say "go".
4. A user "go" (CONFIRMATION) during a draft must **force-promote immediately**,
   even if the auto-review hasn't approved yet.

## 2. Current vs. Proposed Flow

### Current flow (broken)

```
User prompt
  → draft auto-created (activeDraft set, active empty)
  → agent researches... (evidence climbs)
  → RESEARCH_DRAFT_GATED logs fire but NEVER launch a reviewer   ← BUG 1
  → agent tries quest_update_state → blocked (PROVISIONAL_RESEARCH_PENDING)
  → user says "go" → CONFIRMATION_REJECTED (no APPROVE yet)     ← BUG 2
  → checkAndTriggerPlanReview fires → reviewer launches
     → subagent runtime crashes → no verdict → deadlocked        ← BUG 3 (external)
  → draft revision while review running → coalesced into pending ← BUG 4
     (not torn down, old zombie continues)
  → reviewer APPROVE → sets draftLastReviewKey, tells agent
     "await explicit go" — but nothing calls promoteDraft         ← BUG 5
```

### Proposed flow (after Changes A–D)

```
User prompt
  → draft auto-created
  → checkAndTriggerPlanReview fires immediately                   [Change A]
  → reviewer launches (if draft is actionable enough)
  → agent researches, draft updates
  → each draft update:
      1. cancelActiveReview (kills running reviewer)              [Change C]
         → host aborts child session via kill-chain
         → clears .review.active, reviewPromiseByKey, pending
      2. checkAndTriggerPlanReview fires for new hash             [Change A]
  → reviewer APPROVE on actionable draft
      → auto-promoteDraft (draft → active quest)                  [Change B]
      → flow enters research then plan for active quest
  → user says "go" at any time
      → force-promoteDraft (user override)                        [Change D]
```

## 3. Root Cause Analysis

Confirmed from run `1788512794` + code reading:

1. **No reviewer on draft** — `checkAndTriggerPlanReview` (draft branch) only
   fires from `before_agent_start` when a *user refinement/requirement* message
   reaches `dpLen>=2` or `evidence>=7` (`src/hooks/index.ts:732-748`), and from
   the `CONFIRMATION_REJECTED` fallback (`src/hooks/index.ts:498-501`).
   Researcher-only drafting never triggers it; `handleTurnEnd` has no draft
   plan-review trigger.
2. **`go` deadlocks** — promotion requires `isDraftReviewValid()`
   (`src/commands/promote.ts:41-47`, `src/hooks/index.ts:460-503`), which is
   false until a reviewer APPROVEs with a matching boundaryKey
   (`src/critical_agent/policy.ts:93-118`).
3. **Revision while review running** — a changed draft hash makes the old review
   superseded, and a new request is *coalesced into pending* (single-flight +
   `.review.active` witness in `runCriticalReview`,
   `src/critical_agent/policy.ts:255-354`), not torn down. `cancelActiveReview`
   exists (`src/critical_agent/tracker.ts:322-349`) but is never called for
   hash-drift.
4. **No auto-promote on APPROVE** — on APPROVE, `checkAndTriggerPlanReview` only
   sets `draftLastReviewKey` and tells the agent to "await explicit go"
   (`src/critical_agent/policy.ts:958-974`);
   `src/critical_agent/reconcile/approved.ts` writes `lastPlanReviewApproval`
   but nothing calls `promoteDraft`.
5. **External subagent crash** (out of pi-quest scope but flow-breaking):
   the one reviewer that DID fire crashed on a subagent runtime extension-load
   error in `pi-subagents` (`subagent-prompt-runtime.ts`: "Cannot read
   properties of undefined (reading 'runtimeAcknowledgements')"). This turned a
   recoverable gap into a deadlock.

## 4. Implementation Plan

### Change A — Auto-launch reviewer at moment of draft creation/update

Hook `checkAndTriggerPlanReview(pi, ctx)` at two sites:

**Site 1: Auto-draft creation** (`src/hooks/index.ts` ~984-991)

After the `persist/updateUIStatus/sendInternalAgentMessage` block, add:
```ts
checkAndTriggerPlanReview(pi, ctx).catch(() => {});
```
At t=0 the draft is a template placeholder, so the reviewer won't produce a
plan-verdict until the plan is drafted. Compute the guard locally at the
insertion point — `hasActionablePlanDraft` (hooks/index.ts:668-689) is a
different code path. Use the same regex pattern from `isActionableDraftPlan`
(§4 Change B) or a simplified version:
```ts
try {
  const { isActionableDraftPlan } = await import("../critical_agent/policy.ts");
  if (isActionableDraftPlan(slug)) {
    checkAndTriggerPlanReview(pi, ctx).catch(() => {});
  }
} catch {}
```
Or inline the regex if the import order is a concern.

**Site 2: Refine / draft update** (`src/hooks/index.ts:542`)

After `appendToFutureDraft` returns (after writing), replace the entire
`setPendingReview` coalesce block (lines 616-664) with:

1. Find the active plan_review (`findActiveReviewForQuest`) and
   `cancelActiveReview(reviewId, "draft_revised")` — wired via Change C
   to emit `"prompt-template:subagent:cancel"` (see §4 Change C).
2. Clear blockers: `removeReviewActiveFile(questId)`,
   `reviewPromiseByKey.delete(\`${questId}:plan_review:draft:${slug}:${oldHash}\`)`
   (extract `oldHash` from `state.draftLastSavedHash` or current boundaryKey),
   `clearPendingReview(slug,"plan_review")`.
3. Fire `checkAndTriggerPlanReview(pi, ctx)` for the new hash.

The auto-review threshold block (lines 666-748, which also calls
`checkAndTriggerPlanReview` at line 394) remains unchanged — it provides a
secondary trigger for cases where the refine handler doesn't fire (e.g., agent
auto-research). The dedup key (`lastDraftReviewRequestKey`) ensures the second
call is a no-op.

`checkAndTriggerPlanReview` is idempotent: returns null when
`isDraftReviewValid` (policy.ts:887) or when the request key is already
recorded (policy.ts:931-943).

### Change B — Auto-promote on APPROVE

In `checkAndTriggerPlanReview` (`src/critical_agent/policy.ts:958-974`),
after setting `s.draftLastReviewKey` and sending the steer, add:

```ts
if (isActionableDraftPlan(draftSlug) && s.activeDraft === draftSlug) {
  const { promoteDraft } = await import("../commands/promote.ts");
  const res = await promoteDraft(draftSlug, ctx, pi);
  if (res.success) {
    // steer: "Draft 'X' reviewer APPROVED → auto-promoted to current quest (qid Y)".
    // promoteDraft sets researchRequired=true → flow enters research then plan.
  } else {
    // promoteDraft failed (e.g., file write error). Log and leave draft in place
    // for the next review cycle. Do NOT rethrow — the review itself succeeded.
  }
}
```

**Why the ordering is safe:** `lastPlanReviewApproval` is written by
`reconcile/approved.ts:55-67`, which runs at `background.ts:246` (inside
`executeReviewBackground`, BEFORE `resolveExecution(res)` at
`background.ts:254`). So by the time `checkAndTriggerPlanReview`'s
`await runCriticalReview(...)` returns with the verdict, `lastPlanReviewApproval`
is ALREADY set. Combined with `draftLastReviewKey` (set at policy.ts:963),
`isDraftReviewValid(s)` is true immediately — calling `promoteDraft` right after
line 974 passes the gate (`promote.ts:41-47`). After promoting, `s.activeDraft`
is null (promote.ts:110), so later notification turns no-op.

**New helper — `isActionableDraftPlan`** (policy.ts):

`hasActionablePlanDraft` is only a local inline heuristic in
hooks/index.ts:668-689; it is NOT exported and NOT present in policy.ts. Add:

```ts
export function isActionableDraftPlan(slug: string): boolean {
  try {
    const data = readFileSync(`${FUTURE_DIR}/${slug}.md`, "utf8");
    const m = data.match(/##\s*Plan[\s\S]*?(?=\n##\s+|$)/i);
    if (!m) return false;
    const body = m[0].replace(/##\s*Plan[^\n]*\n/i, "").trim();
    if (!body || body === "1." || body === "-" || body.length < 10) return false;
    return /[-*]\s+\S|^\s*\d+\.\s+\S/m.test(body);
  } catch { return false; }
}
```

`policy.ts` already imports `readFileSync`/`existsSync` from `node:fs` (line 2)
and `FUTURE_DIR` (line 3). No new imports needed.

### Change C — True teardown of running reviewer on draft revision

When a draft is updated to a new hash while a plan_review is running, **truly
cancel** the in-flight reviewer and launch a fresh one.

**The kill-chain is verified end-to-end in `pi-subagents` source:**

```
pi-quest cancelActiveReview(reviewId,"draft_revised")     tracker.ts:322-349
  └─ rev.abortController.abort(reason)
      └─ PiSubagentReviewer wired to also emit (pi_adapter.ts wiring, see below)
         pi.events.emit("prompt-template:subagent:cancel",
           {requestId, ownerRunId, nodeId})
         └─ host bridge handler                          slash/prompt-template-bridge.ts:158
            requires hasStructuredDelegationMarker → all 3 fields valid
            attemptControllers.get(attemptKey).abort()   line 170
            └─ AbortController IS controller.signal passed into
               executeRequest(..., controller.signal,...)  bridge line 337
               └─ executeDelegated → execute → execution.ts
                  options.signal.addEventListener("abort", kill, {once:true})
                                                          execution.ts:1297
                  kill() { abortChild();
                           setTimeout(()=>settle(undefined,true),3000).unref() }
                  abortChild() { session.abort() }        execution.ts:619-624
                  └─ terminates the live subagent child session (real kill)
                  + bridge emits status:"cancelled"       lines 371/306
                    → pi-quest settles promptly
                    → finally() clears .review.active     policy.ts:678-685
```

Pre-launch cancel is also handled: `pendingAttemptCancels`/`pendingLegacyCancels`
(bridge lines 149-150,171,179) remember the cancel and abort immediately on
launch. The 3 s hard timer (execution.ts:1289-1292) force-settles if the child
does not stop.

**Three stacking guards a fresh review must clear:**

| Guard | What | Fresh review blocked? |
|-------|------|----------------------|
| Single-flight `reviewPromiseByKey` | key = `${questId}:${kind}:${boundaryKey}` | No — revised hash yields NEW key, old key auto-deletes on settle |
| `.review.active` filesystem witness | `isReviewActive(questId)` | Yes — must be cleared explicitly |
| `inCriticalReview` state flag | set at background.ts:119, cleared at background.ts:337 | No — only consulted at draft shard |

**Teardown sequence:**

1. `cancelActiveReview(reviewId, "draft_revised", ctx)` — sync; marks
   `cancelled` and `abortController.abort(reason)`. The `PiSubagentReviewer`
   wiring emits the cancel event with the stored identity.
2. The host bridge aborts the attempt controller → `kill() → session.abort()`
   terminates the live child → bridge emits `status:"cancelled"` → pi-quest
   settles and `finally()` clears `.review.active`.
3. Belt-and-suspenders fallback if cancel not honored:
   - `removeReviewActiveFile(questId)` (utils/mutex.ts:215-222)
   - `reviewPromiseByKey.delete(<old singleKey>)`
   - `clearPendingReview(slug, "plan_review")` (tracker.ts:68-81)
4. Fire `checkAndTriggerPlanReview(pi, ctx)` (Change A hook at the update site).
5. `inCriticalReview`: do NOT force it false. The fresh `runCriticalReview` only
   consults `isReviewActive(questId)` and single-flight at the draft shard — so
   it can start.

**Safety fallback even if cancel fails:** `isReviewSnapshotCurrent`
(snapshot.ts:151-206) detects draft boundary-key drift and
`reconcileReviewResult` (reconcile.ts:63-76, 107) skips applying a superseded
verdict, so a stale result could never set `lastPlanReviewApproval` or
auto-promote onto the revised draft.

**Wiring — surface cancel identity and emit cancel event:**

Identity generated at launch: `src/critical_agent/pi_adapter.ts:182-187`
computes `requestId`, `nodeId`, `ownerRunId` and emits them at lines 326-337.
Today those locals are closure-private and discarded.

**Do NOT modify `ReviewResult` in `src/types.ts`** — requestId/nodeId/ownerRunId
are subagent delegation metadata, not review results. Use a module-level Map
instead (Finding 2).

Plumbing (three files, see also §11 Finding 1):

**A. Executor closure** (`pi_adapter.ts:175-339`):
- After line 187 (identity generation), store identity keyed by `options.reviewId`:
  ```ts
  const cancelIdentityMap = new Map<string, {requestId,nodeId,ownerRunId}>();
  // inside Promise constructor, after line 187:
  if (options?.reviewId) cancelIdentityMap.set(options.reviewId,
    {requestId, nodeId, ownerRunId: getQuestId(ctx) || getSessionId(ctx)});
  ```
- In `handleResponse` resolve path (line 288) and all reject paths (lines 206,
  220, 262), delete from map: `if (options?.reviewId) cancelIdentityMap.delete(options.reviewId);`

**B. PiSubagentReviewer.review()** (`pi_adapter.ts:376-418`):
Restructure to NOT await the executor immediately (Finding 1):
```ts
// line 407 replacement:
const execPromise = executor(prompt, {
  agent: "reviewer", isCriticalReview: true, reviewKind: input.kind,
  triggerReason: input.triggerReason || input.kind, model: targetModel,
  tools: ["read", "grep", "find", "ls"], async: true,
  reviewId: input.reviewId, timeoutMs: input.timeoutMs,
  onActivity: input.onActivity,
});
// Wire cancel listener using Map (after executor call, before await):
const cancelId = cancelIdentityMap.get(input.reviewId!);
if (cancelId && input.signal) {
  input.signal.addEventListener("abort", () => {
    try { pi.events?.emit("prompt-template:subagent:cancel", cancelId); } catch {}
  }, {once: true});
}
rawRes = await execPromise;  // now await
```

- Gate failures gracefully: if `pi.events.emit` throws or the host does not
  handle the event, the superseded-discard path guarantees correctness.
- Pin the `pi-subagents` version in a comment and log
  `cancel emitted` / `cancel fallback` for diagnostics.

**Draft resolves to a real questId — no special case needed:**
During drafting, `state.questId` is set via `ensureQuestId(ctx)`
(hooks/index.ts:919) BEFORE `state.activeDraft` is set (line 938). So
`isReviewActive(state.questId)` / `removeReviewActiveFile(questId)` operate on
`.pi/quest/current/<qid>/.review.active` — a valid cross-process witness even
while the draft file lives in `FUTURE_DIR`.

### Change D — `go` forces promote

**`promoteDraft` needs a `force` option:**

Current signature (promote.ts:21-25):
```ts
promoteDraft(slug: string, ctx?, pi?): Promise<{success; message?; qid?}>
```
Add options object:
```ts
promoteDraft(slug, ctx, pi, options?: { force?: boolean })
```
Gate becomes: `if (!options?.force && hasReviewer && !isDraftReviewValid(s))`.

**CONFIRMATION handler change:**

At `src/hooks/index.ts:452-503`. Currently:
- `valid` true → `promoteDraft` (lines 461-465).
- `valid` false → logs `CONFIRMATION_REJECTED`, steers, then
  `checkAndTriggerPlanReview` (lines 485-503) — THIS is the "go blocked" deadlock.

Change: when `!valid`, best-effort fire `checkAndTriggerPlanReview` FIRST
(obtain the compliance signal), then STILL `promoteDraft(slug, ctx, pi,
{force:true})` — the user's explicit `go` is an override. Keep a transparency
steer ("promoted despite pending/absent reviewer approval").

The `checkAndTriggerPlanReview` call is fire-and-forget (`.catch(() => {})`)
— do NOT await it; the user's `go` should not wait for a full review cycle.
If the review eventually approves, the draft is already promoted so
`checkAndTriggerPlanReview` returns null (isDraftReviewValid is false after
promote clears `activeDraft`).

## 5. Reference

### 5.1 Key functions and their locations (all `src/…`)

**`runCriticalReview`** — `src/critical_agent/policy.ts:182-866`

```ts
export async function runCriticalReview(
  pi: ExtensionAPI, ctx: ExtensionContext, options: CriticalReviewOptions,
): Promise<CriticalReviewExecutionResult>
```

- **Single-flight** `reviewPromiseByKey` (a `Map`), key =
  `${questId}:${options.kind}:${options.boundaryKey || currentPlanVersion || currentHash}`
  (lines 255-292). If present and `!options.force`, returns existing promise
  (coalesced). Auto-deletes on settle (policy.ts:678-681).
- **Active-file witness** `.review.active` checked by `isReviewActive(questId)`
  (lines 293-354). If active and `!options.force`, it `setPendingReview(...)`
  and returns `{ inProgress: true, skipped: true, error: "global_review_cap_active_file" }`.
- **`AbortController`** created at line 670, passed to `executeReviewBackground`
  → `reviewer.review(...)` as `signal` (background.ts:167).

**`checkAndTriggerPlanReview`** — `src/critical_agent/policy.ts:879-1379`

DRAFT-aware trigger (Change A & B integration point):
- If `s.activeDraft` and `isDraftReviewValid(s)` → returns null
  (lines 886-898).
- If subagent not registered → returns null (lines 899-922).
- Dedup key `draft_review:${draftSlug}:h${hash}` vs `s.lastDraftReviewRequestKey`
  (lines 930-943) → null if same.
- Else `runCriticalReview({ kind:"plan_review", questSlug: draftSlug,
  triggerReason, boundaryKey: draft:${draftSlug}:${hash} })` (lines 944-949).
- Only records `s.lastDraftReviewRequestKey = key` AFTER verdict or error
  (lines 950-957).
- **APPROVE branch** (lines 958-974): sets `s.draftLastReviewKey`, sends steer.
  Change B adds auto-promote here.

**`requestPlanReview`** — `src/critical_agent/policy.ts:866-877`
Thin wrapper. Only caller is `src/tools/update/executor.ts:457` — for an
ALREADY-ACTIVE quest, NOT usable during drafting.

**`requestDirectionReview` / `checkAndTriggerDirectionReview`** — `policy.ts:1120+`
Requires `s.active` (a REAL quest) — returns null during drafting (line 1143).

**`isDraftReviewValid`** — `src/critical_agent/policy.ts:93-118`
```ts
export function isDraftReviewValid(targetState?: StoredState): boolean {
  const s = targetState || state;
  if (!s.activeDraft) return false;
  const approval = s.lastPlanReviewApproval;        // null → false
  // reads future/*.md, computes hash, expectedKey = `draft:${slug}:${hash}`
  // returns approval.boundaryKey === expectedKey ||
  //         approval.boundaryKey === s.draftLastReviewKey
}
```
Recomputes live file hash every call — draft revision automatically invalidates.

**`lastPlanReviewApproval` write site** — `reconcile/approved.ts:55-67`
```ts
targetState.lastPlanReviewApproval = {
  questId, planVersion: snapshot.planVersion, reviewId: correlationId,
  boundaryKey: snapshot.boundaryKey || targetState.lastPlanReviewBoundaryKey || null,
  saveHash: snapshot.stateHash, saveCount: targetState.saveCount, timestamp: Date.now(),
};
if (snapshot.boundaryKey) targetState.lastPlanReviewBoundaryKey = snapshot.boundaryKey;
```
Called from `reconcileReviewResult` at `policy/reconcile.ts:121-131`.

**`promoteDraft`** — `src/commands/promote.ts:21-138`
```ts
export async function promoteDraft(
  slug: string, ctx?: ExtensionContext, pi?: ExtensionAPI,
): Promise<{ success: boolean; message?: string; qid?: string }>
```
- Gate at lines 38-47: `if (hasReviewer && !isDraftReviewValid(s))` → reject.
  Change D adds `{ force: true }` to skip this gate.
- On success: generates new `qid`; sets `s.activeDraft=null`, `s.active=targetSlug`;
  writes to `questPath(qid)`; unlinks `futureDraftPath(targetSlug)`;
  copies to `future-archive/`; sets `s.researchRequired=true`,
  `researchComplete=false`, `reassessmentRequired=false`;
  `startResearchEpoch(s)`; `syncImplementationPermission(s)`.

**CONFIRMATION draft handler** — `src/hooks/index.ts:452-503`
```ts
if (classification === UserMessageClassification.CONFIRMATION) {
  const { isDraftReviewValid } = await import("../critical_agent/policy.ts");
  const { getCustomSubagentRunner, isSubagentToolRegistered } = await import("../critical_agent/index.ts");
  const hasReviewer = Boolean(getCustomSubagentRunner()) || isSubagentToolRegistered(pi, ctx);
  const valid = hasReviewer ? isDraftReviewValid(state) : true;
  if (valid) { const { promoteDraft } = await import("../commands/promote.ts");
               const res = await promoteDraft(state.activeDraft, ctx, pi); ... }
  else { logUserInteraction("CONFIRMATION_REJECTED",...);
         sendInternalAgentMessage(pi,"⚠️ Draft … not yet reviewer-approved …", "steer");
         const { checkAndTriggerPlanReview } = await import("../critical_agent/policy.ts");
         await checkAndTriggerPlanReview(pi, ctx); }
}
```
Change D: replace so CONFIRMATION always promotes via `promoteDraft(slug, ctx, pi,
{ force:true })`, best-effort firing plan review first if `!valid`.

**Auto-review threshold block** — `src/hooks/index.ts:712-748`
```ts
const canAutoReviewDespitePlaceholder = dpLen >= 1 && evidence >= 7;
if ((dpLen >= 2 || (dpLen >= 1 && evidence >= 7)) &&
    (hasActionablePlanDraft || canAutoReviewDespitePlaceholder)) {
  if (!isDraftReviewValid(state)) checkAndTriggerPlanReview(pi, ctx).catch(()=>{});
}
```
In `before_agent_start`, inside the REFINEMENT/QUESTION/REQUIREMENT branch.
Change A fires `checkAndTriggerPlanReview` at additional sites (auto-draft
creation and refine/update) to cover agent-driven research paths.

### 5.2 Reviewer teardown tools

- `cancelActiveReview(reviewId, reason, ctx?)` — `tracker.ts:322-349`.
  Sets `cancelled`, `cancellationRequested`, `cancellationReason`, calls
  `rev.abortController?.abort(reason)`, deletes from `activeReviews`.
- `findActiveReviewForQuest(slug)` — tracker (kind matching, e.g. `plan_review`).
- `getActiveReviews()` — tracker Map.
- `setPendingReview(...)` / `getPendingReview(slug, kind)` / `dequeuePendingIfNeeded(...)`
  — tracker; `onPending` at policy.ts:825-858 dequeues after active completes.
- `removeReviewActiveFile(questId)` — `utils/mutex.ts:215-222`.

### 5.3 State fields that drive everything (`StoredState` in `src/state.ts`)

| Field | Meaning |
|---|---|
| `state.activeDraft` | draft slug (set during drafting; null once promoted) |
| `state.active` | real quest slug (empty during drafting) |
| `state.draftLastReviewKey` | set on APPROVE at policy.ts:963; used by isDraftReviewValid fallback |
| `state.lastPlanReviewApproval` | written in reconcile/approved.ts:55 |
| `state.lastDraftReviewRequestKey` | dedup key for draft plan_review (policy.ts:955) |
| `state.draftCreatedAt`, `state.draftPrompts` | draft metadata / accumulated requirements (cap PROMPT_MAX_COUNT) |
| `state.draftLastSavedHash` | current draft content hash (used by isReviewSnapshotCurrent) |
| `state.awaitingReview` | scalar gate `{kind, reviewId,…}` set in runCriticalReview (~731-737) |
| `state.inCriticalReview` | flag during background review |
| `state.researchRequired` | set true by promoteDraft → next phase is research |
| `state.reassessmentRequired` | if true, direction/plan reviews are suppressed until resolved |

### 5.4 Reconciliation flow

`reconcileReviewResult` in `src/critical_agent/policy/reconcile.ts` → dispatches
to `reconcile/approved.ts`, `reconcile/rejected.ts`, `reconcile/uncertain.ts`.
Approved → writes `lastPlanReviewApproval`. Rejected → likely sets
`reassessmentRequired` (verify before implementing). `submitReviewRebuttal` at
policy.ts:1357 re-triggers a review with a rebuttal.

### 5.5 Kill-chain evidence index

Every path was read in `~/.pi/agent/npm/node_modules/pi-subagents/src`.

| Claim | File:line |
|---|---|
| Cancel event name | `api/delegation.ts:8`; alias in `slash/prompt-template-bridge.ts:30` |
| Host subscribes and aborts attempt controller | `slash/prompt-template-bridge.ts:158-180` (line 170; legacy line 176) |
| Structured marker requires all 3 fields | `slash/prompt-template-bridge.ts:60-68`, handler guard lines 163-167 |
| Pre-launch cancel remembered | `slash/prompt-template-bridge.ts:149-150,171,179` |
| `controller.signal` threaded into execution | Bridge line 337 |
| Execution wires it to `kill()` | `runs/foreground/execution.ts:1297` |
| `kill()→abortChild()→session.abort()` + 3s timer | `runs/foreground/execution.ts:1285-1293`, `619-624` |
| Bridge emits `status:"cancelled"` | `slash/prompt-template-bridge.ts:371/306` |
| Async/background runs also wired | `runs/foreground/subagent-executor.ts:2149-2150` |
| pi-quest emits 3 fields on same bus | `src/critical_agent/pi_adapter.ts:182-187` (generation), `326-337` (emit) |
| pi-quest entry guard is `signal?.aborted` | `src/critical_agent/pi_adapter.ts:376-385` |
| Bus is shared | `slash/prompt-template-bridge.ts:114` |

**Version pin:** read `~/.pi/agent/npm/node_modules/pi-subagents/package.json`
`version` at implementation time and note in a comment near the cancel wiring.

## 6. Verification

### Tests to add (`tests/plan_review.test.ts`)

Test harness helpers (LOCAL to that file):
- `createMockExtensionAPI()` / `createMockContext(tokens, sessionId)` (lines 33 / 109).
- Stub reviewer via `setCustomSubagentRunner(runner)` where runner returns
  `"…VERDICT: APPROVE…"` (see passRunner at plan_review.test.ts:925-948;
  approveRunner at 399-420). Clear with `setCustomSubagentRunner(null)`.
- Wrap logging calls in `asyncContext.run(ctx, () => …)`.
- Setup for DRAFT test: set `s.questId`, `s.activeDraft=slug`, `s.active=""`,
  `s.stack=[]`, `s.draftPrompts=[…]`; `mkdir FUTURE_DIR`;
  write `${FUTURE_DIR}/${slug}.md`; `mkdir ${currentDir}/${qid}`;
  `rm(getQuestLogPath(qid, currentDir), {force:true})`.
- Reviewer registration for draft shard is via `setCustomSubagentRunner`
  (policy.ts:899-900).

Tests:

1. **Draft update → new plan_review** (new hash → new boundaryKey, dedup passes).
2. **Revision while active → true-kills and relaunches** — assert
   `cancelActiveReview` emitted the correct `prompt-template:subagent:cancel`
   payload, old `runCriticalReview` settled with `error:"cancelled"`,
   `.review.active` cleared, fresh `plan_review` launched immediately (no
   coalesce), stale verdict never auto-promotes.
3. **APPROVE auto-promotes** without a user `go`.
4. **`go` force-promotes** even when not yet approved.
5. **True-cancel plumbing** — `requestId`/`nodeId`/`ownerRunId` surfaced and
   cancel event carries all three required fields (stubbed, no live host).

### Verification commands

```bash
npm --prefix .pi/extensions/pi-quest test     # 0 failures, baseline + new tests additive
npm --prefix .pi/extensions/pi-quest run zip  # repackage
```

Note on test-count discrepancy: run `npm test` and record the actual baseline
BEFORE changes; do not trust stale numbers.

## 7. Summary of code touches

| File | Change |
|---|---|
| `src/critical_agent/pi_adapter.ts` | Add `cancelIdentityMap` + store in executor closure + restructure `review()` to wire `signal`→cancel emit (§4 Change C, §11 Finding 1) |
| `src/critical_agent/policy.ts` | Add `isActionableDraftPlan()` helper (§4 Change B); auto-promote in APPROVE branch |
| `src/commands/promote.ts` | Add `force` option (§4 Change D) |
| `src/hooks/index.ts` | Change A: fire at auto-draft (~984) + refine/update (~616-664). Change C: replace coalesce at 616-664 with true-teardown. Change D: force-promote in CONFIRMATION handler (452-503) |
| `tests/plan_review.test.ts` | Tests 1–5 (§6) |

### Wiring checklist (implementation order)

1. `src/critical_agent/pi_adapter.ts` — add `cancelIdentityMap` + store in executor closure + restructure `review()` to wire `signal`→cancel emit (§11 Finding 1).
2. `src/critical_agent/policy.ts` — add `isActionableDraftPlan()` + APPROVE auto-promote.
3. `src/commands/promote.ts` — add `force` option.
4. `src/hooks/index.ts` — Change A at auto-draft; Change C true-teardown + Change A at refine site; Change D force-promote.
5. `tests/plan_review.test.ts` — Tests 1–5.
6. `npm test` → `npm run zip`.

## 8. Prior session context (already merged, do NOT re-do)

The previous task(s) already implemented and tested (present in working tree):

1. **Bug 1 — research-only conflation**: `src/tools/update_populators.ts` split
   `filesTouched`/`filesModified`; `src/validation/consistency/audit.ts:70`
   propagates `isResearchOnly`; `src/validation/consistency/checks.ts:199` gates
   the test-status check on `isResearchOnly`.
2. **Bug 2 — retry storm**: `src/critical_agent/policy.ts` records
   `lastDraftReviewRequestKey` / `lastPlanReviewRequestKey` only when the review
   produced a verdict or error (see `shouldRecordXKey`).
3. **Bug 3 — reviewer delegation**: read-only reviewer agent files at
   `~/.config/opencode/agents/reviewer.md` and `site/.opencode/agents/reviewer.md`;
   fallback in `src/critical_agent/pi_adapter.ts:419-452` retries with
   `agent:"explore"` on mutation-capable/implementation-task errors;
   `CRITICAL_REVIEW_FALLBACK` logging added to `src/logging/types.ts:138`.
4. **Bug 4 — duplicate sections**: alias double-writes removed in
   `src/tools/update_populators.ts`; `SECTION_ALIASES` circular files
   touched↔modified noted in `src/constants.ts:136` (not modified).
5. **quest_mark_saved hard-block gate** (THIS task series, already merged):
   - `src/tool_gating.ts` — new `blockQuestMarkSavedMissingFile(...)`
     hard-blocks `quest_mark_saved` when `questPath(state.questId)` does not
     exist on disk. Wired into BOTH the AWAITING_REVIEW branch (~:360) and
     the journal-permission branch (~:459).
   - `src/tools/update/executor.ts` `executeMarkTool` error text points to
     `quest_update_state` first.
   - `tests/verification.test.ts` gained gate-level assertions.
   - Verified: 83 passed (358 steps) / 0 failed; bundle repackaged.
   - The plan file `BLOCK.md` at repo root documents this gate work.

**Working tree state** (as of last check): several files staged/modified from
those prior sessions. No commits were made for them (not requested). Untracked
run artifacts: `deno.lock`, `.pi/extensions/pi-quest/.pi/`,
`.pi/skills/quest-journal/`, `CHANGELOG.md`, `TEST-PROMPT.md`.

### Useful commands

```bash
cd .pi/extensions/pi-quest && deno test --allow-all --node-modules-dir=none tests/verification.test.ts  # single file
npm --prefix .pi/extensions/pi-quest test   # full suite
npm --prefix .pi/extensions/pi-quest run zip # package
```

## 9. Out of scope / follow-up

- The subagent runtime crash (`pi-subagents` extension-load error) is external
  to pi-quest and should be tracked/filed separately. The changes above also
  make the flow more resilient: a reviewer crash won't deadlock `go` (Change D)
  and re-fires happen on draft updates (Change A).

## 10. The planner-repo convention

All work routes through the Quest Journal: `quest.md` files under
`.pi/quest/current/<qid>/`, auto-maintained. Before compacting, the assistant
must ensure THIS.md captures everything. Completed quests archive to
`.pi/quest/archive/<qid>.zip`; after testing/edits run the bundle zip.

## 11. Research findings — issues not fully accounted for in the plan

### Finding 1 — Change C executor restructuring (critical)

The plan says to wire `signal → cancel event` inside `PiSubagentReviewer.review()`.
But `review()` calls `rawRes = await executor(prompt, {...})` at line 407, which
blocks until the executor resolves. The cancel identity (requestId/nodeId/ownerRunId)
is generated synchronously inside the executor's Promise constructor, but we can't
access it from outside the closure.

**Fix:** Restructure `PiSubagentReviewer.review()` to NOT await the executor
immediately:

```ts
const execPromise = executor(prompt, {...});  // synchronous start
// Wire cancel listener using Map[input.reviewId]
const cancelId = cancelIdentityMap.get(input.reviewId!);
if (cancelId && input.signal) {
  input.signal.addEventListener("abort", () => {
    try { pi.events?.emit("prompt-template:subagent:cancel", cancelId); } catch {}
  }, {once: true});
}
rawRes = await execPromise;  // now await
```

The executor closure (pi_adapter.ts:175-339) must also be modified to store the
identity in the Map after computing it:

```ts
// Inside the Promise constructor, after line 187:
if (options?.reviewId) {
  cancelIdentityMap.set(options.reviewId, {requestId, nodeId, ownerRunId: getQuestId(ctx) || getSessionId(ctx)});
}
```

And cleanup on settle:

```ts
// In the resolve/reject paths, delete from map:
if (options?.reviewId) cancelIdentityMap.delete(options.reviewId);
```

### Finding 2 — `ReviewResult` type change is unnecessary

The plan suggests adding `requestId?/nodeId?/ownerRunId?` to `ReviewResult`.
This is architecturally wrong — these are subagent delegation metadata, not review
results. The module-level `Map<reviewId, CancelIdentity>` is sufficient. Do NOT
modify `ReviewResult` in `src/types.ts`.

### Finding 3 — `registerActiveReview` invariant (tracker.ts:223-227)

```ts
const existing = findActiveReviewForQuest(questSlug);
if (existing) {
  throw new Error(`Invariant violated: active review ${existing.reviewId} already running...`);
}
```

`cancelActiveReview` deletes from `activeReviews` synchronously (line 347).
So after `cancelActiveReview` returns, `registerActiveReview` for the fresh review
will succeed. The teardown sequence is order-correct: cancel → clear → relaunch.

### Finding 4 — Old singleKey format for cleanup

The old singleKey is `${questId}:plan_review:draft:${slug}:${oldHash}`. The
old hash is available as the last segment of the current `boundaryKey`
(`draft:${slug}:${oldHash}` — split on `:` and take the last element).
When deleting from `reviewPromiseByKey` in the teardown belt-and-suspenders,
use: `reviewPromiseByKey.delete(\`${questId}:plan_review:draft:${slug}:${oldHash}\`)`.
The fresh review uses a NEW hash, so it auto-avoids single-flight collision —
the delete is belt-and-suspenders.

### Finding 5 — `state.awaitingReview` not in belt-and-suspenders

The plan's teardown clears `.review.active`, `reviewPromiseByKey`, and pending
review — but not `state.awaitingReview`. This is handled asynchronously by
`background.ts:338-340` in the old review's `finally()`. The stale value doesn't
block `checkAndTriggerPlanReview` (it doesn't check `awaitingReview`). Adding
`targetState.awaitingReview = null` to the belt-and-suspenders would be cleaner
but is not required for correctness.

### Finding 6 — Logging at new callsites

The new `checkAndTriggerPlanReview` calls (Change A: auto-draft creation at
hooks/index.ts:984, refine/update at hooks/index.ts:616-664) should include
`tryLog` / `logEvent` calls for observability. The force-promote path (Change D)
should also log a distinct event (e.g., `FORCE_PROMOTED`).

### Finding 7 — `checkAndTriggerPlanReview` doesn't pass `force` to `runCriticalReview`

Looking at policy.ts:944-949, `checkAndTriggerPlanReview` never passes `force`.
For Change C, this is fine because the belt-and-suspenders clears all guards.
But if the cleanup is incomplete (e.g., `removeReviewActiveFile` fails), the fresh
review would be blocked by `isReviewActive`. Consider passing `force: true` in
the teardown path as a safety net.

### Finding 8 — The rejected verdict clears `lastPlanReviewApproval`

`rejected.ts:78`: `if (isPlanReviewKind) targetState.lastPlanReviewApproval = null;`
This means a REVISE verdict invalidates the approval. Combined with
`isDraftReviewValid` recomputing the live hash, a REVISE after APPROVE won't
auto-promote. This is correct behavior.

### Finding 9 — `isActionableDraftPlan` regex differences

The plan's `isActionableDraftPlan` uses `readFileSync` (sync) while the existing
`hasActionablePlanDraft` uses `readFutureDraft` (async). Both use the same regex
pattern. The sync version is acceptable in policy.ts (already uses `readFileSync`).
No issue.

### Finding 10 — Test helpers use `setCustomSubagentRunner`, not the bridge path

Tests use `setCustomSubagentRunner(runner)` which bypasses the bridge executor.
The cancel identity Map and `signal → cancel emit` wiring won't fire in tests
(because the custom runner doesn't generate requestId/nodeId). Tests for Change C
cancel plumbing must stub `pi.events.emit` and assert the event directly, OR
mock the cancel identity Map population.

## 12. Phase 2 — Close the draft → review → revise loop (run 1788519707)

Context: Phase 1 (Changes A–D) implemented. Live run of TEST-PROMPT.md
(`.pi/quest/current/1788519707/execution.log`, 466 lines, session
`01a06c14-…`) exposed loop failures *around* the new plumbing. The review
agent fired and returned, but the main agent never produced a plan and never
acted on the verdict. User decisions locked in: post-REVISE compulsion =
**hard throttle**; placeholder hatch = **keep** (no `#53` churn).

### 12.1 Run evidence (compact timeline)

- `11:01:47` draft auto-created from TEST-PROMPT (log lines 1–3).
- `11:01:47–11:03:43` main agent does genuine research (turns 2–18,
  evidence 5→27). Draft file stays scaffold placeholder.
- `11:03:43` (turn 18) agent tries `quest_update_state` → blocked
  `draft_not_approved` (lines 206–209).
- `11:04:11` (turn 19) user refinement appended (dpLen=2); two
  `checkAndTriggerPlanReview` calls: first launches `rev_mtmujhsc_u53i`
  (lines 226–228), second dies on `GLOBAL_REVIEW_CAP_HIT single_flight`
  (line 235, dropped reviewId `rev_mtmujhsi_lhun`).
- `11:04:11–11:06:29` review runs 138 s (`durationMs=138137`); main agent
  keeps researching (turns 0–10), never writes a plan.
- `11:06:29` verdict REVISE (placeholder, no file refs/phases) + reviewer
  flags our own prompt bug (§13 "3 concurrent reviews"); steers
  `plan_review_failed` + `plan_review_required` delivered (lines 437–449).
- `11:06:29–11:06:58` main agent: zero tool calls → `TURN_END` → two
  `TURN_RETRY` stalls. Draft still placeholder. End of log.

What worked: auto-draft, refinement append, review launch, single-flight
dedup, subagent run + return, verdict parse/log/remediation steer,
obligation retry, `.review.active` cleanup. The failure is the
agent-action loop around the plumbing.

### 12.2 Deep findings (final)

**D1 — The draft FILE is the sole plan carrier (proven).**
`promoteDraft` copies `future/<slug>.md` verbatim to `quest.md`
(`src/commands/promote.ts:57,82`). Nothing ever writes
`quest_update_state` plan params into the draft file (all `FUTURE_DIR`
writers: scaffold/append/promote/archive). Five model-visible texts point
at the blocked/incapable tool:
`src/hooks/index.ts:740-743`, `src/tools/update/executor.ts:631-632`
(+`638-642` variant), `src/critical_agent/reconcile/rejected.ts:93-103`
(*"update the quest state…"* — at verdict moment),
`src/tool_gating.ts:527-533` (escape hint), plus
`src/critical_agent/policy.ts:1020-1028` (*"re-submit"* — no re-submit
mechanism exists).

**D2 — Placeholder detector broken both ways; supersedes Finding 9.**
Scaffold header is `## Implementation Plan`
(`src/markdown/template/header.ts:56`); detector regex `/##\s*Plan/i`
(`src/hooks/index.ts:692`, `src/critical_agent/policy.ts:130`) never
matches it → a real plan under the scaffold header never counts as
actionable (no auto-review/promote, eternal `PLAN_NOT_DRAFTED_YET`).
Conversely scaffold body lines pass the bullet test (and
`tests/done_issue_coverage.test.ts:1077-1083` enshrines that). Two copies
of the logic exist (hooks inline + `policy.ts:126`).

**D3 — Compulsion must be gate-based; interrupt is impossible.**
`ExtensionAPI` has no interrupt (`src/types.ts:4-36`); verdicts are
`steer`-only + between-turn obligations. `awaitingReview` clears in
`background.ts:336-340` immediately post-verdict, and
`src/tool_gating.ts:469-487` explicitly lets research/read/journal loop
forever after REVISE. No code path compels a plan edit (source-verified).

**D4 — Only two loop links are missing.** Dedup (`policy.ts:950-963`)
and staleness (`snapshot.ts:164-205`) are live-hash-based, so a file edit
naturally re-arms review. Missing: (a) anything *firing*
`checkAndTriggerPlanReview` after a draft-file edit; (b) anything
*forcing* the edit. Note: edit/write tools carry permission `"journal"`
(`tool_gating.ts:391-392`) — allowed post-verdict, but **blocked while
`AWAITING_REVIEW`**, so the plan file is unwritable during the review
itself.

**D5 — two snapshot sources; `provisionalSnapshot` omits `boundaryKey`.**
DISTINGUISH THE TWO SNAPSHOT SOURCES — they are NOT the same object:
- `A. provisionalSnapshot` (`policy.ts:666-688`) is built at launch with
  **no `boundaryKey`**; it is passed to `registerActiveReview`
  (`policy.ts:713`) and becomes `active.snapshot` in the tracker.
- `B. createReviewSnapshot` (`background.ts:122-135`) ALWAYS overwrites
  `provisionalSnapshot` before the review runs, and for a draft computes
  `boundaryKey = draft:slug:hash` (`snapshot.ts:62-122`, branch at
  `activeState.activeDraft === slugOrQid`). This is the snapshot used for
  reconcile/approval.

CONSEQUENCES (two real gaps, NOT three — the third is disproven and
retracted):
(a) **Dead draft-staleness branch in the ACTIVE registration.** `active
.snapshot` (source A) has no `boundaryKey`, so the `draft:` branch of
`isReviewSnapshotCurrent` (`snapshot.ts:164-205`) is never exercised on
the registered record; file edits after a REVISE would not be detected as
boundary drift on that record. F3-0 fixes this by giving source A the key.
(b) **Change-C oldHash teardown is a no-op.** `hooks/index.ts:668-672`
reads `active.snapshot?.boundaryKey` (source A, no key) → `oldHash=""` →
`reviewPromiseByKey.delete(...)` is skipped. F3-0 fixes this (D5(b) real).
(c) **RETRACTED.** The D5(c) claim that `isDraftReviewValid` is false
after a genuine draft APPROVE because `approved.ts:59-60` sees
`snapshot.boundaryKey` undefined is WRONG: the reconcile/approval path
uses source B (`createReviewSnapshot`), which sets `draft:slug:hash`, so
`approved.ts:65-66` writes `lastPlanReviewBoundaryKey` and
`approval.boundaryKey` is non-null → `isDraftReviewValid`
(`policy.ts:108-114`) returns true when the file is unchanged. The only
path where `approval.boundaryKey` is null is source B's fallback when
`createReviewSnapshot` itself throws (`snapshot.ts:79-111`), which is not
the normal draft-APPROVE case. `promote.ts:42` is therefore NOT "dead for
drafts"; draft auto-promote already works on APPROVE (verified by
`tests/plan_review.test.ts` test 14 via the `## Plan` header — see T-D5c).

F3-0 scope/output is unchanged (see below); it is justified by D5(a) and
D5(b) ONLY. It does NOT fix any `isDraftReviewValid` bug, because none
exists on the reconcile path. Blast radius verified safe:
draft → `draft:slug:hash` (intended); pending-drain plan_review → same
key space (`pending_coalesce.ts:69-79`); active `requestPlanReview` →
equals pre-set `lastPlanReviewBoundaryKey` (`executor.ts:440` before
`:457`, no change); root plan_review (`policy.ts:1146`), direction
(`:1382`), rebuttal (`:1414`), final (`:1175`, `archive/gates.ts:89`)
pass no key or non-plan kinds → out of scope, unchanged.

**D6 — Exact sites/tests pinned.** F4 site = single-flight branch
`src/critical_agent/policy.ts:279-312` (matches run: `reason:
single_flight`, fresh `reviewId`, `draft:` boundaryKey). No test asserts
any old steer string (grep clean). No test pins §13 text (`plan_review`
test 2, `critical_agent` test 4 check other prompt parts).
`DRAFT_REVIEW_REQUIRED` (`src/constants.ts:272`) is defined but unused —
reuse for F3(ii). `handleToolResult` has no direct test callers (only a
comment at `done_issue_coverage.test.ts:890`); `pi` is capturable at
`installToolResultListener` (`src/hooks/index.ts:116-123`, currently
drops it). No import cycles: `snapshot.ts` doesn't import `policy.ts`;
`tool_gating.ts` ← `snapshot.ts` is new but acyclic (verified import
lists); `handlers.ts` → policy via dynamic import (existing pattern).

**D7 — Hatch kept (user call).** With F0+F1+F3 the placeholder hatch
(`hooks/index.ts:747-766`) degrades to a bounded once-per-hash guidance
review; test `#53` untouched. Removal is a future optimization, not a
correctness fix.

### 12.3 Changes (final specs)

**F0 — Fix detector; single source of truth** (`policy.ts:120-138`,
`hooks/index.ts:686-707`). New pure `isActionablePlanContent(content)`:
header `/^##\s+(?:Implementation\s+|Execution\s+)?Plan\b/im` to next
`##`; strip scaffold lines (`/^\s*\d+\.\s+Investigate via read\/search/`,
`/plan confidence low/i`); then existing body tests. `isActionableDraftPlan`
delegates; hooks inline IIFE replaced by the import. Prerequisite for F2.

**F1 — Correct the five steer texts** (exact replacements):
- hooks`:740-743`: *"…author the plan by editing that file's
  `## Implementation Plan` section directly (goal, 2–3 stages, findings).
  `quest_update_state` cannot touch a draft before reviewer APPROVE; saving
  a substantive plan sends it for review automatically."*
- executor`:631-632`: *"Draft '<slug>' not yet reviewer-approved — author
  or revise the plan by editing `.pi/quest/future/<slug>.md`
  (`## Implementation Plan`) directly. `quest_update_state` cannot modify
  or realize a draft before APPROVE. …only promotion via 'go' after
  APPROVE realizes the draft."*; `:638-642` append *"— do not retry
  quest_update_state; edit the future draft file instead."*
- rejected`:93-103` plan branch final sentence → *"Revise
  `.pi/quest/future/<slug>.md` directly (the `## Implementation Plan`
  section)… do NOT use `quest_update_state` for draft plans before
  APPROVE. Saving a substantive plan triggers re-review automatically…"*;
  filename from `targetState.activeDraft ?? slug` (note: `snapshot.questId`
  is the numeric qid for drafts — proven by log line 435).
- tool_gating`:527-533`: draft-aware escape hint (file edit first;
  `quest_update_state` noted blocked; keep rebut/ask-human/archive).
- policy`:1020-1028`: *"update …future/<slug>.md and save; re-review
  triggers automatically."*

**F2 — Launch-time yield steer** (draft plan_review only; deliver in
`checkAndTriggerPlanReview` draft shard, immediately after the
`await runCriticalReview(...)` returns at `policy.ts:964-969`, gated on
`kind === "plan_review"`): *"⏸ Plan review <reviewId> launched for
'<questSlug>' — finish your current tool call, then end the turn and
await the verdict; do not start new research reads."* Dynamic
`sendInternalAgentMessage` import (pattern at `:988`). WIRING NOTE: the
`PLAN_REVIEW_STARTED` log at `policy.ts:783-797` is inside
`runCriticalReview`'s background executor and is a LOG site only — do
NOT emit the steer from there (that layer has no agent-message steer
slot); the steer belongs on the synchronous draft-shard return path
above, before the agent is given the next turn.

**F3-0 — Set `snapshot.boundaryKey` in `provisionalSnapshot` for
plan_review** (one line in `policy.ts:666-688`):
`...(options.kind === "plan_review" ? { boundaryKey: options.boundaryKey
?? null } : {})`. Fixes D5(a) and D5(b) — NOT D5(c) (retracted). This
gives the ACTIVE-registration record (source A) the same boundaryKey that
`createReviewSnapshot` (source B) already computes, so the registered
record and the teardown path are consistent. Side effect verified safe:
`approved.ts:65-67` will write `lastPlanReviewBoundaryKey = "draft:slug:
hash"` on draft APPROVEs (already the case via source B) — no reader
assumes non-draft form (`maybeTrigger` compares against active-form
`postPlanning` keys, never equal; `snapshot.ts:197` non-draft branch is
unreachable for `draft:`-prefixed keys; `pending_coalesce` handles drift
explicitly).

DEPENDENCY NOTE (corrected): F3-0 is **NOT a hard prerequisite** for
F3(ii)'s auto-release. F3(ii)'s predicate reads `lastCriticalReview
.snapshot` (source B), which already carries `draft:slug:hash`, so
`isReviewSnapshotCurrent` already returns `current:false` on a file edit
and the throttle already self-releases there. F3-0 only makes the
registered/active record and Change-C teardown consistent (D5(a)/(b)).
Ordering F3-0 before F3(ii) remains recommended for cleanliness but is
not required for correctness of the REVISE-release path.

**F3(i) — Auto re-review on draft-file edit** (`handlers.ts`
`handleToolResult`, after `recordObservedInvestigation` ~`:905`):
`edit|write|user_edit|user_write`, success, path endswith
`future/<activeDraft>.md` → `tryLog("DRAFT_PLAN_EDITED", …)` (new event
in `logging/types.ts`, Phase-1 pattern) + dynamic-import
`checkAndTriggerPlanReview` and fire (dedup-safe). Thread `pi` through:
`handleToolResult(event, ctx, pi?)` + pass at `index.ts:116-123`
(backward-compatible; no direct test callers). Bash-redirect writes:
noted optional extension, out of scope v1.

**F3(ii) — Hard throttle while REVISE outstanding** (user call).
Predicate in `snapshot.ts` (acyclic home):
`isDraftRevisionOutstanding(s)` = `activeDraft` + `lastCriticalReview`
(`reconcile.ts:112`) with `verdict ∈ {REVISE,FAIL,UNCERTAIN}` +
`isReviewSnapshotCurrent(last.snapshot, s).current` +
**`!s.awaitingReview` guard** (throttle and `AWAITING_REVIEW` are mutually
exclusive phases: while a review runs, the awaiting-gate owns write-blocking
and research stays allowed per D4; without the guard the throttle would
wrongly silence research mid-review), all in try/catch → false. Gate in
`tool_gating.ts` (new branch after awaiting-block,
~`:467`): while true, block `research` permission and `read` outside
`.pi/quest/` (helper on `toolInput.path|file`, mirror `:870-872`);
allow draft-file edit/write, `.pi/quest` reads, interaction,
`quest_rebut`/`quest_ask_human`/`quest_archive`. Scope calibration (per user
hard-throttle call): this covers research + non-quest reads; if live runs
show the agent starved of a needed source read, narrow to
research-permission-only as fallback — do NOT widen to allow quest-external
reads by default, the run proves loops re-form there. Code: reuse unused
`DRAFT_REVIEW_REQUIRED`; `stateName: "DRAFT_REVISION_PENDING"`; message
names the file, truncated `requiredActions`, auto-release
("saving triggers re-review"). Auto-release derived: file edit (hash drift detected via
`lastCriticalReview.snapshot` = source B, which already carries
`draft:slug:hash` — see F3-0 DEPENDENCY NOTE), APPROVE, promote. Narrow
by construction: no verdict → research untouched.

**F4 — Audible coalesce-drop** (`policy.ts:279-312` single-flight
branch): steer *"⏸ Review <runningId> already running for '<slug>' —
request coalesced; end the turn and await the verdict."* (`runningId`
via already-imported `findActiveReviewForQuest`); module-level
`lastCoalesceSteerAt` map, 60 s window. Dynamic message import.
Testability: export a `__resetCoalesceSteerForTests()` resetter (module
state would otherwise leak the 60 s window across test cases).

**F5 — Honest gate text** (`research.ts:118-127,172-181`, reword only):
*"research evidence recorded (n); research cannot self-complete while
draft '<slug>' is active — author the plan in
`.pi/quest/future/<slug>.md` (`## Implementation Plan`).
`quest_update_state` unlocks after reviewer APPROVE."*

**F6 — Fix reviewer prompt §13** (`build.ts:144-175`): replace
falsehoods ("implementation is wrong", "3 concurrent",
"`maxConcurrency = 3`") with the true invariant (single-flight per
quest via `canLaunchReview`, global cap 1, changes coalesce never
parallelize, stale results superseded, evaluate the snapshotted plan).
Keep the normative serialization list. NOTE on citation: the run's own
log line 435 cites "build.ts:114-145" for this block — that pointer is
imprecise; the standards list ends and the §13 heading begins at
`build.ts:144`, so the appendix range `build.ts:144-175` is the correct
one.

### 12.4 Tests (TDD — failing first, per repo rules)

- T-F0 (`plan_review.test.ts`): scaffold (`## Implementation Plan` +
  two scaffold lines) → false; `## Plan/1.` → false; real plan under
  either header → true. Update `done_issue_coverage #55:1077` (scaffold
  dash-bullets now false + justification comment); other #55 vectors
  unchanged.
- T-F1: message-text assertions (draft block, REVISE-draft, escape hint).
- T-F2: mock `pi.sendMessage` called on launch, silent on dedup-hit.
- T-F3-0 (`plan_review.test.ts`): after a draft plan_review launches,
  assert the REGISTERED record (`getActiveReviews().get(reviewId).snapshot
  .boundaryKey`) is `draft:<slug>:<hash>` (source A now carries the key,
  fixing D5a/b) and that Change-C teardown's `reviewPromiseByKey.delete`
  emits a non-empty `oldHash`. This tests what F3-0 actually changes
  (the registration/teardown path), distinct from T-D5c below.
- T-D5c (new regression, `plan_review.test.ts`): draft with substantive
  plan APPROVEs via the real driver face → assert
  `isDraftReviewValid(state) === true` (file unchanged) and
  `=== false` after an edit; also assert `lastPlanReviewBoundaryKey ===
  "draft:<slug>:<hash>"` and `approval.boundaryKey` non-null. This pins
  the CORRECT pre-existing behavior on the reconcile path (source B) and
  RED-FLAGS if a future change moves reconcile off source B (the only way
  the retracted D5(c) could become true).
- T-F3(i): simulated tool_result write to `future/<draft>.md` →
  `DRAFT_PLAN_EDITED` + review requested; non-draft writes silent.
- T-F3(ii): REVISE + unchanged file → research blocked
  (`DRAFT_REVIEW_REQUIRED`); after file change → allowed; escapes
  allowed; no-verdict research unaffected.
- T-F4: single-flight drop emits one steer (60 s dedupe).
- T-F6: prompt has invariant, lacks `"3 concurrent"` /
  `"maxConcurrency = 3"`.
- Regression: full `npm --prefix .pi/extensions/pi-quest test` (86
  suites, incl. 11–15); `#53` untouched by design.

### 12.5 Order, verification, risks, deferred

Order: F0 → F3-0 → F1+F5 (texts) → F6 → F2 → F3(i) → F3(ii) → F4 (F1's
auto-review claim depends on F3(i); F3-0 first for cleanliness — F3(ii)'s
release works via source B regardless, see F3-0 DEPENDENCY NOTE). Expected
closed loop after all changes (use as end-to-end acceptance walkthrough):
refinement → hatch or actionable review launches (F0 decides) → F2 yield
steer ends the turn → verdict REVISE → F1-correct remediation steer →
F3(ii) throttle engages (research blocked, file edit open) → agent edits
`future/<slug>.md` → F3(i) fires re-review (`DRAFT_PLAN_EDITED`, throttle
auto-releases via source-B hash drift) → APPROVE → Change-B auto-promote
(`isDraftReviewValid` true via source-B `createReviewSnapshot`) →
research phase. Verify:
DAG lint for new `tool_gating→snapshot` edge, full suite, `run zip`.
Risks: F3(ii) may block reads genuinely needed for revision — mitigated
by `.pi/quest` allowance, escapes, narrow predicate, derived release;
F0 misclassification delaying first review — mitigated by T-F0 vectors.
Housekeeping: mark §7 checklist items and §12 items done as implemented;
keep the execution log of the verification run for the bundle.
Deferred: hatch removal (revisit post-proof), bash-redirect edit
detection, Option-B tool write-through (unneeded — promote reads file).
