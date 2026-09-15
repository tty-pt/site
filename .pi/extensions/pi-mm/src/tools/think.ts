import type { PiCtx, PiToolSpec } from "../hooks/events";
import type { EnvSource } from "./index";
import { axisPath, memDir, exportDetail, sepalConfigured } from "./index";
import { filespecFor, buildGetInvocation, embedEnv } from "../qmap";
import { isNumericRef } from "../resolve";
import { scanTopRef } from "./scan";

export function splitPayload(payload: string): { date: string; text: string } {
  const m = /^(\d{4}-\d{2}-\d{2}(?:T[0-9:.]+)?):([\s\S]*)$/.exec(payload);
  if (m) return { date: m[1], text: m[2].trimStart() };
  const idx = payload.indexOf(":");
  if (idx < 0) return { date: "", text: payload };
  return { date: payload.slice(0, idx), text: payload.slice(idx + 1) };
}

export function makeThinkTool(envSource: EnvSource): PiToolSpec {
  return {
    name: "memory_think",
    label: "Think Memory",
    description: "Recall the stored payload for a key (numeric ref or topic resolved by a top-1 scan). extract ∈ date|text|whole (default whole).",
    parameters: {
      type: "object",
      properties: {
        key: { type: "string", description: "Numeric ref or a topic to resolve." },
        extract: { type: "string", description: "date | text | whole (default whole)." },
      },
      required: ["key"],
      additionalProperties: false,
    },
    execute: async (_id, params, _signal, _onUpdate, ctx) => {
      try {
        const env = await envSource(ctx as PiCtx);
        const key = typeof params["key"] === "string" ? params["key"].trim() : "";
        if (key === "") return { content: [{ type: "text", text: "mm think: empty key (degradation: soft)" }], details: { error: "empty-key" } };
        if (env.cfg.qmapBin === "") return { content: [{ type: "text", text: "mm unavailable: qmap binary not found" }], details: { error: "no-qmap" } };

        const ref = isNumericRef(key)
          ? Number(key)
          : await scanTopRef(envSource, ctx as PiCtx, key);
        if (ref === null) return { content: [{ type: "text", text: `mm think: no memory matches ${key}` }], details: { error: "no-match" } };

        const embed = sepalConfigured(env.cfg);
        const result = await env.runner.run(buildGetInvocation(env.cfg.qmapBin, filespecFor(memDir(env), embed), ref, axisPath(env), memDir(env), embedEnv(env.cfg)));
        const payload = result.code === 0 ? result.stdout.trim() : "";
        if (payload === "") {
          return { content: [{ type: "text", text: `mm think: ref ${ref} returned nothing` }], details: { ref, error: "empty-get" } };
        }
        const { date, text } = splitPayload(payload);
        const extract = typeof params["extract"] === "string" ? params["extract"] : "whole";
        const detail: Record<string, string | number> = { ref };
        if (extract === "date") detail["date"] = date;
        else if (extract === "text") detail["text"] = text;
        else {
          detail["date"] = date;
          detail["text"] = text;
          detail["payload"] = payload;
        }
        return exportDetail(detail);
      } catch (err) {
        const msg = err instanceof Error ? err.message : String(err);
        return { content: [{ type: "text", text: `mm unavailable: ${msg}` }], details: { error: msg } };
      }
    },
  };
}