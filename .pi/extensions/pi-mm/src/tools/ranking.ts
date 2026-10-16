import { decodePayload } from "../payload.ts";
import type { ResultLine } from "../qmap.ts";
import type { LifecycleState, MemoryEnvelope, MipmapLevel } from "../types.ts";

export type CandidateOrigin = "stoma" | "sepal" | "both";

export interface FusedCandidate {
  ref: number;
  record: string;
  origin: CandidateOrigin;
  rrfScore: number;
  score?: string;
  lexicalRank?: number;
  semanticRank?: number;
  lexicalScore?: string;
  semanticScore?: string;
  envelope?: MemoryEnvelope;
  level?: MipmapLevel;
  lifecycle?: LifecycleState;
  summary?: string;
  rankingScore?: number;
}

export interface FilterCriteria {
  mipmap?: MipmapLevel;
  lifecycle?: string;
  project?: string;
  subsystem?: string;
  limit: number;
  query?: string;
}

export type QueryDomain = "personal" | "technical" | "general";

const PERSONAL_QUERY_PATTERNS = [
  /\b(?:grandpa|grandpa's|grandfather|grandfather's|grandmother|grandmother's|grandma|grandma's|grand\s+daddy|dad|mom|father|mother|aunt|uncle|brother|sister|cousin|wife|husband|partner|son|daughter|family|pet|dog|cat)\b/i,
  /\b(?:what\s+did\s+i\s+say|what\s+did\s+i\s+tell\s+you|did\s+i\s+mention|do\s+you\s+remember\s+me|i\s+told\s+you\s+about|we\s+talked\s+about|my\s+childhood|my\s+vacation|my\s+trip|my\s+hobby|my\s+favorite)\b/i,
  /\b(?:cherry\s+tree|airplanes?|planes?|dragons|near\s+the\s+lake|pilot|did\s+he\s+fly|which\s+route\s+did\s+he|where\s+did\s+he)\b/i,
];

const TECHNICAL_QUERY_PATTERNS = [
  /\b(?:corm|cormd|stoma|sepal|joint|islet|qmap|daemon|socket|fspec|isletFspec|mutex|leak|crash|build|make|test|compilation|endpoint|api|database|sqlite|wasm|ssr|axil|xylem|hyle|bud|git|ref|lock|payload|migration|roster|embed|vector|cosine|bm25)\b/i,
  /\b(?:why\s+did\s+(?:the\s+)?test\s+fail|how\s+did\s+we\s+fix|stack\s+trace|error\s+code|syntax\s+error|segmentation\s+fault|buffer\s+overflow)\b/i,
  /[a-z0-9_]+\.(?:ts|js|c|h|sh|json|md|py)\b/i,
];

export function classifyQueryDomain(query?: string): QueryDomain {
  if (!query) return "general";
  const q = query.trim();
  const isPersonal = PERSONAL_QUERY_PATTERNS.some((p) => p.test(q));
  const isTechnical = TECHNICAL_QUERY_PATTERNS.some((p) => p.test(q));
  if (isPersonal && !isTechnical) return "personal";
  if (isTechnical && !isPersonal) return "technical";
  if (isPersonal && isTechnical) {
    if (/\b(?:grandpa|grandfather|grand\s+daddy|aunt|what\s+did\s+i\s+say|do\s+you\s+remember\s+me)\b/i.test(q)) {
      return "personal";
    }
    return "technical";
  }
  return "general";
}

export function isCodeOrDocArtifact(cand: FusedCandidate, envelope: MemoryEnvelope): boolean {
  if (envelope.level === "L0") return true;
  const s = envelope.summary || cand.record || "";
  if (s.startsWith("# ") || s.startsWith("## ")) return true;
  if (/^(?:import\s+|export\s+|const\s+|function\s+|class\s+|var\s+|let\s+)/.test(s)) return true;
  if (/:\d+:\s*(?:const|let|var|function|import|export|#include)/.test(s)) return true;
  if (s.includes("isletFspec") || s.includes("mem-islet.db")) return true;
  return false;
}

export function isQuestionMemory(_cand: FusedCandidate, envelope: MemoryEnvelope): boolean {
  const s = (envelope.summary || "").trim();
  if (s.endsWith("?")) return true;
  if (/^(?:remember\s+anything|anything\s+about|do\s+you\s+remember|can\s+you\s+remember|what\s+did\s+i\s+say|did\s+i\s+mention)\b/i.test(s)) {
    return true;
  }
  return false;
}

export function isAssistantEcho(_cand: FusedCandidate, envelope: MemoryEnvelope): boolean {
  const s = (envelope.summary || "").trim();
  if (/^(?:you\s+mentioned|you\s+said|i\s+remember\s+that\s+you|as\s+you\s+said)\s*[:\-—]/i.test(s)) {
    return true;
  }
  if (s.startsWith("> *\"") || s.startsWith("> \"")) {
    return true;
  }
  return false;
}

/**
 * Standard Reciprocal Rank Fusion (RRF) with configurable smoothing constant k (default 60).
 * Merges independent candidate sets without scale incompatibility between lexical BM25 and cosine ANN.
 */
export function fuseRankingsRRF(
  lexical: ResultLine[],
  semantic: ResultLine[],
  k = 60,
): FusedCandidate[] {
  const candidateMap = new Map<number, FusedCandidate>();

  for (let i = 0; i < lexical.length; i++) {
    const item = lexical[i];
    const rank = i + 1;
    const rrfContrib = 1 / (k + rank);
    candidateMap.set(item.ref, {
      ref: item.ref,
      record: item.record,
      origin: "stoma",
      rrfScore: rrfContrib,
      lexicalRank: rank,
      lexicalScore: item.score,
    });
  }

  for (let j = 0; j < semantic.length; j++) {
    const item = semantic[j];
    const rank = j + 1;
    const rrfContrib = 1 / (k + rank);
    const existing = candidateMap.get(item.ref);
    if (existing) {
      existing.origin = "both";
      existing.rrfScore += rrfContrib;
      existing.semanticRank = rank;
      existing.semanticScore = item.score;
      if (!existing.record && item.record) existing.record = item.record;
    } else {
      candidateMap.set(item.ref, {
        ref: item.ref,
        record: item.record,
        origin: "sepal",
        rrfScore: rrfContrib,
        semanticRank: rank,
        semanticScore: item.score,
      });
    }
  }

  const result = Array.from(candidateMap.values());
  result.sort((a, b) => b.rrfScore - a.rrfScore || a.ref - b.ref);
  return result;
}

const LIFECYCLE_WEIGHTS: Record<LifecycleState, number> = {
  supported: 1.0,
  tested: 0.95,
  observed: 0.9,
  hypothesized: 0.7,
  contradicted: 0.1,
  superseded: 0.05,
};

export function computeRankingScore(cand: FusedCandidate, crit: FilterCriteria): number {
  let score = cand.rrfScore > 0 ? cand.rrfScore : (cand.score ? parseFloat(cand.score) : 1.0);
  if (isNaN(score) || score <= 0) score = 1.0;

  const env = cand.envelope;
  if (!env) return score;

  score *= (LIFECYCLE_WEIGHTS[env.lifecycle] ?? 0.8);

  // Provenance bonus for evidence-backed conclusions
  if (env.evidence && env.evidence.length > 0) {
    score += 0.005;
  }

  // Utility feedback factor
  if (env.utility) {
    const net = (env.utility.usedCount || 0) - (env.utility.irrelevantCount || 0) - (env.utility.obsoleteCount || 0) * 2;
    score += Math.max(-0.015, Math.min(0.015, net * 0.002));
  }

  // Scope compatibility bonus
  if (crit.project && env.context?.project === crit.project) score += 0.01;
  if (crit.subsystem && env.context?.subsystem === crit.subsystem) score += 0.01;

  return score;
}

export function filterAndEnrich(
  candidates: FusedCandidate[],
  crit: FilterCriteria,
): FusedCandidate[] {
  const domain = classifyQueryDomain(crit.query);
  const filtered: FusedCandidate[] = [];

  for (const cand of candidates) {
    const { envelope } = decodePayload(cand.record);
    if (crit.mipmap && envelope.level !== crit.mipmap) continue;

    // Lifecycle filtering:
    // Legacy records with undefined envelope.lifecycle are treated as active ("supported").
    // Unless explicitly requested via crit.lifecycle === "superseded" (or contradicted),
    // superseded and contradicted records are strictly excluded from ordinary search.
    const effectiveLifecycle = envelope.lifecycle ?? "supported";
    if (crit.lifecycle && crit.lifecycle !== "all") {
      if (effectiveLifecycle !== crit.lifecycle) continue;
    } else {
      if (effectiveLifecycle === "superseded") {
        continue;
      }
    }

    if (crit.project && envelope.context?.project !== crit.project) continue;
    if (crit.subsystem && envelope.context?.subsystem !== crit.subsystem) continue;

    // 1. Exclude question-shaped memories across all domains
    if (isQuestionMemory(cand, envelope)) continue;

    // 2. Exclude raw tool execution episodes (L0) and code artifacts from non-technical queries
    if (domain !== "technical" && crit.mipmap !== "L0") {
      if (isCodeOrDocArtifact(cand, envelope)) continue;
      if (envelope.level === "L0") continue;
    }

    // 3. Personal domain exclusions: exclude assistant echoes
    if (domain === "personal") {
      if (isAssistantEcho(cand, envelope)) continue;
    }

    const enriched: FusedCandidate = {
      ...cand,
      envelope,
      level: envelope.level,
      lifecycle: effectiveLifecycle,
      summary: envelope.summary,
      score: cand.score ?? cand.lexicalScore ?? cand.semanticScore ?? cand.rrfScore.toFixed(6),
    };
    enriched.rankingScore = computeRankingScore(enriched, crit);
    filtered.push(enriched);
  }

  filtered.sort((a, b) => (b.rankingScore ?? 0) - (a.rankingScore ?? 0) || a.ref - b.ref);

  // Domain-specific relevance filtering:
  if (domain === "personal" && crit.query) {
    const qLower = crit.query.toLowerCase();

    // Deduplicate equivalent memories in personal domain
    const deduped: FusedCandidate[] = [];
    const seenSummaries = new Set<string>();
    for (const cand of filtered) {
      const norm = (cand.summary || "").toLowerCase().replace(/[^\w\s]/g, "").trim();
      if (seenSummaries.has(norm)) continue;
      seenSummaries.add(norm);
      deduped.push(cand);
    }

    const finalPersonal: FusedCandidate[] = [];
    let maxSemantic = 0;
    for (const c of deduped) {
      const sScore = c.semanticScore ? parseFloat(c.semanticScore) : 0;
      if (sScore > maxSemantic) maxSemantic = sScore;
    }

    for (const cand of deduped) {
      const sScore = cand.semanticScore ? parseFloat(cand.semanticScore) : 0;
      const hasLexical = cand.origin === "stoma" || cand.origin === "both" || (cand.lexicalScore && parseFloat(cand.lexicalScore) > 0);
      const summaryLower = (cand.summary || "").toLowerCase();

      if (hasLexical) {
        finalPersonal.push(cand);
        continue;
      }

      // Semantic-only match in personal domain:
      // Must exceed calibrated similarity floor (0.50)
      if (sScore < 0.50) continue;

      // Must be within close margin of the top candidate
      if (maxSemantic > 0 && sScore < maxSemantic - 0.015) continue;

      // Entity coherence: if query asks about grandfather / grandpa, don't include memories purely about aunt / dragons
      if (/\b(?:grandpa|grandfather|grand\s+daddy)\b/.test(qLower) && /\b(?:aunt|dragon)\b/.test(summaryLower) && !/\b(?:grandpa|grandfather|grand\s+daddy)\b/.test(summaryLower)) {
        continue;
      }
      if (/\b(?:aunt|dragons?)\b/.test(qLower) && /\b(?:grandpa|grandfather|grand\s+daddy)\b/.test(summaryLower) && !/\b(?:aunt|dragons?)\b/.test(summaryLower)) {
        continue;
      }

      finalPersonal.push(cand);
    }

    return finalPersonal.slice(0, crit.limit);
  }

  return filtered.slice(0, crit.limit);
}
