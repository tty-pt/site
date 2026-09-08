import { check } from "../check.ts";
import {
  buildClaimManifest,
  citeLocations,
  type DraftSections,
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