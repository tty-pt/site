export interface ActiveRunHierarchy {
  questId: string | null;
  initialPrompt?: string | null;
  activeRootQuest: string | null;
  activeRootQuestPath: string | null;
  activeSubQuest: string | null;
  activeSubQuestPath: string | null;
  capturedSubQuests: Array<{ name: string; path: string }>;
  logPath: string;
  logExists: boolean;
  logSize?: number;
  startTime?: string;
  endTime?: string;
  questHash?: string | null;
  draftCaptured?: boolean;
  futureCount?: number;
  compactionResumeHash?: string | null;
  semanticSummaryEnabled?: boolean;
  thoughtLoggingEnabled?: boolean;
  filteredCount?: number;
  opencodeSessionId?: string | null;
  startMs?: number | null;
  elapsedMaxMs?: number | null;
  resolutionMethod: string;
  confidence: "high" | "medium" | "low" | "ambiguous";
  ambiguityDetails?: string;
  // Backward compatibility properties
  activeQuest: string | null;
  activeQuestPath: string | null;
  subquests: Array<{ name: string; path: string }>;
  discoveredReason: string;
}

export interface DiagnosticExpectedState {
  questId?: string | null;
  initialPrompt?: string | null;
  activeRootQuest?: string | null;
  activeQuest?: string | null;
  activeSubQuest?: string | null;
  capturedSubQuests?: string[];
  subquests?: string[];
  logExists: boolean;
  draftCaptured?: boolean;
  futureCount?: number;
  compactionResumeHash?: string | null;
}

export interface VerificationResult {
  valid: boolean;
  errors: string[];
  entries: string[];
}

export interface DiagnosticZipOptions {
  projectRoot?: string;
  extensionDir?: string;
  outputZipPath?: string;
  questId?: string;
  timestamp?: string;
  skipVerification?: boolean;
}

export interface DiagnosticZipResult {
  zipPath: string;
  hierarchy: ActiveRunHierarchy;
  manifest: string;
  verification: VerificationResult;
  sha256: string;
  bundleHash?: string;
}

export interface RunArchiveResult {
  zipPath: string;
  questId: string;
  hierarchy: ActiveRunHierarchy;
  summary: string;
  manifest: string;
  sha256: string;
  runDir: string;
  verification: VerificationResult;
}

export interface RunDirectoryResult {
  questId: string;
  runDir: string;
  summaryPath: string;
  manifestPath: string;
  initialPromptPath: string;
  logPath: string;
  questDir: string;
  questFiles: Array<{ name: string; path: string }>;
}

export interface ParsedRunLog {
  questId: string;
  path: string;
  size: number;
  mtime: number;
  startTime?: string;
  endTime?: string;
  rootQuest?: string;
  activeQuest?: string;
  subquests: string[];
  eventCount: number;
  hasRootCompletion?: boolean;
}

export interface ExtractedJournalState {
  goal?: string;
  status?: string;
  remainingTasks?: string[];
  plan?: string[];
  planVersion?: number;
  planConfidence?: string;
  filesTouched?: string[];
  rawContent: string;
}

export interface DiagnosticReportResult {
  projectRoot: string;
  hierarchy: ActiveRunHierarchy;
  logStats: {
    exists: boolean;
    size: number;
    eventsCount: number;
    startTime?: string;
    endTime?: string;
  };
  journalState?: ExtractedJournalState;
  manifest: string;
  summary: string;
}
