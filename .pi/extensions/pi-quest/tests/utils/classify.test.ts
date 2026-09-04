import { check } from "../check.ts";
import { classify } from "../../src/utils/classify.ts";

Deno.test("classifier passes reads and writes through", () => {
  check(classify("read", { path: "x" }) === "read", "read");
  check(classify("grep", {}) === "read", "grep");
  check(classify("find", {}) === "read", "find");
  check(classify("ls", {}) === "read", "ls");
  check(classify("edit", { path: "x" }) === "write", "edit");
  check(classify("write", { path: "x" }) === "write", "write");
});

Deno.test("classifier folds user_ variants", () => {
  check(classify("user_edit", { path: "x" }) === "write", "user_edit");
  check(classify("user_write", { path: "x" }) === "write", "user_write");
});

Deno.test("classifier names journal, ask, and launch tools", () => {
  check(classify("quest_update_state", {}) === "journal", "journal");
  check(classify("quest_subquest", {}) === "journal", "subquest");
  check(classify("ask_questions", {}) === "ask", "ask");
  check(classify("subagent", {}) === "launch", "subagent");
  check(classify("mystery_tool", {}) === "other", "unknown other");
});

Deno.test("classifier allows read-only bash probes", () => {
  check(classify("bash", { command: "git status" }) === "read", "status");
  check(classify("bash", { command: "ls -la && git diff --stat" }) === "read", "chain of probes");
  check(classify("bash", { command: "cat file | head -20" }) === "read", "pipe of probes");
});

Deno.test("classifier treats the rest of bash as mutating", () => {
  check(classify("bash", { command: "rm -rf dist" }) === "mutating-bash", "rm");
  check(classify("bash", { command: "echo hi > out.txt" }) === "mutating-bash", "redirect");
  check(classify("bash", { command: "echo hi 2>/dev/null" }) === "mutating-bash", "stderr redirect strict");
  check(classify("bash", { command: "git status && make" }) === "mutating-bash", "probe plus make");
  check(classify("bash", { command: "deno test" }) === "mutating-bash", "test runner");
  check(classify("bash", { command: "git commit -m x" }) === "mutating-bash", "commit");
  check(classify("bash", {}) === "mutating-bash", "missing command");
  check(classify("bash", { command: "   " }) === "mutating-bash", "blank command");
});
