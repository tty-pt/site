export const ANSI_SGR = /\u001B\[[0-9;]*m/g;

export const QUEST_ROOT = ".pi/quest";

export const QUEST_CURRENT_DIR = ".pi/quest/current";

export const QUEST_ARCHIVE_DIR = ".pi/quest/archive";

export const FUTURE_DIR = ".pi/quest/future";

export const NOTES_FILE = ".pi/context.md";

export const CUSTOM_TYPE = "quest_journal";

export const LEGACY_CUSTOM_TYPE = "task_journal";

export const PROMPT_MAX_CHARS = 4000;

export const PROMPT_MAX_COUNT = 10;

export const DEFAULT_CEILING_TOKENS = 400_000;

export const DEFAULT_PERCENT = 50;

export const DEFAULT_WARNING_PERCENT = 40;

export const MAX_CONTEXT_PCT = 90;

export const DEFAULT_WARNING_DELTA_PCT = 10;

export const DEFAULT_ADAPTIVE_TIERS: Array<{ upTo: number; pct: number }> = [
	{ upTo: 250_000, pct: 75 },
	{ upTo: 500_000, pct: 60 },
	{ upTo: 1_000_000, pct: 40 },
];

export const DEFAULT_SUBQUEST_LAUNCH_MIN_TOKENS = 60_000;

export const DEFAULT_PRE_COMPACT_WARNING_TOKENS = 30_000;

export const MAX_QUEST_NAME_DISPLAY_LENGTH = 24;

export const SECTION_ALIASES: Record<string, string[]> = {
	"goal": ["goal", "goals", "goals & scope"],
	"original request": ["original request", "original user request", "request"],
	"parent quest": ["parent quest", "parent", "parentquest"],
	"current status": ["current status", "status"],
	"current understanding": ["current understanding", "understanding", "architectural understanding"],
	"key assumptions": ["key assumptions", "assumptions", "working assumptions"],
	"open questions & uncertainties": ["open questions & uncertainties", "open questions", "uncertainties", "known problems / uncertainties"],
	"research findings": ["research findings", "in-depth analysis & findings", "important findings", "findings", "analysis & findings", "research / findings", "research & findings", "important discoveries", "discoveries"],
	"plan": ["plan", "detailed multi-stage execution plan", "execution plan", "provisional plan", "plan (provisional)"],
	"plan confidence": ["plan confidence", "confidence"],
	"plan version": ["plan version", "version"],
	"plan revisions": ["plan revisions", "plan revision history", "revisions", "plan history"],
	"last research at": ["last research at", "last research timestamp", "research at", "last research"],
	"last plan revision at": ["last plan revision at", "last plan revision timestamp", "plan revision at", "last plan revision"],
	"research round": ["research round", "research cycle"],
	"reassessment status": ["reassessment status", "reassessment state"],
	"reassessment version": ["reassessment version"],
	"resolved reassessment version": ["resolved reassessment version"],
	"reassessment evidence": ["reassessment evidence"],
	"rejected approaches": ["rejected approaches", "discarded approaches", "failed approaches", "disproved hypotheses"],
	"latest reassessment": ["latest reassessment", "reassessment conclusion", "reassessment", "reassessment findings"],
	"execution snapshot": ["execution snapshot", "execution state", "snapshot", "state"],
	"execution state": ["execution state"],
	"completed": ["completed", "completed tasks", "completed work", "done", "accomplished", "finished"],
	"in progress": ["in progress", "active work", "current work", "working on"],
	"build & run commands": ["build & run commands", "build & run", "commands"],
	"decisions made": ["decisions made", "decisions"],
	"constraints & rules": ["constraints & rules", "constraints", "rules"],
	"files examined": ["files examined", "examined files"],
	"files touched": ["files touched", "files modified", "touched files", "modified files", "files"],
	"files modified": ["files modified", "files touched", "modified files", "touched files", "files"],
	"test / build status": ["test / build status", "tdd & quality checklist", "test status", "build & test status", "build status", "test / build", "test & build status"],
	"acceptance criteria & polish checklist": ["acceptance criteria & polish checklist", "acceptance criteria", "polish checklist"],
	"sub-quests": ["sub-quests", "subquests", "sub quests"],
	"quest refinements & user feedback loops": ["quest refinements & user feedback loops", "refinements", "user refinements", "feedback loops"],
	"remaining work": ["remaining work", "remaining tasks", "remaining", "checklist"],
	"exact next action": ["exact next action", "next recommended step", "next action", "next step", "next steps"],
	"resume prompt": ["resume prompt", "resume context", "resume briefing"],
};

export const SYNTHETIC_PROMPT_PREFIXES = [
	"/quest",
	"/subquest",
	"/sub-quest",
	"/quest-save",
	"/quest-refine",
	"/quest-del",
	"/quest-draft",
	"/quest-economy",
	"/quest-warning",
	"/quest-subquest-threshold",
	"/quest-status",
	"/quests",
	"quest-journal:",
	"quest-journal economy:",
	"quest-journal in-flight steer:",
	"quest-journal in-flight budget steer:",
	"⚡",
	"⚖",
	"[quest journal]",
	"[questjournal]",
	"<background-task-notification>",
	"background task completed",
	"reviewer:",
	"pass 1",
	"pass 2",
	"**post-compaction",
	"⚡ **post-compaction",
	"now working on quest",
	"now working on sub-quest",
	"finish current work, then update that file",
	"before context compaction resets",
	"original user request(s) for this quest",
	"original user request --",
	"sub-quest '",
	"quest '",
	"economy auto-compaction",
	"turn has performed",
	"turn 1 protocol",
	"mandatory upfront research",
	"mandatory tdd & quality",
	"context is approaching the configured compaction threshold",
	"context compaction is now being requested",
	"context compaction is imminent",
	"🚨",
	"🚨 **critical",
	"critical quest journal compaction",
	"context compaction warning",
	"context usage has reached or exceeded",
];

export const INTERNAL_MESSAGE_PREFIX = "[QuestJournal internal]";

export const DIRECTION_REVIEW_COOLDOWN_MS = 45_000;
export const SUBSTANTIVE_TURNS_PER_DIRECTION_REVIEW = 5;
export const GLOBAL_REVIEW_CAP = 1;
export const STALENESS_ON_DIRTY = false;

export const REVIEW_LOCK_STALE_MS_DEFAULT = 30_000;
export const ENV_REVIEW_LOCK_STALE_MS = "PI_QUEST_REVIEW_LOCK_STALE_MS";

export const SEMANTIC_SUMMARY_ENABLED_DEFAULT = true;
export const THOUGHT_LOGGING_ENABLED_DEFAULT = true;
export const ENV_SEMANTIC_SUMMARY = "PI_QUEST_SEMANTIC_SUMMARY";
export const ENV_THOUGHT_LOGGING = "PI_QUEST_THOUGHT_LOGGING";

export const AUTONOMOUS_SUBQUEST_DURING_DRAFTING_DEFAULT = false;
export const ENV_AQM_SUBQUEST_DRAFT = "PI_QUEST_AQM_SUBQUEST_DRAFT";

export const DEFAULT_CHECKPOINT_INTERVAL_TURNS = 6;
export const DEFAULT_CHECKPOINT_INTERVAL_MS = 0; // timer disabled (turn-count deterministic)
export const PERIODIC_CHECKPOINT_BURST_MS = 50;

export const QuestErrorCode = {
	IMPLEMENTATION_BLOCKED: "IMPLEMENTATION_BLOCKED",
	UNKNOWN_TOOL_BLOCKED: "UNKNOWN_TOOL_BLOCKED",
	RESEARCH_REQUIRED: "RESEARCH_REQUIRED",
	RESEARCH_EVIDENCE_REQUIRED: "RESEARCH_EVIDENCE_REQUIRED",
	REASSESSMENT_REQUIRED: "REASSESSMENT_REQUIRED",
	REASSESSMENT_EVIDENCE_REQUIRED: "REASSESSMENT_EVIDENCE_REQUIRED",
	CONFIRMATION_REQUIRED: "CONFIRMATION_REQUIRED",
	DRAFT_REVIEW_REQUIRED: "DRAFT_REVIEW_REQUIRED",
	CHECKPOINT_REQUIRED: "CHECKPOINT_REQUIRED",
	PERSISTENCE_FAILURE: "PERSISTENCE_FAILURE",
	SAVE_VERIFICATION_FAILURE: "SAVE_VERIFICATION_FAILURE",
	COMPACTION_FAILURE: "COMPACTION_FAILURE",
	CONTINUATION_FAILURE: "CONTINUATION_FAILURE",
	RESUME_DELIVERY_FAILURE: "RESUME_DELIVERY_FAILURE",
	RESUME_STATE_INCONSISTENT: "RESUME_STATE_INCONSISTENT",
	PENDING_RESUME_INCONSISTENT: "PENDING_RESUME_INCONSISTENT",
	STATE_RECONSTRUCTION_FAILURE: "STATE_RECONSTRUCTION_FAILURE",
	SUBQUEST_FAILURE: "SUBQUEST_FAILURE",
	ARCHIVE_FAILURE: "ARCHIVE_FAILURE",
	CRITICAL_REVIEW_FAILED: "CRITICAL_REVIEW_FAILED",
	CRITICAL_REVIEW_UNCERTAIN: "CRITICAL_REVIEW_UNCERTAIN",
	CRITICAL_REVIEW_ERROR: "CRITICAL_REVIEW_ERROR",
	PLAN_REVIEW_REQUIRED: "PLAN_REVIEW_REQUIRED",
	PLAN_REVIEW_FAILED: "PLAN_REVIEW_FAILED",
	PLAN_REVIEW_UNCERTAIN: "PLAN_REVIEW_UNCERTAIN",
	PLAN_REVIEW_ERROR: "PLAN_REVIEW_ERROR",
} as const;

export type QuestErrorCode = typeof QuestErrorCode[keyof typeof QuestErrorCode];

