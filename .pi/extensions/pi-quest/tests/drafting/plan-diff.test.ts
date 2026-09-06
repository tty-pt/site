import { check } from "../check.ts";
import { diffPlans } from "../../src/drafting/plan-diff.ts";

Deno.test("identical plans produce no diff", () => {
  check(diffPlans("a\nb", "a\nb") === null, "identical is null");
  check(diffPlans("", "") === null, "empty is null");
});

Deno.test("additions and removals render as signed lines", () => {
  const diff = diffPlans("step one\nstep two", "step one\nstep three");
  check(diff !== null && diff.includes("- step two"), "removal marked");
  check(diff !== null && diff.includes("+ step three"), "addition marked");
  check(diff !== null && !diff.includes("step one"), "shared context omitted");
});

Deno.test("oversized diffs fall back to null", () => {
  const oldPlan = Array.from({ length: 50 }, (_, i) => `old ${i}`).join("\n");
  const newPlan = Array.from({ length: 50 }, (_, i) => `new ${i}`).join("\n");
  check(diffPlans(oldPlan, newPlan) === null, "huge rewrite is null");
  check(diffPlans(oldPlan, newPlan, 200) !== null, "raised cap keeps it");
});
