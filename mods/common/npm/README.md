# @ttypt/site-ui

[![npm version](https://img.shields.io/badge/npm-v0.1.0-CB3837?logo=npm)](#)
[![License: BSD-2-Clause](https://img.shields.io/badge/License-BSD--2--Clause-blue)](#)
[![TypeScript Ready](https://img.shields.io/badge/TypeScript-Ready-3178C6?logo=typescript)](#)
[![Zero Dependencies](https://img.shields.io/badge/dependencies-0-success)](#)

> Universal Node.js and Browser bindings for **`mods/common`**, the pure-C UI primitives, layout scaffolding, and media player controls used across the `tty.pt` music platform.

---

## 1. Quickstart

```bash
npm install @ttypt/site-ui
```

```typescript
import { createPageLayout, createMediaControls } from "@ttypt/site-ui";

// 1. Generate standard dual-root page layout
const layout = createPageLayout({ title: "My Song Portal", activeNav: "song" });
console.log(layout.renderHtml());

// 2. Manage audio/media controls
const player = createMediaControls({ mediaUrl: "song.mp3", duration: 210 });
player.play();
```

---

## 2. C Development Headers via NPM

```makefile
CFLAGS += -I./node_modules/@ttypt/site-ui/include
```

## License

BSD 2-Clause License. Copyright (c) 2026, tty-pt.
