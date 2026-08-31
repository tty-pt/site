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

export interface PendingAgentNotification {
	id: string;
	code: string;
	correlationId?: string;
	message: string;
	deliverAs?: "steer" | "followUp" | "nextTurn";
	requiredNextAction?: string;
	details?: Record<string, any> | string;
	attempts: number;
	createdAt: number;
	lastAttemptAt?: number;
}

export type CriticalReviewKind = "direction" | "final_acceptance";
export type CriticalReviewVerdict = "PASS" | "FAIL" | "UNCERTAIN";
export type CriticalReviewSeverity = "NONE" | "MINOR" | "MAJOR" | "CRITICAL";

export interface CriticalReviewFinding {
	issue: string;
	evidence: string;
}

export interface CriticalReviewSelfCritique {
	initialJudgment: CriticalReviewVerdict;
	critique: string[];
	revisedJudgment: CriticalReviewVerdict;
}

export interface CriticalReviewOriginalRequestCheck {
	satisfied: string[];
	unsatisfied: string[];
}

export interface CriticalReviewState {
	id: string;
	questId: string;
	kind: CriticalReviewKind;
	reviewedStateVersion: {
		planVersion: number;
		saveHash?: string | null;
		saveCount?: number;
	};
	verdict: CriticalReviewVerdict;
	severity: CriticalReviewSeverity;
	findings: CriticalReviewFinding[];
	requiredActions: string[];
	originalRequestCheck?: CriticalReviewOriginalRequestCheck;
	selfCritique?: CriticalReviewSelfCritique;
	resolved: boolean;
	timestamp: number;
	correlationId?: string;
	error?: string;
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
	lastWarnedCompactionTokens?: number | null;
	lastPromptAt?: number;
	lastResumePromptAt?: number;
	lastResumeTarget?: string | null;
	lastResumeCompactCount?: number;
	pickerCancelled?: boolean;

	// Compaction & resume transaction tracking
	activeTransaction?: CompactionTransaction | null;
	activeCompactionId?: string | null;
	lastDeliveredCompactionId?: string | null;
	pendingResume?: PendingResume | null;
	pendingNotifications?: PendingAgentNotification[];

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
	// Critical Review Subagent State
	inCriticalReview?: boolean;
	lastCriticalReview?: CriticalReviewState | null;
	criticalReviews?: CriticalReviewState[];
	criticalReviewAttempts?: Record<string, number>;
	lastReviewedSaveHash?: string | null;
	lastReviewedPlanVersion?: number | null;
	lastReviewedSaveCount?: number | null;

	sessionModifiedFiles?: string[];
	lastNotifiedPressure?: CompactionPressure;
	lastContinuationTransitionKey?: string | null;

	// Observed investigation receipts & epochs
	investigationEpoch?: number;
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
	details?: Record<string, any> | string;
}

