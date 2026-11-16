# @tty-pt/site-ui

[![npm version](https://img.shields.io/badge/npm-v0.1.0-CB3837?logo=npm)](#)
[![License: BSD-2-Clause](https://img.shields.io/badge/License-BSD--2--Clause-blue)](https://github.com/tty-pt/site/blob/main/LICENSE)
[![TypeScript Ready](https://img.shields.io/badge/TypeScript-Ready-3178C6?logo=typescript)](#)
[![Zero Dependencies](https://img.shields.io/badge/dependencies-0-success)](#)

> Universal Node.js and Browser bindings for **`mods/common`**, the pure-C UI primitives, dual-root layout scaffolding (`#chrome-root` + `#bud-root`), form action bars, delete confirmation dialogs, and media player controls used across the `tty.pt` music platform. Canonical C header: `<site_ui.h>`.

📖 **Full C & Platform Documentation**: See the main [**site GitHub README**](https://github.com/tty-pt/site#readme) and [`mods/common/README.md`](https://github.com/tty-pt/site/blob/main/mods/common/README.md) for isomorphic SSR/WASM UI conventions.

---

## 1. Overview & Domain Concepts

`@tty-pt/site-ui` exposes the shared UI layout and control primitives from `mods/common`:

1. **Dual-Root Isomorphic Page Scaffolding (`createPageLayout`)**:
   - Generates the standard `#chrome-root` (persistent site navigation/audio chrome) and `#bud-root` (page content slot) HTML structure.
2. **Standardized Form Action & Delete Confirmation Controls (`createFormActions`, `createDeleteConfirmForm`)**:
   - Produces consistent submit/cancel button bars and CSRF-protected delete confirmation forms across modules (`poem`, `song`, `gig`, `grp`).
3. **Stateful Audio & Media Controls (`createMediaControls`)**:
   - Manages playback state (`play`, `pause`, `seek`, `currentTime`, `duration`) for song audio streams.

---

## 2. Installation

```bash
npm install @tty-pt/site-ui
```

---

## 3. Quickstart

```typescript
import {
  createPageLayout,
  createMediaControls,
  createFormActions,
  createDeleteConfirmForm
} from "@tty-pt/site-ui";

// 1. Generate standard dual-root page layout
const layout = createPageLayout({ title: "Song Catalog", activeNav: "song" });
console.log(layout.renderHtml());

// 2. Manage audio/media controls
const player = createMediaControls({ mediaUrl: "/song/1/audio.mp3", duration: 210 });
player.play();
player.seek(45);

// 3. Render standard form actions & delete confirmation modal
const actions = createFormActions({ cancelHref: "/song", submitLabel: "Save Song" });
const confirmDel = createDeleteConfirmForm({
  module: "song",
  id: "song_1",
  title: "Ave Maria"
});
console.log(actions.renderHtml(), confirmDel.renderHtml());
```

---

## 4. JavaScript / TypeScript API Reference

- `createPageLayout(opts?: PageLayoutOptions): PageLayout`: Builds `{ chromeRoot, budRoot, meta, renderHtml() }`.
- `createMediaControls(opts?: { mediaUrl?: string; duration?: number }): MediaControls`: Stateful controller with `.play()`, `.pause()`, `.seek(time)`, `.isPlaying`, and `.currentTime`.
- `createFormActions(opts?: FormActionsOptions): FormActions`: Renders standardized form submit/cancel action markup.
- `createDeleteConfirmForm(opts: DeleteConfirmOptions): DeleteConfirmForm`: Renders a CSRF-aware delete confirmation form for a module item.
- `version(): string`: Returns the package semantic version string.

---

## 5. C Development Headers & Native Integration

```makefile
CFLAGS += -I./node_modules/@tty-pt/site-ui/include
```

---

## License

BSD-2-Clause — Copyright (c) 2026, Paulo Quirino <quirinpa@gmail.com>. See [LICENSE](https://github.com/tty-pt/site/blob/main/LICENSE).
