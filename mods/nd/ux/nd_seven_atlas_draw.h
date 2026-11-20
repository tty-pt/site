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

enum nd_seven_botanical_kind {
	NDB_GRASS_TUFT_1 = 0,
	NDB_GRASS_TUFT_2,
	NDB_GRASS_TUFT_3,
	NDB_TALL_GRASS_1,
	NDB_TALL_GRASS_2,
	NDB_REED_1,
	NDB_REED_2,
	NDB_COUNT
};

void atlas_load(void);
const uint8_t *atlas_px(void);
const uint32_t *atlas_rgba(void);
const uint32_t *atlas_norm_rgba(void);
uint32_t atlas_rgba_version(void);
int nd_seven_plant_rect(uint8_t plant_id, nd_seven_sprite_rect_t *out_rect);
int nd_seven_plant_has_normal(uint8_t plant_id);
int nd_seven_stone_rect(const ndw_obj_t *o, nd_seven_sprite_rect_t *out_rect);
int nd_seven_stone_has_normal(const ndw_obj_t *o);
const char *nd_seven_stone_name(const ndw_obj_t *o);
int nd_seven_obj_rect(const ndw_obj_t *o, nd_seven_sprite_rect_t *out_rect);
int nd_seven_botanical_rect(int kind, nd_seven_sprite_rect_t *out_rect);

uint32_t tint_col(uint32_t c, int r, int g, int b);
uint32_t shade(uint32_t c, int num);

#include "nd_seven_mesh3d.h"

int nd_seven_obj_sprite(const ndw_obj_t *o);
void plant_tint(int fg, tint_t *t);

void nd_seven_mesh3d_init(void);
const nd_mesh3d_vert_t *nd_seven_mesh3d_verts(uint32_t *out_count);
const uint32_t *nd_seven_mesh3d_indices(uint32_t *out_count);
const nd_mesh3d_slice_t *nd_seven_mesh3d_slice(uint8_t mesh_id);
uint8_t nd_seven_plant_mesh3d_id(uint8_t plant_id);
uint8_t nd_seven_stone_mesh3d_id(const ndw_obj_t *o);
uint8_t nd_seven_obj_mesh3d_id(const ndw_obj_t *o);
uint8_t nd_seven_entity_mesh3d_id(int spr_id);

#endif /* ND_SEVEN_ATLAS_DRAW_H */
