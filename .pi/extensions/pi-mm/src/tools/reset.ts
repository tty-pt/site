import type { PiCtx, PiToolSpec } from "../hooks/events";
import type { EnvSource } from "./index";
import { axisPath, memDir, exportDetail } from "./index";
import { filespecFor, buildListInvocation, buildForgetInvocation } from "../qmap";
import { parseBareRefs } from "../resolve";

export function makeResetTool(envSource: EnvSource): PiToolSpec {
  return {
    name: "memory_reset",
    label: "Reset Memory",
    description: "Forget every memory: enumerate all refs and delete each on the primary and every roster axis. Idempotent (re-running is a no-op).",
    parameters: {
      type: "object",
      properties: {},
      additionalProperties: false,
    },
    execute: async (_id, _params, _signal, _onUpdate, ctx) => {
      try {
        const env = await envSource(ctx as PiCtx);
        if (env.cfg.qmapBin === "") return { content: [{ type: "text", text: "mm unavailable: qmap binary not found" }], details: { error: "no-qmap" } };

        const axes = axisPath(env);
        const cwd = memDir(env);
        const fspec = filespecFor(cwd);
        const list = await env.runner.run(buildListInvocation(env.cfg.qmapBin, fspec, axes, cwd));
        const refs = parseBareRefs(list.stdout);
        let forgotten = 0;
        for (const ref of refs) {
          const result = await env.runner.run(buildForgetInvocation(env.cfg.qmapBin, fspec, ref, axes, cwd));
          if (result.code === 0) forgotten += 1;
        }
        return exportDetail({ refsForgotten: forgotten });
      } catch (err) {
        const msg = err instanceof Error ? err.message : String(err);
        return { content: [{ type: "text", text: `mm unavailable: ${msg}` }], details: { error: msg } };
      }
    },
  };
}