import test from "node:test";
import assert from "node:assert/strict";
import { ViewportController, LaminaSurface, FlexNode, attachCanvas, createLaminaBridge, rgba, rgb, colorR, colorG, colorB, colorA, version } from "../dist/index.js";

test("ViewportController worldToScreen and screenToWorld coordinate transformations", () => {
  const vp = new ViewportController(800, 600);
  vp.setCamera(100, 200, 2.0);

  const screen = vp.worldToScreen(100, 200);
  assert.equal(screen.x, 400);
  assert.equal(screen.y, 300);

  const world = vp.screenToWorld(400, 300);
  assert.equal(world.x, 100);
  assert.equal(world.y, 200);
});

test("Color packing helpers roundtrip RGB and RGBA channels", () => {
  const c = rgba(255, 128, 64, 200);
  assert.equal(colorR(c), 255);
  assert.equal(colorG(c), 128);
  assert.equal(colorB(c), 64);
  assert.equal(colorA(c), 200);

  const solid = rgb(10, 20, 30);
  assert.equal(colorR(solid), 10);
  assert.equal(colorG(solid), 20);
  assert.equal(colorB(solid), 30);
  assert.equal(colorA(solid), 255);
});

test("LaminaSurface 2D drawing, fills, borders, lines and pixel buffer", () => {
  const surf = new LaminaSurface(64, 64);
  assert.equal(surf.width, 64);
  assert.equal(surf.height, 64);

  surf.clear(rgba(0, 0, 0, 255));
  assert.equal(surf.pick(0, 0), rgba(0, 0, 0, 255));

  const red = rgba(255, 0, 0, 255);
  surf.fill(10, 10, 20, 20, red);
  assert.equal(surf.pick(15, 15), red);
  assert.equal(surf.pick(5, 5), rgba(0, 0, 0, 255));

  const blue = rgba(0, 0, 255, 255);
  surf.paint(30, 30, blue);
  assert.equal(surf.pick(30, 30), blue);

  const pixels = surf.getPixels();
  assert.equal(pixels.length, 64 * 64);
  assert.equal(pixels[15 * 64 + 15], red);

  const clamped = surf.getClampedPixels();
  assert.equal(clamped.length, 64 * 64 * 4);

  surf.free();
});

test("FlexNode calculates nested flexbox layout trees in pure C", () => {
  const root = new FlexNode("row");
  root.setSize(600, 400);
  root.setPadding(10, 10, 10, 10);

  const sidebar = new FlexNode("column");
  sidebar.setSize(150, 0);

  const content = new FlexNode("column");
  content.setFlex(1, 1);

  root.addChild(sidebar);
  root.addChild(content);

  root.computeLayout(600, 400);

  const rootBounds = root.getBounds();
  assert.equal(rootBounds.width, 600);
  assert.equal(rootBounds.height, 400);

  const sideBounds = sidebar.getBounds();
  assert.equal(sideBounds.x, 10);
  assert.equal(sideBounds.y, 10);
  assert.equal(sideBounds.width, 150);
  assert.equal(sideBounds.height, 380);

  const contentBounds = content.getBounds();
  assert.equal(contentBounds.x, 160);
  assert.equal(contentBounds.y, 10);
  assert.equal(contentBounds.width, 430);
  assert.equal(contentBounds.height, 380);

  root.free();
});

test("attachCanvas and LaminaSurface.flush integrate with canvas targets", () => {
  let putImageDataCalls = 0;
  const mockCtx = {
    putImageData(imgData, x, y) {
      putImageDataCalls++;
      assert.equal(imgData.width, 32);
      assert.equal(imgData.height, 32);
      assert.equal(x, 0);
      assert.equal(y, 0);
    }
  };
  const mockCanvas = {
    width: 32,
    height: 32,
    getContext(type) {
      if (type === "2d") return mockCtx;
      return null;
    }
  };

  const surf = new LaminaSurface(32, 32, { canvas: mockCanvas });
  surf.clear(rgb(255, 0, 0));
  surf.flush();
  assert.equal(putImageDataCalls, 1);

  const target = attachCanvas(mockCanvas);
  assert.equal(target.isWebGL(), false);
  target.present(surf);
  assert.equal(putImageDataCalls, 2);

  target.destroy();
  surf.free();
});

test("createLaminaRenderer hardware-accelerates via WebGL textured quad pipeline", () => {
  let drawArraysCalls = 0;
  let texImage2DCalls = 0;

  const mockGl = {
    VERTEX_SHADER: 35633,
    FRAGMENT_SHADER: 35632,
    LINK_STATUS: 35714,
    ARRAY_BUFFER: 34962,
    STATIC_DRAW: 35044,
    FLOAT: 5126,
    TEXTURE_2D: 3553,
    TEXTURE_MIN_FILTER: 10241,
    TEXTURE_MAG_FILTER: 10240,
    NEAREST: 9728,
    TEXTURE_WRAP_S: 10242,
    TEXTURE_WRAP_T: 10243,
    CLAMP_TO_EDGE: 33071,
    RGBA: 6408,
    UNSIGNED_BYTE: 5121,
    TRIANGLES: 4,

    createShader: () => ({}),
    shaderSource: () => {},
    compileShader: () => {},
    createProgram: () => ({}),
    attachShader: () => {},
    linkProgram: () => {},
    getProgramParameter: () => true,
    useProgram: () => {},
    createBuffer: () => ({}),
    bindBuffer: () => {},
    bufferData: () => {},
    getAttribLocation: () => 0,
    enableVertexAttribArray: () => {},
    vertexAttribPointer: () => {},
    createTexture: () => ({}),
    bindTexture: () => {},
    texParameteri: () => {},
    viewport: () => {},
    texImage2D: () => { texImage2DCalls++; },
    drawArrays: (mode, first, count) => {
      drawArraysCalls++;
      assert.equal(mode, 4);
      assert.equal(first, 0);
      assert.equal(count, 6);
    },
    deleteTexture: () => {},
    deleteBuffer: () => {},
    deleteProgram: () => {}
  };

  const mockWebGlCanvas = {
    width: 64,
    height: 64,
    getContext(type) {
      if (type === "webgl2" || type === "webgl") return mockGl;
      return null;
    }
  };

  const target = attachCanvas(mockWebGlCanvas);
  assert.equal(target.isWebGL(), true);

  const surf = new LaminaSurface(64, 64);
  surf.clear(rgb(0, 255, 0));
  target.present(surf);

  assert.equal(texImage2DCalls, 1);
  assert.equal(drawArraysCalls, 1);

  surf.flush(mockWebGlCanvas);
  assert.equal(texImage2DCalls, 2);
  assert.equal(drawArraysCalls, 2);

  target.destroy();
  surf.free();
});

test("createLaminaBridge provides Web HAL WASM host imports", () => {
  const bridge = createLaminaBridge(null, () => ({ memory: new WebAssembly.Memory({ initial: 1 }) }));
  const imports = bridge.makeLaminaImports();
  assert.ok(typeof imports.lam_canvas_flush === "function");
  assert.ok(typeof imports.lam_flush === "function");
  assert.ok(typeof imports.lam_fill === "function");
  assert.ok(typeof imports.lam_tile_draw === "function");

  // Call flush with null canvas (should safely return without crashing)
  imports.lam_canvas_flush(0, 10, 10);
  bridge.destroy();
});

test("version returns 1.0.1", () => {
  assert.equal(version(), "1.0.1");
});
