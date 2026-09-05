// HIGH_LEVEL: #drafting — research reads stay open; only the draft file writes.
// SPEC: B2.1 tool classes (mutation-probing: block write signals, never research).
import type { ToolClass } from "../domain/gates";

const READ_TOOLS = new Set(["read", "grep", "find", "ls"]);
const WRITE_TOOLS = new Set(["edit", "write"]);
const LAUNCH_TOOLS = new Set(["subagent"]);

// Commands that mutate the worktree. Anything else a shell can spell is
// treated as research. Residual risk is deliberate: a novel file-writer with
// no redirect and no known name passes this gate; the backstops are setback
// detection, validator judgment, and path-gated edit/write tools.
const DESTRUCTIVE_COMMANDS = new Set([
  "rm",
  "mv",
  "cp",
  "mkdir",
  "rmdir",
  "touch",
  "chmod",
  "chown",
  "ln",
  "dd",
  "shred",
  "tee",
]);

const DESTRUCTIVE_GIT = new Set([
  "commit",
  "push",
  "apply",
  "reset",
  "clean",
  "rm",
  "mv",
  "checkout",
  "branch",
  "tag",
  "stash",
  "rebase",
  "merge",
  "pull",
  "clone",
]);

const BLOCKED_RUNNERS = [
  "make",
  "npm run build",
  "npm run publish",
  "npm install",
  "npm uninstall",
  "npm publish",
  "cargo build",
  "cargo install",
  "deno run",
  "deno compile",
  "deno publish",
];

const BLOCKED_INTERPRETERS = new Set(["node", "python", "python3"]);

// Test and typecheck runners run as evidence gathering, never as builds.
const ALLOWED_RUNNERS = [
  "deno test",
  "deno lint",
  "deno check",
  "npm test",
  "make test",
  "make check",
  "pytest",
  "jest",
  "vitest",
  "cargo test",
  "go test",
  "tsc",
];

export function classify(toolName: string, input: Record<string, unknown>): ToolClass {
  if (toolName.startsWith("user_")) return classify(toolName.slice("user_".length), input);
  if (toolName === "ask_questions") return "ask";
  if (toolName.startsWith("quest_")) return "journal";
  if (READ_TOOLS.has(toolName)) return "read";
  if (WRITE_TOOLS.has(toolName)) return "write";
  if (LAUNCH_TOOLS.has(toolName)) return "launch";
  if (toolName === "bash") return classifyBash(input);
  return "other";
}

function splitSegments(command: string): string[] {
  return command.split(/&&|\|\||;;|[|;]/).map((s) => s.trim()).filter((s) => s.length > 0);
}

function stripNullSinks(segment: string): string {
  return segment
    .replace(/&>\s*\/dev\/null/g, "")
    .replace(/[12]>>?\s*\/dev\/null/g, "")
    .replace(/>>?\s*\/dev\/null/g, "")
    .replace(/[12]?>&[12]/g, "");
}

function substitutionBodies(segment: string): string[] {
  const bodies: string[] = [];
  const dollar = /\$\(([^()]*)\)/g;
  let m: RegExpExecArray | null;
  while ((m = dollar.exec(segment)) !== null) bodies.push(m[1]);
  const backtick = /`([^`]*)`/g;
  while ((m = backtick.exec(segment)) !== null) bodies.push(m[1]);
  return bodies;
}

function firstWord(segment: string): string {
  const tokens = segment.trim().split(/\s+/);
  for (const token of tokens) {
    if (token === "" || token === "sudo" || /^[A-Za-z_][A-Za-z0-9_]*=/.test(token)) continue;
    const base = token.split("/").pop() ?? token;
    return base;
  }
  return "";
}

function isAllowedRunner(segment: string): boolean {
  return ALLOWED_RUNNERS.some((runner) => segment === runner || segment.startsWith(`${runner} `));
}

function isBlockedRunner(segment: string): boolean {
  if (isAllowedRunner(segment)) return false;
  return BLOCKED_RUNNERS.some((runner) => segment === runner || segment.startsWith(`${runner} `));
}

function isDestructiveGit(segment: string): boolean {
  const words = segment.trim().split(/\s+/);
  if (words[0] !== "git") return false;
  return DESTRUCTIVE_GIT.has(words[1] ?? "");
}

function isDestructiveSed(segment: string): boolean {
  if (firstWord(segment) !== "sed") return false;
  return segment.split(/\s+/).some((token) => token === "-i" || token.startsWith("-i") || token === "--in-place");
}

function isMutatingSegment(segment: string): boolean {
  const scrubbed = stripNullSinks(segment);
  if (scrubbed.includes(">")) return true;
  const word = firstWord(scrubbed);
  if (DESTRUCTIVE_COMMANDS.has(word)) return true;
  if (isDestructiveGit(scrubbed) || isDestructiveSed(scrubbed)) return true;
  if (BLOCKED_INTERPRETERS.has(word)) return true;
  if (isBlockedRunner(scrubbed)) return true;
  return substitutionBodies(segment).some(isMutatingSegment);
}

function classifyBash(input: Record<string, unknown>): ToolClass {
  const command = input["command"];
  if (typeof command !== "string" || command.trim() === "") return "mutating-bash";
  const segments = splitSegments(command);
  if (segments.length === 0) return "mutating-bash";
  return segments.some(isMutatingSegment) ? "mutating-bash" : "read";
}
