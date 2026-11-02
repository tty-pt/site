import test from "node:test";
import assert from "node:assert/strict";
import {
  createPageLayout,
  createMediaControls,
  createFormActions,
  createDeleteConfirmForm,
  version
} from "../dist/index.js";

test("createPageLayout generates standard dual-root DOM scaffolding", () => {
  const layout = createPageLayout({ title: "Music Library", activeNav: "song" });
  assert.equal(layout.chromeRoot, "#chrome-root");
  assert.equal(layout.budRoot, "#bud-root");

  const html = layout.renderHtml();
  assert.ok(html.includes('id="chrome-root"'));
  assert.ok(html.includes('id="bud-root"'));
});

test("createMediaControls manages audio/video player state", () => {
  const player = createMediaControls({ mediaUrl: "https://example.com/audio.mp3", duration: 180 });
  assert.equal(player.isPlaying, false);

  player.play();
  assert.equal(player.isPlaying, true);

  player.seek(45);
  assert.equal(player.currentTime, 45);

  player.pause();
  assert.equal(player.isPlaying, false);

  assert.ok(version().length > 0);
});

test("createFormActions generates form action buttons matching site_forms.c", () => {
  const actions = createFormActions({ cancelHref: "/song", submitLabel: "Save Song" });
  assert.equal(actions.submitLabel, "Save Song");
  assert.equal(actions.cancelHref, "/song");

  const html = actions.renderHtml();
  assert.ok(html.includes("Save Song"));
  assert.ok(html.includes('href="/song"'));
  assert.ok(html.includes("btn-primary"));
});

test("createDeleteConfirmForm generates delete confirmation form matching site_forms.c", () => {
  const del = createDeleteConfirmForm({
    module: "song",
    id: "42",
    title: "Amazing Grace",
    csrfToken: "tok_123"
  });
  assert.equal(del.actionPath, "/song/42/delete");
  assert.equal(del.cancelPath, "/song/42");

  const html = del.renderHtml();
  assert.ok(html.includes('action="/song/42/delete"'));
  assert.ok(html.includes('value="tok_123"'));
  assert.ok(html.includes("Amazing Grace"));
});
