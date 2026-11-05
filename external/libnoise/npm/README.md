# @tty-pt/noise

Universal Node.js and Browser WebAssembly bindings for `libnoise`, an N-dimensional procedural noise engine with Hermite smoothstep interpolation and hypercube matrix stepping.

## Features
- **Point Queries ($O(1)$)**: Zero-allocation continuous noise queries across 1D, 2D, 3D, and 4D.
- **Matrix Stepping**: Bulk hypercube generation without per-point function call overhead.
- **Dual Runtime**: Native C execution in Node.js via Node-API, and high-performance WebAssembly in the browser via `@tty-pt/stele`.

## Usage
```javascript
import { noisePoint2D, noisePoint3D, noiseOctaves } from "@tty-pt/noise";

// 2D continuous point query (returns 0..255)
const val2d = noisePoint2D(100, 200, 42 /* seed */, 4 /* shift */);

// 3D continuous point query
const val3d = noisePoint3D(10, 20, 30, 42, 4);

// Multi-octave 2D matrix (16x16 = 256 cells)
const grid = noiseOctaves(2, [0, 0], [{ x: 4, w: 1 }], 42, 4);
```

## License
CC BY-NC-SA 4.0 — Author: Paulo Quirino <q@qnixsoft.com>
