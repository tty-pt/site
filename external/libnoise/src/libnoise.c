#include <stddef.h>
#include <stdint.h>
#define XXH_INLINE_ALL 1
#include <xxhash.h>
#include <ttypt/noise.h>

int ttypt_noise_point_1d(int16_t x, uint32_t seed, uint8_t shift) {
    return noise_point_1d(x, seed, shift);
}

int ttypt_noise_point_2d(int16_t x, int16_t y, uint32_t seed, uint8_t shift) {
    return noise_point_2d(x, y, seed, shift);
}

int ttypt_noise_point_3d(int16_t x, int16_t y, int16_t z, uint32_t seed, uint8_t shift) {
    return noise_point_3d(x, y, z, seed, shift);
}

int ttypt_noise_point_4d(int16_t x, int16_t y, int16_t z, int16_t w, uint32_t seed, uint8_t shift) {
    return noise_point_4d(x, y, z, w, seed, shift);
}

int ttypt_noise_point_n(const int16_t *p, uint32_t seed, uint8_t shift, uint8_t dim) {
    return noise_point_n(p, seed, shift, dim);
}

void ttypt_noise_oct(uint32_t *m, int16_t *s, size_t oct_n, octave_t *oct, unsigned seed, unsigned cy, uint8_t dim) {
    noise_oct(m, s, oct_n, oct, seed, cy, dim);
}

/* Wrapping original-algorithm sampler (pure CPU, integer-only) */
uint32_t ttypt_noise_wpoint_n(const int16_t *p, unsigned x, unsigned w, unsigned seed, uint8_t dim, unsigned flags) {
    return noise_wpoint_n(p, x, w, seed, dim, flags);
}

uint32_t ttypt_noise_wpoint_c(noise_wcache_t *c, const int16_t *p, unsigned x, unsigned w, unsigned seed, uint8_t dim, unsigned flags) {
    return noise_wpoint_c(c, p, x, w, seed, dim, flags);
}

void ttypt_noise_wrow(uint32_t *out, unsigned n, const int16_t *p0, int16_t dstep, unsigned x, unsigned w, unsigned seed, uint8_t dim, unsigned flags, noise_wcache_t *c) {
    noise_wrow(out, n, p0, dstep, x, w, seed, dim, flags, c);
}

void ttypt_noise_wplane(uint32_t *out, unsigned n, unsigned rows, size_t stride, const int16_t *p0, unsigned x, unsigned w, unsigned seed, uint8_t dim, unsigned flags, noise_wcache_t *c) {
    noise_wplane(out, n, rows, stride, p0, x, w, seed, dim, flags, c);
}
