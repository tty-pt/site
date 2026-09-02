import { QuestErrorCode } from "./constants.ts";
export { QuestErrorCode };

export interface ExtensionAPI {
	on(event: string, handler: (event: any, ctx: any) => Promise<any> | any): void;
	appendEntry<T = any>(type: string, data: T): void;
	registerEntryRenderer<T = any>(type: string, renderer: (entry: any, o: any, theme: any) => any): void;
	registerTool(tool: any): void;
	registerCommand(name: string, command: any): void;
	sendUserMessage(msg: any, options?: any): void;
	sendMessage?(message: any, options?: any): void;
	getAllTools?(): Array<{
		name: string;
		description?: string;
		parameters?: unknown;
		sourceInfo?: unknown;
	}>;
	getActiveTools?(): string[];
	executeTool?(name: string, params: any, signal?: AbortSignal, onUpdate?: any, ctx?: any): Promise<any>;
	events?: {
		on(event: string, handler: (data: unknown) => void): (() => void) | void;
		emit(event: string, data: unknown): void;
	};
}

export interface ExtensionContext {
	cwd: string;
	sessionManager: any;
	ui: any;
	hasUI?: boolean;
	mode?: string;
	isIdle?: () => boolean;
	getContextUsage?: () => { tokens?: number | null; contextWindow?: number | null; percent?: number | null } | undefined;
	compact?: (options: any) => void;
}

export type EventCallback = (event: any, ctx: ExtensionContext) => Promise<any> | any;

export interface ToolCallGateResult {
	block?: boolean;
	reason?: string;
}

export interface QuestChoiceResult {
	name: string;
	goal?: string;
}

export enum CompactionPressure {
	NONE = "NONE",
	WARNING = "WARNING",
	CRITICAL = "CRITICAL",
}

export enum UserMessageClassification {
	CONVERSATIONAL_ACK = "CONVERSATIONAL_ACK",
	CONFIRMATION = "CONFIRMATION",
	QUESTION_OR_DISCUSSION = "QUESTION_OR_DISCUSSION",
	REFINEMENT_OR_REQUIREMENT = "REFINEMENT_OR_REQUIREMENT",
}

export enum QuestLifecycleState {
	IDLE = "IDLE",
	RESEARCH_PENDING = "RESEARCH_PENDING",
	REASSESSMENT_PENDING = "REASSESSMENT_PENDING",
	ACTIVE_CLEAN = "ACTIVE_CLEAN",
	ACTIVE_DIRTY = "ACTIVE_DIRTY",
	PRE_COMPACT_DUMP_PENDING = "PRE_COMPACT_DUMP_PENDING",
	COMPACTING = "COMPACTING",
}

export interface SaveGeneration {
	count: number;
	path: string;
	hash: string;
	savedAt: number;
}

export type CompactionTransactionPhase =
	| "prepared"
	| "invalidated_by_new_save"
	| "in-flight"
	| "completed"
	| "resume-pending"
	| "resume-delivered"
	| "failed"
	| "inconsistent";

export interface CompactionTransaction {
	id: string;
	phase: CompactionTransactionPhase;
	activeQuest: string;
	questPath?: string;
	reason: ResumeReason;
	checkpointSaveCount?: number;
	checkpointHash?: string;
	observedSaveCount?: number;
	observedHash?: string;
	observedQuestPath?: string;
	stack: string[];
	researchRound: number;
	reassessmentVersion: number;
	planVersion: number;
	createdAt: number;
	completedAt?: number;
	failedAt?: number;
	error?: string;
}

export interface PendingResume {
	compactionId: string;
	activeQuest: string;
	reason: ResumeReason;
	checkpointSaveCount: number;
	checkpointHash: string;
	checkpointQuestPath: string;
	attempts: number;
	createdAt: number;
	lastAttemptAt?: number;
	deliveredAt?: number;
}

export interface PendingSubquestResumeResolution {
	child: string;
	resolution: "obsolete-after-archive" | "adopted" | "inconsistent" | string;
	resolvedAt: number;
	parent: string | null;
	details?: string;
}

export type SubquestReconciliationStatus = "still-valid" | "obsolete" | "adopted" | "inconsistent";

export type CompactionState =
	| { kind: "idle" }
	| { kind: "prepared"; id: string; quest: string; saveCount: number; hash: string }
	| { kind: "in-flight"; id: string; quest: string; reason: ResumeReason }
	| { kind: "completed"; id: string; quest: string }
	| { kind: "resume_pending"; id: string; quest: string; reason: ResumeReason }
	| { kind: "failed"; id: string; quest: string; reason: string }
	| { kind: "inconsistent"; id: string; quest: string; reason: string };

export type EpistemicPhaseKind =
	| "idle"
	| "provisional_root"
	| "research_pending"
	| "confirmation_pending"
	| "reassessment_pending"
	| "implementation_allowed";

export interface EpistemicPhase {
	kind: EpistemicPhaseKind;
	round?: number;
	planVersion?: number;
	reassessmentVersion?: number;
	reason?: string;
	evidence?: string;
}

export type ObligationKind =
	| "error"
	| "steer"
	| "resume"
	| "checkpoint_required"
	| "reassessment"
	| "confirmation"
	| "critical_review"
	| "custom";

export type ObligationStatus =
	| "pending"
	| "delivering"
	| "fulfilled"
	| "superseded"
	| "cancelled"
	| "failed";

export interface AgentObligation {
	id: string;
	questId?: string;
	kind?: ObligationKind;
	code?: QuestErrorCode | string;
	message: string;
	status?: ObligationStatus;
	deliverAs?: "steer" | "followUp" | "nextTurn";
	requiredNextAction?: string;
	details?: Record<string, unknown> | string;
	stateGeneration?: number;
	planVersion?: number;
	reassessmentVersion?: number;
	correlationId?: string;
	dedupKey?: string;
	createdAt: number;
	attempts: number;
	lastAttemptAt?: number;
	deliveredAt?: number;
	fulfilledAt?: number;
	fulfilledReason?: string;
	supersededAt?: number;
	supersededReason?: string;
	cancelledAt?: number;
	cancelledReason?: string;
	failedAt?: number;
	failedReason?: string;
	superseded?: boolean;
	isCurrent?: (currentState: StoredState) => boolean;
	isFulfilled?: (currentState: StoredState) => boolean;
}

export interface PendingAgentNotification extends AgentObligation {}

export type CriticalReviewKind = "direction" | "plan_review" | "final_acceptance";
export type CriticalReviewVerdict = "APPROVE" | "REVISE" | "UNCERTAIN" | "PASS" | "FAIL";
export type CriticalReviewSeverity = "NONE" | "MINOR" | "MAJOR" | "CRITICAL";

export type ReviewTimeoutLayer =
	| "quest_journal_deadline"
	| "subagent_bridge_deadline"
	| "child_process_deadline"
	| "provider_model_timeout";

export interface ReviewActivityStats {
	turns: number;
	tools: number;
	reads: number;
	searches: number;
	writes: number;
	commands: number;
	files: number;
	lastActivityAt: number;
	lastTool?: string;
	observedFilePaths?: string[];
}

export interface ReviewSnapshot {
	questId: string;
	sessionId: string;
	reviewId: string;
	reviewKind: CriticalReviewKind;
	planVersion: number;
	boundaryKey?: string | null;
	saveGeneration: number;
	stateHash: string | null;
	originalUserRequest: string;
	currentUnderstanding: string;
	assumptions: string;
	plan: string;
	planRevisions: string;
	findings: string;
	filesChanged: string;
	relevantDiff: string;
	testStatus: string;
	nextAction: string;
	createdAt: number;
	refinements?: string[];
	executionSnapshot?: string;
	remainingWork?: string;
	status?: string;
}

export interface ActiveReview {
	reviewId: string;
	childSessionId?: string;
	parentSessionId: string;
	questId: string;
	questSlug: string;
	kind: CriticalReviewKind;
	triggerReason?: string;
	snapshot: ReviewSnapshot;
	startedAt: number;
	activity: ReviewActivityStats;
	status: "starting" | "running" | "completed" | "failed" | "timed_out" | "superseded" | "cancelled";
	verdict?: CriticalReviewVerdict;
	error?: string;
	timeoutLayer?: ReviewTimeoutLayer;
	promise?: Promise<any>;
	// Explicit cancellation tracking
	cancelled?: boolean;
	cancellationRequested?: boolean;
	cancellationReason?: string;
	cancelledAt?: number;
	abortController?: AbortController;
	asyncGeneration?: number;
}

export interface PendingReviewRequest {
	questSlug: string;
	kind: CriticalReviewKind;
	triggerReason?: string;
	planVersion: number;
	stateHash?: string | null;
	boundaryKey?: string | null;
	saveCount?: number;
	requestedAt: number;
	rebuttal?: string;
	model?: string;
	timeoutMs?: number;
	force?: boolean;
	asyncGeneration?: number;
	superseded?: boolean;
	cancelledAt?: number;
}

export interface CriticalReviewFinding {
	issue: string;
	evidence: string;
}

export interface CriticalReviewSelfCritique {
	initialJudgment: CriticalReviewVerdict;
	critique: string[];
	revisedJudgment: CriticalReviewVerdict;
}

export interface CriticalReviewPromptComplianceItem {
	requirement: string;
	planHandling?: string;
	status: "SATISFIED" | "UNSATISFIED" | "UNCERTAIN" | "YES" | "NO";
}

export interface CriticalReviewOriginalRequestCheck {
	satisfied: string[];
	unsatisfied: string[];
	items?: CriticalReviewPromptComplianceItem[];
}

export interface QuestReviewContext {
	originalRequest: string;
	refinements: string[];
	currentUnderstanding: string;
	keyAssumptions: string;
	openQuestions: string;
	plan: string;
	planConfidence: string;
	planRevisions: string;
	findings: string;
	filesModified: string;
	testStatus: string;
	executionSnapshot: string;
	exactNextAction: string;
	remainingWork: string;
	status: string;
}

export interface ReviewInput {
	kind: CriticalReviewKind;
	questSlug: string;
	triggerReason?: string;
	boundaryKey?: string | null;
	context: QuestReviewContext;
	snapshot?: ReviewSnapshot;
	rebuttal?: string;
	model?: string;
	reviewId?: string;
	childSessionId?: string;
	parentSessionId?: string;
	onActivity?: (activity: ReviewActivityStats) => void;
	timeoutMs?: number;
	signal?: AbortSignal;
	asyncGeneration?: number;
}

export interface ReviewResult {
	verdict: CriticalReviewVerdict;
	severity: CriticalReviewSeverity;
	findings: CriticalReviewFinding[];
	requiredActions: string[];
	originalRequestCheck: CriticalReviewOriginalRequestCheck;
	selfCritique?: CriticalReviewSelfCritique;
	parseError?: string;
	rawText?: string;
	childSessionId?: string;
	childTranscriptRef?: string;
	activity?: ReviewActivityStats;
	durationMs?: number;
	timeoutLayer?: ReviewTimeoutLayer;
}

export interface CriticalReviewer {
	isAvailable(): boolean;
	review(input: ReviewInput): Promise<ReviewResult>;
}

export interface PlanReviewApproval {
	questId: string;
	planVersion: number;
	reviewId: string;
	boundaryKey?: string | null;
	saveHash?: string | null;
	saveCount?: number;
	timestamp: number;
}

export interface CriticalReviewState {
	id: string;
	questId: string;
	kind: CriticalReviewKind;
	reviewId?: string;
	childSessionId?: string;
	parentSessionId?: string;
	reviewedStateVersion: {
		planVersion: number;
		saveHash?: string | null;
		saveCount?: number;
	};
	snapshot?: ReviewSnapshot;
	verdict: CriticalReviewVerdict;
	severity: CriticalReviewSeverity;
	findings: CriticalReviewFinding[];
	requiredActions: string[];
	originalRequestCheck?: CriticalReviewOriginalRequestCheck;
	selfCritique?: CriticalReviewSelfCritique;
	resolved: boolean;
	superseded?: boolean;
	supersededBy?: {
		planVersion: number;
		saveHash?: string | null;
		saveCount?: number;
		reason?: string;
	};
	durationMs?: number;
	activity?: ReviewActivityStats;
	childTranscriptRef?: string;
	timestamp: number;
	correlationId?: string;
	error?: string;
	timeoutLayer?: ReviewTimeoutLayer;
}

export interface StoredState {
	questId?: string | null;
	rootQuest?: string | null;
	active: string | null;
	saveCount: number;
	compactCount: number;
	prompts: string[];
	refinements?: string[];
	stack: string[];
	dirty?: boolean;
	compactionPending?: boolean;
	archiveCompactionPending?: string | null;
	subquestLaunchCompactionPending?: boolean;
	pendingSubquestResume?: string | null;
	pendingSubquestResumeResolution?: PendingSubquestResumeResolution | null;
	preCompactionCheckpointPending?: boolean;
	preCompactionSaveRequestPending?: boolean;
	saveGeneration?: SaveGeneration | null;
	lastSavedHash?: string | null;
	economyTokens?: number | null;
	economyPercent?: number | null;
	warningMarginTokens?: number | null;
	warningPercent?: number | null;
	warningTokens?: number | null;
	subquestCompactTokens?: number | null;
	/** @deprecated compaction pressure removed — retained for migration compat */
	lastWarnedCompactionTokens?: number | null;
	lastPromptAt?: number;
	lastResumePromptAt?: number;
	lastResumeTarget?: string | null;
	lastResumeCompactCount?: number;
	pickerCancelled?: boolean;
	// Periodic heartbeat checkpoint tracking (decoupled from direction review)
	lastPeriodicCheckpointAt?: number;
	lastPeriodicCheckpointTurn?: number;
	lastPeriodicSteerTurn?: number;
	lastPeriodicSteerAt?: number;

	// Compaction & resume transaction tracking
	activeTransaction?: CompactionTransaction | null;
	activeCompactionId?: string | null;
	lastDeliveredCompactionId?: string | null;
	pendingResume?: PendingResume | null;
	pendingNotifications?: PendingAgentNotification[];
	obligationHistory?: AgentObligation[];

	// Iterative research & reassessment epistemic state
	pendingRootQuest?: boolean;
	pendingRootRequest?: string | null;
	questIdentityEstablished?: boolean;
	researchRound?: number;
	researchComplete?: boolean;
	researchRequired?: boolean;
	reassessmentRequired?: boolean;
	reassessmentReason?: string | null;
	reassessmentEvidence?: string | null;
	reassessmentVersion?: number;
	resolvedReassessmentVersion?: number;
	lastPlanRevisionsText?: string | null;
	confirmedQuests?: string[];
	lastReassessmentPromptAt?: number;
	lastReassessmentReason?: string | null;
	lastCheckpointPromptAt?: number;
	planVersion?: number;
	planConfidence?: "low" | "medium" | "high";
	lastResearchAt?: number;
	lastPlanRevisionAt?: number;
	lastPromptedReassessmentVersion?: number;
	implementationAllowed?: boolean;
	awaitingUserConfirmation?: boolean;
	consecutiveFailures?: number;
	substantiveTurnsSinceCheckpoint?: number;
	currentTurn?: number;
	currentTurnCorrelationId?: string;
	// Auto-draft accumulation state (future/ while user still talking)
	activeDraft?: string | null;
	draftPrompts?: string[];
	draftCreatedAt?: number | null;
	draftLastSavedHash?: string | null;
	draftLastReviewKey?: string | null;
	semanticSummaryEnabled?: boolean;
	thoughtLoggingEnabled?: boolean;
	autonomousSubquestDuringDrafting?: boolean;
	initialPromptLogged?: boolean;

	// Critical Review Subagent State
	inCriticalReview?: boolean;
	lastCriticalReview?: CriticalReviewState | null;
	criticalReviews?: CriticalReviewState[];
	criticalReviewAttempts?: Record<string, number>;
	lastReviewedSaveHash?: string | null;
	lastReviewedPlanVersion?: number | null;
	lastReviewedSaveCount?: number | null;
	lastPlanReviewApproval?: PlanReviewApproval | null;
	lastPlanReviewRequestedVersion?: number | null;
	lastPlanReviewBoundaryKey?: string | null;
	lastPlanReviewRequestKey?: string | null;
	lastDraftReviewRequestKey?: string | null;
	lastDirectionReviewKey?: string | null;
	lastDirectionReviewAt?: number | null;

	// Async generation epoch
	asyncGeneration?: number;

	// Turn-stop gate: scalar awaiting review (plan_review / final_acceptance only, survives compaction)
	awaitingReview?: { kind: CriticalReviewKind; reviewId: string; triggerReason?: string; since: number } | null;

	sessionModifiedFiles?: string[];
	/** @deprecated compaction pressure removed */
	lastNotifiedPressure?: CompactionPressure;
	lastContinuationTransitionKey?: string | null;

	// Observed investigation receipts & epochs
	investigationEpoch?: number;
	logCursor?: number;
	currentReceipt?: InvestigationReceipt | null;
	lastCompletedReceipt?: InvestigationReceipt | null;
}

export type InvestigationKind =
	| "none"
	| "file-read"
	| "code-search"
	| "architecture-research"
	| "external-research";

export interface InvestigationReceipt {
	epoch: number;
	epochType: "research" | "reassessment" | "historical";
	startedAt: number;
	toolCalls: number;
	readTargets: string[];
	searchTargets: string[];
	commands: string[];
	evidenceCount: number;
	lastEvidenceAt?: number;
	completedAt?: number;
	isHistorical?: boolean;
}

export interface LoadedQuestState {
	questId?: string | null;
	originalRequest: string;
	refinements: string[];
	exists: boolean;
	researchRound: number;
	researchComplete: boolean;
	researchRequired: boolean;
	planVersion: number;
	planConfidence: "low" | "medium" | "high";
	lastResearchAt?: number;
	lastPlanRevisionAt?: number;
	awaitingUserConfirmation?: boolean;
	reassessmentRequired: boolean;
	reassessmentReason: string | null;
	reassessmentEvidence: string | null;
	reassessmentVersion: number;
	resolvedReassessmentVersion: number;
	lastPlanRevisionsText: string | null;
}

export interface MarkdownSection {
	heading: string;
	normalized: string;
	level: number;
	body: string;
	raw: string;
}

export interface MarkdownBlock {
	type: "preamble" | "section";
	heading?: string;
	title?: string;
	normalizedTitle?: string;
	body: string;
	raw: string;
}

export interface ConsistencyAuditResult {
	consistent: boolean;
	issues: string[];
	warnings: string[];
}

export type ResumeReason =
	| "normal-compaction"
	| "archive-compaction"
	| "subquest-launch"
	| "compaction-failure-fallback";

export type ToolPermission =
	| "read"
	| "research"
	| "journal"
	| "implementation"
	| "interaction"
	| "unknown";

export interface AgentErrorOptions {
	code: QuestErrorCode | string;
	correlationId?: string;
	deliverAs?: "steer" | "followUp" | "nextTurn";
	requiredNextAction?: string;
	details?: Record<string, unknown> | string;
}

export interface ScheduledTask {
	taskId: string;
	taskType: string;
	questSlug: string;
	sessionId: string;
	transactionId?: string | null;
	asyncGeneration: number;
	timer: any;
	scheduledAt: number;
	delayMs: number;
	fn: () => void | Promise<void>;
	cancelled: boolean;
	cancellationReason?: string;
	cancelledAt?: number;
}

