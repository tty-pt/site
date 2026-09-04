// HIGH_LEVEL: #implementing — setbacks recorded with evidence, nothing blocks.
// SPEC: B1.7 (advisory — recorded condition, never a gate).
import type { ToolResultEvent } from "../hooks/events";

export interface SetbackSignal {
  reason: string;
  evidence: string[];
}

const INVESTIGATIVE =
  /\b(rg|grep|egrep|fgrep|ag|ack|fd|find|wc|ls|stat|file|du|df|tree|cat|head|tail)\b/i;
const TEST_OR_BUILD =
  /make\s+test|deno\s+test|npm\s+test|pytest|cargo\s+test|jest|vitest|make\b|npm\s+run\s+build|npm\s+build|cargo\s+build|tsc\b/i;
const FAILURE_SIGNALS =
  /\b(?:FAIL|FAILED|assertion failed|panic:|Segmentation fault|make:\s*\*\*\*|TypeError|SyntaxError)\b/i;
const ERROR_SIGNALS =
  /\b(?:error:|FAILED|panic:|Segmentation fault|permission denied|no such file|cannot open|failed to)\b/i;

function outputText(event: ToolResultEvent): string {
  return event.content.map((c) => c.text ?? "").join("\n");
}

function exitCode(event: ToolResultEvent): unknown {
  const details = event.details as Record<string, unknown> | undefined;
  return details?.["exitCode"] ?? details?.["code"];
}

export function detectSetback(event: ToolResultEvent): SetbackSignal | null {
  if (event.toolName === "bash") {
    const raw = event.input["command"];
    const cmd = typeof raw === "string" ? raw : "";
    if (cmd.trim() === "") return null;
    const output = outputText(event);
    if (INVESTIGATIVE.test(cmd)) {
      if (exitCode(event) === 1) return null;
      if (output.trim() === "") return null;
      if (!ERROR_SIGNALS.test(output)) return null;
    }
    if ((event.isError || FAILURE_SIGNALS.test(output)) && TEST_OR_BUILD.test(cmd)) {
      return {
        reason: `Test/build command failed: ${cmd.slice(0, 200)}`,
        evidence: [output.slice(0, 1500) || `Command '${cmd}' failed`],
      };
    }
    if (event.isError) {
      return {
        reason: `Command failed with error: ${cmd.slice(0, 200)}`,
        evidence: [output.slice(0, 1500) || `Command '${cmd}' failed with error`],
      };
    }
    return null;
  }
  if ((event.toolName === "edit" || event.toolName === "write") && event.isError) {
    return {
      reason: `File ${event.toolName} failed`,
      evidence: [outputText(event).slice(0, 1500) || `${event.toolName} reported an error`],
    };
  }
  return null;
}
