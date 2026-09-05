import { check } from "../check.ts";
import type { ToolResultEvent } from "../../src/hooks/events.ts";
import { detectSetback } from "../../src/implementing/setbacks.ts";

function bash(command: string, text: string, isError: boolean, details?: unknown): ToolResultEvent {
  return {
    type: "tool_result",
    toolCallId: "1",
    toolName: "bash",
    input: { command },
    content: [{ type: "text", text }],
    isError,
    details,
  };
}

Deno.test("setbacks catch test and build failures", () => {
  const hit = detectSetback(bash("deno test", "FAILED foo_test", false));
  check(hit !== null && hit.reason.includes("Test/build"), "test failure recorded");
  check(hit !== null && hit.evidence[0].includes("FAILED"), "evidence kept");
  const err = detectSetback(bash("rm -rf dist", "", true));
  check(err !== null && err.reason.includes("Command failed"), "error exit recorded");
});

Deno.test("setbacks ignore investigative noise", () => {
  check(detectSetback(bash("grep -r foo .", "", true, { exitCode: 1 })) === null, "grep exit 1 ignored");
  check(detectSetback(bash("ls empty", "", true)) === null, "empty ls ignored");
  check(detectSetback(bash("cat file", "hello", false)) === null, "clean probe ignored");
  const real = detectSetback(bash("cat missing", "cat: cannot open missing", true));
  check(real !== null, "real cat error recorded");
});

Deno.test("setbacks catch build error counts", () => {
  const make = detectSetback(bash("make -j4", "4 errors generated.\nmake: Error 1", false));
  check(make !== null && make.reason.includes("Test/build"), "error counts recorded");
  const clean = detectSetback(bash("make -j4", "0 errors, build clean", false));
  check(clean === null, "zero errors ignored");
});

Deno.test("setbacks catch failed file writes and ignore the rest", () => {
  const write: ToolResultEvent = {
    type: "tool_result",
    toolCallId: "2",
    toolName: "write",
    input: { path: "x" },
    content: [{ type: "text", text: "denied" }],
    isError: true,
  };
  check(detectSetback(write) !== null, "write error recorded");
  const read: ToolResultEvent = { ...write, toolName: "read", isError: true };
  check(detectSetback(read) === null, "read errors ignored");
  const quest: ToolResultEvent = { ...write, toolName: "quest_update_state" };
  check(detectSetback(quest) === null, "quest tools ignored");
});
