#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define XXH_INLINE_ALL 1
#include <xxhash.h>
#include <ttypt/noise.h>
#include <ttypt/iso.h>

ISO_EXPORT(int32_t, noise_wasm_point_1d, int32_t x, uint32_t seed, uint32_t shift) {
    return noise_point_1d((int16_t)x, seed, (uint8_t)shift);
}

ISO_EXPORT(int32_t, noise_wasm_point_2d, int32_t x, int32_t y, uint32_t seed, uint32_t shift) {
    return noise_point_2d((int16_t)x, (int16_t)y, seed, (uint8_t)shift);
}

ISO_EXPORT(int32_t, noise_wasm_point_3d, int32_t x, int32_t y, int32_t z, uint32_t seed, uint32_t shift) {
    return noise_point_3d((int16_t)x, (int16_t)y, (int16_t)z, seed, (uint8_t)shift);
}

ISO_EXPORT(int32_t, noise_wasm_point_4d, int32_t x, int32_t y, int32_t z, int32_t w, uint32_t seed, uint32_t shift) {
    int16_t p[4] = { (int16_t)x, (int16_t)y, (int16_t)z, (int16_t)w };
    return noise_point_n(p, seed, (uint8_t)shift, 4);
}

ISO_EXPORT(int32_t, noise_wasm_point_n, const int16_t *coords, uint32_t seed, uint32_t shift, uint32_t dim) {
    if (!coords || dim == 0 || dim > 4) return 128;
    return noise_point_n(coords, seed, (uint8_t)shift, (uint8_t)dim);
}

ISO_EXPORT(void, noise_wasm_octaves, uint32_t *m, const int16_t *s, size_t oct_n, const uint32_t *oct_tuples, uint32_t seed, uint32_t cy, uint32_t dim) {
    if (!m || !s || !oct_tuples || oct_n == 0 || dim == 0) return;
    octave_t oct[16];
    size_t count = oct_n > 16 ? 16 : oct_n;
    for (size_t i = 0; i < count; i++) {
        oct[i].x = oct_tuples[i * 2];
        oct[i].w = oct_tuples[i * 2 + 1];
    }
    noise_oct(m, (int16_t *)s, count, oct, seed, cy, (uint8_t)dim);
}
