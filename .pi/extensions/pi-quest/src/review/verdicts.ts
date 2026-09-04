// HIGH_LEVEL: #review result — exactly one PASS | FAIL with findings.
// HIGH_LEVEL: #stale results — validity is decided against the target, never here.
import type { ReviewVerdict } from "../domain/quest";

export interface ParsedReview {
  verdict: ReviewVerdict;
  findings: string;
  severity: string;
}

const MAX_FINDINGS_CHARS = 4000;

function normalize(raw: string): ReviewVerdict | null {
  const upper = raw.toUpperCase().trim();
  if (upper === "PASS" || upper === "APPROVE") return "PASS";
  if (upper === "FAIL" || upper === "REVISE") return "FAIL";
  return null;
}

function collectBullets(lines: string[]): string {
  const items: string[] = [];
  let inSection = false;
  for (const line of lines) {
    const trimmed = line.trim();
    if (/^(FINDINGS|REQUIRED REVISIONS|REQUIRED ACTIONS)\s*:/i.test(trimmed)) {
      inSection = true;
      continue;
    }
    if (/^[A-Z][A-Z /-]*\s*:/.test(trimmed) && !trimmed.startsWith("-") && !trimmed.startsWith("*")) {
      if (!/^(FINDINGS|REQUIRED)/i.test(trimmed)) inSection = false;
      continue;
    }
    if (!inSection) continue;
    const bullet = trimmed.replace(/^[-*]\s*/, "").replace(/^\d+\.\s*/, "").trim();
    if (bullet) items.push(bullet);
  }
  return items.join("; ").slice(0, MAX_FINDINGS_CHARS);
}

export function parseReviewText(text: string): ParsedReview {
  if (typeof text !== "string" || text.trim() === "") {
    return {
      verdict: "FAIL",
      severity: "MAJOR",
      findings: "Reviewer returned no output (treated as FAIL; rebut with evidence or approve manually).",
    };
  }
  const lines = text.split(/\r?\n/);
  let verdict: ReviewVerdict | null = null;
  let severity = "NONE";
  for (let i = lines.length - 1; i >= 0; i -= 1) {
    const m = lines[i].trim().match(/^VERDICT:\s*([A-Za-z]+)\b/);
    if (m) {
      verdict = normalize(m[1]);
      break;
    }
  }
  for (let i = lines.length - 1; i >= 0; i -= 1) {
    const m = lines[i].trim().match(/^SEVERITY:\s*(NONE|MINOR|MAJOR|CRITICAL)\b/i);
    if (m) {
      severity = m[1].toUpperCase();
      break;
    }
  }
  if (verdict === null) {
    return {
      verdict: "FAIL",
      severity: "MAJOR",
      findings: "Reviewer returned no parseable VERDICT line (treated as FAIL; rebut with evidence or approve manually).",
    };
  }
  if (verdict === "FAIL" && severity === "NONE") severity = "MAJOR";
  if (verdict === "PASS" && severity !== "NONE" && severity !== "MINOR") severity = "NONE";
  const findings = collectBullets(lines);
  return {
    verdict,
    severity,
    findings: findings || (verdict === "PASS" ? "No blocking findings." : "Reviewer gave no itemized findings."),
  };
}
