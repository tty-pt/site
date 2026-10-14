import type { PiCtx, PiToolSpec } from "../hooks/events.ts";
import type { EnvSource } from "./index.ts";
import { axisPath, memDir, exportDetail, sepalConfigured } from "./index.ts";
import { filespecFor, buildGetInvocation, buildStoreInvocation, embedEnv } from "../qmap.ts";
import { decodePayload, encodePayload } from "../payload.ts";
import type { LifecycleState, LifecycleTransition } from "../types.ts";

const PERMITTED_TRANSITIONS: Record<LifecycleState, LifecycleState[]> = {
  observed: ["hypothesized", "tested", "supported", "contradicted", "superseded"],
  hypothesized: ["tested", "supported", "contradicted", "superseded"],
  tested: ["supported", "contradicted", "superseded"],
  supported: ["tested", "contradicted", "superseded"],
  contradicted: ["hypothesized", "tested", "supported", "superseded"],
  superseded: [], // terminal state
};

const LIFECYCLE_PARAMS = {
  type: "object",
  properties: {
    ref: { type: "number", description: "Target memory numeric ref." },
    lifecycle: {
      type: "string",
      enum: ["observed", "hypothesized", "tested", "supported", "contradicted", "superseded"],
      description: "New lifecycle state.",
    },
    reason: { type: "string", description: "Rationale for state transition." },
    evidenceRef: { type: "number", description: "Supporting or contradicting evidence ref." },
  },
  required: ["ref", "lifecycle"],
  additionalProperties: false,
};

function validateTransition(from: LifecycleState, to: LifecycleState, reason?: string, evRef?: number): string | null {
  if (from === to) return null;
  const allowed = PERMITTED_TRANSITIONS[from] || [];
  if (!allowed.includes(to)) {
    return `invalid transition from ${from} to ${to}`;
  }
  if ((to === "contradicted" || to === "superseded") && !reason && evRef === undefined) {
    return `transition to ${to} requires a reason or evidenceRef`;
  }
  return null;
}

async function executeLifecycle(
  envSource: EnvSource,
  params: Record<string, unknown>,
  ctx: PiCtx,
): Promise<{ content: Array<{ type: string; text?: string }>; details?: unknown }> {
  try {
    const env = await envSource(ctx);
    const ref = Number(params["ref"]);
    const nextState = params["lifecycle"] as LifecycleState;
    const reason = typeof params["reason"] === "string" ? params["reason"].trim() : undefined;
    const evidenceRef = typeof params["evidenceRef"] === "number" ? params["evidenceRef"] : undefined;

    if (!Number.isInteger(ref) || ref < 0) {
      return { content: [{ type: "text", text: "mm lifecycle: invalid ref" }], details: { error: "invalid-ref" } };
    }
    if (env.cfg.qmapBin === "") {
      return { content: [{ type: "text", text: "mm unavailable: qmap binary not found" }], details: { error: "no-qmap" } };
    }

    const embed = sepalConfigured(env.cfg);
    const cwd = memDir(env);
    const fspec = filespecFor(cwd, embed);
    const axes = axisPath(env);
    const eEnv = embedEnv(env.cfg);

    const getRes = await env.runner.run(buildGetInvocation(env.cfg.qmapBin, fspec, ref, axes, cwd, eEnv));
    if (getRes.code !== 0 || !getRes.stdout.trim()) {
      return { content: [{ type: "text", text: `mm lifecycle: ref ${ref} not found` }], details: { error: "not-found", ref } };
    }

    const { date, envelope } = decodePayload(getRes.stdout.trim());
    const fromState = envelope.lifecycle;
    const err = validateTransition(fromState, nextState, reason, evidenceRef);
    if (err) {
      return { content: [{ type: "text", text: `mm lifecycle error: ${err}` }], details: { error: err, ref } };
    }

    const transition: LifecycleTransition = {
      from: fromState,
      to: nextState,
      timestamp: env.nowProvider().toISOString(),
      reason,
      evidenceRef,
    };

    envelope.lifecycle = nextState;
    envelope.history = envelope.history ?? [];
    envelope.history.push(transition);

    const updatedPayload = encodePayload(date, envelope);
    const storeRes = await env.runner.run(buildStoreInvocation(env.cfg.qmapBin, fspec, ref, updatedPayload, axes, cwd, eEnv));
    if (storeRes.code !== 0) {
      return { content: [{ type: "text", text: `mm lifecycle update failed: ${storeRes.stderr}` }], details: { error: storeRes.stderr, ref } };
    }

    return exportDetail({
      ref,
      previousState: fromState,
      currentState: nextState,
      historyCount: envelope.history.length,
    });
  } catch (err) {
    const msg = err instanceof Error ? err.message : String(err);
    return { content: [{ type: "text", text: `mm unavailable: ${msg}` }], details: { error: msg } };
  }
}

export function makeLifecycleTool(envSource: EnvSource): PiToolSpec {
  return {
    name: "memory_lifecycle",
    label: "Update Memory Lifecycle",
    description: "Update the lifecycle state of a memory (observed -> hypothesized -> tested -> supported -> contradicted -> superseded).",
    parameters: LIFECYCLE_PARAMS,
    execute: (_id, params, _sig, _upd, ctx) => executeLifecycle(envSource, params, ctx as PiCtx),
  };
}
