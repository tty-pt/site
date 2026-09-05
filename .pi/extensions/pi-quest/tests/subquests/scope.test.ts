import { check } from "../check.ts";
import { triageInQuestRequest } from "../../src/subquests/scope.ts";

Deno.test("triage records refinements with routing guidance", () => {
  const triaged = triageInQuestRequest("abc123", "refinement");
  check(triaged !== null && triaged.record, "refinement recorded");
  check(triaged!.steer.includes("quest_update_state"), "amendment route named");
  check(triaged!.steer.includes("quest_subquest"), "sub-quest route named");
  check(triaged!.steer.includes("finish or archive"), "separate-quest instruction present");
  check(triaged!.steer.includes("abc123"), "qid carried");
});

Deno.test("triage leaves non-refinements to the agent", () => {
  check(triageInQuestRequest("abc123", "question") === null, "questions pass through");
  check(triageInQuestRequest("abc123", "ack") === null, "acks pass through");
  check(triageInQuestRequest("abc123", "confirmation") === null, "approvals pass through");
});
