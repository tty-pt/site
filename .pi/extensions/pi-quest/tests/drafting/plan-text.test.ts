import { check } from "../check.ts";
import {
  buildClaimManifest,
  bumpReviewCount,
  citeLocations,
  draftProfileText,
  type DraftSections,
  meetsReviewThresholds,
  parseDraftSections,
  spliceAnalysisSection,
  splicePlanSection,
} from "../../src/drafting/plan-text.ts";

Deno.test("citeLocations extracts file:line ranges and ignores non-citations", () => {
  const text = `Refactor gig.c:403-520 and song.c:415. See site_page.c:214 too.
A poem.c = 123 line exemplar has no colon-line pair.
Realms of text like: 'a=b' or URLs (http://x/y.c:5) stay unmatched.
macro site_ui.c:#ifndef is not a line cite.`;
  const cites = citeLocations(text);
  check(cites.length === 3, "three citations parsed");
  const gig = cites.find((c) => c.file === "gig.c");
  check(gig?.lineStart === 403 && gig.lineEnd === 520, "range parsed");
  const song = cites.find((c) => c.file === "song.c");
  check(song?.lineStart === 415 && song.lineEnd === undefined, "single line parsed");
  const ui = cites.find((c) => c.file === "site_page.c");
  check(ui?.lineStart === 214, "header cite parsed");
});

Deno.test("citeLocations handles deep paths and header extensions", () => {
  const cites = citeLocations("Revisit gig/ux/detail.c:495-530 and common.h:216.");
  check(cites.some((c) => c.file === "gig/ux/detail.c"), "deep path parsed");
  check(cites.some((c) => c.file === "common.h"), "header parsed");
});

const SECTIONS: DraftSections = {
  requirements: ["reduce consumer boilerplate"],
  evidence: ["site_ui_respond_with_state is dead (site_page.c:214)"],
  plan: "Phase 1: gig.c:403-520 delegates to song_load_row_summary (song.c).",
  analysis: "Root cause: buf allocation is unbounded (alloc.c:77) and never freed.",
};

Deno.test("buildClaimManifest lists evidence and plan citations for spot-checking", () => {
  const manifest = buildClaimManifest(SECTIONS);
  check(manifest.includes("site_page.c:214"), "evidence citation listed");
  check(manifest.includes("gig.c:403-520"), "plan citation listed");
  check(manifest.split("\n").length >= 2, "multiple claims listed");
  check(buildClaimManifest({ requirements: [], evidence: [], plan: "no cites here.", analysis: "" }) === "", "no manifest for citation-free plans");
});

Deno.test("parseDraftSections folds pre-draft findings into evidence", () => {
  const sections = parseDraftSections(
    "## Requirements\n- one\n\n## Findings (pre-draft investigation)\n- a global registry found\n- filter.c hard cap\n\n## Implementation Plan\nplan\n",
  );
  check(sections.evidence.length === 2, "findings bullets join evidence");
  check(sections.evidence[0] === "a global registry found", "findings text kept verbatim");
});

Deno.test("Evidence and pre-draft findings merge without double counting", () => {
  const sections = parseDraftSections(
    "## Requirements\n- one\n\n## Evidence\n- measured it\n\n## Findings (pre-draft investigation)\n- found in code\n\n## Implementation Plan\nplan\n",
  );
  check(sections.evidence.length === 2, "evidence and findings merge");
  check(meetsReviewThresholds(sections, { requirements: 2, evidence: 2 }), "findings count toward the bar");
});

Deno.test("findings alone satisfy the 1-requirement-plus-evidence leg of the bar", () => {
  const sections = parseDraftSections(
    `## Requirements\n- one\n\n## Findings (pre-draft investigation)\n${Array.from({ length: 7 }, (_, i) => `- finding ${i}`).join("\n")}\n\n## Implementation Plan\nplan\n`,
  );
  check(meetsReviewThresholds(sections), "1 requirement + 7 findings reviewable");
});

Deno.test("draftProfileText reports the reviewability verdict for reviewable drafts", () => {
  const reviewable = draftProfileText(
    { requirements: ["a", "b"], evidence: ["e"], plan: "plan", analysis: "" },
    { requirements: 2, evidence: 7 },
    "claims check: 1 citations resolve",
  );
  check(reviewable.includes("requirements 2"), "requirement count shown");
  check(reviewable.includes("evidence 1"), "evidence count shown");
  check(reviewable.includes("plan present"), "plan presence shown");
  check(reviewable.includes("maturity bar: met"), "bar verdict shown");
  check(reviewable.includes("claims check: 1 citations resolve"), "citation summary folded");
});

Deno.test("draftProfileText names exactly what is missing below the bar", () => {
  const thin = draftProfileText(
    { requirements: ["a"], evidence: [], plan: "", analysis: "" },
    { requirements: 2, evidence: 7 },
  );
  check(thin.includes("requirements 1"), "requirement count shown");
  check(thin.includes("evidence 0"), "evidence count shown");
  check(thin.includes("plan missing"), "plan absence named");
  check(thin.includes("NOT met"), "below bar flagged");
  check(thin.includes("2 requirements"), "requirements leg of the bar named");
  check(thin.includes("7 evidence"), "evidence leg of the bar named");
});

Deno.test("parseDraftSections collects the ## Analysis body for analysis quests", () => {
  const sections = parseDraftSections(
    "## Requirements\n- one\n\n## Analysis\nThe root cause is alloc.c:77.\nSee also filter.c:12 for the cap.\n\n## Implementation Plan\nplan\n",
  );
  check(sections.analysis.includes("alloc.c:77"), "analysis body collected");
  check(sections.analysis.includes("filter.c:12"), "multi-line body kept");
  check(sections.plan === "plan", "plan unaffected by analysis section");
});

Deno.test("spliceAnalysisSection replaces in place or appends on demand", () => {
  const replaced = spliceAnalysisSection(
    "## Requirements\n- one\n\n## Analysis\nstale\n\n## Implementation Plan\nplan\n",
    "fresh analysis",
  );
  const parsed = parseDraftSections(replaced);
  check(parsed.analysis === "fresh analysis", "analysis body replaced");
  check(parsed.plan === "plan", "plan untouched");
  const appended = spliceAnalysisSection("## Requirements\n- one\n", "new body");
  check(parseDraftSections(appended).analysis === "new body", "analysis appended when missing");
});

Deno.test("meetsReviewThresholds swaps the deliverable by kind", () => {
  const base = { requirements: ["a", "b"], evidence: ["e"], analysis: "deep dive" };
  const withPlan = { ...base, plan: "steps" };
  const withAnalysis = { ...base, plan: "" };
  check(meetsReviewThresholds(withPlan), "standard kind passes on plan");
  check(!meetsReviewThresholds({ ...withPlan, plan: "" }), "standard kind needs the plan");
  check(meetsReviewThresholds({ ...withAnalysis, plan: "" }, undefined, "analysis"), "analysis kind passes on analysis body");
  check(!meetsReviewThresholds({ ...withPlan, analysis: "" }, undefined, "analysis"), "analysis kind needs the analysis body");
});

Deno.test("draftProfileText and manifest name the analysis deliverable by kind", () => {
  const sections: DraftSections = {
    requirements: ["a"],
    evidence: [],
    plan: "plan text",
    analysis: "cite alloc.c:77 as the bounded fix",
  };
  const profile = draftProfileText(sections, { requirements: 2, evidence: 7 }, "", "analysis");
  check(profile.includes("analysis present"), "analysis presence shown");
  check(profile.includes("authored analysis"), "analysis gap leg named");
  const missing = draftProfileText({ ...sections, analysis: "" }, { requirements: 2, evidence: 7 }, "", "analysis");
  check(missing.includes("analysis missing"), "missing analysis named");
  check(missing.includes("## Analysis section"), "gap names the analysis section");
  const manifest = buildClaimManifest(sections, "analysis");
  check(manifest.includes("alloc.c:77"), "manifest cites the analysis body");
  check(!manifest.includes("plan text"), "manifest ignores the plan for analysis");
});

Deno.test("bumpReviewCount lands the marker in the analysis body by kind", () => {
  const doc = "## Requirements\n- one\n\n## Analysis\nclaim alloc.c:77\n\n## Implementation Plan\nunused\n";
  const bumped = bumpReviewCount(doc, 1, "analysis");
  const sections = parseDraftSections(bumped);
  check(sections.analysis.includes("review-count"), "marker inside analysis body");
  check(sections.plan === "unused", "plan untouched for analysis bump");
  check(bumpReviewCount("## Requirements\n- one\n\n## Analysis\n\n", 2, "analysis") === "## Requirements\n- one\n\n## Analysis\n\n", "empty analysis stays untouched");
});