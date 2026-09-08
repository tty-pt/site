import { check } from "../check.ts";
import {
  classifyTimeoutLayer,
  isModelResolutionOrProviderError,
  resolveDefaultReviewModel,
  resolveReviewThinking,
  reviewModelCandidates,
  stripThinkingSuffix,
} from "../../src/review/model.ts";
import { fakeCtx } from "../fake-pi.ts";

function clearReviewEnv(): void {
  Deno.env.delete("PI_CRITICAL_REVIEW_MODEL");
  Deno.env.delete("PI_REVIEW_MODEL");
  Deno.env.delete("PI_PROVIDER");
  Deno.env.delete("PI_MODEL");
  Deno.env.delete("PI_REVIEW_THINKING");
  Deno.env.delete("PI_CRITICAL_REVIEW_THINKING");
  Deno.env.delete("PI_CRITICAL_REVIEW_MODEL_FALLBACK");
}

Deno.test("suffix stripping removes only registered thinking levels", () => {
  check(
    stripThinkingSuffix("some/model:high") === "some/model",
    "high stripped",
  );
  check(
    stripThinkingSuffix("some/model:minimal") === "some/model",
    "minimal stripped",
  );
  check(
    stripThinkingSuffix("some/model") === "some/model",
    "bare id untouched",
  );
  check(
    stripThinkingSuffix("owner/name:custom") === "owner/name:custom",
    "unknown suffix kept",
  );
  check(
    stripThinkingSuffix("owner/name") === "owner/name",
    "slash ids untouched",
  );
});

Deno.test("default review model passes explicit env through verbatim", () => {
  clearReviewEnv();
  Deno.env.set("PI_CRITICAL_REVIEW_MODEL", "some/model:high");
  try {
    check(
      resolveDefaultReviewModel() === "some/model:high",
      "explicit env wins verbatim",
    );
  } finally {
    clearReviewEnv();
  }
});

Deno.test("default review model combines the launcher pair verbatim", () => {
  clearReviewEnv();
  Deno.env.set("PI_PROVIDER", "someprovider");
  Deno.env.set("PI_MODEL", "somemodel");
  try {
    check(
      resolveDefaultReviewModel() === "someprovider/somemodel",
      "provider pair joined without remapping",
    );
  } finally {
    clearReviewEnv();
  }
});

Deno.test("default review model reads the context model verbatim", () => {
  clearReviewEnv();
  // The structural ctx carries no model; richer hosts may attach one.
  const objectCtx = { ...fakeCtx("/tmp"), model: { provider: "someprovider", id: "somemodel" } };
  check(
    resolveDefaultReviewModel(objectCtx as never) === "someprovider/somemodel",
    "context object joined",
  );
  const stringCtx = { ...fakeCtx("/tmp"), model: "someprovider/somemodel" };
  check(
    resolveDefaultReviewModel(stringCtx as never) === "someprovider/somemodel",
    "context string kept",
  );
});

Deno.test("default review model is empty without any source", () => {
  clearReviewEnv();
  check(resolveDefaultReviewModel() === "", "empty means inherit");
});

Deno.test("review thinking inherits by default and validates env", () => {
  clearReviewEnv();
  check(resolveReviewThinking() === undefined, "no env means inherit");
  Deno.env.set("PI_REVIEW_THINKING", "off");
  check(resolveReviewThinking() === "off", "explicit level honored");
  Deno.env.set("PI_REVIEW_THINKING", "turbo");
  check(resolveReviewThinking() === undefined, "unknown level ignored");
  Deno.env.delete("PI_REVIEW_THINKING");
  Deno.env.set("PI_CRITICAL_REVIEW_THINKING", "low");
  check(resolveReviewThinking() === "low", "alias honored");
  clearReviewEnv();
});

Deno.test("model errors are recognized for retry", () => {
  check(
    isModelResolutionOrProviderError('Model "some/model:high" not found.'),
    "model not found retries",
  );
  check(isModelResolutionOrProviderError("unknown provider foo"), "unknown provider retries");
  check(!isModelResolutionOrProviderError("Subagent delegation failed (error)"), "bridge failure does not retry");
  check(!isModelResolutionOrProviderError("review cancelled: aborted"), "cancel never retries");
});

Deno.test("timeout layers classify provider failures", () => {
  check(classifyTimeoutLayer("rate limit 429") === "provider_model_timeout", "rate limit is provider");
  check(classifyTimeoutLayer("spawn ENOENT") === "child_process_deadline", "spawn is child");
  check(classifyTimeoutLayer("weird hang") === "quest_journal_deadline", "unknown is journal");
});

Deno.test("candidates order target then its stripped variant", () => {
  clearReviewEnv();
  const cands = reviewModelCandidates("some/model:high");
  check(cands.length === 2, "target plus stripped variant");
  check(cands[0].model === "some/model:high", "verbatim target first");
  check(cands[1].model === "some/model", "stripped variant second");
});

Deno.test("candidates keep unsuffixed targets and honor the fallback", () => {
  clearReviewEnv();
  const single = reviewModelCandidates("some/model");
  check(single.length === 1 && single[0].model === "some/model", "no duplicate stripped twin");
  Deno.env.set("PI_CRITICAL_REVIEW_MODEL_FALLBACK", "other/model");
  try {
    const cands = reviewModelCandidates("some/model");
    check(cands.length === 2, "fallback appended");
    check(cands[1].model === "other/model", "fallback second");
  } finally {
    clearReviewEnv();
  }
});

Deno.test("candidates degrade to a single inherit when no target", () => {
  clearReviewEnv();
  const cands = reviewModelCandidates("");
  check(cands.length === 1, "one inherit attempt");
  check(cands[0].model === undefined && cands[0].label === "inherit", "no model field sent");
});
