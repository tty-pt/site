import { check } from "../check.ts";
import { classify } from "../../src/utils/classify.ts";

Deno.test("classifier passes tools and folds variants", () => {
  check(classify("read", { path: "x" }) === "read", "read");
  check(classify("grep", {}) === "read", "grep");
  check(classify("edit", { path: "x" }) === "write", "edit");
  check(classify("write", { path: "x" }) === "write", "write");
  check(classify("user_edit", { path: "x" }) === "write", "user_edit");
  check(classify("quest_update_state", {}) === "journal", "journal");
  check(classify("ask_questions", {}) === "ask", "ask");
  check(classify("subagent", {}) === "launch", "subagent");
  check(classify("mystery_tool", {}) === "other", "unknown other");
});

Deno.test("classifier allows arbitrary research under inversion", () => {
  check(classify("bash", { command: "find mods -name '*.c' | sort | head -80" }) === "read", "find-sort pipeline");
  check(classify("bash", { command: "find . -type f 2>/dev/null | head -200" }) === "read", "stderr sink");
  check(classify("bash", { command: "grep -r foo . 2>/dev/null | cut -d: -f1 | sort -u" }) === "read", "grep-cut-sort");
  check(classify("bash", { command: "ls mods/; echo ---; wc -l mods/*/*.c" }) === "read", "inspect chain");
  check(classify("bash", { command: "for f in $(find mods -name '*.c'); do echo $f; done" }) === "read", "read-only loop");
  check(classify("bash", { command: "ffmpeg -version" }) === "read", "unknown binary defaults open");
  check(classify("bash", { command: "pwd; ls" }) === "read", "probes stay open");
});

Deno.test("classifier allows test runners as evidence gathering", () => {
  check(classify("bash", { command: "deno test" }) === "read", "deno test");
  check(classify("bash", { command: "npm test" }) === "read", "npm test");
  check(classify("bash", { command: "make test" }) === "read", "make test");
  check(classify("bash", { command: "pytest -x" }) === "read", "pytest");
  check(classify("bash", { command: "tsc --noEmit" }) === "read", "tsc");
});

Deno.test("classifier prohibits write signals", () => {
  check(classify("bash", { command: "echo hi > out.txt" }) === "mutating-bash", "stdout redirect");
  check(classify("bash", { command: "cmd 2>err.log" }) === "mutating-bash", "stderr redirect");
  check(classify("bash", { command: "make > build.log 2>&1" }) === "mutating-bash", "mixed redirect");
  check(classify("bash", { command: "find . -name x | tee out.txt" }) === "mutating-bash", "tee writes");
  check(classify("bash", { command: "rm -rf dist" }) === "mutating-bash", "rm");
  check(classify("bash", { command: "sudo rm -rf /tmp/x" }) === "mutating-bash", "sudo rm");
  check(classify("bash", { command: "FOO=1 mv a b" }) === "mutating-bash", "env-prefixed mv");
  check(classify("bash", { command: "mkdir -p .pi/quest/current/x" }) === "mutating-bash", "mkdir");
  check(classify("bash", { command: "sed -i s/a/b/ file" }) === "mutating-bash", "sed in place");
  check(classify("bash", { command: "git commit -m x" }) === "mutating-bash", "commit");
  check(classify("bash", { command: "git checkout -- file" }) === "mutating-bash", "checkout");
  check(classify("bash", { command: "echo $(rm -rf x)" }) === "mutating-bash", "substitution smuggling");
  check(classify("bash", { command: "echo `mkdir p`" }) === "mutating-bash", "backtick smuggling");
  check(classify("bash", { command: "make" }) === "mutating-bash", "bare make builds");
  check(classify("bash", { command: "npm run build" }) === "mutating-bash", "build script");
  check(classify("bash", { command: "deno run main.ts" }) === "mutating-bash", "deno run");
  check(classify("bash", { command: "node script.js" }) === "mutating-bash", "node script");
  check(classify("bash", {}) === "mutating-bash", "missing command");
  check(classify("bash", { command: "   " }) === "mutating-bash", "blank command");
});

Deno.test("classifier keeps null sinks and merges open", () => {
  check(classify("bash", { command: "cat f 2>&1 | head" }) === "read", "fd merge to pipe");
  check(classify("bash", { command: "echo hi > /dev/null" }) === "read", "null redirect");
  check(classify("bash", { command: "git status 2>/dev/null" }) === "read", "sinked status");
});
