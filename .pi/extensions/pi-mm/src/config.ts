import { readFile } from "node:fs/promises";
import { join } from "node:path";

export interface MmConfig {
  qmapBin: string;
  axisLibs: string | null;
  memDir: string;
  scanLimit: number;
}

export const DEFAULT_CONFIG: MmConfig = {
  qmapBin: "",
  axisLibs: null,
  memDir: ".pi/mm",
  scanLimit: 10,
};

export type QmapSource = "settings" | "env" | "path" | "insite" | "none";

export interface QmapResolution {
  source: QmapSource;
  path: string;
}

export function loadConfig(raw: unknown): MmConfig {
  if (typeof raw !== "object" || raw === null) return DEFAULT_CONFIG;
  const record = raw as Record<string, unknown>;
  const bin = record["qmapBin"];
  const libs = record["axisLibs"];
  const dir = record["memDir"];
  const limit = record["scanLimit"];
  return {
    qmapBin: typeof bin === "string" && bin.trim() !== "" ? bin.trim() : DEFAULT_CONFIG.qmapBin,
    axisLibs: typeof libs === "string" && libs.trim() !== "" ? libs.trim() : DEFAULT_CONFIG.axisLibs,
    memDir: typeof dir === "string" && dir.trim() !== "" ? dir.trim() : DEFAULT_CONFIG.memDir,
    scanLimit: typeof limit === "number" && Number.isInteger(limit) && limit > 0 ? limit : DEFAULT_CONFIG.scanLimit,
  };
}

export function applyEnv(cfg: MmConfig, env: Record<string, string | undefined>): MmConfig {
  const envBin = env["QMAP_BIN"];
  const envLibs = env["QMAP_AXIS_PATH"];
  return {
    qmapBin: cfg.qmapBin === "" && typeof envBin === "string" && envBin.trim() !== "" ? envBin.trim() : cfg.qmapBin,
    axisLibs: cfg.axisLibs === null && typeof envLibs === "string" && envLibs.trim() !== "" ? envLibs.trim() : cfg.axisLibs,
    memDir: cfg.memDir,
    scanLimit: cfg.scanLimit,
  };
}

export function resolveQmapBin(
  cfg: MmConfig,
  cwd: string,
  pathDirs: string[],
  probe: (p: string) => boolean,
): QmapResolution {
  if (cfg.qmapBin !== "") return { source: "settings", path: cfg.qmapBin };
  for (const dir of pathDirs) {
    const candidate = join(dir, "qmap");
    if (probe(candidate)) return { source: "path", path: candidate };
  }
  const inSite = join(cwd, "external", "libqmap", "bin", "qmap");
  if (probe(inSite)) return { source: "insite", path: inSite };
  return { source: "none", path: "" };
}

export function resolveAxisPath(cfg: MmConfig, cwd: string): string {
  if (cfg.axisLibs !== null) return cfg.axisLibs;
  return join(cwd, "external", "libjoint", "lib") + ":" + join(cwd, "external", "libstoma", "lib");
}

export async function readMmConfig(cwd: string, env: Record<string, string | undefined> = {}): Promise<MmConfig> {
  let raw: unknown;
  try {
    const text = await readFile(join(cwd, ".pi", "settings.json"), "utf8");
    const parsed: unknown = JSON.parse(text);
    raw = typeof parsed === "object" && parsed !== null
      ? (parsed as Record<string, unknown>)["pi-mm"]
      : undefined;
  } catch {
    raw = undefined;
  }
  return applyEnv(loadConfig(raw), { ...Deno.env.toObject(), ...env });
}