import install from "../index.ts";

function check(cond: boolean, msg: string): void {
  if (!cond) throw new Error(msg);
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

Deno.test("installer runs without side effects yet", () => {
  // Slices subscribe their events here; the skeleton must stay inert.
  install({
    on(_event: string, _handler: unknown): void {},
  });
});
