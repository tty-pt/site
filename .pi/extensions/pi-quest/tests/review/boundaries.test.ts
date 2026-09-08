// Boundary: src/review holds no provider knowledge. Ambient provider names,
// builtin provider catalogs, and model family names must live only in
// operator-owned data (env, settings, agent overrides) — never in code.
import { check } from "../check.ts";

const BANNED = [
  "kilo",
  "cline",
  "antigravity",
  "openrouter",
  "google",
  "anthropic",
  "requesty",
  "bedrock",
  "ollama",
  "groq",
  "cerebras",
  "together",
  "deepseek",
  "mistral",
  "xai",
  "vertex",
  "gemini",
  "claude",
  "zenmux",
  "qoder",
  "crofai",
  "llm7",
];

Deno.test("review sources name no providers or model families", async () => {
  const dir = new URL("../../src/review/", import.meta.url);
  const pattern = new RegExp(`\\b(${BANNED.join("|")})\\b`, "i");
  for await (const entry of Deno.readDir(dir)) {
    if (!entry.isFile || !entry.name.endsWith(".ts")) continue;
    const text = await Deno.readTextFile(new URL(entry.name, dir));
    const match = pattern.exec(text);
    check(match === null, `${entry.name} names no provider (${match?.[0] ?? "clean"})`);
  }
});
