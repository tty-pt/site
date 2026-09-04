import { createHash } from "node:crypto";
import { readFile } from "node:fs/promises";

export function isPlaceholderOrEmpty(text: string | undefined): boolean {
  if (!text) return true;
  const trimmed = text.trim();
  if (
    !trimmed || trimmed === "-" || trimmed === "- [ ]" ||
    trimmed === "- [ ] not started · in progress · blocked · done"
  ) return true;

  const placeholderPrefixes = [
    "> Core architectural facts",
    "> Material assumptions",
    "> Material uncertainties",
    "> Factual discoveries",
    "> Multi-stage execution plan",
    "> low · medium · high",
    "> Most justified immediate action",
    "> What we are trying to accomplish",
    "> Paste the verbatim user prompt",
    "> Concise epistemic briefing",
    "> A concise briefing",
    "> What are we proposing to change",
  ];

  for (const prefix of placeholderPrefixes) {
    if (trimmed.toLowerCase().startsWith(prefix.toLowerCase())) {
      return true;
    }
  }

  const lines = trimmed
    .split(/\r?\n/)
    .map((l) => l.trim())
    .filter((l) =>
      l && !l.startsWith(">") && l !== "-" && l !== "- [ ]" && l !== "1." &&
      l !== "- [ ] not started · in progress · blocked · done"
    );

  return lines.length === 0;
}

export function computeContentHash(content: string): string {
  return createHash("sha256").update(content, "utf8").digest("hex").slice(
    0,
    16,
  );
}

export async function computeFileFingerprint(
  p: string,
): Promise<{ hash: string; size: number } | null> {
  try {
    const content = await readFile(p, "utf8");
    return {
      hash: computeContentHash(content),
      size: new TextEncoder().encode(content).length,
    };
  } catch {
    return null;
  }
}
