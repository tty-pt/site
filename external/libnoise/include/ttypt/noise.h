#ifndef TTYPT_NOISE_H
#define TTYPT_NOISE_H

/*
 * ttypt/noise.h — Standalone Generic N-Dimensional Procedural Noise Library.
 *
 * A noise algorithm that wraps well around data type limits.
 *
 * LICENCE: CC BY-NC-SA 4.0
 * Author: Paulo Quirino <q@qnixsoft.com>
 *
 * DEFINITIONS:
 *
 * A point has DIM coordinates (int16_t).
 * A matrix has 2^y (cy) edge length and (2^cy)^dim total elements.
 * A noise feature quad has 2^x edge length and 2^dim vertices with random values.
 * Other values are the result of a fade (multilinear smoothstep) between those.
 *
 * Provides:
 *   1. Dense Matrix Stepper (noise_oct, noise_m, noise_quad): bulk hypercube generation.
 *   2. Generic Point Sampler (noise_point_n, noise_point_2d, noise_point_3d): O(1) zero-allocation point queries.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#ifdef __has_include
  #if __has_include(<xxhash.h>)
    #include <xxhash.h>
    #define TTYPT_NOISE_HASH XXH32
  #endif
#endif

#ifndef TTYPT_NOISE_HASH
static inline uint32_t
ttypt_noise_mix32(const void *key, size_t len, uint32_t seed)
{
	const uint8_t *data = (const uint8_t *)key;
	uint32_t h = seed ^ 0x9E3779B1u;
	for (size_t i = 0; i < len; i++) {
		h ^= (uint32_t)data[i] * 0x85EBCA6Bu;
		h ^= h >> 13;
		h *= 0xC2B2AE35u;
		h ^= h >> 16;
	}
	return h;
}
#define TTYPT_NOISE_HASH ttypt_noise_mix32
#endif

#define NOISE_MAX ((uint32_t) -1)
#define COORDMASK_LSHIFT(x) (uint16_t) ((((uint16_t) -1) << (x)))

typedef struct {
	unsigned x; /* 2^x = side length of noise quad */
	unsigned w; /* weight / bitshift divider */
} octave_t;

/* Euclidean integer floor division */
static inline int
noise_fdiv(int a, int b)
{
	int d = a / b;
	return (a ^ b) < 0 && (d * b != a) ? d - 1 : d;
}

/* Deterministic random value for an N-dimensional point p */
static inline uint32_t
noise_r(int16_t *p, unsigned seed, unsigned w, uint8_t dim)
{
	register uint32_t v = TTYPT_NOISE_HASH(p, sizeof(int16_t) * dim, seed);
	return ((long long unsigned) v) >> w;
}

/* Generate values at N-dimensional quad vertices */
static inline void
noise_get_v(uint32_t *v, int16_t *s, uint16_t x, unsigned w, unsigned seed, uint8_t dim)
{
	const int16_t d = 1 << x;
	int16_t va[dim * (1 << dim)];
	int i, j;

	for (i = 0; i < (1 << dim); i++) {
		for (j = 0; j < dim; j++)
			va[i * dim + j] = s[j] + ((i & (1 << (dim - j - 1))) ? d : 0);

		v[i] = noise_r(va + dim * i, seed, w, dim);
	}
}

static inline void
calc_steps(int32_t *st, uint32_t *v, uint16_t z, uint16_t vl, unsigned y)
{
	int n;
	for (n = 0; n < vl; n++)
		st[n] = ((long) v[n + vl] - v[n]) >> z << y;
}

static inline void
step(uint32_t *v, int32_t *st, uint16_t vl, uint16_t mul)
{
	int n;
	for (n = 0; n < vl; n++)
		v[n] = (long) v[n] + ((long) st[n] * mul);
}

/* Fade between vertices and step target values across matrix */
static inline void
noise_quad(uint32_t *c, uint32_t *vc, unsigned z, unsigned cy, uint8_t dim)
{
	uint16_t ndim = dim - 1;
	uint16_t tvl = 1 << ndim;
	int32_t st[(tvl << 1) - 1], *stc = st;
	uint32_t *ce_p[dim], *vt;
	size_t cd = 1 << (cy * ndim);
	goto start;

	do {
		do {
			cd >>= cy; ndim--; vc = vt; stc += tvl; tvl >>= 1;
start:			ce_p[ndim] = c + (cd << z);
			vt = vc + (tvl << 1);

			calc_steps(stc, vc, z, tvl, 0);
			memcpy(vt, vc, tvl * sizeof(uint32_t));
		} while (ndim);

		for (; c < ce_p[0]; c += cd, vt[0] += *stc)
			*c += vt[0];

		do {
			++ndim;
			if (ndim >= dim)
				return;
			c -= cd << z; cd <<= cy; vt = vc;
			tvl <<= 1; stc -= tvl; vc -= (tvl << 1);

			step(vt, stc, tvl, 1);
			c += cd;
		} while (c >= ce_p[ndim]);
	} while (1);
}

static inline void
_noise_mr(uint32_t *c, uint32_t *v, unsigned x, int16_t *qs, uint16_t ndim,
          unsigned w, unsigned seed, unsigned cy, uint8_t dim)
{
	int i = dim - 1 - ndim;
	uint16_t ced = (1 << (cy * (ndim + 1))), cd;
	uint32_t *ce = c + ced;

	ced >>= cy;
	cd = ced << x;

	for (; c < ce; qs[i] += (1 << x), c += cd) {
		if (ndim == 0) {
			noise_get_v(v, qs, x, w, seed, dim);
			noise_quad(c, v, x, cy, dim);
		} else {
			_noise_mr(c, v, x, qs, ndim - 1, w, seed, cy, dim);
		}
	}
	qs[i] -= 1 << cy;
}

static inline void
_noise_m(uint32_t *c, uint32_t *v, unsigned x, int16_t *qs, unsigned w,
         unsigned seed, unsigned cy, uint8_t dim)
{
	_noise_mr(c, v, x, qs, dim - 1, w, seed, cy, dim);
}

static inline void
__fix_v(uint32_t *v, int32_t *st, int16_t *ms, int16_t *qs, unsigned x, uint16_t vl, unsigned cy)
{
	register uint32_t *vn = v + vl;
	register uint16_t dd = (*ms - *qs) >> cy;

	calc_steps(st, v, x, vl, cy);
	if (dd)
		step(v, st, vl, dd);

	memcpy(vn, v, sizeof(uint32_t) * vl);
	step(vn, st, vl, 1);
}

static inline void
noise_fix_v(uint32_t *v, int16_t *qs, int16_t *ms, unsigned x, unsigned cy, uint8_t dim)
{
	uint16_t vl = 1 << dim;
	int32_t st[vl - 1], *stc = st;
	int ndim = dim - 1;
	int first = 1;
	vl >>= 1;

	for (;;) {
		__fix_v(v, stc, ms, qs, x, vl, cy);
		if (ndim) {
			ndim--; qs++; ms++; vl >>= 1; stc += vl;
		} else if (first) {
			v += vl << 1;
			first = 0;
		} else break;
	}
}

static inline void
noise_get_s(int16_t *s, int16_t *p, uint16_t mask, uint8_t dim)
{
	for (int i = 0; i < dim; i++)
		s[i] = ((int16_t) p[i]) & mask;
}

/* Fills matrix "mat" with deterministic N-dimensional noise */
static inline void
noise_m(uint32_t *mat, int16_t *ms, unsigned x, unsigned w, unsigned seed, unsigned cy, uint8_t dim)
{
	uint32_t v[(1 << (dim + 1)) - 1];

	if (cy > x)
		_noise_m(mat, v, x, ms, w, seed, cy, dim);
	else {
		int16_t qs[dim];
		noise_get_s(qs, ms, COORDMASK_LSHIFT(x), dim);
		noise_get_v(v, qs, x, w, seed, dim);
		if (x > cy)
			noise_fix_v(v, qs, ms, x, cy, dim);
		noise_quad(mat, v, cy, cy, dim);
	}
}

/* Multi-octave N-dimensional matrix noise evaluation */
static inline void
noise_oct(uint32_t *m, int16_t *s, size_t oct_n, octave_t *oct, unsigned seed, unsigned cy, uint8_t dim)
{
	unsigned cm = 1 << (dim * cy);
	memset(m, 0, sizeof(uint32_t) * cm);
	for (size_t i = 0; i < oct_n; i++)
		noise_m(m, s, oct[i].x, oct[i].w, seed, cy, dim);
}

/* --- Generic N-Dimensional Point Sampler --------------------------------- */

/* Evaluates continuous N-dimensional smoothstep value noise at point p[dim].
 * Returns fixed-point value in range 0..255. Zero heap allocations. */
static inline int
noise_point_n(const int16_t *p, uint32_t seed, uint8_t shift, uint8_t dim)
{
	if (dim == 0 || dim > 4)
		return 128;

	int cell = 1 << shift;
	int16_t g[4];
	int s[4];

	for (int i = 0; i < dim; i++) {
		int gi = noise_fdiv(p[i], cell);
		g[i] = (int16_t)gi;
		int fi = ((p[i] - gi * cell) * 256) >> shift;
		s[i] = (fi * fi * (768 - 2 * fi)) >> 16; /* Hermite smoothstep 0..255 */
	}

	/* Fetch random values at the 2^dim hypercube vertices */
	int num_v = 1 << dim;
	int v[16];
	int16_t corner[4];

	for (int i = 0; i < num_v; i++) {
		for (int j = 0; j < dim; j++)
			corner[j] = g[j] + ((i >> (dim - 1 - j)) & 1);
		v[i] = (int)(noise_r(corner, seed, 24, dim) & 0xFFu);
	}

	/* Multilinear interpolation across dimensions */
	int cur_v = num_v;
	for (int d = 0; d < dim; d++) {
		int next_v = cur_v >> 1;
		int step_weight = s[dim - 1 - d];
		for (int i = 0; i < next_v; i++) {
			int v0 = v[i << 1];
			int v1 = v[(i << 1) + 1];
			v[i] = v0 + (((v1 - v0) * step_weight) >> 8);
		}
		cur_v = next_v;
	}

	return v[0];
}

/* Point-evaluation ergonomic helpers */
static inline int
noise_point_1d(int16_t x, uint32_t seed, uint8_t shift)
{
	int16_t p[1] = { x };
	return noise_point_n(p, seed, shift, 1);
}

static inline int
noise_point_2d(int16_t x, int16_t y, uint32_t seed, uint8_t shift)
{
	int16_t p[2] = { x, y };
	return noise_point_n(p, seed, shift, 2);
}

static inline int
noise_point_3d(int16_t x, int16_t y, int16_t z, uint32_t seed, uint8_t shift)
{
	int16_t p[3] = { x, y, z };
	return noise_point_n(p, seed, shift, 3);
}

static inline int
noise_point_4d(int16_t x, int16_t y, int16_t z, int16_t w, uint32_t seed, uint8_t shift)
{
	int16_t p[4] = { x, y, z, w };
	return noise_point_n(p, seed, shift, 4);
}

#endif /* TTYPT_NOISE_H */
