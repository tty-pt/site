# pi-quest Architecture Map & Ownership Boundaries

This map documents the single authoritative owner, transition coordinator, side effects, persistence mechanism, and logging path for each major subsystem in `pi-quest`.

---

## 1. Quest Lifecycle

* **State Owner**: `state.ts` (`sessionStates.get(sessionId)` / `StoredState`)
* **Transition Owner**: `lifecycle.ts` (`initProvisionalRootQuest`, `activateExistingQuest`, `archiveQuestFile`, `promptForQuestChoice`)
* **Side Effects**: Sets active quest ID, initializes stack, synchronizes implementation permissions via `gates.ts`, creates/updates quest Markdown files on disk, cleans drafts, updates live UI widget.
* **Persistence**: `persistence.ts` (`persist()`, `verifyAndMarkSaved()`) + `.pi/quest/current/<qid>/quest.md`
* **Logging**: `logging.ts` (`QUEST_DETECTED`, `QUEST_CREATED`, `QUEST_ACTIVATED`, `QUEST_SWITCHED`, `ARCHIVE`)

```text
User/Pi Prompt
  → lifecycle.ts (init / activate / switch)
    → state.ts (update session state)
    → gates.ts (syncImplementationPermission)
    → persistence.ts (persist state snapshot)
    → logging.ts (logQuestTransition)
```

---

## 2. Reassessment & Gating

* **State Owner**: `state.ts` (`reassessmentRequired`, `reassessmentReason`, `reassessmentEvidence`, `reassessmentVersion`, `resolvedReassessmentVersion`, `researchComplete`, `researchRequired`, `implementationAllowed`, `awaitingUserConfirmation`)
* **Transition Owner**:
  * Triggers: `research.ts` (`triggerReassessment`), `turn_analysis.ts` (`applyTurnEndStateTransitions`), `hooks/turn_analysis.ts` (failed test/build signals)
  * Resolutions: `tools/update_operation.ts` (`executeUpdateStateTool`), `classification.ts` (`acceptRootConfirmation`)
  * Authority: `gates.ts` (`canImplement`, `syncImplementationPermission`, `getImplementationBlockReason`)
* **Side Effects**: Blocks/unblocks mutating tools (edit, write, mutating bash commands, subagents) at the tool execution boundary (`tool_gating.ts`), queues steering instructions, updates UI status tag.
* **Persistence**: `persistence.ts` (`persist()`, `verifyAndMarkSaved()`) + `## Reassessment status` in `quest.md`
* **Logging**: `logging.ts` (`REASSESSMENT_REQUIRED`, `REASSESSMENT_RESOLVED`, `GATE_BLOCKED`, `IMPLEMENTATION_BLOCKED`, `RESEARCH_COMPLETED`)

```text
Turn Analysis / Test Failure / User Refinement
  → research.ts (triggerReassessment)
    → gates.ts (syncImplementationPermission: false)
    → tool_gating.ts (enforce block on mutating tools)
    → messaging.ts (send model-visible steer message)
    → persistence.ts (persist)
    → logging.ts (logReassessmentTransition)
```

---

## 3. Checkpoint & Persistence

* **State Owner**: `state.ts` (`saveCount`, `compactCount`, `dirty`, `saveGeneration`, `lastSavedHash`)
* **Transition Owner**: `persistence.ts` (`verifyAndMarkSaved`, `persist`), `compaction/transaction.ts` (`invalidatePreparedCompactionTransaction`)
* **Side Effects**: Computes SHA-256 fingerprint of `quest.md`, executes consistency audit (`auditQuestConsistency`), marks quest clean (`dirty = false`), invalidates stale prepared compaction transactions, updates UI status.
* **Persistence**: Authoritative snapshot via `persist()` (`pi.appendEntry<StoredState>`), disk state verified at `.pi/quest/current/<qid>/quest.md`
* **Logging**: `logging.ts` (`SAVE_STARTED`, `SAVE_VERIFIED`, `SAVE_FAILED`, `SAVE_REJECTED`, `PERSISTENCE_DEGRADED`)

```text
quest_mark_saved / verifyAndMarkSaved
  → paths.ts (computeFileFingerprint)
  → validation.ts (auditQuestConsistency)
  → compaction/transaction.ts (invalidatePreparedCompactionTransaction)
  → persistence.ts (persist snapshot to session manager)
  → logging.ts (logPersistenceTransition)
```

---

## 4. Compaction & Resume

* **State Owner**: `state.ts` (`compactionPending`, `activeTransaction`, `activeCompactionId`, `pendingResume`, `pendingSubquestResume`, `pendingSubquestResumeResolution`)
* **Transition Owner**: `compaction/` (`compaction/execution.ts`, `compaction/transaction.ts`, `compaction/pressure.ts`, `compaction/resume.ts`)
* **Side Effects**: Checks token pressure against economy thresholds, requests pre-compaction checkpoint if dirty, blocks unverified compactions, manages immutable transaction lifecycle (`prepared` → `in-flight` → `completed` → `resume-pending` → `resume-delivered` / `failed`), synthesizes and dispatches resume directives, reconciles interrupted sub-quests.
* **Persistence**: `persistence.ts` (`persist()`) across session compact boundaries
* **Logging**: `logging.ts` (`COMPACTION_PRESSURE_WARNING`, `COMPACTION_STARTED`, `COMPACTION_COMPLETED`, `COMPACTION_FAILED`, `RESUME_DELIVERED`, `RESUME_STATE_INCONSISTENT`)

```text
session_before_compact / pressure threshold
  → compaction/pressure.ts (detect pressure)
  → compaction/transaction.ts (createOrGetCompactionTransaction)
  → session_compact event
    → compaction/execution.ts (handleCompactionCompleted)
    → compaction/resume.ts (dispatchCompactionResume)
    → logging.ts (logCompactionTransition / logResumeTransition)
```

---

## 5. Critical Review

* **State Owner**: `state.ts` (`lastCriticalReview`, `criticalReviews`, `criticalReviewAttempts`, `lastPlanReviewApproval`, `inCriticalReview`)
* **Transition Owner**:
  * Policy & Evaluation: `critical_agent/policy.ts` (`runCriticalReview`, `isPlanReviewValidForState`, `isCriticalReviewValidForCompletion`)
  * Prompt & Parser: `critical_agent/prompt.ts` (`buildCriticalReviewPrompt`, `parseCriticalReviewResponse`)
  * Pi Transport Adapter: `critical_agent/pi_adapter.ts` (`PiSubagentReviewer`, `resolveSubagentExecutor`, `isSubagentToolRegistered`)
* **Boundary**: `CriticalReviewer` interface (`review(input: ReviewInput): Promise<ReviewResult>`)
* **Side Effects**: Dispatches independent read-only reviewer subagent, performs 2-pass self-critique, parses verdicts (`APPROVE`/`PASS`, `REVISE`/`FAIL`, `UNCERTAIN`), triggers reassessment or remediation work on failure, updates plan review approvals.
* **Persistence**: `persistence.ts` (`persist()`) + stored in `StoredState.criticalReviews` + required actions appended to `quest.md`
* **Logging**: `logging.ts` (`PLAN_REVIEW_REQUESTED`, `PLAN_REVIEW_STARTED`, `PLAN_REVIEW_APPROVED`, `PLAN_REVIEW_FAILED`, `CRITICAL_REVIEW_STARTED`, `CRITICAL_REVIEW_PASSED`, `CRITICAL_REVIEW_FAILED`, `SELF_CRITIQUE_STARTED`, `SELF_CRITIQUE_REVISED`)

```text
Policy Trigger (Plan Update / Phase Completion / Archive)
  → critical_agent/policy.ts (runCriticalReview)
    → critical_agent/prompt.ts (extractQuestReviewContext, buildCriticalReviewPrompt)
    → critical_agent/pi_adapter.ts: PiSubagentReviewer (execute read-only subagent)
    → critical_agent/prompt.ts (parseCriticalReviewResponse)
    → critical_agent/policy.ts (handle verdict: approve/remediation/reassessment)
    → persistence.ts (persist)
    → logging.ts (logCriticalReviewTransition)
```

---

## 6. Agent Obligations

* **State Owner**: `state.ts` (`pendingNotifications: AgentObligation[]`)
* **Transition Owner**: `obligations.ts` (`createAgentObligation`, `queueAgentObligation`, `isObligationCurrent`, `drainAgentObligations`)
* **Typed Evaluators**: `registerObligationEvaluator("research" | "reassessment" | "confirmation" | "checkpoint" | "error")`
* **Side Effects**: Queues undelivered model-facing notices on transport errors, supersedes stale obligations when quest or state generation advances, delivers active obligations on turn boundaries via `messaging.ts`.
* **Persistence**: `persistence.ts` (`persist()`)
* **Logging**: `logging.ts` (`AGENT_MESSAGE_QUEUED`, `AGENT_MESSAGE_DELIVERED`, `AGENT_MESSAGE_SUPERSEDED`, `AGENT_MESSAGE_RETRIED`)

```text
reportAgentError / Gate Block (delivery failure fallback)
  → obligations.ts (queueAgentObligation)
Turn Start / Turn End
  → obligations.ts (drainAgentObligations)
    → obligations.ts: isObligationCurrent (typed freshness validation)
    → messaging.ts: sendInternalAgentMessage (deliver message)
    → persistence.ts (persist updated queue)
    → logging.ts (logAgentMessageTransition)
```

---

## 7. Terminal Completion & Archiving

* **State Owner**: `diagnostic/status.ts` (`calculateAuthoritativeTerminalStatus`), `lifecycle.ts` (`archiveQuestFile`)
* **Transition Owner**: `lifecycle.ts` (`archiveQuestFile`, `onLifecycleStageTransition`)
* **Ordered Stages**:
  1. `terminal_commit`: Commit terminal state in `quest.md`, log `IMPLEMENTATION_COMPLETED`, verify save.
  2. `active_removal`: Cleanly remove `.pi/quest/current/<qid>`.
  3. `zip_creation`: Create diagnostic run archive (`createRunArchive` → `.pi/quest/archive/<qid>.zip`).
  4. `changelog_appended`: Append truthful completion entry in project `CHANGELOG.md`.
* **Side Effects**: Removes active directory, archives logs/manifests, appends changelog, clears active state.
* **Persistence**: Final diagnostic zip archive + project `CHANGELOG.md`
* **Logging**: `logging.ts` (`IMPLEMENTATION_COMPLETED`, `ARCHIVE`)

```text
quest_archive tool / /quest-del command
  → critical_agent/policy.ts (isCriticalReviewValidForCompletion)
  → lifecycle.ts: Stage 1 terminal_commit (write terminal quest.md, verify save)
  → lifecycle.ts: Stage 2 active_removal (remove current/<qid>)
  → diagnostic/packaging.ts: Stage 3 zip_creation (createRunArchive)
  → diagnostic/status.ts: Stage 4 changelog_appended (appendChangelogEntry)
  → logging.ts (logImplementationOutcome, logSubquestTransition)
```
