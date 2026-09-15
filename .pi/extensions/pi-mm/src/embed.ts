import type { ExecFn } from "./qmap.ts";
import { SEPAL_VEC_MAX } from "./qmap.ts";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { rm } from "node:fs/promises";

let embedCounter = 0;

export interface EmbedVector {
  vecFile: string;
  qdim: number;
}

function writeLeFloat32(path: string, floats: number[]): void {
  const buf = new ArrayBuffer(floats.length * 4);
  const view = new DataView(buf);
  for (let i = 0; i < floats.length; i++) view.setFloat32(i * 4, floats[i], true);
  Deno.writeFileSync(path, new Uint8Array(buf));
}

function extractEmbedding(stdout: string): number[] | null {
  let data: unknown;
  try {
    data = JSON.parse(stdout);
  } catch {
    return null;
  }
  if (typeof data !== "object" || data === null) return null;
  const rec = data as { data?: unknown };
  if (!Array.isArray(rec.data) || rec.data.length === 0) return null;
  const first = rec.data[0] as { embedding?: unknown };
  if (!Array.isArray(first.embedding)) return null;
  const floats: number[] = [];
  for (const item of first.embedding) {
    const n = typeof item === "number" ? item : typeof item === "string" ? Number(item) : NaN;
    if (!Number.isFinite(n)) return null;
    floats.push(n);
  }
  return floats;
}

export async function embedQuery(
  url: string,
  model: string,
  text: string,
  exec: ExecFn,
): Promise<EmbedVector | null> {
  if (text === "") return null;
  const body = JSON.stringify({ model, input: text });
  const result = await exec("curl", ["-sS", "-X", "POST", url, "-H", "Content-Type: application/json", "-d", body]);
  if (result.code !== 0) return null;
  const floats = extractEmbedding(result.stdout);
  if (floats === null || floats.length === 0 || floats.length > SEPAL_VEC_MAX) return null;
  embedCounter += 1;
  const vecFile = join(tmpdir(), `mm-embed-${Date.now()}-${embedCounter}.bin`);
  writeLeFloat32(vecFile, floats);
  return { vecFile, qdim: floats.length };
}

export async function cleanupEmbed(files: string[]): Promise<void> {
  for (const file of files) {
    try {
      await rm(file, { force: true });
    } catch {
      /* best-effort: stray temp vectors are harmless */
    }
  }
}