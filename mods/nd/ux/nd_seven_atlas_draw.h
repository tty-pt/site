#ifndef ND_SEVEN_ATLAS_DRAW_H
#define ND_SEVEN_ATLAS_DRAW_H

#include <stdint.h>
#include <stddef.h>
#include <nd/seven-world.h>
#include "nd_seven_atlas.h"

#define SCALE       2
#define LAMB_SIZE   32
#define LAMB_AX     16
#define LAMB_AY     16
#define LAMB_FOOT_Y 25

#define ND_SEVEN_RUNTIME_ATLAS_H 4096

#define RGB(r, g, b) (0xFF000000u | ((uint32_t)(b) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(r))
#define RGBA(r, g, b, a) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(r))

typedef struct { uint8_t r, g, b, leaf; } tint_t;

void atlas_load(void);
const uint8_t *atlas_px(void);
const uint32_t *atlas_rgba(void);
uint32_t atlas_rgba_version(void);
int nd_seven_plant_rect(uint8_t plant_id, nd_seven_sprite_rect_t *out_rect);
int nd_seven_obj_rect(const ndw_obj_t *o, nd_seven_sprite_rect_t *out_rect);

uint32_t tint_col(uint32_t c, int r, int g, int b);
uint32_t shade(uint32_t c, int num);

int nd_seven_obj_sprite(const ndw_obj_t *o);
void plant_tint(int fg, tint_t *t);

#endif /* ND_SEVEN_ATLAS_DRAW_H */
