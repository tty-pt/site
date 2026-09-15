import { embedQuery, cleanupEmbed } from "../src/embed.ts";
import type { ExecFn } from "../src/qmap.ts";
import { SEPAL_VEC_MAX } from "../src/qmap.ts";
import { existsSync, readFileSync, unlinkSync } from "node:fs";

function check(cond: boolean, msg: string): void {
  if (!cond) throw new Error(msg);
}

const URL = "http://localhost:4242/v1/embeddings";
const MODEL = "nomic-embed-text";

interface Capture {
  command: string;
  args: string[];
  options?: { cwd?: string; env?: Record<string, string> };
}

function execCapture(
  respond: (command: string, args: string[]) => { stdout: string; stderr: string; code: number },
): { fn: ExecFn; calls: Capture[] } {
  const calls: Capture[] = [];
  const fn: ExecFn = (command, args, options) => {
    calls.push({ command, args, options });
    return Promise.resolve(respond(command, args));
  };
  return { fn, calls };
}

function openAIResp(embedding: number[]): string {
  return JSON.stringify({ data: [{ embedding }] });
}

Deno.test("embedQuery: curl args byte-pinned (POST + JSON body), temp vec written as LE float32", async () => {
  const { fn, calls } = execCapture((_c, _a) => ({ stdout: openAIResp([0.1, 0.2, 0.3]), stderr: "", code: 0 }));
  const result = await embedQuery(URL, MODEL, "beacon", fn);
  check(result !== null, "embed result present");
  check(calls.length === 1, "one exec call");
  const c = calls[0];
  check(c.command === "curl", "curl binary");
  check(c.args[0] === "-sS" && c.args[1] === "-X" && c.args[2] === "POST", "quiet + POST");
  check(c.args[3] === URL, "url passthrough");
  const bodyIdx = c.args.indexOf("-d");
  check(bodyIdx > 0, "-d present");
  check(c.args[bodyIdx + 1].includes(`"model":"${MODEL}"`), "model in body");
  check(c.args[bodyIdx + 1].includes('"input":"beacon"'), "input in body");
  check(c.args.includes("Content-Type: application/json"), "json content type");
  const qdim = result?.qdim ?? 0;
  check(qdim === 3, `qdim 3, got ${qdim}`);
  const path = result?.vecFile ?? "";
  check(existsSync(path), "vec file exists");
  const bytes = new Uint8Array(readFileSync(path));
  check(bytes.length === 12, `12 bytes for 3 floats, got ${bytes.length}`);
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  check(Math.abs(view.getFloat32(0, true) - 0.1) < 1e-6, "float32 LE slot 0");
  check(Math.abs(view.getFloat32(4, true) - 0.2) < 1e-6, "float32 LE slot 1");
  check(Math.abs(view.getFloat32(8, true) - 0.3) < 1e-6, "float32 LE slot 2");
  unlinkSync(path);
});

Deno.test("embedQuery: nonzero curl → null", async () => {
  const { fn, calls } = execCapture(() => ({ stdout: "", stderr: "connection refused", code: 7 }));
  const result = await embedQuery(URL, MODEL, "beacon", fn);
  check(result === null, "null on curl failure");
  check(calls.length === 1, "curl attempted");
});

Deno.test("embedQuery: no embedding key in response → null", async () => {
  const { fn } = execCapture(() => ({ stdout: JSON.stringify({ data: [{ what: 1 }] }), stderr: "", code: 0 }));
  const result = await embedQuery(URL, MODEL, "beacon", fn);
  check(result === null, "null when embedding absent");
});

Deno.test("embedQuery: malformed JSON → null", async () => {
  const { fn } = execCapture(() => ({ stdout: "not json at all", stderr: "", code: 0 }));
  const result = await embedQuery(URL, MODEL, "beacon", fn);
  check(result === null, "null on parse failure");
});

Deno.test("embedQuery: dim > SEPAL_VEC_MAX → null", async () => {
  const big = Array.from({ length: SEPAL_VEC_MAX + 2 }, (_, i) => (i % 1000) / 1000);
  const { fn } = execCapture(() => ({ stdout: openAIResp(big), stderr: "", code: 0 }));
  const result = await embedQuery(URL, MODEL, "beacon", fn);
  check(result === null, "null on overflow");
});

Deno.test("cleanupEmbed: best-effort removes present files, ignores missing", async () => {
  const tmp = await Deno.makeTempFile({ prefix: "mm-embed-cleanup-" });
  check(existsSync(tmp), "temp file written");
  await cleanupEmbed([tmp]);
  check(!existsSync(tmp), "file removed");
  await cleanupEmbed([tmp, "/nonexistent/mm-embed-xyz.bin"]);
  await cleanupEmbed([]);
});