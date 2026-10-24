#ifndef TTYPT_LAM_H
#define TTYPT_LAM_H

/*
 * lam.h — Canonical 2D immediate-mode graphics and tilemap interface.
 * Implements two-level WASM import namespace "lamina" with native fallback.
 */

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __wasm__
__attribute__((import_module("lamina"), import_name("lam_fill")))
void lam_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);

__attribute__((import_module("lamina"), import_name("lam_tile_draw")))
void lam_tile_draw(uint32_t ref, uint32_t idx, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t rx, uint32_t ry);

__attribute__((import_module("lamina"), import_name("lam_flush")))
void lam_flush(void);

__attribute__((import_module("lamina"), import_name("lam_canvas_flush")))
void lam_canvas_flush(const void *buf, uint32_t w, uint32_t h);
#else
static inline void lam_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) { (void)x; (void)y; (void)w; (void)h; (void)color; }
static inline void lam_tile_draw(uint32_t ref, uint32_t idx, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t rx, uint32_t ry) { (void)ref; (void)idx; (void)x; (void)y; (void)w; (void)h; (void)rx; (void)ry; }
static inline void lam_flush(void) {}
static inline void lam_canvas_flush(const void *buf, uint32_t w, uint32_t h) { (void)buf; (void)w; (void)h; }
#endif

#ifdef __cplusplus
}
#endif

#endif /* TTYPT_LAM_H */
