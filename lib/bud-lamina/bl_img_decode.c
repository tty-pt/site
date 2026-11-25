#define _POSIX_C_SOURCE 200809L

#include "bl_img_decode.h"
#include <dlfcn.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_GIF
#define STBI_ONLY_BMP
#define STBI_MAX_DIMENSIONS 8192

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
#include "stb_image.h"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

typedef uint8_t *(*webp_decode_rgba_fn)(const uint8_t *data, size_t data_size,
                                        int *width, int *height);
typedef void (*webp_free_fn)(void *ptr);

static uint8_t *decode_webp_rgba(const uint8_t *bytes, size_t len,
                                 uint32_t *w_out, uint32_t *h_out)
{
	static int tried = 0;
	static webp_decode_rgba_fn fn_decode = NULL;
	static webp_free_fn fn_free = NULL;

	if (!tried) {
		tried = 1;
		void *so = dlopen("libwebp.so.7", RTLD_LAZY);
		if (!so) so = dlopen("libwebp.so", RTLD_LAZY);
		if (so) {
			/* ISO C forbids direct object-to-function pointer cast under -Wpedantic */
			void *p_dec = dlsym(so, "WebPDecodeRGBA");
			void *p_fre = dlsym(so, "WebPFree");
			memcpy(&fn_decode, &p_dec, sizeof(fn_decode));
			memcpy(&fn_free, &p_fre, sizeof(fn_free));
		}
	}
	if (!fn_decode || !fn_free)
		return NULL;

	int w = 0, h = 0;
	uint8_t *raw = fn_decode(bytes, len, &w, &h);
	if (!raw || w <= 0 || h <= 0 || w > 8192 || h > 8192) {
		if (raw) fn_free(raw);
		return NULL;
	}
	size_t n = (size_t)w * (size_t)h * 4u;
	uint8_t *copy = malloc(n);
	if (copy) {
		memcpy(copy, raw, n);
		if (w_out) *w_out = (uint32_t)w;
		if (h_out) *h_out = (uint32_t)h;
	}
	fn_free(raw);
	return copy;
}

uint8_t *bl_img_decode_rgba(const uint8_t *bytes, size_t len,
                            uint32_t *w_out, uint32_t *h_out)
{
	if (!bytes || len < 12 || len > (size_t)INT_MAX)
		return NULL;

	/* WebP: RIFF....WEBP */
	if (memcmp(bytes, "RIFF", 4) == 0 && memcmp(bytes + 8, "WEBP", 4) == 0)
		return decode_webp_rgba(bytes, len, w_out, h_out);

	int w = 0, h = 0, comp = 0;
	unsigned char *px = stbi_load_from_memory(bytes, (int)len, &w, &h, &comp, 4);
	if (!px)
		return NULL;
	if (w <= 0 || h <= 0 || w > 8192 || h > 8192) {
		stbi_image_free(px);
		return NULL;
	}
	if (w_out) *w_out = (uint32_t)w;
	if (h_out) *h_out = (uint32_t)h;
	return (uint8_t *)px;
}
