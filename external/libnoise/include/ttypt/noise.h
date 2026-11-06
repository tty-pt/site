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
 *   3. Wrapping Original-Algorithm Sampler (noise_wpoint_n, noise_wpoint_c,
 *      noise_wrow): point / cached / row-stepped queries that reproduce the
 *      matrix stepper exactly, so coordinates (e.g. an ever-increasing time
 *      counter cast to int16_t) may keep incrementing across the data-type
 *      boundary without a seam.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#ifdef __has_include
  #if __has_include(<xxhash.h>)
    /* Inline the implementation: a bare XXH32 reference becomes an
     * unresolved env import in WASM builds, which hosts stub to 0 and
     * flatten every noise sample to a constant. */
    #ifndef XXH_INLINE_ALL
      #define XXH_INLINE_ALL
    #endif
    /* Byte-wise reads: the default packed-union reader type-puns the
     * int16_t lattice key as uint32 and clang -O3 -flto (WASM) drops the
     * key stores under strict aliasing, so every sample read 0. Method 3
     * is alias-safe, libc-free, and yields the identical hash. */
    #ifndef XXH_FORCE_MEMORY_ACCESS
      #define XXH_FORCE_MEMORY_ACCESS 3
    #endif
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

/* --- Wrapping Original-Algorithm Sampler --------------------------------- */

/* Point queries that follow the ORIGINAL lattice algorithm of noise_m:
 *   - lattice keys are the masked int16 coordinates themselves
 *     (s = p & COORDMASK_LSHIFT(x)), hashed with noise_r;
 *   - the far vertex of a quad is s + 2^x computed in int16_t, so it wraps
 *     exactly like the coordinate does (32767 + 1 == -32768): no seam at
 *     the data-type boundary on any axis;
 *   - interpolation is the matrix stepper's: axis 0 first, step = (v1 - v0)
 *     >> x, value = v0 + f * step (see calc_steps / step / noise_quad).
 * Results are therefore value-equal to noise_m over lattice-aligned
 * matrices. Values are on noise_m's scale: (hash >> w), interpolated.
 * With NOISE_FADE, f is remapped by a Hermite smoothstep onto [0, 2^x)
 * (lattice values are unchanged, derivatives vanish at cell borders).
 * Requires 1 <= dim <= 4 and 1 <= x <= 15. */

#define NOISE_FADE 1u

/* Hermite smoothstep of f in [0, 2^x), mapped onto [0, 2^x) */
static inline uint32_t
noise_wfade(uint32_t f, unsigned x)
{
	return (uint32_t) (((uint64_t) f * f * ((3ull << x) - 2ull * f)) >> (2 * x));
}

/* Matrix-stepper step between two vertex values (calc_steps, y = 0) */
static inline int32_t
noise_wstep(uint32_t v0, uint32_t v1, unsigned x)
{
	return (int32_t) (((int64_t) v1 - (int64_t) v0) >> x);
}

/* Collapse the first `levels` axes of the 2^dim vertex values in v,
 * axis 0 first, at in-quad offsets f[] (already faded if requested). */
static inline void
noise_wreduce(uint32_t *v, const uint32_t *f, unsigned x, unsigned levels, uint8_t dim)
{
	unsigned half = 1u << dim;
	for (unsigned j = 0; j < levels; j++) {
		half >>= 1;
		for (unsigned n = 0; n < half; n++)
			v[n] = v[n] + (uint32_t) ((int64_t) noise_wstep(v[n], v[n + half], x) * f[j]);
	}
}

/* Lattice cell origin and in-quad offsets of p */
static inline void
noise_wcell(int16_t *s, uint32_t *f, const int16_t *p, unsigned x, unsigned flags, uint8_t dim)
{
	const uint16_t mask = COORDMASK_LSHIFT(x);
	for (unsigned j = 0; j < dim; j++) {
		s[j] = (int16_t) (p[j] & mask);
		f[j] = (uint16_t) p[j] & (uint16_t) ~mask;
		if (flags & NOISE_FADE)
			f[j] = noise_wfade(f[j], x);
	}
}

/* Uncached point query (2^dim hashes) */
static inline uint32_t
noise_wpoint_n(const int16_t *p, unsigned x, unsigned w, unsigned seed, uint8_t dim, unsigned flags)
{
	int16_t s[4];
	uint32_t f[4], v[16];
	noise_wcell(s, f, p, x, flags, dim);
	noise_get_v(v, s, x, w, seed, dim);
	noise_wreduce(v, f, x, dim, dim);
	return v[0];
}

static inline uint32_t
noise_wpoint_2d(int16_t x0, int16_t x1, unsigned x, unsigned w, unsigned seed, unsigned flags)
{
	int16_t p[2] = { x0, x1 };
	return noise_wpoint_n(p, x, w, seed, 2, flags);
}

static inline uint32_t
noise_wpoint_3d(int16_t x0, int16_t x1, int16_t x2, unsigned x, unsigned w, unsigned seed, unsigned flags)
{
	int16_t p[3] = { x0, x1, x2 };
	return noise_wpoint_n(p, x, w, seed, 3, flags);
}

/* Corner-reuse cache: remembers the vertex values of the last lattice
 * cell, so slowly-varying (e.g. domain-warped) coordinates hash only when
 * they enter a new cell. `fetches` counts corner-set fetches (each one is
 * 2^dim hashes) for profiling; callers may reset it at will. */
typedef struct {
	int16_t s[4];
	uint32_t v[16];
	unsigned x, w, seed;
	uint8_t dim, valid;
	uint32_t fetches;
} noise_wcache_t;

static inline void
noise_wcache_init(noise_wcache_t *c)
{
	memset(c, 0, sizeof(*c));
}

/* Vertex values of the cell at s, from the cache when it matches */
static inline const uint32_t *
noise_wcorners(noise_wcache_t *c, int16_t *s, unsigned x, unsigned w, unsigned seed, uint8_t dim)
{
	int hit = c->valid && c->x == x && c->w == w && c->seed == seed && c->dim == dim;
	for (unsigned j = 0; hit && j < dim; j++)
		hit = c->s[j] == s[j];
	if (!hit) {
		noise_get_v(c->v, s, x, w, seed, dim);
		memcpy(c->s, s, sizeof(int16_t) * dim);
		c->x = x; c->w = w; c->seed = seed; c->dim = dim; c->valid = 1;
		c->fetches++;
	}
	return c->v;
}

/* Cached point query: value-equal to noise_wpoint_n */
static inline uint32_t
noise_wpoint_c(noise_wcache_t *c, const int16_t *p, unsigned x, unsigned w, unsigned seed, uint8_t dim, unsigned flags)
{
	int16_t s[4];
	uint32_t f[4], v[16];
	noise_wcell(s, f, p, x, flags, dim);
	memcpy(v, noise_wcorners(c, s, x, w, seed, dim), sizeof(uint32_t) << dim);
	noise_wreduce(v, f, x, dim, dim);
	return v[0];
}

/* Row stepper: out[k] = noise_wpoint_n(p0 + k * dstep along the LAST axis),
 * k < n, the coordinate wrapping in int16_t. Like noise_get_v + noise_quad,
 * it fetches the 2^dim corners once per lattice cell and collapses the
 * other axes once per cell; each sample is then one multiply-add on the
 * innermost axis. `c` may be NULL. */
static inline void
noise_wrow(uint32_t *out, unsigned n, const int16_t *p0, int16_t dstep, unsigned x,
           unsigned w, unsigned seed, uint8_t dim, unsigned flags, noise_wcache_t *c)
{
	const unsigned L = dim - 1u;
	const uint16_t mask = COORDMASK_LSHIFT(x), lo = (uint16_t) ~mask;
	noise_wcache_t local;
	int16_t p[4], s[4];
	uint32_t f[4], v[16], b0 = 0;
	int32_t st = 0;
	int have = 0;

	if (!c) {
		noise_wcache_init(&local);
		c = &local;
	}
	memcpy(p, p0, sizeof(int16_t) * dim);

	for (unsigned k = 0; k < n; k++, p[L] = (int16_t) (p[L] + dstep)) {
		int16_t sl = (int16_t) (p[L] & mask);
		uint32_t fl = (uint16_t) p[L] & lo;

		if (!have || sl != s[L]) {
			noise_wcell(s, f, p, x, flags, dim);
			memcpy(v, noise_wcorners(c, s, x, w, seed, dim), sizeof(uint32_t) << dim);
			noise_wreduce(v, f, x, L, dim);
			b0 = v[0];
			st = noise_wstep(v[0], v[1], x);
			have = 1;
		}
		if (flags & NOISE_FADE)
			fl = noise_wfade(fl, x);
		out[k] = b0 + (uint32_t) ((int64_t) st * fl);
	}
}

/* Plane stepper: out[r * stride + k] = noise_wpoint_n(p0 with the last two
 * axes advanced by r (rows, axis dim-2) and k (columns, axis dim-1)), for
 * r < rows, k < n; both coordinates wrap in int16_t. In noise_quad order it
 * fetches the 2^dim corners ONCE per 2D lattice cell (rows x cols / 4^x
 * fetches), collapses the fixed outer axes once per cell, the row axis once
 * per row, and spends one multiply-add per sample. Requires dim >= 2.
 * `c` may be NULL. */
static inline void
noise_wplane(uint32_t *out, unsigned n, unsigned rows, size_t stride, const int16_t *p0,
             unsigned x, unsigned w, unsigned seed, uint8_t dim, unsigned flags, noise_wcache_t *c)
{
	const unsigned R = dim - 2u, C = dim - 1u, side = 1u << x;
	const uint16_t lo = (uint16_t) (side - 1u);
	noise_wcache_t local;
	int16_t p[4], s[4];
	uint32_t f[4], v[16];

	if (!c) {
		noise_wcache_init(&local);
		c = &local;
	}
	memcpy(p, p0, sizeof(int16_t) * dim);

	for (unsigned r = 0; r < rows; ) {
		int16_t pr = (int16_t) (p0[R] + (int) r);
		unsigned rlen = side - ((uint16_t) pr & lo);
		if (rlen > rows - r)
			rlen = rows - r;
		p[R] = pr;

		for (unsigned k = 0; k < n; ) {
			int16_t pc = (int16_t) (p0[C] + (int) k);
			unsigned klen = side - ((uint16_t) pc & lo);
			if (klen > n - k)
				klen = n - k;
			p[C] = pc;

			/* one corner fetch per 2D cell; collapse outer axes */
			noise_wcell(s, f, p, x, flags, dim);
			memcpy(v, noise_wcorners(c, s, x, w, seed, dim), sizeof(uint32_t) << dim);
			noise_wreduce(v, f, x, R, dim);
			{
				const uint32_t a00 = v[0], a01 = v[1], a10 = v[2], a11 = v[3];
				const int32_t s0 = noise_wstep(a00, a10, x), s1 = noise_wstep(a01, a11, x);
				uint32_t fr0 = (uint16_t) pr & lo, fc0 = (uint16_t) pc & lo;

				for (unsigned i = 0; i < rlen; i++) {
					uint32_t fr = fr0 + i, *o = out + (size_t) (r + i) * stride + k;
					if (flags & NOISE_FADE)
						fr = noise_wfade(fr, x);
					const uint32_t b0 = a00 + (uint32_t) ((int64_t) s0 * fr);
					const uint32_t b1 = a01 + (uint32_t) ((int64_t) s1 * fr);
					const int32_t st = noise_wstep(b0, b1, x);
					for (unsigned j = 0; j < klen; j++) {
						uint32_t fc = fc0 + j;
						if (flags & NOISE_FADE)
							fc = noise_wfade(fc, x);
						o[j] = b0 + (uint32_t) ((int64_t) st * fc);
					}
				}
			}
			k += klen;
		}
		r += rlen;
	}
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
