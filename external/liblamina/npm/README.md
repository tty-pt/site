# @ttypt/lamina

[![npm version](https://img.shields.io/badge/npm-v0.1.0-CB3837?logo=npm)](#)
[![License: BSD-2-Clause](https://img.shields.io/badge/License-BSD--2--Clause-blue)](#)
[![TypeScript Ready](https://img.shields.io/badge/TypeScript-Ready-3178C6?logo=typescript)](#)
[![Zero Dependencies](https://img.shields.io/badge/dependencies-0-success)](#)

> Universal Node.js and Browser bindings for **`liblamina`**, the 2D immediate-mode canvas graphics, tilemap rendering, and WebGL viewport engine.

---

## 1. Quickstart

```bash
npm install @ttypt/lamina
```

```typescript
import { ViewportController } from "@ttypt/lamina";

const viewport = new ViewportController(1280, 720);
viewport.setCamera(100, 200, 1.5 /* zoom */);

// Transform game/map coordinates to canvas screen pixels
const screen = viewport.worldToScreen(100, 200);
console.log(screen); // { x: 640, y: 360 }
```

---

## 2. C Development Headers via NPM

```makefile
CFLAGS += -I./node_modules/@ttypt/lamina/include
```

## License

BSD 2-Clause License. Copyright (c) 2026, tty-pt.
