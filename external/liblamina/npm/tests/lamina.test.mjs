import test from "node:test";
import assert from "node:assert/strict";
import { ViewportController, version } from "../dist/index.js";

test("ViewportController worldToScreen and screenToWorld coordinate transformations", () => {
  const vp = new ViewportController(800, 600);
  vp.setCamera(100, 100, 2.0);

  const screen = vp.worldToScreen(100, 100);
  assert.equal(screen.x, 400);
  assert.equal(screen.y, 300);

  const world = vp.screenToWorld(400, 300);
  assert.equal(world.x, 100);
  assert.equal(world.y, 100);

  assert.ok(version().length > 0);
});
