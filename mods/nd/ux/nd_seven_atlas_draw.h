#ifndef ND_SEVEN_ATLAS_DRAW_H
#define ND_SEVEN_ATLAS_DRAW_H

#include <stdint.h>
#include <stddef.h>
#include <nd/seven-world.h>
#include "nd_seven_atlas.h"

#define SCALE      2
#define LAMB_SIZE  32
#define LAMB_AX    16
#define LAMB_AY    28

#define RGB(r, g, b) (0xFF000000u | ((uint32_t)(b) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(r))

typedef struct { uint8_t r, g, b, leaf; } tint_t;

typedef struct {
	uint32_t *fb;
	int w, h;
	int ox, oy;
} ctx_t;

void atlas_load(void);
const uint8_t *atlas_px(void);

uint32_t tint_col(uint32_t c, int r, int g, int b);
uint32_t shade(uint32_t c, int num);

void put_px(uint32_t *fb, int w, int h, int x, int y, uint32_t c);
void fill_px(uint32_t *fb, int w, int h, int x0, int y0, int x1, int y1, uint32_t c);

void blit(uint32_t *fb, int w, int h, int id,
          int sx, int sy, int sw, int sh,
          int ax, int ay, int dx, int dy, int scale, const tint_t *tint, int flip);

void blit_all(uint32_t *fb, int w, int h, int id, int ax, int ay, int dx, int dy, int scale,
              const tint_t *tint);

void blit_all_f(uint32_t *fb, int w, int h, int id, int ax, int ay, int dx, int dy,
                int scale, const tint_t *tint, int flip);

void urect(const ctx_t *c, int ux, int uy, int uw, int uh, uint32_t col);
void uellipse(const ctx_t *c, int ux, int uy, int rx, int ry, uint32_t col);
void shadow(const ctx_t *c, int rx);

const uint8_t *font_rows(int ch);
void draw_glyph(const ctx_t *c, int ux, int uy, int ch, uint32_t col);
void draw_pawn(const ctx_t *c, int glyph, int fg);

void plant_tint(int fg, tint_t *t);
void draw_world_obj(uint32_t *fb, int w, int h, const ndw_obj_t *o, int sx, int sy, uint32_t anim);
void draw_lamb(uint32_t *fb, int w, int h, int sx, int sy);

#endif /* ND_SEVEN_ATLAS_DRAW_H */
