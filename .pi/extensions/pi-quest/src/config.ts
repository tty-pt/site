// HIGH_LEVEL: #configurations — four settings under "pi-quest", all optional.
// HIGH_LEVEL: #interfaces — bindings select the peer tools; built-ins apply otherwise.
import { readFile } from "node:fs/promises";
import { join } from "node:path";
export interface DraftThresholds {
  requirements: number;
  evidence: number;
}

export interface InterfaceBindings {
  asking: { tool: string };
  reviewRunner: { tool: string };
}

export interface QuestConfig {
  askTimeoutMs: number;
  depthCap: number;
  draftThresholds: DraftThresholds;
  bindings: InterfaceBindings;
}

export const DEFAULT_CONFIG: QuestConfig = {
  askTimeoutMs: 60000,
  depthCap: 3,
  draftThresholds: { requirements: 2, evidence: 7 },
  bindings: { asking: { tool: "ask_questions" }, reviewRunner: { tool: "subagent" } },
};

export function loadConfig(raw: unknown): QuestConfig {
  if (typeof raw !== "object" || raw === null) return DEFAULT_CONFIG;
  const record = raw as Record<string, unknown>;
  const timeout = record["askTimeoutMs"];
  const depth = record["depthCap"];
  const thresholds = record["draftThresholds"] as Record<string, unknown> | undefined;
  const bindings = record["bindings"] as Record<string, unknown> | undefined;
  return {
    askTimeoutMs: typeof timeout === "number" ? timeout : DEFAULT_CONFIG.askTimeoutMs,
    depthCap: typeof depth === "number" ? depth : DEFAULT_CONFIG.depthCap,
    draftThresholds: {
      requirements: typeof thresholds?.["requirements"] === "number"
        ? thresholds["requirements"] as number
        : DEFAULT_CONFIG.draftThresholds.requirements,
      evidence: typeof thresholds?.["evidence"] === "number"
        ? thresholds["evidence"] as number
        : DEFAULT_CONFIG.draftThresholds.evidence,
    },
    bindings: {
      asking: {
        tool: typeof (bindings?.["asking"] as Record<string, unknown> | undefined)?.["tool"] === "string"
          ? (bindings?.["asking"] as Record<string, unknown>)["tool"] as string
          : DEFAULT_CONFIG.bindings.asking.tool,
      },
      reviewRunner: {
        tool:
          typeof (bindings?.["reviewRunner"] as Record<string, unknown> | undefined)?.["tool"] === "string"
            ? (bindings?.["reviewRunner"] as Record<string, unknown>)["tool"] as string
            : DEFAULT_CONFIG.bindings.reviewRunner.tool,
      },
    },
  };
}

export async function readQuestConfig(cwd: string): Promise<QuestConfig> {
  try {
    const raw = await readFile(join(cwd, ".pi", "settings.json"), "utf8");
    const parsed: unknown = JSON.parse(raw);
    if (typeof parsed !== "object" || parsed === null) return DEFAULT_CONFIG;
    return loadConfig((parsed as Record<string, unknown>)["pi-quest"]);
  } catch {
    return DEFAULT_CONFIG;
  }
}
