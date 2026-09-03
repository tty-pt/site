import { CausalChainEvent } from "../types.ts";

export interface SummaryState {
  quests: Set<string>;
  majorPhases: Set<string>;
  researchCycles: number;
  reassessmentCycles: number;
  implementationAttempts: number;
  implementationAllowedCount: number;
  implementationBlockedCount: number;
  blockedGates: Set<string>;
  failureCount: number;
  failures: Array<
    {
      type: string;
      code?: string;
      reason?: string;
      failureId?: string;
      recovered?: boolean;
      recoveryAction?: string;
    }
  >;
  unrecoveredFailures: Array<
    { type: string; code?: string; reason?: string; failureId?: string }
  >;
  compactionsMap: Map<
    string,
    {
      id: string;
      status: string;
      phases: Set<string>;
      success: boolean;
      failed: boolean;
      inconsistent: boolean;
    }
  >;
  anonCompactionCounter: number;
  resumesMap: Map<
    string,
    {
      id: string;
      success: boolean;
      failed: boolean;
      retried: number;
      obsolete: boolean;
    }
  >;
  anonResumeCounter: number;
  hasUnresolvedError: boolean;
  hasCriticalReviewFailure: boolean;
  criticalReviewPassed: boolean;
  lastError?: string;
  deadlockWarnings: string[];
  modifiedFilesSet: Set<string>;
  completedTasksSet: Set<string>;
  causalChain: CausalChainEvent[];
  totalTestsRun: number;
  testsPassedCount: number;
  testsFailedCount: number;
  lastTestCommand?: string;
  lastPassedCommand?: string;
  lastFailedCommand?: string;
  lastFailureReason?: string;
  lastTestStatus: "PASSED" | "FAILED" | "NOT_RUN";
  failureIdMap: Map<string, number>;
  draftCaptured: boolean;
  futureCount: number;
  compactionResumeHash?: string | null;
  filteredCount: number;
  coalesceCount: number;
  attemptIncrementCount: number;
  opencodeSessionId?: string | null;
  piSessionIds: Set<string>;
  dialogueCount: number;
  thoughtCount: number;
  startMs?: number | null;
  elapsedMaxMs?: number | null;
  semanticSummaryEnabled: boolean;
  thoughtLoggingEnabled: boolean;
}

export function createInitialState(): SummaryState {
  return {
    quests: new Set(),
    majorPhases: new Set(),
    researchCycles: 0,
    reassessmentCycles: 0,
    implementationAttempts: 0,
    implementationAllowedCount: 0,
    implementationBlockedCount: 0,
    blockedGates: new Set(),
    failureCount: 0,
    failures: [],
    unrecoveredFailures: [],
    compactionsMap: new Map(),
    anonCompactionCounter: 0,
    resumesMap: new Map(),
    anonResumeCounter: 0,
    hasUnresolvedError: false,
    hasCriticalReviewFailure: false,
    criticalReviewPassed: false,
    lastError: undefined,
    deadlockWarnings: [],
    modifiedFilesSet: new Set(),
    completedTasksSet: new Set(),
    causalChain: [],
    totalTestsRun: 0,
    testsPassedCount: 0,
    testsFailedCount: 0,
    lastTestCommand: undefined,
    lastPassedCommand: undefined,
    lastFailedCommand: undefined,
    lastFailureReason: undefined,
    lastTestStatus: "NOT_RUN" as const,
    failureIdMap: new Map(),
    draftCaptured: false,
    futureCount: 0,
    compactionResumeHash: null,
    filteredCount: 0,
    coalesceCount: 0,
    attemptIncrementCount: 0,
    opencodeSessionId: null,
    piSessionIds: new Set(),
    dialogueCount: 0,
    thoughtCount: 0,
    startMs: null,
    elapsedMaxMs: null,
    semanticSummaryEnabled: false,
    thoughtLoggingEnabled: false,
  };
}
