import type { PiExecResult } from "./hooks/events.ts";
import { levelWindow } from "./window.ts";

export const FILESPEC = "mem.db@joint,stoma:a:s";

export function filespecFor(memDir: string): string {
  return `${memDir}/mem.db@joint,stoma:a:s`;
}

export interface Invocation {
  command: string;
  args: string[];
  env: Record<string, string>;
  cwd: string;
}

export interface QmapRunner {
  run(invocation: Invocation): Promise<PiExecResult>;
}

export class ShellQmapRunner implements QmapRunner {
  constructor(
    private readonly exec: (
      command: string,
      args: string[],
      options?: { cwd?: string; env?: Record<string, string> },
    ) => Promise<PiExecResult>,
  ) {}

  run(invocation: Invocation): Promise<PiExecResult> {
    return this.exec(invocation.command, invocation.args, { cwd: invocation.cwd, env: invocation.env });
  }
}

function base(bin: string, axisLibPath: string, memDir: string): Invocation {
  return { command: bin, args: [], env: { QMAP_AXIS_PATH: axisLibPath }, cwd: memDir };
}

export function buildStoreInvocation(
  bin: string,
  filespec: string,
  ref: number,
  payload: string,
  axisLibPath: string,
  memDir: string,
): Invocation {
  const inv = base(bin, axisLibPath, memDir);
  inv.args = ["-p", `${ref}:${payload}`, filespec];
  return inv;
}

export function buildScanInvocation(
  bin: string,
  filespec: string,
  expr: string,
  limit: number,
  axisLibPath: string,
  memDir: string,
): Invocation {
  const inv = base(bin, axisLibPath, memDir);
  inv.args = ["-X", expr, "-g", ".", filespec, "-t", String(limit)];
  return inv;
}

export function buildListInvocation(bin: string, filespec: string, axisLibPath: string, memDir: string): Invocation {
  const inv = base(bin, axisLibPath, memDir);
  inv.args = ["-g", ".", filespec];
  return inv;
}

export function buildGetInvocation(
  bin: string,
  filespec: string,
  ref: number,
  axisLibPath: string,
  memDir: string,
): Invocation {
  const inv = base(bin, axisLibPath, memDir);
  inv.args = ["-r", "-g", String(ref), filespec];
  return inv;
}

export function buildForgetInvocation(
  bin: string,
  filespec: string,
  ref: number,
  axisLibPath: string,
  memDir: string,
): Invocation {
  const inv = base(bin, axisLibPath, memDir);
  inv.args = ["-d", String(ref), filespec];
  return inv;
}

export function scanExpr(topic: string, level: number, now: Date): string {
  const stoma = `stoma="field=text query=${topic} matched=1"`;
  const window = levelWindow(level, now);
  if (window === null) return stoma;
  return `(joint="a=${window.a} b=${window.b}" AND ${stoma})`;
}

export interface ResultLine {
  ref: number;
  score?: string;
  record: string;
}

export function parseResultLines(stdout: string): ResultLine[] {
  const lines: ResultLine[] = [];
  for (const raw of stdout.split("\n")) {
    const line = raw.replace(/\r$/, "");
    if (line.trim() === "") continue;
    const scored = /^(\d+)\s+([0-9]+(?:\.[0-9]+)?)\s+([\s\S]*)$/.exec(line);
    if (scored) {
      lines.push({ ref: Number(scored[1]), score: scored[2], record: scored[3].trim() });
      continue;
    }
    const plain = /^(\d+)\s+([\s\S]*)$/.exec(line);
    if (plain) lines.push({ ref: Number(plain[1]), record: plain[2].trim() });
  }
  return lines;
}

export function payloadDate(timestamp: string | undefined, now: Date): string {
  if (typeof timestamp === "string" && /^\d{4}-\d{2}-\d{2}/.test(timestamp)) return timestamp.slice(0, 10);
  return toISODateLocal(now);
}

function toISODateLocal(d: Date): string {
  const y = d.getFullYear();
  const m = String(d.getMonth() + 1).padStart(2, "0");
  const day = String(d.getDate()).padStart(2, "0");
  return `${y}-${m}-${day}`;
}