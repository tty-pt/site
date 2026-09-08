import { check } from "../check.ts";
import {
  buildClaimManifest,
  citeLocations,
  draftProfileText,
  type DraftSections,
  meetsReviewThresholds,
  parseDraftSections,
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
};

Deno.test("buildClaimManifest lists evidence and plan citations for spot-checking", () => {
  const manifest = buildClaimManifest(SECTIONS);
  check(manifest.includes("site_page.c:214"), "evidence citation listed");
  check(manifest.includes("gig.c:403-520"), "plan citation listed");
  check(manifest.split("\n").length >= 2, "multiple claims listed");
  check(buildClaimManifest({ requirements: [], evidence: [], plan: "no cites here." }) === "", "no manifest for citation-free plans");
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
    { requirements: ["a", "b"], evidence: ["e"], plan: "plan" },
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
    { requirements: ["a"], evidence: [], plan: "" },
    { requirements: 2, evidence: 7 },
  );
  check(thin.includes("requirements 1"), "requirement count shown");
  check(thin.includes("evidence 0"), "evidence count shown");
  check(thin.includes("plan missing"), "plan absence named");
  check(thin.includes("NOT met"), "below bar flagged");
  check(thin.includes("2 requirements"), "requirements leg of the bar named");
  check(thin.includes("7 evidence"), "evidence leg of the bar named");
});