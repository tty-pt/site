# libnoise — Standalone Generic N-Dimensional Procedural Noise Library

Pure-C, zero-dependency, generic N-dimensional fixed-point noise library.

## Features
- **Generic across dimensions ($N = 1, 2, 3, 4, \dots$)**: Parameterized by `uint8_t dim`.
- **Matrix Stepper**: Bulk hypercube matrix stepping (`noise_oct`, `noise_m`, `noise_quad`) for generating dense $(2^{cy})^{\text{dim}}$ matrices without per-point function call overhead.
- **Point Sampler**: $O(1)$ zero-allocation point evaluation (`noise_point_n`, `noise_point_2d`, `noise_point_3d`, `noise_point_4d`) using Hermite smoothstep and multilinear hypercube vertex interpolation.
- **Zero Allocations & Deterministic**: Integer fixed-point math, suitable for real-time systems, native C modules, and WebAssembly kernels.

## Header Usage
```c
#include <ttypt/noise.h>

/* Point evaluation in 3D: */
int val3d = noise_point_3d(x, y, z, seed, shift);

/* Matrix evaluation in 3D: */
uint32_t cube[512]; /* (2^3)^3 = 8x8x8 */
int16_t origin[3] = { 0, 0, 0 };
octave_t oct[] = { { 4, 1 } };
noise_oct(cube, origin, 1, oct, seed, 3, 3);
```

## License
CC BY-NC-SA 4.0 — Author: Paulo Quirino <q@qnixsoft.com>
