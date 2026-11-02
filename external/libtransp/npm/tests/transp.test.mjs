import test from "node:test";
import assert from "node:assert/strict";
import { transposeText, TRANSP_HTML } from "../dist/index.js";

test("transposeText transposes chords accurately", () => {
  const input = "C G Am F";
  // Transpose up 2 semitones: C -> D, G -> A, Am -> Bm, F -> G
  const transposed = transposeText(input, 2);
  assert.ok(transposed.includes("D"));
  assert.ok(transposed.includes("A"));
  assert.ok(transposed.includes("Bm"));
  assert.ok(transposed.includes("G"));
});

test("transposeText with HTML output formatting", () => {
  const input = "C\nHello lyrics";
  const html = transposeText(input, 2, TRANSP_HTML);
  assert.ok(html.includes("<div>") || html.includes("<b>D</b>"));
});
