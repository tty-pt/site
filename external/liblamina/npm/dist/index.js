import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __dirname = typeof import.meta !== "undefined" && import.meta.url
  ? path.dirname(fileURLToPath(import.meta.url))
  : "";

let wasmInstance = null;
try {
  const wasmPath = path.join(__dirname, "lamina.wasm");
  if (fs.existsSync(wasmPath)) {
    const wasmBuf = fs.readFileSync(wasmPath);
    const mod = new WebAssembly.Module(wasmBuf);
    wasmInstance = new WebAssembly.Instance(mod, {}).exports;
  }
} catch {}

/**
 * ViewportController: Manages canvas / tilemap viewport transformations and rendering
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
    if (wasmInstance && wasmInstance.lamina_wasm_world_to_screen && wasmInstance.malloc) {
      const outPtr = wasmInstance.malloc(16);
      wasmInstance.lamina_wasm_world_to_screen(worldX, worldY, this.cameraX, this.cameraY, this.zoom, this.width, this.height, outPtr);
      const arr = new Float64Array(wasmInstance.memory.buffer, outPtr, 2);
      const res = { x: Math.round(arr[0]), y: Math.round(arr[1]) };
      wasmInstance.free(outPtr);
      return res;
    }
    const cx = this.width / 2;
    const cy = this.height / 2;
    return {
      x: Math.round(cx + (worldX - this.cameraX) * this.zoom),
      y: Math.round(cy + (worldY - this.cameraY) * this.zoom)
    };
  }

  screenToWorld(screenX, screenY) {
    if (wasmInstance && wasmInstance.lamina_wasm_screen_to_world && wasmInstance.malloc) {
      const outPtr = wasmInstance.malloc(16);
      wasmInstance.lamina_wasm_screen_to_world(screenX, screenY, this.cameraX, this.cameraY, this.zoom, this.width, this.height, outPtr);
      const arr = new Float64Array(wasmInstance.memory.buffer, outPtr, 2);
      const res = { x: Math.round(arr[0]), y: Math.round(arr[1]) };
      wasmInstance.free(outPtr);
      return res;
    }
    const cx = this.width / 2;
    const cy = this.height / 2;
    return {
      x: (screenX - cx) / this.zoom + this.cameraX,
      y: (screenY - cy) / this.zoom + this.cameraY
    };
  }
}

export function version() {
  return "0.1.0";
}
