import { check } from "../check.ts";
import { isResultValidFor } from "../../src/review/protocol.ts";

Deno.test("review results bind to their target revision", () => {
  const result = { verdict: "PASS" as const, target: "hash-1", findings: "clean" };
  check(isResultValidFor(result, "hash-1"), "same target valid");
  check(!isResultValidFor(result, "hash-2"), "advanced target invalid");
});
