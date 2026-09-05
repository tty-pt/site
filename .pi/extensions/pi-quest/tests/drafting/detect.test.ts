import { check } from "../check.ts";
import { mkdtempSync } from "node:fs";
import { readFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { getState, replaceState } from "../../src/app/store.ts";
import { detectSubstantiveRequest } from "../../src/drafting/detect.ts";
import { createQuest, IDLE_STATE } from "../../src/domain/quest.ts";
import { draftPath } from "../../src/domain/paths.ts";
import { fakeCtx, fakePi } from "../fake-pi.ts";

function tmp(): string {
  return mkdtempSync(join(tmpdir(), "pi-quest-detect-"));
}

Deno.test("detection opens a provisional quest on substance", async () => {
  replaceState(IDLE_STATE);
  const pi = fakePi();
  const created = await detectSubstantiveRequest(
    pi,
    fakeCtx(tmp()),
    "Rebuild the review pipeline so verdicts carry severity and evidence.",
  );
  check(created, "created");
  check(getState().phase === "provisional", "provisional");
  check(getState().qid !== null, "qid assigned");
  check(pi.sent.length === 1, "agent steered");
  check(pi.appended.length === 1, "snapshot emitted");
  replaceState(IDLE_STATE);
});

Deno.test("detection pre-creates the draft scaffold file", async () => {
  replaceState(IDLE_STATE);
  const pi = fakePi();
  const cwd = tmp();
  const request = "Rebuild the review pipeline so verdicts carry severity and evidence.";
  const created = await detectSubstantiveRequest(pi, fakeCtx(cwd), request);
  check(created, "created");
  check(getState().phase === "provisional", "flow unchanged: still provisional");
  check(getState().draft === null, "flow unchanged: first draft still later");
  const file = await readFile(join(cwd, draftPath(getState().qid!)), "utf8");
  check(file.includes(request), "scaffold carries the request");
  check(file.includes("## Implementation Plan"), "scaffold has plan section");
  replaceState(IDLE_STATE);
});

Deno.test("detection ignores acks, commands, and active quests", async () => {
  replaceState(IDLE_STATE);
  const pi = fakePi();
  const ctx = fakeCtx(tmp());
  check(!await detectSubstantiveRequest(pi, ctx, "thanks!"), "ack ignored");
  check(!await detectSubstantiveRequest(pi, ctx, "/quests"), "command ignored");
  check(!await detectSubstantiveRequest(pi, ctx, "what does this do?"), "short question ignored");
  check(getState().phase === "idle", "still idle");
  replaceState(createQuest("existing work", "abc123"));
  check(!await detectSubstantiveRequest(pi, ctx, "A whole new substantive request arrives."), "active quest blocks creation");
  replaceState(IDLE_STATE);
});
