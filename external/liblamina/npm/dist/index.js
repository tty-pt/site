import { loadWasmModule, loadNativeAddon, getSubstrate } from "@tty-pt/stele";
import { createLaminaRenderer } from "./renderer.js";
import { createLaminaBridge } from "./bridge.js";

export { createLaminaRenderer } from "./renderer.js";
export { createLaminaBridge } from "./bridge.js";

const nativeBinding = loadNativeAddon(import.meta.url, "lamina");
let wasmInstance = null;

export function getLaminaWasm() {
  if (wasmInstance) return wasmInstance;
  wasmInstance = loadWasmModule(new URL("./lamina.wasm", import.meta.url));
  return wasmInstance;
}

/**
 * Color packing helpers
 */
export function rgba(r, g, b, a = 255) {
  return (((a & 0xff) << 24) | ((b & 0xff) << 16) | ((g & 0xff) << 8) | (r & 0xff)) >>> 0;
}

export function rgb(r, g, b) {
  return rgba(r, g, b, 255);
}

export function colorR(c) {
  return c & 0xff;
}

export function colorG(c) {
  return (c >> 8) & 0xff;
}

export function colorB(c) {
  return (c >> 16) & 0xff;
}

export function colorA(c) {
  return (c >>> 24) & 0xff;
}

/**
 * attachCanvas: Creates an automatic hardware-accelerated WebGL / 2D renderer target for a canvas.
 */
export function attachCanvas(canvasEl) {
  const renderer = createLaminaRenderer(canvasEl);
  return {
    isWebGL: () => renderer.isWebGL(),
    present: (surface) => {
      renderer.render(surface.getClampedPixels(), surface.width, surface.height);
    },
    destroy: () => renderer.destroy()
  };
}

/**
 * LaminaSurface: Pure-C immediate-mode 2D software framebuffer
 */
export class LaminaSurface {
  constructor(width, height, options = {}) {
    this.width = width;
    this.height = height;
    this.canvas = options.canvas || null;
    this._renderer = null;

    const wasm = getLaminaWasm();
    this.wasmHandle = wasm.exports.lamina_surface_new(width, height);
    if (!this.wasmHandle) {
      throw new Error(`Failed to allocate LaminaSurface(${width}x${height})`);
    }

    if (this.canvas) {
      this._renderer = createLaminaRenderer(this.canvas);
    }
  }

  free() {
    if (this._renderer) {
      this._renderer.destroy();
      this._renderer = null;
    }
    if (this.wasmHandle) {
      const wasm = getLaminaWasm();
      wasm.exports.lamina_surface_free(this.wasmHandle);
      this.wasmHandle = 0;
    }
  }

  clear(color = 0x00000000) {
    const wasm = getLaminaWasm();
    wasm.exports.lamina_surface_clear(this.wasmHandle, color >>> 0);
    return this;
  }

  fill(x, y, w, h, color) {
    const wasm = getLaminaWasm();
    wasm.exports.lamina_surface_fill(this.wasmHandle, x, y, w, h, color >>> 0);
    return this;
  }

  rect(x, y, w, h, color, thickness = 1) {
    const wasm = getLaminaWasm();
    wasm.exports.lamina_surface_rect(this.wasmHandle, x, y, w, h, color >>> 0, thickness);
    return this;
  }

  line(x0, y0, x1, y1, color, thickness = 1) {
    const wasm = getLaminaWasm();
    wasm.exports.lamina_surface_line(this.wasmHandle, x0, y0, x1, y1, color >>> 0, thickness);
    return this;
  }

  paint(x, y, color) {
    const wasm = getLaminaWasm();
    wasm.exports.lamina_surface_paint(this.wasmHandle, x, y, color >>> 0);
    return this;
  }

  pick(x, y) {
    const wasm = getLaminaWasm();
    return wasm.exports.lamina_surface_pick(this.wasmHandle, x, y) >>> 0;
  }

  getPixels() {
    const wasm = getLaminaWasm();
    const ptr = wasm.exports.lamina_surface_get_pixels(this.wasmHandle);
    const mem = (wasm.exports.memory || getSubstrate().memory).buffer;
    return new Uint32Array(mem, ptr, this.width * this.height);
  }

  getClampedPixels() {
    const wasm = getLaminaWasm();
    const ptr = wasm.exports.lamina_surface_get_pixels(this.wasmHandle);
    const mem = (wasm.exports.memory || getSubstrate().memory).buffer;
    return new Uint8ClampedArray(mem, ptr, this.width * this.height * 4);
  }

  flush(targetCanvas = this.canvas) {
    if (!targetCanvas) {
      throw new Error("No canvas element specified for LaminaSurface.flush()");
    }
    if (!this._renderer || this.canvas !== targetCanvas) {
      if (this._renderer) this._renderer.destroy();
      this.canvas = targetCanvas;
      this._renderer = createLaminaRenderer(targetCanvas);
    }
    this._renderer.render(this.getClampedPixels(), this.width, this.height);
    return this;
  }
}

/**
 * ViewportController: Manages canvas / tilemap viewport transformations
 */
export class ViewportController {
  constructor(width = 640, height = 480) {
    this.width = width;
    this.height = height;
    this.cameraX = 0;
    this.cameraY = 0;
    this.zoom = 1.0;
  }

  setCamera(x, y, zoom = this.zoom) {
    this.cameraX = x;
    this.cameraY = y;
    this.zoom = zoom;
  }

  worldToScreen(worldX, worldY) {
    if (nativeBinding && nativeBinding.worldToScreen) {
      return nativeBinding.worldToScreen(worldX, worldY, this.cameraX, this.cameraY, this.zoom, this.width, this.height);
    }
    const wasm = getLaminaWasm();
    const outPtr = wasm.exports.malloc(16);
    wasm.exports.lamina_wasm_world_to_screen(worldX, worldY, this.cameraX, this.cameraY, this.zoom, this.width, this.height, outPtr);
    const mem = (wasm.exports.memory || getSubstrate().memory).buffer;
    const arr = new Float64Array(mem, outPtr, 2);
    const res = { x: Math.round(arr[0]), y: Math.round(arr[1]) };
    wasm.exports.free(outPtr);
    return res;
  }

  screenToWorld(screenX, screenY) {
    if (nativeBinding && nativeBinding.screenToWorld) {
      return nativeBinding.screenToWorld(screenX, screenY, this.cameraX, this.cameraY, this.zoom, this.width, this.height);
    }
    const wasm = getLaminaWasm();
    const outPtr = wasm.exports.malloc(16);
    wasm.exports.lamina_wasm_screen_to_world(screenX, screenY, this.cameraX, this.cameraY, this.zoom, this.width, this.height, outPtr);
    const mem = (wasm.exports.memory || getSubstrate().memory).buffer;
    const arr = new Float64Array(mem, outPtr, 2);
    const res = { x: Math.round(arr[0]), y: Math.round(arr[1]) };
    wasm.exports.free(outPtr);
    return res;
  }
}

/**
 * FlexNode: CSS-like flexbox UI element tree layout engine
 */
export class FlexNode {
  constructor(direction = "column") {
    const wasm = getLaminaWasm();
    const dir = direction === "row" ? 0 : 1;
    this.wasmHandle = wasm.exports.lamina_node_new(dir);
  }

  setSize(w, h) {
    const wasm = getLaminaWasm();
    wasm.exports.lamina_node_set_size(this.wasmHandle, w, h);
    return this;
  }

  setPadding(l, t, r, b) {
    const wasm = getLaminaWasm();
    wasm.exports.lamina_node_set_padding(this.wasmHandle, l, t, r, b);
    return this;
  }

  setFlex(grow = 0, shrink = 1) {
    const wasm = getLaminaWasm();
    wasm.exports.lamina_node_set_flex(this.wasmHandle, grow, shrink);
    return this;
  }

  addChild(child) {
    const wasm = getLaminaWasm();
    wasm.exports.lamina_node_add_child(this.wasmHandle, child.wasmHandle);
    return this;
  }

  computeLayout(availableWidth, availableHeight, startX = 0, startY = 0) {
    const wasm = getLaminaWasm();
    wasm.exports.lamina_node_compute_layout(this.wasmHandle, startX, startY, availableWidth, availableHeight);
    return this;
  }

  getBounds() {
    const wasm = getLaminaWasm();
    const outPtr = wasm.exports.malloc(32);
    wasm.exports.lamina_node_get_bounds(this.wasmHandle, outPtr);
    const mem = (wasm.exports.memory || getSubstrate().memory).buffer;
    const arr = new Float64Array(mem, outPtr, 4);
    const bounds = { x: arr[0], y: arr[1], width: arr[2], height: arr[3] };
    wasm.exports.free(outPtr);
    return bounds;
  }

  free() {
    if (this.wasmHandle) {
      const wasm = getLaminaWasm();
      wasm.exports.lamina_node_free(this.wasmHandle);
      this.wasmHandle = 0;
    }
  }
}

export function version() {
  return "1.0.1";
}
