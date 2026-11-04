# @tty-pt/transp

[![npm version](https://img.shields.io/badge/npm-v0.1.0-CB3837?logo=npm)](#)
[![License: BSD-2-Clause](https://img.shields.io/badge/License-BSD--2--Clause-blue)](#)
[![TypeScript Ready](https://img.shields.io/badge/TypeScript-Ready-3178C6?logo=typescript)](#)
[![Node.js Native](https://img.shields.io/badge/Node.js-N--API-339933?logo=node.js)](#)
[![Zero Dependencies](https://img.shields.io/badge/dependencies-0-success)](#)

> Universal Node.js and Browser bindings for **`libtransp`**, the pure-C musical chord transposition and lyrics alignment engine.

---

## 1. Quickstart

```bash
npm install @tty-pt/transp
```

```typescript
import { transposeText, TRANSP_HTML, TRANSP_BEMOL } from "@tty-pt/transp";

// 1. Transpose chords up 2 semitones: C -> D, G -> A, Am -> Bm, F -> G
const input = "C G Am F\nSinging in the morning sun";
const transposed = transposeText(input, 2);
console.log(transposed);

// 2. Output formatted HTML markup for songbook / web viewers:
const html = transposeText(input, 2, TRANSP_HTML);
console.log(html);
// <div><b>D A Bm G</b></div><div>Singing in the morning sun</div>
```

---

## 2. Supported Output Flags

- `TRANSP_HTML`: Wraps lines in `<div>` and bold chord blocks `<b>`.
- `TRANSP_BEMOL`: Renders accidentals as flats (`Db` instead of `C#`).
- `TRANSP_LATIN`: Uses Latin solfège note names (`Do`, `Re`, `Mi`, `Fa`, `Sol`, `La`, `Si`).
- `TRANSP_HIDE_CHORDS`: Strips chord lines, rendering lyrics only.
- `TRANSP_HIDE_LYRICS`: Strips lyric lines, rendering chords only.

---

## 3. JavaScript / TypeScript API Reference

### `transposeText(text: string, semitones: number, flags?: number): string`
Parses, transposes, and renders text with musical chords while preserving character column alignment. **Executes strictly in compiled WebAssembly / C** via `transp_wasm_transpose()`.
* `text`: Multi-line lyrics/chord string.
* `semitones`: Pitch shift from `-11` to `+11`.
* `flags`: Optional bitwise combination of transposition flags (`TRANSP_HTML`, `TRANSP_BEMOL`, etc.).

### `getTranspWasm(): WebAssembly.Instance`
Returns the WebAssembly instance loaded on the Stele shared-memory substrate.

### Transposition Flags
* `TRANSP_HIDE_CHORDS = 0x01`
* `TRANSP_HIDE_LYRICS = 0x02`
* `TRANSP_HTML = 0x04`
* `TRANSP_BEMOL = 0x08`
* `TRANSP_REMOVE_COMMENTS = 0x10`
* `TRANSP_BREAK_SLASH = 0x20`
* `TRANSP_LATIN = 0x80`

---

## 4. C Development Headers via NPM

```makefile
CFLAGS += -I./node_modules/@tty-pt/transp/include
LDLIBS += -ltransp
```

---

## 4. Building from Source

```bash
make npm-build    # Compiles native Node-API addon inside npm/
make npm-test     # Runs verification test suite
```

## License

BSD 2-Clause License. Copyright (c) 2026, tty-pt.
