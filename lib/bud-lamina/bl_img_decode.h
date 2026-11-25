#ifndef BL_IMG_DECODE_H
#define BL_IMG_DECODE_H

#include <stddef.h>
#include <stdint.h>

/* Decode PNG, JPEG (baseline & progressive), GIF, BMP, or WebP bytes in memory
 * into tightly-packed 4-channel RGBA8 pixels allocated via malloc().
 * Caller must free() the returned buffer. Returns NULL on failure. */
uint8_t *bl_img_decode_rgba(const uint8_t *bytes, size_t len,
                            uint32_t *w_out, uint32_t *h_out);

#endif /* BL_IMG_DECODE_H */
