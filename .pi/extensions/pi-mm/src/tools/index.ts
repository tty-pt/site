import type { Pi, PiCtx, PiToolSpec } from "../hooks/events";
import type { MmConfig } from "../config";
import { readMmConfig, resolveAxisPath, sepalConfigured } from "../config";
import type { ExecFn, QmapRunner } from "../qmap";
import { ShellQmapRunner } from "../qmap";
import { makeStoreTool } from "./store";
import { makeScanTool } from "./scan";
import { makeThinkTool } from "./think";
import { makeForgetTool } from "./forget";
import { makeResetTool } from "./reset";

export { makeStoreTool, makeScanTool, makeThinkTool, makeForgetTool, makeResetTool };
export { sepalConfigured };

export interface ToolEnv {
  cwd: string;
  cfg: MmConfig;
  runner: QmapRunner;
  exec: ExecFn;
  nowProvider: () => Date;
}

export type EnvSource = (ctx: PiCtx) => Promise<ToolEnv>;

export function give(env: ToolEnv): EnvSource {
  return () => Promise.resolve(env);
}

export async function readToolEnv(cwd: string, runner: QmapRunner, exec: ExecFn): Promise<ToolEnv> {
  const cfg = await readMmConfig(cwd);
  return { cwd, cfg, runner, exec, nowProvider: () => new Date() };
}

export function defaultEnvSource(pi: Pi): EnvSource {
  const runner = new ShellQmapRunner((command, args, options) => pi.exec(command, args, options));
  return (ctx) => readToolEnv(ctx.cwd, runner, (command, args, options) => pi.exec(command, args, options));
}

export function axisPath(env: ToolEnv): string {
  return resolveAxisPath(env.cfg, env.cwd);
}

export function memDir(env: ToolEnv): string {
  return env.cwd + "/" + env.cfg.memDir;
}

export function exportDetail(content: unknown): { content: Array<{ type: string; text?: string }>; details: unknown } {
  return { content: [{ type: "text" }], details: content };
}

export function installTools(pi: Pi, env: EnvSource = defaultEnvSource(pi)): void {
  const tools: PiToolSpec[] = [
    makeStoreTool(env),
    makeScanTool(env),
    makeThinkTool(env),
    makeForgetTool(env),
    makeResetTool(env),
  ];
  for (const tool of tools) pi.registerTool(tool);
}