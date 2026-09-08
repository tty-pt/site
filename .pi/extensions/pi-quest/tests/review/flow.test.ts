import { check } from "../check.ts";
import { getState, replaceState } from "../../src/app/store.ts";
import { createDraft, createQuest } from "../../src/domain/quest.ts";
import type { Qid } from "../../src/domain/qid.ts";
import type { Pi } from "../../src/hooks/events.ts";
import { fakeCtx, fakePi } from "../fake-pi.ts";
import { resetNoticedReviews, reviewRunningNotice, runIsolatedReview, shouldNoticeReview } from "../../src/review/flow.ts";

function clearReviewEnv(): void {
  Deno.env.delete("PI_CRITICAL_REVIEW_MODEL");
  Deno.env.delete("PI_REVIEW_MODEL");
  Deno.env.delete("PI_PROVIDER");
  Deno.env.delete("PI_MODEL");
  Deno.env.delete("PI_REVIEW_THINKING");
  Deno.env.delete("PI_CRITICAL_REVIEW_THINKING");
  Deno.env.delete("PI_CRITICAL_REVIEW_MODEL_FALLBACK");
}

Deno.test("running notice orders end-of-turn and names the quest", () => {
  const text = reviewRunningNotice("abc123");
  check(text.includes("abc123"), "qid named");
  check(text.includes("end your turn"), "end-turn order kept");
  check(text.includes("verdict arrives as a new turn"), "wait semantics kept");
  check(text.length < 120, "token-cheap");
});

Deno.test("running notice fires once per review target", () => {
  check(shouldNoticeReview("abc123", "hash-a"), "first boot noticed");
  check(!shouldNoticeReview("abc123", "hash-a"), "repeat boot silent");
  check(shouldNoticeReview("abc123", "hash-b"), "new target noticed");
  check(shouldNoticeReview("def456", "hash-a"), "other quest noticed");
});

function flowBus(): { pi: Pi; emitted: Array<{ event: string; data: unknown }>; feed: (data: unknown) => void } {
  const pi = fakePi();
  pi.toolNames = ["subagent"];
  const emitted: Array<{ event: string; data: unknown }> = [];
  const handlers = new Map<string, Array<(data: unknown) => void>>();
  (pi as { events: unknown }).events = {
    on: (event: string, handler: (data: unknown) => void) => {
      const list = handlers.get(event) ?? [];
      list.push(handler);
      handlers.set(event, list);
      return () => {};
    },
    emit: (event: string, data: unknown) => {
      emitted.push({ event, data });
    },
  };
  const feed = (data: unknown) => {
    for (const handler of handlers.get("prompt-template:subagent:response") ?? []) handler(data);
  };
  return { pi, emitted, feed };
}

function requests(emitted: Array<{ event: string; data: unknown }>): Array<Record<string, unknown>> {
  return emitted
    .filter((e) => e.event === "prompt-template:subagent:request")
    .map((e) => e.data as Record<string, unknown>);
}

async function waitForRequests(
  emitted: Array<{ event: string; data: unknown }>,
  count: number,
): Promise<void> {
  for (let i = 0; i < 200 && requests(emitted).length < count; i += 1) {
    await new Promise((r) => setTimeout(r, 5));
  }
  check(requests(emitted).length >= count, `expected ${count} review attempts`);
}

const MODEL_NOT_FOUND =
  'Model "some/model:high" not found. Use --list-models to see available models.';

Deno.test("model-not-found retries on the stripped variant and records the verdict", async () => {
  clearReviewEnv();
  resetNoticedReviews();
  const qid = "fb0001" as Qid;
  replaceState(createDraft(createQuest("req", qid), "mat"));
  const { pi, emitted, feed } = flowBus();
  const pending = runIsolatedReview({
    pi,
    ctx: fakeCtx("/tmp"),
    qid,
    target: "t1",
    prompt: "review this",
    model: "some/model:high",
  });
  await waitForRequests(emitted, 1);
  feed({ requestId: requests(emitted)[0]["requestId"], status: "error", error: MODEL_NOT_FOUND });
  await waitForRequests(emitted, 2);
  const second = requests(emitted)[1];
  check(second["model"] === "some/model", "retry uses the suffix-stripped id");
  feed({ requestId: second["requestId"], status: "completed", result: { kind: "text", text: "VERDICT: PASS" } });
  const outcome = await pending;
  check(outcome.status === "verdict", "verdict after retry");
  check(getState().lastReview?.verdict === "PASS", "verdict recorded");
});

Deno.test("inherit launches send no model field", async () => {
  clearReviewEnv();
  resetNoticedReviews();
  const qid = "fb0004" as Qid;
  replaceState(createDraft(createQuest("req", qid), "mat"));
  const { pi, emitted, feed } = flowBus();
  const pending = runIsolatedReview({
    pi,
    ctx: fakeCtx("/tmp"),
    qid,
    target: "t4",
    prompt: "review this",
  });
  await waitForRequests(emitted, 1);
  check(!("model" in requests(emitted)[0]), "inherit omits the model field");
  feed({ requestId: requests(emitted)[0]["requestId"], status: "completed", result: { kind: "text", text: "VERDICT: PASS" } });
  const outcome = await pending;
  check(outcome.status === "verdict", "inherit verdict recorded");
});

Deno.test("thinking reaches the delegation only when resolved", async () => {
  clearReviewEnv();
  resetNoticedReviews();
  const qid = "fb0005" as Qid;
  replaceState(createDraft(createQuest("req", qid), "mat"));
  const { pi, emitted, feed } = flowBus();
  const pending = runIsolatedReview({
    pi,
    ctx: fakeCtx("/tmp"),
    qid,
    target: "t5",
    prompt: "review this",
    thinking: "off",
  });
  await waitForRequests(emitted, 1);
  check(requests(emitted)[0]["thinking"] === "off", "explicit thinking sent");
  feed({ requestId: requests(emitted)[0]["requestId"], status: "completed", result: { kind: "text", text: "VERDICT: PASS" } });
  const outcome = await pending;
  check(outcome.status === "verdict", "thinking verdict recorded");
});

Deno.test("the reviewer's verbatim text is recorded with the verdict", async () => {
  clearReviewEnv();
  resetNoticedReviews();
  const qid = "fb0007" as Qid;
  replaceState(createDraft(createQuest("req", qid), "mat"));
  const { pi, emitted, feed } = flowBus();
  const pending = runIsolatedReview({
    pi,
    ctx: fakeCtx("/tmp"),
    qid,
    target: "t7",
    prompt: "review this",
  });
  await waitForRequests(emitted, 1);
  const raw = "VERDICT: FAIL\nSEVERITY: MAJOR\nFINDINGS:\n- Issue: double-free\n  Evidence: respond_html frees page\n\nREQUIRED REVISIONS:\n- remove the trailing free";
  feed({ requestId: requests(emitted)[0]["requestId"], status: "completed", result: { kind: "text", text: raw } });
  const outcome = await pending;
  check(outcome.status === "verdict", "verdict outcome");
  check(outcome.status === "verdict" && outcome.review.text.includes("double-free"), "outcome carries the raw text");
  check(getState().lastReview?.findings.includes("double-free") === true, "findings recorded");
  check(getState().lastReview?.reviewText?.includes("REQUIRED REVISIONS") === true, "review text recorded in state");
});

Deno.test("exhausted candidates report failure", async () => {
  clearReviewEnv();
  resetNoticedReviews();
  const qid = "fb0002" as Qid;
  replaceState(createDraft(createQuest("req", qid), "mat"));
  const { pi, emitted, feed } = flowBus();
  const pending = runIsolatedReview({
    pi,
    ctx: fakeCtx("/tmp"),
    qid,
    target: "t2",
    prompt: "review this",
    model: "some/model:high",
  });
  for (let attempt = 1; attempt <= 2; attempt += 1) {
    await waitForRequests(emitted, attempt);
    const req = requests(emitted)[attempt - 1];
    feed({ requestId: req["requestId"], status: "error", error: MODEL_NOT_FOUND });
  }
  const outcome = await pending;
  check(outcome.status === "failed", "all candidates exhausted");
  check(requests(emitted).length === 2, "target plus stripped variant attempted");
});

Deno.test("operator fallback appends after the stripped variant", async () => {
  clearReviewEnv();
  Deno.env.set("PI_CRITICAL_REVIEW_MODEL_FALLBACK", "other/model");
  resetNoticedReviews();
  const qid = "fb0006" as Qid;
  replaceState(createDraft(createQuest("req", qid), "mat"));
  const { pi, emitted, feed } = flowBus();
  const pending = runIsolatedReview({
    pi,
    ctx: fakeCtx("/tmp"),
    qid,
    target: "t6",
    prompt: "review this",
    model: "some/model:high",
  });
  await waitForRequests(emitted, 1);
  feed({ requestId: requests(emitted)[0]["requestId"], status: "error", error: MODEL_NOT_FOUND });
  await waitForRequests(emitted, 2);
  feed({ requestId: requests(emitted)[1]["requestId"], status: "error", error: MODEL_NOT_FOUND });
  await waitForRequests(emitted, 3);
  check(requests(emitted)[2]["model"] === "other/model", "fallback last");
  feed({ requestId: requests(emitted)[2]["requestId"], status: "completed", result: { kind: "text", text: "VERDICT: PASS" } });
  const outcome = await pending;
  check(outcome.status === "verdict", "fallback verdict recorded");
  clearReviewEnv();
});

Deno.test("non-model failures do not retry", async () => {
  clearReviewEnv();
  resetNoticedReviews();
  const qid = "fb0003" as Qid;
  replaceState(createDraft(createQuest("req", qid), "mat"));
  const { pi, emitted, feed } = flowBus();
  const pending = runIsolatedReview({
    pi,
    ctx: fakeCtx("/tmp"),
    qid,
    target: "t3",
    prompt: "review this",
    model: "some/model:high",
  });
  await waitForRequests(emitted, 1);
  feed({ requestId: requests(emitted)[0]["requestId"], status: "error", error: "boom" });
  const outcome = await pending;
  check(outcome.status === "failed", "bridge failure surfaces");
  await new Promise((r) => setTimeout(r, 20));
  check(requests(emitted).length === 1, "no retry on non-model failure");
});
