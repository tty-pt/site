#ifndef ND_TERRAIN_H
#define ND_TERRAIN_H

/*
 * nd/terrain.h — Procedural Terrain & Cavern Engine.
 *
 * Pure C, header-only, zero heap allocations, zero POSIX I/O dependencies.
 * Rebased onto the canonical procedural field <nd/field.h>.
 *
 * Mathematical model: Hybrid Heightfield + 3D Cavity Density (H + C)
 *   - H(wx, wy, world): Surface elevation, water table, slope from <nd/field.h>.
 *   - C(x, y, z, world): Subterranean 3D cavity noise for caverns, tunnels, and grottoes.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include <nd/field.h>

#ifndef ND_TERRAIN_SUB
#define ND_TERRAIN_SUB 4  /* Default sub-unit resolution multiplier */
#endif

typedef struct {
	int16_t elevation;      /* Surface ground height H(wx, wy) */
	int16_t water_level;    /* Planetary water surface W(wx, wy, time) */
	int16_t depth;          /* Water depth = W - H (depth <= 0 is dry land) */
	int16_t slope_dx;       /* Terrain gradient dH/dx */
	int16_t slope_dy;       /* Terrain gradient dH/dy */
	uint8_t is_water;       /* Convenience flag: depth > 0 */
} nd_terrain_query_t;

/* Sea level elevation in world units */
static inline int16_t
nd_terrain_water_level(uint16_t world)
{
	return nd_field_water_level(world);
}

static inline int16_t
nd_terrain_sea_level(uint16_t world)
{
	return nd_field_water_level(world);
}

/* Surface elevation at continuous world coordinates (wx, wy) with fine octaves */
static inline int16_t
nd_terrain_height_fine(int32_t wx, int32_t wy, uint16_t world)
{
	nd_field_sample_t s = nd_field_sample(world, 3, wx, wy);
	return s.elevation;
}

/* Surface elevation H(x, y, world) at integer room coordinates */
static inline int16_t
nd_terrain_height(int16_t x, int16_t y, uint16_t world)
{
	int32_t wx = (int32_t)x * ND_FIELD_ROOM_W + (ND_FIELD_ROOM_W / 2);
	int32_t wy = -(int32_t)y * ND_FIELD_ROOM_H + (ND_FIELD_ROOM_H / 2);
	nd_field_sample_t s = nd_field_sample(world, 0, wx, wy);
	return s.elevation;
}

/* Directional surface slope normal & sunlight hillshading */
static inline uint16_t
nd_terrain_hillshade(int32_t wx, int32_t wy, uint16_t world)
{
	int16_t h_e = nd_terrain_height_fine(wx + 2, wy, world);
	int16_t h_w = nd_terrain_height_fine(wx - 2, wy, world);
	int16_t h_s = nd_terrain_height_fine(wx, wy + 2, world);
	int16_t h_n = nd_terrain_height_fine(wx, wy - 2, world);

	int dh_dx = (h_e - h_w);
	int dh_dy = (h_s - h_n);
	int slope = dh_dx + dh_dy;

	int shade_delta = (slope * ND_SHADE_NEUTRAL) / (ND_FIELD_STENCIL_SPAN * ND_FIELD_FINE_AMP_BUDGET);
	int shade_val = ND_SHADE_NEUTRAL + shade_delta;
	if (shade_val < ND_SHADE_MIN) shade_val = ND_SHADE_MIN;
	if (shade_val > ND_SHADE_MAX) shade_val = ND_SHADE_MAX;
	return (uint16_t)shade_val;
}

/* Single authoritative query for any continuous point on the planet */
static inline nd_terrain_query_t
nd_terrain_eval(uint16_t world, int32_t wx, int32_t wy, uint32_t anim)
{
	(void)anim;
	nd_field_sample_t s = nd_field_sample(world, 3, wx, wy);
	nd_terrain_query_t q;
	q.elevation   = s.elevation;
	q.water_level = s.water_level;
	q.depth       = (int16_t)(s.water_level - s.elevation);
	q.is_water    = (uint8_t)(q.depth > 0);

	int16_t h_e = nd_terrain_height_fine(wx + 2, wy, world);
	int16_t h_w = nd_terrain_height_fine(wx - 2, wy, world);
	int16_t h_s = nd_terrain_height_fine(wx, wy + 2, world);
	int16_t h_n = nd_terrain_height_fine(wx, wy - 2, world);
	q.slope_dx = (int16_t)(h_e - h_w);
	q.slope_dy = (int16_t)(h_s - h_n);

	return q;
}

/* Surface gradient / slope (dH/dx, dH/dy) at room coordinates */
static inline void
nd_terrain_slope(int16_t x, int16_t y, uint16_t world, int16_t *dx, int16_t *dy)
{
	int16_t h_e = nd_terrain_height(x + 1, y, world);
	int16_t h_w = nd_terrain_height(x - 1, y, world);
	int16_t h_n = nd_terrain_height(x, y + 1, world);
	int16_t h_s = nd_terrain_height(x, y - 1, world);
	if (dx) *dx = (int16_t)((h_e - h_w) / 2);
	if (dy) *dy = (int16_t)((h_n - h_s) / 2);
}

/* --- 3D Subterranean Caverns & Density Field ------------------------------ */

#define ND_TERRAIN_CAVE_THRESHOLD 46

static inline int
nd_terrain_cavity(int32_t x, int32_t y, int32_t z, uint16_t world)
{
	uint32_t seed1 = 0x43415631u ^ ((uint32_t)world * 0x9E3779B1u);
	uint32_t seed2 = 0x43415632u ^ ((uint32_t)world * 0x85EBCA77u);

	int n1 = noise_point_3d((int16_t)x, (int16_t)y, (int16_t)z, seed1, 4);
	int n2 = noise_point_3d((int16_t)(x + 3), (int16_t)(y - 5), (int16_t)(z + 7), seed2, 4);

	int d1 = n1 - 128; if (d1 < 0) d1 = -d1;
	int d2 = n2 - 128; if (d2 < 0) d2 = -d2;

	return (d1 + d2) / 2;
}

static inline int
nd_terrain_is_solid(int16_t x, int16_t y, int16_t z, uint16_t world)
{
	int16_t h = nd_terrain_height(x, y, world);
	if (z > h)
		return 0; /* Open air above the surface */
	if (z == h)
		return 1; /* Surface turf / crust */

	int cavity = nd_terrain_cavity((int32_t)x * ND_TERRAIN_SUB,
	                               (int32_t)y * ND_TERRAIN_SUB,
	                               (int32_t)z * ND_TERRAIN_SUB, world);
	return cavity >= ND_TERRAIN_CAVE_THRESHOLD;
}

static inline int
nd_terrain_is_cave(int16_t x, int16_t y, int16_t z, uint16_t world)
{
	int16_t h = nd_terrain_height(x, y, world);
	if (z >= h)
		return 0;

	int cavity = nd_terrain_cavity((int32_t)x * ND_TERRAIN_SUB,
	                               (int32_t)y * ND_TERRAIN_SUB,
	                               (int32_t)z * ND_TERRAIN_SUB, world);
	return cavity < ND_TERRAIN_CAVE_THRESHOLD;
}

static inline int
nd_terrain_is_entrance(int16_t x, int16_t y, uint16_t world)
{
	int32_t wx = (int32_t)x * ND_FIELD_ROOM_W + (ND_FIELD_ROOM_W / 2);
	int32_t wy = -(int32_t)y * ND_FIELD_ROOM_H + (ND_FIELD_ROOM_H / 2);
	nd_field_sample_t s = nd_field_sample(world, 0, wx, wy);
	if (s.elevation <= s.water_level)
		return 0;

	return nd_terrain_is_cave(x, y, s.elevation - 1, world);
}

/* --- Climate & Biome Resolution ------------------------------------------ */

static inline uint8_t
nd_terrain_biome(int16_t x, int16_t y, int16_t z, uint16_t world)
{
	int32_t wx = (int32_t)x * ND_FIELD_ROOM_W + (ND_FIELD_ROOM_W / 2);
	int32_t wy = -(int32_t)y * ND_FIELD_ROOM_H + (ND_FIELD_ROOM_H / 2);
	nd_field_sample_t s = nd_field_sample(world, 0, wx, wy);

	if (z <= s.elevation && s.elevation < s.water_level)
		return BIOME_WATER;

	if (z < s.elevation && nd_terrain_is_cave(x, y, z, world)) {
		if (z < -60) return BIOME_VOLCANIC;
		return BIOME_SHRUBLAND;
	}

	return s.bio;
}

/* --- Unified Spatial Cell Query ------------------------------------------ */

typedef struct {
	int16_t elevation;      /* Surface height H(x, y, w) in altitude units */
	int16_t water_level;    /* Sea/water level W(w) */
	int16_t slope_x;        /* dH/dx */
	int16_t slope_y;        /* dH/dy */
	uint8_t biome;          /* Biome index (BIOME_WATER, BIOME_TUNDRA, etc.) */
	uint8_t is_solid;       /* 1 = solid rock/ground, 0 = open air/cavern */
	uint8_t is_cave;        /* 1 = subterranean cavity / tunnel */
	uint8_t is_entrance;    /* 1 = cave entrance breaching surface */
	uint8_t is_water;       /* 1 = submerged under water table */
} nd_terrain_cell_t;

static inline nd_terrain_cell_t
nd_terrain_query(int16_t x, int16_t y, int16_t z, uint16_t world)
{
	nd_terrain_cell_t cell;
	int32_t wx = (int32_t)x * ND_FIELD_ROOM_W + (ND_FIELD_ROOM_W / 2);
	int32_t wy = -(int32_t)y * ND_FIELD_ROOM_H + (ND_FIELD_ROOM_H / 2);
	nd_field_sample_t s = nd_field_sample(world, 0, wx, wy);

	cell.elevation = s.elevation;
	cell.water_level = s.water_level;
	cell.biome = s.bio;
	nd_terrain_slope(x, y, world, &cell.slope_x, &cell.slope_y);

	if (z > s.elevation) {
		cell.is_solid = 0;
		cell.is_cave = 0;
	} else if (z == s.elevation) {
		cell.is_solid = 1;
		cell.is_cave = 0;
	} else {
		int cavity = nd_terrain_cavity((int32_t)x * ND_TERRAIN_SUB,
		                               (int32_t)y * ND_TERRAIN_SUB,
		                               (int32_t)z * ND_TERRAIN_SUB, world);
		cell.is_cave = (cavity < ND_TERRAIN_CAVE_THRESHOLD);
		cell.is_solid = !cell.is_cave;
	}

	cell.is_entrance = 0;
	if (s.elevation > s.water_level) {
		int cavity = nd_terrain_cavity((int32_t)x * ND_TERRAIN_SUB,
		                               (int32_t)y * ND_TERRAIN_SUB,
		                               (int32_t)(s.elevation - 1) * ND_TERRAIN_SUB, world);
		cell.is_entrance = (cavity < ND_TERRAIN_CAVE_THRESHOLD);
	}
	cell.is_water = (uint8_t)(cell.elevation < cell.water_level && z <= cell.water_level);
	return cell;
}


#endif /* ND_TERRAIN_H */
