/*
 * nd_seven_mesh3d.h — Pure-C 3D Procedural Geological Mesh Engine
 * + Orthographic Pixel-Art Albedo & Tangent-Space Normal Map Baker.
 *
 * All 3D botanical mesh generation and foliage atlas generation live in nd-plant:
 *   - <nd/plant_mesh3d.h>
 *   - <nd/plant_foliage_atlas.h>
 *
 * This header provides:
 *   1. 3D Faceted Geological & Mineral Generator (All 51 NDS_SP_* Specimens across pebbles,
 *      rocks, boulders, crystals, and ores).
 *   2. Deterministic software Z-buffer orthographic 55-deg diorama rasterizer
 *      (nd_seven_mesh3d_bake_ortho_rgba) that bakes any 3D mesh template slice into
 *      lossless 32-bit RGBA pixel-art albedo sprites + tangent-space normal maps (_n.png).
 */

#ifndef ND_SEVEN_MESH3D_H
#define ND_SEVEN_MESH3D_H

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <nd/plant.h>
#include <nd/plant_mesh3d.h>

/* Canonical world-unit dimensions for geological tiers relative to the 18.0 wu (40 cm) Lamb:
 *   kind 9  (NDW_K_STONE):   hand specimens, crystals, pebbles (13..16 wu wide, 10..13 wu tall, < 0.75x Lamb)
 *   kind 10 (NDW_K_ROCK):    outcrop rocks & large crystal clusters (25..31 wu wide, 21..26 wu tall, ~1.3x Lamb)
 *   kind 11 (NDW_K_BOULDER): monolithic boulders (48..58 wu wide, 40..48 wu tall, ~2.4x Lamb) */
static inline void
nd_m3d_stone_metric_dims(uint8_t kind, uint8_t variant,
                         float *out_w_wu, float *out_h_wu, float *out_ay_wu)
{
	float v_mod = 0.90f + 0.065f * (float)(variant & 3u);
	float w_wu = 15.0f * v_mod;
	float h_wu = 11.5f * v_mod;
	if (kind == 11u || kind == 6u) {        /* NDW_K_BOULDER */
		w_wu = 52.0f * v_mod;
		h_wu = 44.0f * v_mod;
	} else if (kind == 10u || kind == 5u) { /* NDW_K_ROCK */
		w_wu = 28.0f * v_mod;
		h_wu = 23.0f * v_mod;
	}
	if (out_w_wu)  *out_w_wu  = w_wu;
	if (out_h_wu)  *out_h_wu  = h_wu;
	if (out_ay_wu) *out_ay_wu = fmaxf(2.0f, h_wu * 0.22f);
}

/* Ground-breaker relief mesh templates (206..213, layered after canonical ND_MESH3D_TOTAL = 206):
 *   0..1: Mossy Earth & Root Hummock
 *   2..3: Embedded Scree & Fieldstone Outcrop
 *   4..5: Turf Berm & Exposed Root Mound
 *   6..7: Fallen Mossy Log with broken stubs & humus saddle */
#define ND_MESH3D_GEOBREAK_BASE  ND_MESH3D_TOTAL
#define ND_MESH3D_GEOBREAK_COUNT 8u
#define ND_SEVEN_MESH3D_TOTAL    (ND_MESH3D_GEOBREAK_BASE + ND_MESH3D_GEOBREAK_COUNT)


void nd_m3d_build_stone_spec(nd_mesh3d_builder_t *b,
                             unsigned sp, uint16_t sw, uint16_t sh,
                             nd_mesh3d_slice_t *out_slice);
void nd_seven_mesh3d_bake_ortho_rgba(const nd_mesh3d_vert_t *verts,
                                     const uint32_t *indices,
                                     const nd_mesh3d_slice_t *slice,
                                     uint32_t *out_albedo_rgba,
                                     uint32_t *out_normal_rgba,
                                     uint32_t out_stride_px);
void nd_m3d_build_geobreak_prop(nd_mesh3d_builder_t *b, uint32_t variant,
                                nd_mesh3d_slice_t *out_slice);

#endif /* ND_SEVEN_MESH3D_H */
