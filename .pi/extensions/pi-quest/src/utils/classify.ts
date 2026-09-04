// HIGH_LEVEL: #drafting — research reads stay open; only the draft file writes.
// SPEC: B2.1 tool classes.
import type { ToolClass } from "../domain/gates";

const READ_TOOLS = new Set(["read", "grep", "find", "ls"]);
const WRITE_TOOLS = new Set(["edit", "write"]);
const LAUNCH_TOOLS = new Set(["subagent"]);
const READ_PROBES = [
  "cat",
  "ls",
  "head",
  "tail",
  "wc",
  "pwd",
  "which",
  "echo",
  "true",
  "git status",
  "git diff",
  "git log",
  "git show",
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

function classifyBash(input: Record<string, unknown>): ToolClass {
  const command = input["command"];
  if (typeof command !== "string" || command.trim() === "") return "mutating-bash";
  const segments = splitSegments(command);
  if (segments.length === 0) return "mutating-bash";
  const clean = segments.every((seg) =>
    !seg.includes(">") &&
    READ_PROBES.some((probe) => seg === probe || seg.startsWith(`${probe} `))
  );
  return clean ? "read" : "mutating-bash";
}
