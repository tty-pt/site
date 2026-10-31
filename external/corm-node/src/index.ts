import { createRequire } from "node:module";
import path from "node:path";

// Locate and load native addon with fallback
let nativeBinding: any = null;
try {
  const req = createRequire(import.meta.url);
  nativeBinding = req("../build/Release/corm_node.node");
} catch {
  try {
    const req = createRequire(import.meta.url);
    nativeBinding = req(path.join(process.cwd(), "build/Release/corm_node.node"));
  } catch {
    // Fallback if native addon is not compiled
  }
}

/**
 * Corm Candidate Set representing a collection of 32-bit record references.
 */
export class CandidateSet {
  private handle: any;

  constructor(handle?: any) {
    if (handle) {
      this.handle = handle;
    } else if (nativeBinding) {
      this.handle = nativeBinding.setCreate();
    }
  }

  public push(ref: number): this {
    if (nativeBinding && this.handle) {
      nativeBinding.setPush(this.handle, ref);
    }
    return this;
  }

  public seal(): this {
    if (nativeBinding && this.handle) {
      nativeBinding.setSeal(this.handle);
    }
    return this;
  }

  public toArray(): number[] {
    if (nativeBinding && this.handle) {
      return nativeBinding.setToArray(this.handle);
    }
    return [];
  }

  public intersect(other: CandidateSet): CandidateSet {
    if (nativeBinding && this.handle && other.handle) {
      const res = nativeBinding.setIntersect(this.handle, other.handle);
      return new CandidateSet(res);
    }
    return new CandidateSet();
  }

  public union(other: CandidateSet): CandidateSet {
    if (nativeBinding && this.handle && other.handle) {
      const res = nativeBinding.setUnion(this.handle, other.handle);
      return new CandidateSet(res);
    }
    return new CandidateSet();
  }

  public subtract(other: CandidateSet): CandidateSet {
    if (nativeBinding && this.handle && other.handle) {
      const res = nativeBinding.setSubtract(this.handle, other.handle);
      return new CandidateSet(res);
    }
    return new CandidateSet();
  }
}

/**
 * Interface representing a query axis in the Corm Recall Kernel model.
 */
export interface CormAxis<TFilter = any, TRank = any> {
  readonly name: string;
  fill?(params: TFilter): Promise<number[]> | number[] | CandidateSet;
  rank?(params: TRank, ref: number): Promise<number> | number;
}

/**
 * Native Temporal Interval Axis (wrapping libjoint)
 */
export class JointTemporalAxis implements CormAxis<{ start: number; end: number; resourceId?: number }> {
  public readonly name = "temporal";
  private nativeHandle: any = null;
  private resourceMapping: Map<number, number> = new Map();

  constructor(filePath?: string) {
    if (nativeBinding && nativeBinding.jointInit) {
      this.nativeHandle = nativeBinding.jointInit(filePath || "");
    }
  }

  public registerInterval(ref: number, resourceId: number, start: number, end: number) {
    this.resourceMapping.set(ref, resourceId);
    if (nativeBinding && this.nativeHandle) {
      nativeBinding.jointRecord(this.nativeHandle, ref, start, end);
    }
  }

  public fill(params: { start: number; end: number; resourceId?: number }): CandidateSet {
    const set = new CandidateSet();
    if (nativeBinding && this.nativeHandle) {
      const intersectingRefs: number[] = nativeBinding.jointIntersect(
        this.nativeHandle,
        params.start,
        params.end
      );
      for (const ref of intersectingRefs) {
        if (params.resourceId !== undefined) {
          const res = this.resourceMapping.get(ref);
          if (res !== undefined && res !== params.resourceId) continue;
        }
        set.push(ref);
      }
    }
    set.seal();
    return set;
  }
}

/**
 * Dense Vector Axis (wrapping vector cosine similarity and embeddings)
 */
export class SepalVectorAxis implements CormAxis<void, { queryVector: number[] }> {
  public readonly name = "vector";
  private vectors: Map<number, number[]> = new Map();

  public registerVector(ref: number, vector: number[]) {
    this.vectors.set(ref, vector);
  }

  public rank(params: { queryVector: number[] }, ref: number): number {
    const vec = this.vectors.get(ref);
    if (!vec || vec.length !== params.queryVector.length) return 0.0;

    let dot = 0.0;
    let normA = 0.0;
    let normB = 0.0;
    for (let i = 0; i < vec.length; i++) {
      dot += vec[i] * params.queryVector[i];
      normA += vec[i] * vec[i];
      normB += params.queryVector[i] * params.queryVector[i];
    }
    if (normA === 0 || normB === 0) return 0.0;
    return dot / (Math.sqrt(normA) * Math.sqrt(normB));
  }
}

/**
 * Lexical Full-Text Search Axis (wrapping BM25 / token indexing)
 */
export class LemmaLexicalAxis implements CormAxis<{ term: string }, { term: string }> {
  public readonly name = "lexical";
  private documents: Map<number, string[]> = new Map();

  public registerText(ref: number, text: string) {
    const tokens = text
      .toLowerCase()
      .replace(/[^\w\s]/g, "")
      .split(/\s+/)
      .filter(Boolean);
    this.documents.set(ref, tokens);
  }

  public fill(params: { term: string }): CandidateSet {
    const set = new CandidateSet();
    const queryTokens = params.term.toLowerCase().split(/\s+/).filter(Boolean);

    for (const [ref, docTokens] of this.documents.entries()) {
      const match = queryTokens.some((q) => docTokens.includes(q));
      if (match) {
        set.push(ref);
      }
    }
    set.seal();
    return set;
  }

  public rank(params: { term: string }, ref: number): number {
    const tokens = this.documents.get(ref);
    if (!tokens || tokens.length === 0) return 0.0;

    const queryTokens = params.term.toLowerCase().split(/\s+/).filter(Boolean);
    let matches = 0;
    for (const q of queryTokens) {
      if (tokens.includes(q)) matches++;
    }
    return matches / queryTokens.length;
  }
}

/**
 * Multi-Axis Composable Query Builder
 */
export class CormQuery<T = any> {
  private db: CormDb<T>;
  private filters: Array<{ axis: string; params: any; join: "AND" | "OR" | "NOT" }> = [];
  private rankers: Array<{ axis: string; params: any; weight: number }> = [];
  private limitTopK: number = 20;
  private scoreThreshold: number = 0.0;

  constructor(db: CormDb<T>) {
    this.db = db;
  }

  public filter(axis: string, params: any, join: "AND" | "OR" | "NOT" = "AND"): this {
    this.filters.push({ axis, params, join });
    return this;
  }

  public rank(axis: string, params: any, weight: number = 1.0): this {
    this.rankers.push({ axis, params, weight });
    return this;
  }

  public topK(k: number): this {
    this.limitTopK = k;
    return this;
  }

  public minScore(score: number): this {
    this.scoreThreshold = score;
    return this;
  }

  public async execute(): Promise<Array<{ ref: number; score: number; data?: T | null }>> {
    let runningSet: CandidateSet | null = null;

    // 1. Evaluate filter axes & combine candidate sets
    for (const f of this.filters) {
      const axis = this.db.getAxis(f.axis);
      if (!axis || !axis.fill) continue;

      const fillRes = await axis.fill(f.params);
      let candSet: CandidateSet;
      if (fillRes instanceof CandidateSet) {
        candSet = fillRes;
      } else {
        candSet = new CandidateSet();
        for (const ref of fillRes) candSet.push(ref);
        candSet.seal();
      }

      if (!runningSet) {
        runningSet = candSet;
      } else {
        if (f.join === "AND") runningSet = runningSet.intersect(candSet);
        else if (f.join === "OR") runningSet = runningSet.union(candSet);
        else if (f.join === "NOT") runningSet = runningSet.subtract(candSet);
      }
    }

    const candidateRefs = runningSet ? runningSet.toArray() : [];

    // 2. Score and rank candidates using streaming ranker (with native async libuv worker)
    const refsToRank: number[] = [];
    const scoresToRank: number[] = [];

    for (const ref of candidateRefs) {
      let finalScore = 0.0;
      for (const r of this.rankers) {
        const axis = this.db.getAxis(r.axis);
        if (axis && axis.rank) {
          const score = await axis.rank(r.params, ref);
          finalScore += score * r.weight;
        }
      }

      refsToRank.push(ref);
      scoresToRank.push(finalScore);
    }

    let results: Array<{ ref: number; score: number }> = [];
    if (nativeBinding && nativeBinding.rankAsync) {
      results = await nativeBinding.rankAsync(
        refsToRank,
        scoresToRank,
        this.limitTopK,
        this.scoreThreshold
      );
    } else {
      results = refsToRank
        .map((ref, i) => ({ ref, score: scoresToRank[i] }))
        .filter((item) => item.score >= this.scoreThreshold)
        .sort((a, b) => b.score - a.score)
        .slice(0, this.limitTopK);
    }

    return results.map((item: any) => ({
      ...item,
      data: this.db.get(String(item.ref)),
    }));
  }
}

/**
 * Primary Corm Database wrapper
 */
export class CormDb<T = any> {
  private handle: any = null;
  private axes: Map<string, CormAxis> = new Map();

  constructor(filePath?: string, dbName: string = "corm") {
    if (nativeBinding) {
      this.handle = nativeBinding.open(filePath || "", dbName);
    }
  }

  public registerAxis(axis: CormAxis): this {
    this.axes.set(axis.name, axis);
    return this;
  }

  public getAxis(name: string): CormAxis | undefined {
    return this.axes.get(name);
  }

  public put(key: string, value: T): number {
    if (nativeBinding && this.handle) {
      const payload = typeof value === "string" ? value : JSON.stringify(value);
      return nativeBinding.put(this.handle, key, payload);
    }
    return 0;
  }

  public get(key: string): T | null {
    if (nativeBinding && this.handle) {
      const raw = nativeBinding.get(this.handle, key);
      if (!raw) return null;
      try {
        return JSON.parse(raw);
      } catch {
        return raw as any;
      }
    }
    return null;
  }

  public del(key: string): boolean {
    if (nativeBinding && this.handle) {
      return nativeBinding.del(this.handle, key);
    }
    return false;
  }

  public query(): CormQuery<T> {
    return new CormQuery<T>(this);
  }

  public save(): void {
    if (nativeBinding && nativeBinding.save) {
      nativeBinding.save();
    }
  }

  public close(): void {
    if (nativeBinding && this.handle) {
      nativeBinding.close(this.handle);
      this.handle = null;
    }
  }
}
