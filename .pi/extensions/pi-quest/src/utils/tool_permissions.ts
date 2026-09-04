import { basename } from "node:path";
import {
  FUTURE_DIR,
  NOTES_FILE,
  QUEST_ARCHIVE_DIR,
  QUEST_CURRENT_DIR,
  QUEST_ROOT,
} from "../constants.ts";
import { isFutureDraftPath, questPath } from "../paths.ts";
import { state } from "../state.ts";
import { ToolPermission } from "../types.ts";
import {
  cleanCommandPreamble,
  hasFileRedirection,
  isHelpOrVersionInvocation,
  splitBashCommandChain,
  splitBashTokens,
} from "./shell_parser.ts";

export const MUTATING_BINARIES = new Set([
  "rm",
  "rmdir",
  "unlink",
  "cp",
  "mv",
  "mkdir",
  "touch",
  "chmod",
  "chown",
  "chgrp",
  "truncate",
  "dd",
  "patch",
  "install",
  "ln",
  "tee",
  "split",
  "sponge",
  "make",
  "cmake",
  "ninja",
  "gcc",
  "clang",
  "cc",
  "c++",
  "g++",
  "ld",
  "ar",
  "as",
  "rustc",
  "cargo",
  "npm",
  "npx",
  "yarn",
  "pnpm",
  "pip",
  "pip3",
  "pipenv",
  "poetry",
  "bundle",
  "gem",
  "nano",
  "vim",
  "nvim",
  "emacs",
  "ed",
  "ex",
  "pytest",
  "jest",
  "vitest",
]);

export const INTERPRETER_BINARIES = new Set([
  "python",
  "python3",
  "node",
  "deno",
  "ruby",
  "php",
  "bash",
  "sh",
  "zsh",
]);

export const READ_BINARIES = new Set([
  "pwd",
  "cd",
  "ls",
  "dir",
  "tree",
  "find",
  "fd",
  "which",
  "whereis",
  "type",
  "whatis",
  "file",
  "stat",
  "du",
  "df",
  "grep",
  "rg",
  "egrep",
  "fgrep",
  "ag",
  "ack",
  "cat",
  "head",
  "tail",
  "less",
  "more",
  "nl",
  "wc",
  "diff",
  "cmp",
  "comm",
  "sort",
  "uniq",
  "cut",
  "paste",
  "column",
  "fold",
  "fmt",
  "pr",
  "strings",
  "od",
  "hexdump",
  "xxd",
  "jq",
  "yq",
  "tr",
  "awk",
  "gawk",
  "mawk",
  "echo",
  "printf",
  "date",
  "uptime",
  "hostname",
  "uname",
  "whoami",
  "id",
  "groups",
  "env",
  "printenv",
  "test",
  "[",
  "[[",
  "true",
  "false",
  "sleep",
]);

export const JOURNAL_TOOL_NAMES = new Set([
  "quest_update_state",
  "quest_mark_saved",
  "quest_subquest",
  "quest_archive",
  "quest_rebut",
  "quest_ask_human",
  "quest_refine",
  "quest_draft",
  "quest_save",
  "quest_status",
  "quests",
  "quest",
  "memory_write",
  "memory_forget",
  "memory_restore",
  "scratchpad",
]);

export const READ_TOOL_NAMES = new Set([
  "read",
  "doc_to_md",
  "fetch",
  "fetch_content",
  "get_search_content",
  "memory_read",
  "memory_search",
  "memory_status",
  "search_graph",
  "query_graph",
  "trace_path",
  "get_code_snippet",
  "get_graph_schema",
  "get_architecture",
  "search_code",
  "list_projects",
  "index_status",
  "check_index_coverage",
  "detect_changes",
  "bg_status",
  "bg_logs",
  "bg_result",
]);

export const RESEARCH_TOOL_NAMES = new Set([
  "web_search",
  "source_check",
  "bg_delegate",
  "fusion_reason",
  "fusion_investigate",
  "fusion_research",
  "fusion_validate",
]);

export const MUTATING_TOOL_NAMES = new Set([
  "bg_run",
  "bg_run_pi_attested",
  "bg_kill",
  "manage_adr",
  "ingest_traces",
  "delete_project",
  "index_repository",
]);

export const READ_GIT_SUBCOMMANDS = new Set([
  "status",
  "diff",
  "log",
  "show",
  "branch",
  "tag",
  "rev-parse",
  "remote",
  "describe",
  "config",
  "ls-files",
  "ls-tree",
  "cat-file",
  "blame",
  "shortlog",
  "check-ref-format",
  "version",
  "help",
]);

export const MUTATING_GIT_SUBCOMMANDS = new Set([
  "add",
  "commit",
  "rm",
  "mv",
  "checkout",
  "switch",
  "reset",
  "clean",
  "restore",
  "revert",
  "merge",
  "rebase",
  "cherry-pick",
  "pull",
  "push",
  "stash",
  "apply",
  "init",
  "clone",
  "fetch",
  "submodule",
]);

export function isMutatingGitSubcommand(gitSub: string): boolean {
  return MUTATING_GIT_SUBCOMMANDS.has(gitSub);
}

export function isReadGitSubcommand(gitSub: string): boolean {
  return READ_GIT_SUBCOMMANDS.has(gitSub);
}

export function classifySpecialGitCommand(
  gitSub: string,
  tokens: string[],
): ToolPermission | null {
  if (gitSub === "branch") {
    return tokens.some((t) =>
        t === "-d" || t === "-D" || t === "-m" || t === "-M"
      )
      ? "implementation"
      : "read";
  }
  if (gitSub === "tag") {
    return tokens.some((t) => t === "-d" || t === "--delete")
      ? "implementation"
      : "read";
  }
  if (gitSub === "remote") {
    return tokens.some((t) =>
        t === "add" || t === "remove" || t === "rm" || t === "rename" ||
        t === "set-url"
      )
      ? "implementation"
      : "read";
  }
  if (gitSub === "config") {
    return tokens.some((t) =>
        t === "--get" || t === "--list" || t === "-l" || t.startsWith("--get-")
      )
      ? "read"
      : "implementation";
  }
  return null;
}

export function classifyGitCommand(tokens: string[]): ToolPermission {
  const nonFlagSubcommands = tokens.slice(1).filter((t) => !t.startsWith("-"));
  const gitSub = (nonFlagSubcommands[0] || "").toLowerCase();

  if (isMutatingGitSubcommand(gitSub)) {
    return "implementation";
  }

  const specialResult = classifySpecialGitCommand(gitSub, tokens);
  if (specialResult !== null) {
    return specialResult;
  }

  if (isReadGitSubcommand(gitSub)) {
    return "read";
  }

  return "implementation";
}

export function classifyInterpreterCommand(
  bin: string,
  tokens: string[],
): ToolPermission {
  const inlineScript = tokens.slice(1).join(" ");
  if (
    /open\s*\([^)]*['"][waWA+]/.test(inlineScript) ||
    /fs\.(?:write|append|unlink|rm|mkdir|copy)/.test(inlineScript) ||
    /writeFileSync|appendFileSync|writeFile|appendFile|unlinkSync|rmSync|mkdirSync/
      .test(inlineScript) ||
    /os\.(?:remove|unlink|rmdir|mkdir|rename)/.test(inlineScript) ||
    /shutil\.(?:rmtree|copy|move)/.test(inlineScript) ||
    /write_text|write_bytes/.test(inlineScript)
  ) {
    return "implementation";
  }
  return "read";
}

export function classifySedPerlCommand(
  bin: string,
  tokens: string[],
): ToolPermission {
  if (bin === "sed") {
    const hasInPlace = tokens.some((t) =>
      t === "-i" || t.startsWith("-i") || t.startsWith("--in-place")
    );
    return hasInPlace ? "implementation" : "read";
  }
  if (bin === "perl") {
    const hasInPlace = tokens.some((t) =>
      t === "-i" || t.startsWith("-i") || t.includes("-pi") || t.includes("-i")
    );
    return hasInPlace ? "implementation" : "read";
  }
  return "read";
}

export function classifyAwkCommand(
  bin: string,
  tokens: string[],
): ToolPermission {
  if (bin === "awk" || bin === "gawk" || bin === "mawk") {
    const awkProg = tokens.slice(1).join(" ");
    if (
      />\s*['"]?[a-zA-Z0-9_\-\.\/]+['"]?/.test(awkProg) ||
      /system\s*\(/.test(awkProg)
    ) {
      return "implementation";
    }
  }
  return "read";
}

export function classifyByBinaryCategory(
  bin: string,
  tokens: string[],
): ToolPermission {
  if (MUTATING_BINARIES.has(bin)) {
    return "implementation";
  }
  if (bin === "sed" || bin === "perl") {
    return classifySedPerlCommand(bin, tokens);
  }
  if (bin === "git") {
    return classifyGitCommand(tokens);
  }
  if (INTERPRETER_BINARIES.has(bin)) {
    return classifyInterpreterCommand(bin, tokens);
  }
  if (bin === "awk" || bin === "gawk" || bin === "mawk") {
    return classifyAwkCommand(bin, tokens);
  }
  return "read";
}

function classifyXargs(tokens: string[]): ToolPermission {
  const nonFlagTokens = tokens.slice(1).filter((t) => !t.startsWith("-"));
  if (nonFlagTokens.length > 0) {
    return classifySingleBashCommand(nonFlagTokens.join(" "));
  }
  return "read";
}

export function classifySingleBashCommand(cmdSegment: string): ToolPermission {
  const trimmed = cmdSegment.trim();
  if (!trimmed) return "read";

  if (hasFileRedirection(trimmed)) {
    return "implementation";
  }

  const cleaned = cleanCommandPreamble(trimmed);
  if (!cleaned) return "read";

  // Standalone shell keywords/terminators that execute no commands
  if (/^(?:done|fi|esac|else|do|then)$/i.test(cleaned)) {
    return "read";
  }

  // Shell loop headers: for var in ... or for ((...))
  if (/^for\s+(?:[A-Za-z_][A-Za-z0-9_]*\s+in\b|\(\(.*\)\))/i.test(cleaned)) {
    return "read";
  }

  // Shell conditional/loop tests: while <test>, until <test>, if <test>
  const testMatch = cleaned.match(/^(?:while|until|if)\s+(.+)$/i);
  if (testMatch) {
    return classifySingleBashCommand(testMatch[1]);
  }

  const tokens = splitBashTokens(cleaned);
  if (tokens.length === 0) return "read";

  const bin = basename(tokens[0]).toLowerCase();
  if (bin === "xargs") {
    return classifyXargs(tokens);
  }

  if (isHelpOrVersionInvocation(tokens)) {
    return "read";
  }

  return classifyByBinaryCategory(bin, tokens);
}

export function classifyBashCommand(commandStr: string): ToolPermission {
  if (!commandStr || typeof commandStr !== "string" || !commandStr.trim()) {
    return "read";
  }

  const segments = splitBashCommandChain(commandStr);
  if (segments.length === 0) return "read";

  for (const segment of segments) {
    const perm = classifySingleBashCommand(segment);
    if (perm === "implementation") {
      return "implementation";
    }
  }

  return "read";
}

export function isCriticalReviewSubagentInvocation(input?: any): boolean {
  if (!input) return false;
  if (input.isCriticalReview === true || input.review === true) return true;
  const kind = input.reviewKind || input.kind;
  if (
    kind === "direction" || kind === "plan_review" ||
    kind === "final_acceptance" || kind === "critical_review"
  ) return true;

  const agent = String(input.agent || "").toLowerCase().trim();
  if (
    agent === "critical-reviewer" ||
    agent === "critic" ||
    agent === "critical_reviewer" ||
    agent === "reviewer" ||
    agent === "critical-agent"
  ) {
    return true;
  }

  const task = typeof input.task === "string" ? input.task : "";
  if (
    task.includes("[CRITICAL REVIEW]") ||
    task.includes("Critical Agent Direction Review") ||
    task.includes("Critical Agent Final Acceptance Review") ||
    task.includes("technically severe reviewer")
  ) {
    return true;
  }

  return false;
}

export function isJournalPath(normPath: string): boolean {
  if (!normPath) return false;
  const p = normPath.replace(/\\/g, "/");
  const activePath = state.questId ? questPath(state.questId) : "";
  if (activePath && (p === activePath || p.endsWith(activePath))) {
    return true;
  }
  if (isFutureDraftPath(p, state.activeDraft)) {
    return true;
  }
  if (/(?:^|\/)\.pi\/quest\//.test(p)) {
    return true;
  }
  return (
    p.startsWith(`${QUEST_CURRENT_DIR}/`) ||
    p.startsWith(`${FUTURE_DIR}/`) ||
    p.startsWith(`${QUEST_ARCHIVE_DIR}/`) ||
    p.startsWith(`${QUEST_ROOT}/`) ||
    p.startsWith(".pi/quest/") ||
    p === NOTES_FILE ||
    p === "MEMORY.md" ||
    p === "SCRATCHPAD.md" ||
    p.startsWith("daily/") ||
    p.endsWith("/MEMORY.md") ||
    p.endsWith("/SCRATCHPAD.md") ||
    p.endsWith(`/${NOTES_FILE}`)
  );
}

export function classifyToolCall(
  toolName: string,
  input?: any,
): ToolPermission {
  const norm = (toolName || "").toLowerCase().trim();

  if (
    norm === "edit" || norm === "write" || norm === "user_edit" ||
    norm === "user_write"
  ) {
    const rawPath = typeof input?.path === "string"
      ? input.path
      : typeof input?.file === "string"
      ? input.file
      : typeof input?.target === "string"
      ? input.target
      : "";
    const normPath = rawPath.replace(/^\.\//, "").replace(/\\/g, "/");
    return isJournalPath(normPath) ? "journal" : "implementation";
  }

  if (JOURNAL_TOOL_NAMES.has(norm)) {
    return "journal";
  }

  if (
    norm === "ask_questions" ||
    norm === "ask_question" ||
    norm === "ask_user_question" ||
    norm === "ask_user" ||
    norm === "ask_human" ||
    norm.startsWith("ask_") ||
    norm.includes("question")
  ) {
    return "interaction";
  }

  if (READ_TOOL_NAMES.has(norm)) {
    return "read";
  }

  if (RESEARCH_TOOL_NAMES.has(norm)) {
    return "research";
  }

  if (norm === "subagent") {
    const action = input?.action;
    if (
      action === "list" || action === "get" || action === "doctor" ||
      action === "status"
    ) {
      return "read";
    }
    if (isCriticalReviewSubagentInvocation(input)) {
      return "research";
    }
    return "implementation";
  }

  if (norm === "bash" || norm === "user_bash") {
    const cmd = typeof input === "string"
      ? input
      : typeof input?.command === "string"
      ? input.command
      : typeof input?.cmd === "string"
      ? input.cmd
      : "";
    return classifyBashCommand(cmd);
  }

  if (MUTATING_TOOL_NAMES.has(norm)) {
    return "implementation";
  }

  // Hallucinated `grep`/`rg` tool — treat as read (search) not unknown, to avoid UNKNOWN_TOOL spam
  if (READ_BINARIES.has(norm)) {
    return "read";
  }
  if (MUTATING_BINARIES.has(norm)) {
    return "implementation";
  }

  return "unknown";
}
