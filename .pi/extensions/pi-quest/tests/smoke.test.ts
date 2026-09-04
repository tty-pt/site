import install from "../index.ts";
import type { Pi } from "../src/hooks/events.ts";

function check(cond: boolean, msg: string): void {
  if (!cond) throw new Error(msg);
}

function fakePi(): Pi {
  return {
    on(_event: never, _handler: never): void {},
    appendEntry(_customType: string, _data: unknown): void {},
    registerTool(_tool: never): void {},
    registerCommand(_name: string, _options: never): void {},
  } as unknown as Pi;
}

Deno.test("manifest declares the pi extension entry point", async () => {
  const raw = await Deno.readTextFile(
    new URL("../package.json", import.meta.url),
  );
  const pkg = JSON.parse(raw);
  const exts: unknown = pkg?.pi?.extensions;
  check(
    Array.isArray(exts) && exts.includes("index.ts"),
    "pi.extensions must include index.ts",
  );
});

Deno.test("manifest declares the quest-journal skill", async () => {
  const raw = await Deno.readTextFile(
    new URL("../package.json", import.meta.url),
  );
  const pkg = JSON.parse(raw);
  const skills: unknown = pkg?.pi?.skills;
  check(
    Array.isArray(skills) &&
      skills.includes("skills/quest-journal/SKILL.md"),
    "pi.skills must include skills/quest-journal/SKILL.md",
  );
});

Deno.test("entry default-exports an installer function", () => {
  check(typeof install === "function", "default export must be a function");
});

Deno.test("installer subscribes without side effects yet", () => {
  const seen: string[] = [];
  const pi = fakePi();
  const origOn = pi.on.bind(pi);
  (pi as { on: unknown }).on = (event: string, handler: unknown) => {
    seen.push(event);
    return origOn(event as never, handler as never);
  };
  install(pi);
  check(seen.includes("session_start"), "subscribes session_start");
  check(seen.includes("turn_end"), "subscribes turn_end");
  check(seen.includes("tool_call"), "subscribes tool_call");
  check(seen.includes("before_agent_start"), "subscribes before_agent_start");
});
