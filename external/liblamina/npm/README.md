# @tty-pt/lamina

[![npm version](https://img.shields.io/badge/npm-v0.1.0-CB3837?logo=npm)](#)
[![License: BSD-2-Clause](https://img.shields.io/badge/License-BSD--2--Clause-blue)](#)
[![TypeScript Ready](https://img.shields.io/badge/TypeScript-Ready-3178C6?logo=typescript)](#)
[![Zero Dependencies](https://img.shields.io/badge/dependencies-0-success)](#)

> Universal Node.js and Browser bindings for **`liblamina`**, the 2D immediate-mode canvas graphics, tilemap rendering, and WebGL viewport engine.

---

## 1. Quickstart

```bash
npm install @tty-pt/lamina
```

```typescript
import { ViewportController } from "@tty-pt/lamina";

const viewport = new ViewportController(1280, 720);
viewport.setCamera(100, 200, 1.5 /* zoom */);

// Transform game/map coordinates to canvas screen pixels
const screen = viewport.worldToScreen(100, 200);
console.log(screen); // { x: 640, y: 360 }
```

---

## 2. JavaScript / TypeScript API Reference

### `ViewportController`
2D tilemap and canvas camera viewport controller:
* `constructor(width?: number, height?: number)`: Initializes viewport with dimensions (default: 640x480).
* `.setCamera(x: number, y: number, zoom?: number): void`: Updates camera center position and zoom factor.
* `.worldToScreen(worldX: number, worldY: number): { x: number, y: number }`: Projects continuous world coordinates to screen canvas pixels. **Executes strictly in compiled WebAssembly / C** via `lamina_wasm_world_to_screen()`.
* `.screenToWorld(screenX: number, screenY: number): { x: number, y: number }`: Unprojects screen canvas pixel coordinates back to continuous world coordinates strictly via WebAssembly.

### `getLaminaWasm(): WebAssembly.Instance`
Returns the WebAssembly instance loaded on the Stele shared-memory substrate.

### `version(): string`
Returns the library semantic version string.

---

## 3. C Development Headers via NPM

```makefile
CFLAGS += -I./node_modules/@tty-pt/lamina/include
```

## License

BSD 2-Clause License. Copyright (c) 2026, tty-pt.
