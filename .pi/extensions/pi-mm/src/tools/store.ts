import type { PiCtx, PiToolSpec } from "../hooks/events";
import type { EnvSource } from "./index";
import { axisPath, memDir, exportDetail } from "./index";
import { filespecFor, buildStoreInvocation, buildListInvocation } from "../qmap";
import { parseBareRefs, nextRef } from "../resolve";
import { payloadDate } from "../qmap";

export function makeStoreTool(envSource: EnvSource): PiToolSpec {
  return {
    name: "memory_store",
    label: "Store Memory",
    description: "Store a text memory at a generated ref. timestamp defaults to now (YYYY-MM-DD). Returns the ref and @key.",
    parameters: {
      type: "object",
      properties: {
        text: { type: "string", description: "Memory content." },
        timestamp: { type: "string", description: "ISO timestamp (used as DATE prefix); default now." },
      },
      required: ["text"],
      additionalProperties: false,
    },
    execute: async (_id, params, _signal, _onUpdate, ctx) => {
      try {
        const env = await envSource(ctx as PiCtx);
        const text = typeof params["text"] === "string" ? params["text"].trim() : "";
        if (text === "") return { content: [{ type: "text", text: "mm: store requires non-empty text (degradation: soft)" }], details: { error: "empty-text" } };
        const ts = params["timestamp"];
        const date = payloadDate(typeof ts === "string" ? ts : undefined, env.nowProvider());
        const axes = axisPath(env);
        const cwd = memDir(env);
        const runner = env.runner;
        if (env.cfg.qmapBin === "") return { content: [{ type: "text", text: `mm unavailable: qmap binary not found (configured: empty; in-site probe failed under ${cwd})` }], details: { error: "no-qmap" } };

        const fspec = filespecFor(cwd);
        const list = await runner.run(buildListInvocation(env.cfg.qmapBin, fspec, axes, cwd));
        const refs = parseBareRefs(list.stdout);
        const ref = nextRef(refs);
        const payload = `${date}:${text}`;
        const result = await runner.run(buildStoreInvocation(env.cfg.qmapBin, fspec, ref, payload, axes, cwd));
        if (result.code !== 0) {
          return { content: [{ type: "text", text: `mm store failed (exit ${result.code}): ${result.stderr || result.stdout}` }], details: { ref, key: `@${date}`, error: result.stderr || result.stdout } };
        }
        return exportDetail({ ref, key: `@${date}` });
      } catch (err) {
        const msg = err instanceof Error ? err.message : String(err);
        return { content: [{ type: "text", text: `mm unavailable: ${msg}` }], details: { error: msg } };
      }
    },
  };
}