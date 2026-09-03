import assert from "node:assert";
import plugin, {
  AgentObligation,
  createAgentObligation,
  createDefaultState,
  drainAgentObligations,
  fulfillObligation,
  getObligationHistory,
  getPendingObligations,
  getState,
  isObligationCurrent,
  queueAgentObligation,
  reconcileObligations,
  reconstructPendingNotifications,
  restoreSessionState,
  setSessionState,
  snapshotState,
  StoredState,
  supersedeObligation,
} from "../src/index.ts";
import {
  createMockContext,
  createMockExtensionAPI,
} from "./compaction_test_helpers.ts";

Deno.test("Durable Obligations Lifecycle Suite: 10 Core Architectural Invariants", async (t) => {
  // -----------------------------------------------------------------------
  // 1. Five independent obligations remain five independent obligations
  // -----------------------------------------------------------------------
  await t.step(
    "1. Five independent obligations remain five independent obligations",
    () => {
      const state: StoredState = createDefaultState();
      state.active = "test-quest";
      state.questId = "q_obl_1";

      const o1 = createAgentObligation(state, {
        id: "obl_1",
        kind: "error",
        code: "TEST_FAILED",
        message: "rerun tests after fixing assertion",
        deliverAs: "steer",
      });
      const o2 = createAgentObligation(state, {
        id: "obl_2",
        kind: "error",
        code: "COMPILER_FAILED",
        message: "investigate compiler failure in module auth",
        deliverAs: "steer",
      });
      const o3 = createAgentObligation(state, {
        id: "obl_3",
        kind: "reassessment",
        code: "REASSESSMENT_REQUIRED",
        message: "reassess the plan after contradictory benchmark",
        deliverAs: "followUp",
      });
      const o4 = createAgentObligation(state, {
        id: "obl_4",
        kind: "critical_review",
        code: "CRITICAL_REVIEW_FAILED",
        message: "perform Critical Agent review for plan drift",
        deliverAs: "steer",
      });
      const o5 = createAgentObligation(state, {
        id: "obl_5",
        kind: "checkpoint_required",
        code: "CHECKPOINT_REQUIRED",
        message: "checkpoint state before compaction threshold",
        deliverAs: "followUp",
      });

      queueAgentObligation(state, o1);
      queueAgentObligation(state, o2);
      queueAgentObligation(state, o3);
      queueAgentObligation(state, o4);
      queueAgentObligation(state, o5);

      assert.strictEqual(
        state.pendingNotifications?.length,
        5,
        "Pending notifications must contain exactly 5 obligations",
      );
      const ids = state.pendingNotifications!.map((o) => o.id);
      assert.deepStrictEqual(
        ids,
        ["obl_1", "obl_2", "obl_3", "obl_4", "obl_5"],
        "All 5 obligation identities must be preserved independently",
      );

      for (const obl of state.pendingNotifications!) {
        assert.strictEqual(
          obl.status,
          "pending",
          `Obligation ${obl.id} must have status pending`,
        );
      }
    },
  );

  // -----------------------------------------------------------------------
  // 2. Delivering all five together does not merge them
  // -----------------------------------------------------------------------
  await t.step("2. Delivering all five together does not merge them", () => {
    const api = createMockExtensionAPI();
    plugin(api.mockPi as any);
    const ctx = createMockContext(10000, "session_obl_delivery");
    const state = getState(ctx as any);
    state.active = "test-quest";
    state.questId = "q_obl_2";

    const o1 = createAgentObligation(state, {
      id: "obl_1",
      kind: "error",
      code: "TEST_FAILED",
      message: "rerun tests",
    });
    const o2 = createAgentObligation(state, {
      id: "obl_2",
      kind: "error",
      code: "COMPILER_FAILED",
      message: "investigate compiler failure",
    });
    const o3 = createAgentObligation(state, {
      id: "obl_3",
      kind: "error",
      code: "REASSESS",
      message: "reassess plan",
    });
    const o4 = createAgentObligation(state, {
      id: "obl_4",
      kind: "error",
      code: "REVIEW",
      message: "perform review",
    });
    const o5 = createAgentObligation(state, {
      id: "obl_5",
      kind: "error",
      code: "SAVE",
      message: "checkpoint state",
    });

    queueAgentObligation(state, o1);
    queueAgentObligation(state, o2);
    queueAgentObligation(state, o3);
    queueAgentObligation(state, o4);
    queueAgentObligation(state, o5);

    assert.strictEqual(state.pendingNotifications?.length, 5);

    // Deliver obligations
    const delivered = drainAgentObligations(api.mockPi as any, ctx);
    assert.strictEqual(
      delivered,
      true,
      "drainAgentObligations must report delivery success",
    );

    // ASSERTIONS:
    // 1. All 5 obligations still exist independently in pending queue
    assert.strictEqual(
      state.pendingNotifications?.length,
      5,
      "All 5 obligations must remain in pending queue after delivery",
    );
    const ids = state.pendingNotifications!.map((o) => o.id);
    assert.deepStrictEqual(
      ids,
      ["obl_1", "obl_2", "obl_3", "obl_4", "obl_5"],
      "Identities must not be merged",
    );

    // 2. Delivery metadata updated on each individual obligation
    for (const obl of state.pendingNotifications!) {
      assert.strictEqual(
        obl.status,
        "delivering",
        `Obligation ${obl.id} must have status delivering`,
      );
      assert.ok(
        typeof obl.deliveredAt === "number",
        `Obligation ${obl.id} must have deliveredAt timestamp`,
      );
      assert.strictEqual(
        obl.attempts,
        1,
        `Obligation ${obl.id} attempts must be 1`,
      );
    }

    // 3. Subsequent drain tick does NOT redeliver already-delivering obligations
    const deliveredAgain = drainAgentObligations(api.mockPi as any, ctx);
    assert.strictEqual(
      deliveredAgain,
      false,
      "Subsequent drain must not redeliver delivering obligations",
    );
    for (const obl of state.pendingNotifications!) {
      assert.strictEqual(
        obl.status,
        "delivering",
        `Obligation ${obl.id} must remain delivering`,
      );
      assert.strictEqual(
        obl.attempts,
        1,
        `Obligation ${obl.id} attempts must remain 1 without infinite delivery loop`,
      );
    }
  });

  // -----------------------------------------------------------------------
  // 3. Fulfilling O1 leaves O2–O5 pending
  // -----------------------------------------------------------------------
  await t.step("3. Fulfilling O1 leaves O2–O5 pending", () => {
    const state: StoredState = createDefaultState();
    state.active = "test-quest";
    state.questId = "q_obl_3";

    const o1 = createAgentObligation(state, {
      id: "obl_1",
      kind: "error",
      code: "TEST_FAILED",
      message: "rerun tests",
    });
    const o2 = createAgentObligation(state, {
      id: "obl_2",
      kind: "error",
      code: "COMPILER_FAILED",
      message: "investigate compiler failure",
    });
    const o3 = createAgentObligation(state, {
      id: "obl_3",
      kind: "error",
      code: "REASSESS",
      message: "reassess plan",
    });
    const o4 = createAgentObligation(state, {
      id: "obl_4",
      kind: "error",
      code: "REVIEW",
      message: "perform review",
    });
    const o5 = createAgentObligation(state, {
      id: "obl_5",
      kind: "error",
      code: "SAVE",
      message: "checkpoint state",
    });

    queueAgentObligation(state, o1);
    queueAgentObligation(state, o2);
    queueAgentObligation(state, o3);
    queueAgentObligation(state, o4);
    queueAgentObligation(state, o5);

    // Fulfill O1
    const fulfilled = fulfillObligation(
      state,
      "obl_1",
      "Unit tests passed after fixing parser bug",
    );
    assert.strictEqual(
      fulfilled,
      true,
      "fulfillObligation must succeed for obl_1",
    );

    // ASSERTIONS:
    // 1. O1 is no longer in pendingNotifications
    const pendingIds = state.pendingNotifications!.map((o) => o.id);
    assert.deepStrictEqual(
      pendingIds,
      ["obl_2", "obl_3", "obl_4", "obl_5"],
      "O1 must be removed from pending queue, O2-O5 remain pending",
    );

    // 2. O1 is recorded in obligationHistory
    assert.strictEqual(state.obligationHistory?.length, 1);
    assert.strictEqual(state.obligationHistory![0].id, "obl_1");
    assert.strictEqual(state.obligationHistory![0].status, "fulfilled");
    assert.strictEqual(
      state.obligationHistory![0].fulfilledReason,
      "Unit tests passed after fixing parser bug",
    );
    assert.ok(typeof state.obligationHistory![0].fulfilledAt === "number");

    // 3. O2–O5 remain pending and untouched
    for (const id of ["obl_2", "obl_3", "obl_4", "obl_5"]) {
      const obl = state.pendingNotifications!.find((o) => o.id === id);
      assert.ok(obl, `Obligation ${id} must still be in pending queue`);
      assert.strictEqual(obl!.status, "pending");
    }
  });

  // -----------------------------------------------------------------------
  // 4. Resolving the condition behind O2 does not affect O3–O5
  // -----------------------------------------------------------------------
  await t.step(
    "4. Resolving the condition behind O2 does not affect O3–O5",
    () => {
      const state: StoredState = createDefaultState();
      state.active = "test-quest";
      state.questId = "q_obl_4";

      let compilerResolved = false;
      const o2 = createAgentObligation(state, {
        id: "obl_2",
        kind: "error",
        code: "COMPILER_FAILED",
        message: "investigate compiler failure",
        isFulfilled: () => compilerResolved,
      });
      const o3 = createAgentObligation(state, {
        id: "obl_3",
        kind: "error",
        code: "REASSESS",
        message: "reassess plan",
      });
      const o4 = createAgentObligation(state, {
        id: "obl_4",
        kind: "error",
        code: "REVIEW",
        message: "perform review",
      });
      const o5 = createAgentObligation(state, {
        id: "obl_5",
        kind: "error",
        code: "SAVE",
        message: "checkpoint state",
      });

      queueAgentObligation(state, o2);
      queueAgentObligation(state, o3);
      queueAgentObligation(state, o4);
      queueAgentObligation(state, o5);

      // Before resolution: all 4 pending
      reconcileObligations(state);
      assert.strictEqual(state.pendingNotifications?.length, 4);

      // Condition behind O2 resolves
      compilerResolved = true;
      reconcileObligations(state);

      // ASSERTIONS:
      // O2 fulfilled automatically via predicate, O3-O5 unaffected
      const pendingIds = state.pendingNotifications!.map((o) => o.id);
      assert.deepStrictEqual(
        pendingIds,
        ["obl_3", "obl_4", "obl_5"],
        "O2 must be fulfilled; O3-O5 must remain pending",
      );

      const o2Hist = state.obligationHistory?.find((o) => o.id === "obl_2");
      assert.ok(o2Hist, "O2 must be recorded in history");
      assert.strictEqual(o2Hist!.status, "fulfilled");
    },
  );

  // -----------------------------------------------------------------------
  // 5. Duplicate requests for O3 coalesce into O3 rather than creating O3a/O3b/O3c
  // -----------------------------------------------------------------------
  await t.step(
    "5. Duplicate requests for O3 coalesce into O3 rather than creating O3a/O3b/O3c",
    () => {
      const state: StoredState = createDefaultState();
      state.active = "test-quest";
      state.questId = "q_obl_5";

      const o3_first = createAgentObligation(state, {
        id: "obl_3",
        kind: "error",
        code: "REASSESSMENT_REQUIRED",
        message: "reassess plan due to benchmark drift",
        dedupKey: "reassessment:benchmark_drift",
      });
      queueAgentObligation(state, o3_first);

      assert.strictEqual(state.pendingNotifications?.length, 1);
      assert.strictEqual(state.pendingNotifications![0].attempts, 0);

      // Second duplicate request for same dedupKey
      const o3_second = createAgentObligation(state, {
        id: "obl_3_duplicate_a",
        kind: "error",
        code: "REASSESSMENT_REQUIRED",
        message: "reassess plan due to benchmark drift",
        dedupKey: "reassessment:benchmark_drift",
        correlationId: "corr_second",
      });
      queueAgentObligation(state, o3_second);

      // Third duplicate request
      const o3_third = createAgentObligation(state, {
        id: "obl_3_duplicate_b",
        kind: "error",
        code: "REASSESSMENT_REQUIRED",
        message: "reassess plan due to benchmark drift",
        dedupKey: "reassessment:benchmark_drift",
        correlationId: "corr_third",
      });
      queueAgentObligation(state, o3_third);

      // ASSERTIONS:
      // Queue still contains exactly 1 obligation, coalescing attempts and correlationId
      assert.strictEqual(
        state.pendingNotifications?.length,
        1,
        "Duplicate requests must coalesce into single obligation",
      );
      assert.strictEqual(
        state.pendingNotifications![0].id,
        "obl_3",
        "Original obligation ID must be preserved",
      );
      assert.strictEqual(
        state.pendingNotifications![0].attempts,
        2,
        "Attempts count must be incremented on coalescing",
      );
      assert.strictEqual(
        state.pendingNotifications![0].correlationId,
        "corr_third",
        "Latest correlationId must be captured",
      );
    },
  );

  // -----------------------------------------------------------------------
  // 6. A stale obligation is superseded only when its own causal condition is no longer applicable
  // -----------------------------------------------------------------------
  await t.step(
    "6. A stale obligation is superseded only when its own causal condition is no longer applicable",
    () => {
      const state: StoredState = createDefaultState();
      state.active = "test-quest";
      state.questId = "q_obl_6";
      state.reassessmentRequired = true;
      state.reassessmentVersion = 1;
      state.resolvedReassessmentVersion = 0;

      const o3 = createAgentObligation(state, {
        id: "obl_3",
        kind: "reassessment",
        code: "REASSESSMENT_REQUIRED",
        message: "reassess the plan",
      });
      o3.reassessmentVersion = 1;

      const o4 = createAgentObligation(state, {
        id: "obl_4",
        kind: "critical_review",
        code: "CRITICAL_REVIEW_FAILED",
        message: "perform Critical Agent review",
      });

      const o5 = createAgentObligation(state, {
        id: "obl_5",
        kind: "custom",
        code: "CUSTOM_REQUIREMENT",
        message: "custom invariant check",
      });

      queueAgentObligation(state, o3);
      queueAgentObligation(state, o4);
      queueAgentObligation(state, o5);

      assert.strictEqual(state.pendingNotifications?.length, 3);

      // Reassessment is resolved in state
      state.reassessmentRequired = false;
      state.resolvedReassessmentVersion = 1;

      // Reconcile obligations against new authoritative state
      reconcileObligations(state);

      // ASSERTIONS:
      // 1. O3 is superseded because its causal condition (reassessmentRequired) is cleared
      const pendingIds = state.pendingNotifications!.map((o) => o.id);
      assert.deepStrictEqual(
        pendingIds,
        ["obl_4", "obl_5"],
        "O3 must be superseded; O4 and O5 must remain pending",
      );

      const o3Hist = state.obligationHistory?.find((o) => o.id === "obl_3");
      assert.ok(o3Hist, "O3 must be recorded in obligationHistory");
      assert.strictEqual(o3Hist!.status, "superseded");
      assert.strictEqual(o3Hist!.superseded, true);
      assert.ok(typeof o3Hist!.supersededAt === "number");

      // 2. O4 and O5 remain pending
      assert.strictEqual(
        isObligationCurrent(o4, state),
        true,
        "O4 must still be current",
      );
      assert.strictEqual(
        isObligationCurrent(o5, state),
        true,
        "O5 must still be current",
      );
    },
  );

  // -----------------------------------------------------------------------
  // 7. Compaction/recovery preserves all still-actionable independent obligations
  // -----------------------------------------------------------------------
  await t.step(
    "7. Compaction/recovery preserves all still-actionable independent obligations",
    () => {
      const state: StoredState = createDefaultState();
      state.active = "test-quest";
      state.questId = "q_obl_7";

      const o4 = createAgentObligation(state, {
        id: "obl_4",
        kind: "critical_review",
        code: "CRITICAL_REVIEW_FAILED",
        message: "perform Critical Agent review",
        status: "delivering",
        deliverAs: "steer",
      });
      o4.attempts = 2;
      o4.deliveredAt = Date.now() - 5000;

      const o5 = createAgentObligation(state, {
        id: "obl_5",
        kind: "custom",
        code: "MIGRATION_REQUIRED",
        message: "migrate data schema v2",
        status: "pending",
        deliverAs: "followUp",
      });

      const o1_fulfilled = createAgentObligation(state, {
        id: "obl_1",
        kind: "error",
        code: "TEST_FAILED",
        message: "rerun tests",
        status: "fulfilled",
      });
      o1_fulfilled.fulfilledAt = Date.now() - 10000;
      o1_fulfilled.fulfilledReason = "tests passing";

      queueAgentObligation(state, o4);
      queueAgentObligation(state, o5);
      state.obligationHistory = [o1_fulfilled];

      // Snapshot state (persisted across compaction)
      const ctx = createMockContext(10000, "session_obl_compaction");
      setSessionState(ctx as any, state);
      const snapshot = snapshotState(ctx as any);

      // Restore session state as happens on compaction recovery
      const restored = restoreSessionState(snapshot);

      // ASSERTIONS:
      // 1. Pending actionable obligations preserved with exact properties
      assert.strictEqual(restored.pendingNotifications?.length, 2);
      const restoredO4 = restored.pendingNotifications!.find((o) =>
        o.id === "obl_4"
      );
      assert.ok(restoredO4, "O4 must be restored");
      assert.strictEqual(restoredO4!.status, "delivering");
      assert.strictEqual(restoredO4!.attempts, 2);
      assert.strictEqual(restoredO4!.code, "CRITICAL_REVIEW_FAILED");

      const restoredO5 = restored.pendingNotifications!.find((o) =>
        o.id === "obl_5"
      );
      assert.ok(restoredO5, "O5 must be restored");
      assert.strictEqual(restoredO5!.status, "pending");
      assert.strictEqual(restoredO5!.code, "MIGRATION_REQUIRED");

      // 2. Historical fulfilled obligations preserved
      assert.strictEqual(restored.obligationHistory?.length, 1);
      assert.strictEqual(restored.obligationHistory![0].id, "obl_1");
      assert.strictEqual(restored.obligationHistory![0].status, "fulfilled");
      assert.strictEqual(
        restored.obligationHistory![0].fulfilledReason,
        "tests passing",
      );
    },
  );

  // -----------------------------------------------------------------------
  // 8. Delivery does not mark obligations fulfilled
  // -----------------------------------------------------------------------
  await t.step("8. Delivery does not mark obligations fulfilled", () => {
    const api = createMockExtensionAPI();
    plugin(api.mockPi as any);
    const ctx = createMockContext(10000, "session_delivery_not_fulfill");
    const state = getState(ctx as any);
    state.active = "test-quest";
    state.questId = "q_obl_8";

    const o = createAgentObligation(state, {
      id: "obl_critical",
      kind: "critical_review",
      code: "CRITICAL_REVIEW_FAILED",
      message: "Critical finding must be remediated",
    });
    queueAgentObligation(state, o);

    assert.strictEqual(state.pendingNotifications?.length, 1);
    assert.strictEqual(state.pendingNotifications![0].status, "pending");

    // Deliver via drainAgentObligations
    drainAgentObligations(api.mockPi as any, ctx);

    // ASSERTIONS:
    // 1. Must NOT be marked fulfilled
    assert.strictEqual(
      state.pendingNotifications?.length,
      1,
      "Obligation must still be in pending queue",
    );
    assert.strictEqual(
      state.pendingNotifications![0].status,
      "delivering",
      "Status must be delivering, NOT fulfilled",
    );
    assert.notStrictEqual(
      state.pendingNotifications![0].status,
      "fulfilled",
      "Delivery must never mark obligation fulfilled",
    );

    // 2. Not in fulfilled history
    const inHistory = state.obligationHistory?.find((h) =>
      h.id === "obl_critical" && h.status === "fulfilled"
    );
    assert.strictEqual(
      inHistory,
      undefined,
      "Obligation must not be in fulfilled history merely from delivery",
    );
  });

  // -----------------------------------------------------------------------
  // 9. A fulfilled obligation remains visible in execution history
  // -----------------------------------------------------------------------
  await t.step(
    "9. A fulfilled obligation remains visible in execution history",
    () => {
      const state: StoredState = createDefaultState();
      state.active = "test-quest";
      state.questId = "q_obl_9";

      const o1 = createAgentObligation(state, {
        id: "obl_1",
        kind: "error",
        code: "TEST_FAILED",
        message: "rerun tests",
      });
      const o2 = createAgentObligation(state, {
        id: "obl_2",
        kind: "error",
        code: "COMPILER_FAILED",
        message: "investigate compiler error",
      });

      queueAgentObligation(state, o1);
      queueAgentObligation(state, o2);

      fulfillObligation(state, "obl_1", "All 42 tests passing");
      fulfillObligation(state, "obl_2", "Header syntax error resolved");

      // ASSERTIONS:
      const history = getObligationHistory(state);
      assert.strictEqual(
        history.length,
        2,
        "Both fulfilled obligations must be in history",
      );

      const h1 = history.find((h) => h.id === "obl_1");
      assert.ok(h1, "O1 must exist in history");
      assert.strictEqual(h1!.status, "fulfilled");
      assert.strictEqual(h1!.fulfilledReason, "All 42 tests passing");
      assert.strictEqual(h1!.message, "rerun tests");

      const h2 = history.find((h) => h.id === "obl_2");
      assert.ok(h2, "O2 must exist in history");
      assert.strictEqual(h2!.status, "fulfilled");
      assert.strictEqual(h2!.fulfilledReason, "Header syntax error resolved");
      assert.strictEqual(h2!.message, "investigate compiler error");
    },
  );

  // -----------------------------------------------------------------------
  // 10. A late/stale message cannot resurrect an already fulfilled or superseded obligation
  // -----------------------------------------------------------------------
  await t.step(
    "10. A late/stale message cannot resurrect an already fulfilled or superseded obligation",
    () => {
      const state: StoredState = createDefaultState();
      state.active = "test-quest";
      state.questId = "q_obl_10";

      const o1 = createAgentObligation(state, {
        id: "obl_1",
        kind: "error",
        code: "TEST_FAILED",
        message: "rerun tests",
      });
      const o2 = createAgentObligation(state, {
        id: "obl_2",
        kind: "reassessment",
        code: "REASSESSMENT_REQUIRED",
        message: "reassess plan",
      });

      queueAgentObligation(state, o1);
      queueAgentObligation(state, o2);

      // Fulfill O1 and supersede O2
      fulfillObligation(state, "obl_1", "Tests passing");
      supersedeObligation(state, "obl_2", "Plan updated");

      assert.strictEqual(
        getPendingObligations(state).length,
        0,
        "No pending obligations",
      );
      assert.strictEqual(
        state.obligationHistory?.length,
        2,
        "2 historical obligations",
      );

      // Stale transport retry or delayed message tries to queue O1 again with same ID
      const lateO1 = createAgentObligation(state, {
        id: "obl_1",
        kind: "error",
        code: "TEST_FAILED",
        message: "rerun tests",
      });
      queueAgentObligation(state, lateO1);

      // Stale message tries to queue O2 again with same ID
      const lateO2 = createAgentObligation(state, {
        id: "obl_2",
        kind: "reassessment",
        code: "REASSESSMENT_REQUIRED",
        message: "reassess plan",
      });
      queueAgentObligation(state, lateO2);

      // ASSERTIONS:
      // Neither O1 nor O2 should be resurrected into pending queue
      assert.strictEqual(
        getPendingObligations(state).length,
        0,
        "Late messages must NOT resurrect fulfilled/superseded obligations",
      );
      assert.strictEqual(
        state.pendingNotifications?.length,
        0,
        "pendingNotifications must remain empty",
      );

      // History remains intact
      const history = getObligationHistory(state);
      assert.strictEqual(history.length, 2);
      assert.strictEqual(
        history.find((h) => h.id === "obl_1")?.status,
        "fulfilled",
      );
      assert.strictEqual(
        history.find((h) => h.id === "obl_2")?.status,
        "superseded",
      );
    },
  );

  // -----------------------------------------------------------------------
  // 11. Drain idempotency - delivering obligations must not be redelivered in a loop
  // -----------------------------------------------------------------------
  await t.step(
    "11. Drain idempotency - delivering obligations must not be redelivered in a loop",
    () => {
      const api = createMockExtensionAPI();
      plugin(api.mockPi as any);
      const ctx = createMockContext(10000, "session_drain_loop");
      const state = getState(ctx as any);
      state.active = "drain-loop-quest";
      state.questId = "q_drain_loop";

      const o1 = createAgentObligation(state, {
        id: "obl_1",
        kind: "error",
        code: "TEST_FAILED",
        message: "rerun tests",
      });
      const o2 = createAgentObligation(state, {
        id: "obl_2",
        kind: "error",
        code: "COMPILER_FAILED",
        message: "investigate compiler",
      });
      const o3 = createAgentObligation(state, {
        id: "obl_3",
        kind: "error",
        code: "REASSESS",
        message: "reassess plan",
      });
      const o4 = createAgentObligation(state, {
        id: "obl_4",
        kind: "error",
        code: "REVIEW",
        message: "perform review",
      });
      const o5 = createAgentObligation(state, {
        id: "obl_5",
        kind: "error",
        code: "SAVE",
        message: "checkpoint",
      });

      queueAgentObligation(state, o1);
      queueAgentObligation(state, o2);
      queueAgentObligation(state, o3);
      queueAgentObligation(state, o4);
      queueAgentObligation(state, o5);

      const first = drainAgentObligations(api.mockPi as any, ctx);
      assert.strictEqual(first, true, "First drain must deliver");
      assert.strictEqual(
        getPendingObligations(state).length,
        0,
        "getPendingObligations must be 0 after delivery (delivering excluded)",
      );
      assert.strictEqual(
        state.pendingNotifications?.length,
        5,
        "delivering items stay in array but are not pending",
      );
      for (const obl of state.pendingNotifications!) {
        assert.strictEqual(obl.status, "delivering");
        assert.strictEqual(obl.attempts, 1);
      }
      for (let i = 0; i < 5; i++) {
        const again = drainAgentObligations(api.mockPi as any, ctx);
        assert.strictEqual(
          again,
          false,
          `Drain loop iteration ${i} must not redeliver`,
        );
        assert.strictEqual(getPendingObligations(state).length, 0);
        for (const obl of state.pendingNotifications!) {
          assert.strictEqual(
            obl.attempts,
            1,
            `attempts must stay 1 in loop iteration ${i}`,
          );
        }
      }
    },
  );
});
