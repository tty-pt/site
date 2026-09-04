import assert from "node:assert";
import {
  resolveActiveRole,
  resolveCallerSelf,
} from "../src/roles.ts";
import type { ActiveReview, ExtensionContext } from "../src/types.ts";

function makeReview(overrides: Partial<ActiveReview>): ActiveReview {
  return {
    reviewId: "r1",
    childSessionId: "reviewer-child-session",
    parentSessionId: "main-session",
    questId: "q1",
    questSlug: "q1",
    kind: "plan_review",
    snapshot: {
      questId: "q1",
      sessionId: "main-session",
      reviewId: "r1",
      reviewKind: "plan_review",
      planVersion: 1,
      saveGeneration: 0,
      stateHash: null,
      originalUserRequest: "req",
      currentUnderstanding: "u",
      assumptions: "",
      plan: "p",
      planRevisions: "",
      findings: "",
      filesChanged: "",
      relevantDiff: "",
      testStatus: "",
      nextAction: "",
      createdAt: 0,
    },
    startedAt: 0,
    activity: { turns: 0, tools: 0, reads: 0, searches: 0, writes: 0, commands: 0, files: 0, lastActivityAt: 0 },
    status: "running",
    ...overrides,
  };
}

function ctxWithSession(sessionId: string, childSessionId?: string): ExtensionContext {
  const c: any = {
    sessionManager: { id: sessionId, sessionId },
    cwd: process.cwd(),
    ui: { notify: () => {} },
  };
  if (childSessionId) c.childSessionId = childSessionId;
  return c;
}

Deno.test("roles: main (parent) session always resolves to implementer-drafter", async (t) => {
  await t.step(
    "parent session is implementer-drafter even with an active reviewer child",
    () => {
      const self = { sessionId: "main-session", isChild: false };
      const role = resolveActiveRole("q1", self, [
        makeReview({}), // active reviewer child for q1
      ]);
      assert.strictEqual(role.role, "implementer-drafter");
      assert.strictEqual(role.hasActiveReview, true);
    },
  );

  await t.step("session with no active reviews is implementer-drafter", () => {
    const self = { sessionId: "some-session", isChild: false };
    const role = resolveActiveRole("q1", self, []);
    assert.strictEqual(role.role, "implementer-drafter");
    assert.strictEqual(role.hasActiveReview, false);
  });
});

Deno.test("roles: reviewer child resolves to reviewer only when it matches an active review", async (t) => {
  await t.step("child session matching active review childSessionId is reviewer", () => {
    const self = { sessionId: "reviewer-child-session", isChild: true, parentSessionId: "main-session" };
    const role = resolveActiveRole("q1", self, [makeReview({})]);
    assert.strictEqual(role.role, "reviewer");
    assert.strictEqual(role.reviewId, "r1");
    assert.strictEqual(role.parentSessionId, "main-session");
    assert.strictEqual(role.childSessionId, "reviewer-child-session");
  });

  await t.step("child session not matching any active review is not a reviewer", () => {
    const self = { sessionId: "unrelated-child", isChild: true, parentSessionId: "other" };
    const role = resolveActiveRole("q1", self, [makeReview({})]);
    assert.strictEqual(role.role, "implementer-drafter");
  });

  await t.step("a review for a different quest does not make the caller a reviewer", () => {
    const self = { sessionId: "reviewer-child-session", isChild: true, parentSessionId: "main-session" };
    const role = resolveActiveRole("other-quest", self, [makeReview({})]);
    assert.strictEqual(role.role, "implementer-drafter");
  });

  await t.step("a completed review is ignored (not active)", () => {
    const self = { sessionId: "reviewer-child-session", isChild: true, parentSessionId: "main-session" };
    const role = resolveActiveRole("q1", self, [
      makeReview({ status: "completed" }),
    ]);
    assert.strictEqual(role.role, "implementer-drafter");
  });
});

Deno.test("roles: resolveCallerSelf reads session and child identity from context", () => {
  const main = resolveCallerSelf(ctxWithSession("main-session"));
  assert.strictEqual(main.sessionId, "main-session");
  assert.strictEqual(main.isChild, false);

  const child = resolveCallerSelf(ctxWithSession("reviewer-child-session", "reviewer-child-session"));
  assert.strictEqual(child.sessionId, "reviewer-child-session");
  assert.strictEqual(child.isChild, true);

  const none = resolveCallerSelf(undefined);
  assert.strictEqual(typeof none.sessionId, "string");
  assert.strictEqual(none.isChild, false);
});