export type MajorPhaseName =
	| "INITIALIZATION"
	| "RESEARCH"
	| "PLANNING"
	| "CONFIRMATION"
	| "IMPLEMENTATION"
	| "VERIFICATION"
	| "REASSESSMENT"
	| "CHECKPOINT"
	| "COMPACTION"
	| "RESUME"
	| "RECOVERY"
	| "COMPLETION";

export type QuestLogEventType =
	// 0. Structured concrete tool activity
	| "TOOL_ACTIVITY"
	// 1. Quest initialization decisions
	| "QUEST_DETECTED"
	| "QUEST_REUSED"
	| "QUEST_CREATED"
	| "QUEST_START"
	| "QUEST_SWITCH"
	| "QUEST_INITIALIZATION_FAILED"
	| "QUEST_ACTIVATION_FAILED"
	// 2. Agent-turn boundaries
	| "TURN_START"
	| "TURN_END"
	// 3. Gate state transitions
	| "GATE_BLOCKED"
	| "GATE_OPENED"
	| "GATE_STATE_CHANGED"
	// 4. Research evidence lifecycle
	| "RESEARCH_REQUIRED"
	| "RESEARCH_EVIDENCE"
	| "RESEARCH_REJECTED"
	| "RESEARCH_COMPLETED"
	// 5. Reassessment lifecycle
	| "REASSESSMENT_REQUIRED"
	| "REASSESSMENT_EVIDENCE"
	| "REASSESSMENT_REJECTED"
	| "REASSESSMENT_COMPLETED"
	| "REASSESSMENT_RESOLUTION_FAILED"
	// 6. Implementation attempt & outcome
	| "IMPLEMENTATION_ATTEMPT"
	| "IMPLEMENTATION_ALLOWED"
	| "IMPLEMENTATION_BLOCKED"
	| "IMPLEMENTATION_COMPLETED"
	| "IMPLEMENTATION_FAILED"
	// 7. Tool classification anomalies
	| "UNKNOWN_TOOL"
	| "UNEXPECTED_TOOL_RESULT"
	| "TOOL_CLASSIFICATION_MISMATCH"
	// 8. Tool-result failures
	| "TOOL_FAILURE"
	| "TOOL_TIMEOUT"
	| "TOOL_CANCELLED"
	// 9. Test/build lifecycle
	| "TEST_STARTED"
	| "TEST_PASSED"
	| "TEST_FAILED"
	| "BUILD_STARTED"
	| "BUILD_PASSED"
	| "BUILD_FAILED"
	| "TEST_FAILURE"
	// 10. State-update lifecycle
	| "STATE_UPDATE_REJECTED"
	| "STATE_UPDATE_ACCEPTED"
	| "STATE_UPDATE_FAILED"
	| "STATE_RECONCILIATION_REQUIRED"
	// 11. Persistence lifecycle
	| "SAVE_STARTED"
	| "SAVE_VERIFIED"
	| "SAVE_REJECTED"
	| "SAVE_FAILED"
	| "PERSISTENCE_DEGRADED"
	| "PERSISTENCE_RECOVERED"
	// 12. Compaction transaction lifecycle
	| "CHECKPOINT"
	| "COMPACTION_PREPARED"
	| "COMPACTION_INVALIDATED"
	| "COMPACTION_STARTED"
	| "COMPACTION_COMPLETED"
	| "COMPACTION_FAILED"
	| "COMPACTION_INCONSISTENT"
	| "COMPACTION_EXTERNAL"
	| "COMPACTION_BLOCKED"
	// 13. Resume obligation lifecycle
	| "RESUME_OBLIGATION_CREATED"
	| "RESUME_ATTEMPTED"
	| "RESUME_DELIVERED"
	| "RESUME_FAILED"
	| "RESUME_RETRIED"
	| "RESUME_RECONCILIATION_REQUIRED"
	| "RESUME_OBSOLETED"
	// 14. Agent-message transport
	| "AGENT_MESSAGE_ATTEMPTED"
	| "AGENT_MESSAGE_DELIVERED"
	| "AGENT_MESSAGE_FAILED"
	| "AGENT_MESSAGE_QUEUED"
	| "AGENT_MESSAGE_RETRIED"
	| "AGENT_MESSAGE_SUPERSEDED"
	// 15. Continuation/deadlock detection
	| "NO_PROGRESS"
	| "REPEATED_BLOCK"
	| "REPEATED_FAILURE"
	| "TURN_RETRY"
	| "TURN_RETRY_ATTEMPTED"
	| "TURN_RETRY_EXHAUSTED"
	// 16. User interaction lifecycle
	| "CONFIRMATION_REQUESTED"
	| "CONFIRMATION_RECEIVED"
	| "CONFIRMATION_REJECTED"
	| "USER_REFINEMENT_RECEIVED"
	// 17. Subquest lifecycle
	| "SUBQUEST_START"
	| "SUBQUEST_SWITCH"
	| "SUBQUEST_RETURN"
	| "SUBQUEST_FAILED"
	| "SUBQUEST_RESUME_PENDING"
	| "SUBQUEST_RESUME_FAILED"
	| "SUBQUEST_COMPLETE"
	| "ARCHIVE"
	// 18. Critical Agent Review & Plan Review lifecycle
	| "CRITICAL_REVIEW_REQUESTED"
	| "CRITICAL_REVIEW_STARTED"
	| "SUBAGENT_STARTED"
	| "SUBAGENT_ACTIVITY"
	| "CRITICAL_REVIEW_PASSED"
	| "CRITICAL_REVIEW_FAILED"
	| "CRITICAL_REVIEW_UNCERTAIN"
	| "CRITICAL_REVIEW_UNAVAILABLE"
	| "CRITICAL_REVIEW_ERROR"
	| "CRITICAL_REVIEW_SUPERSEDED"
	| "CRITICAL_REVIEW_SUPPRESSED_DUPLICATE"
	| "CRITICAL_REVIEW_COALESCED"
	| "GLOBAL_REVIEW_CAP_HIT"
	| "DIRECTION_REVIEW_THROTTLED"
	| "PLAN_REVIEW_REQUESTED"
	| "PLAN_REVIEW_STARTED"
	| "PLAN_REVIEW_APPROVED"
	| "PLAN_REVIEW_FAILED"
	| "PLAN_REVIEW_UNCERTAIN"
	| "REMEDIATION_REQUIRED"
	| "SELF_CRITIQUE_STARTED"
	| "SELF_CRITIQUE_REVISED"
	// 18b. B2 Logging maturity
	| "DRAFT_APPENDED"
	| "DRAFT_APPEND_DEDUPED"
	| "DRAFT_CONVERSATIONAL_IGNORED"
	| "DRAFT_PROMOTED"
	| "DRAFT_DISCARDED"
	| "SYNTHETIC_FILTERED"
	| "CLASSIFICATION_RESULT"
	| "REQUIRE_CONFIRM_DECISION"
	| "ATTEMPT_INCREMENTED"
	| "PENDING_COALESCED_DROPPED"
	| "PENDING_COALESCED_RESOLVED"
	| "PLAN_REVIEW_SUPPRESSED_MATERIAL_CHANGE"
	| "CRITICAL_REVIEW_FORCED"
	| "FIRST_PLAN_REVIEW_ALREADY_FIRED"
	| "REVIEW_DEDUP_HIT"
	| "MUTEX_WAIT"
	| "MUTEX_ACQUIRED"
	| "CRITICAL_REVIEW_ORPHAN_CLEARED"
	| "SNAPSHOT_FALLBACK"
	| "RESUME_DIRECTIVE_SENT"
	// 18c. B3 dedup+user+semantic
	| "INITIAL_PROMPT"
	| "USER_PROMPT"
	| "SEMANTIC_SNAPSHOT"
	| "STEP_SUMMARY"
	| "DIALOGUE"
	| "AGENT_THOUGHT"
	// 19. Unknown/inconsistent states
	| "STATE_INCONSISTENT"
	| "RECOVERY_STARTED"
	| "RECOVERY_COMPLETED"
	| "RECOVERY_FAILED"
	| "ERROR";

export interface QuestLogContext {
	questId?: string;
	root?: string;
	rootQuest?: string;
	sessionId?: string;
	quest?: string;
	turn?: number | string;
	turnId?: string;
	correlationId?: string;
	tool?: string;
	operation?: "success" | "failure" | "blocked" | string;
	outcome?: string;
	phase?: string;
	path?: string;
	command?: string;
	query?: string;
	target?: string;
	action?: string;
	reason?: string;
	triggerReason?: string;
	boundaryKey?: string;
	code?: string;
	readsCount?: number;
	searchesCount?: number;
	writesCount?: number;
	commandsCount?: number;
	logPath?: string;
	compactionId?: string;
	obligationId?: string;
	reviewId?: string;
	round?: number;
	version?: number;
	planVersion?: number;
	reassessmentVersion?: number;
	gate?: string;
	from?: string;
	to?: string;
	kind?: string;
	category?: string;
	categories?: string;
	requiredAction?: string;
	reads?: number;
	searches?: number;
	evidence?: number;
	allowed?: boolean;
	attempt?: number;
	checkpoint?: string;
	type?: string;
	subquest?: string;
	parent?: string;
	childSessionId?: string;
	parentSessionId?: string;
	timeoutLayer?: string;
	durationMs?: number;
	reviewedVersion?: number;
	reviewedSaveCount?: number;
	reviewedSaveHash?: string;
	supersededByVersion?: number;
	supersededBySaveCount?: number;
	supersededBySaveHash?: string;
	status?: string;
	gen?: number;
	hash?: string;
	deliverAs?: string;
	substantive?: boolean;
	toolsUsed?: number;
	mutations?: number;
	failures?: number;
	questDirty?: boolean;
	implementationAllowed?: boolean;
	turns?: number;
	count?: number;
	permission?: string;
	investigation?: string;
	error?: string;
	severity?: string;
	verdict?: string;
	// B2/B3 Observability context
	draftPromptsCount?: number;
	attemptKey?: string;
	attempts?: number;
	requireConfirm?: boolean;
	syntheticPrefix?: string;
	classification?: string;
	thoughtHash?: string;
	thoughtLen?: number;
	thoughtSlice?: string;
	ref?: string;
	dialogueRole?: string;
	dialogueHash?: string;
	dialogueSlice?: string;
	dialogueLen?: number;
	transcriptRef?: string;
	piSessionId?: string;
	// Causal Chain & Observability properties
	intent?: string;
	intentHash?: string;
	intentLen?: number;
	slice?: string;
	elapsedMs?: number;
	opencodeSessionId?: string;
	semanticSummaryEnabled?: boolean;
	thoughtLoggingEnabled?: boolean;
	lockKey?: string;
	waitMs?: number;
	holdMs?: number;
	contention?: boolean;
	shard?: string;
	staleCount?: number;
	candidateCount?: number;
	chosenKind?: string;
	targetAction?: string;
	result?: string;
	consequence?: string;
	failureId?: string;
	recoveryFor?: string;
	recoveryConclusion?: string;
	filesModified?: string;
	testStatus?: string;
	completedTasks?: number | string;
	remainingTasks?: number | string;
	activeGate?: string;
	[key: string]: any;
}

export interface QuestLogEntry {
	timestamp: string;
	type: QuestLogEventType;
	quest: string;
	context: Record<string, string>;
	message: string;
	raw: string;
}

export interface CausalChainEvent {
	turn?: number | string;
	correlationId?: string;
	phase?: string;
	intent?: string;
	action?: string;
	result?: string;
	consequence?: string;
	recoveryFor?: string;
	failureId?: string;
	timestamp?: string;
}

export interface QuestRunSummary {
	quests: string[];
	majorPhases: string[];
	researchCycles: number;
	reassessmentCycles: number;
	implementationAttempts: number;
	implementationAllowedCount: number;
	implementationBlockedCount: number;
	blockedGates: string[];
	failureCount: number;
	failures: Array<{ type: string; code?: string; reason?: string; failureId?: string; recovered?: boolean; recoveryAction?: string }>;
	unrecoveredFailures: Array<{ type: string; code?: string; reason?: string; failureId?: string }>;
	compactionCount: number;
	successfulCompactions: number;
	failedCompactions: number;
	inconsistentCompactions: number;
	compactions: Array<{ id?: string; status: string; phase?: string }>;
	resumeCount: number;
	resumeSuccessCount: number;
	resumeFailedCount: number;
	resumePendingCount: number;
	hasUnresolvedError: boolean;
	hasCriticalReviewFailure?: boolean;
	criticalReviewPassed?: boolean;
	lastError?: string;
	deadlockWarnings: string[];
	testVerification: {
		status: "PASSED" | "FAILED" | "NOT_RUN";
		lastTestCommand?: string;
		lastPassedCommand?: string;
		lastFailedCommand?: string;
		lastFailureReason?: string;
		totalTestsRun: number;
		testsPassed: number;
		testsFailed: number;
	};
	implementationSummary: {
		totalAttempts: number;
		allowed: number;
		blocked: number;
		blockedGates: string[];
		completedTasks: string[];
		modifiedFiles: string[];
	};
	causalChain: CausalChainEvent[];
	terminalVerdictReason: string;
	formattedSummary: string;
}
