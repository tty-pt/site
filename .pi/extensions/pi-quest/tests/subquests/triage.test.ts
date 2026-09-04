import { check } from "../check.ts";
import { classifyUserMessage } from "../../src/subquests/triage.ts";

Deno.test("triage confirms approvals", () => {
  for (const text of ["go", "yes", "lgtm", "please proceed", "looks good", "approve"]) {
    check(classifyUserMessage(text) === "confirmation", `"${text}" confirms`);
  }
});

Deno.test("triage hears acks", () => {
  for (const text of ["thanks", "ok cool", "hi"]) {
    check(classifyUserMessage(text) === "ack", `"${text}" acks`);
  }
});

Deno.test("triage routes short questions to discussion", () => {
  check(classifyUserMessage("what does this do?") === "question", "short question");
  check(classifyUserMessage(" explain the gate table ") === "question", "explainer");
});

Deno.test("triage treats the rest as refinements", () => {
  check(
    classifyUserMessage("Also handle the edge case where the draft is empty and add retries.") === "refinement",
    "requirement refines",
  );
  check(
    classifyUserMessage("What is the full history of this design decision and every alternative we rejected along the way, in detail?".repeat(4)) === "refinement",
    "long question refines",
  );
});
