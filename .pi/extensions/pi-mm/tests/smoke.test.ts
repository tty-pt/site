import install from "../index.ts";
import type { Pi, PiToolSpec } from "../src/hooks/events.ts";

function check(cond: boolean, msg: string): void {
  if (!cond) throw new Error(msg);
}

function fakePi(): Pi & { tools: PiToolSpec[] } {
  const tools: PiToolSpec[] = [];
  const pi = {
    tools,
    on(): void {},
    appendEntry(): void {},
    registerTool(tool: PiToolSpec): void { tools.push(tool); },
    exec(): Promise<{ stdout: string; stderr: string; code: number }> {
      return Promise.resolve({ stdout: "", stderr: "", code: 0 });
    },
  };
  return pi as Pi & { tools: PiToolSpec[] };
}

Deno.test("manifest declares the pi extension entry point", async () => {
  const raw = await Deno.readTextFile(new URL("../package.json", import.meta.url));
  const pkg = JSON.parse(raw);
  const exts: unknown = pkg?.pi?.extensions;
  check(Array.isArray(exts) && exts.includes("index.ts"), "pi.extensions must include index.ts");
});

Deno.test("manifest declares the pi-mm skill", async () => {
  const raw = await Deno.readTextFile(new URL("../package.json", import.meta.url));
  const pkg = JSON.parse(raw);
  const skills: unknown = pkg?.pi?.skills;
  check(Array.isArray(skills) && skills.includes("skills/pi-mm/SKILL.md"), "pi.skills must include skills/pi-mm/SKILL.md");
});

Deno.test("entry default-exports an installer function", () => {
  check(typeof install === "function", "default export must be a function");
});

Deno.test("installer registers the five memory tools", () => {
  const api = fakePi();
  install(api);
  const names = api.tools.map((t) => t.name);
  for (const expected of ["memory_store", "memory_scan", "memory_think", "memory_forget", "memory_reset"]) {
    check(names.includes(expected), `expected tool ${expected}; got ${names.join(", ")}`);
  }
  check(names.length >= 5, "at least the five memory tools registered");
});