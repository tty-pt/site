#ifndef ND_TERRAIN_H
#define ND_TERRAIN_H

/*
 * nd/terrain.h — Canonical 3D Procedural Terrain & Cavern Engine.
 *
 * Pure C, header-only, zero heap allocations, zero POSIX I/O dependencies.
 * Built directly on top of the generic N-dimensional noise library <ttypt/noise.h>.
 *
 * Mathematical model: Hybrid Heightfield + 3D Cavity Density (H + C)
 *   - H(x, y, w): Surface elevation, water table, and slope gradient.
 *   - C(x, y, z, w): Subterranean 3D cavity noise for caverns, tunnels, and grottoes.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include <ttypt/noise.h>

#ifndef ND_TERRAIN_SUB
#define ND_TERRAIN_SUB 4  /* Default sub-unit resolution multiplier */
#endif

/* Biome constants mirroring nd/world-types.h enum biome */
#ifndef BIOME_WATER
#define BIOME_WATER                     0
#define BIOME_PERMANENT_ICE             1
#define BIOME_TUNDRA                    2
#define BIOME_TUNDRA2                   3
#define BIOME_TUNDRA3                   4
#define BIOME_TUNDRA4                   5
#define BIOME_COLD_DESERT               6
#define BIOME_SHRUBLAND                 7
#define BIOME_CONIFEROUS_FOREST         8
#define BIOME_BOREAL_FOREST             9
#define BIOME_TEMPERATE_GRASSLAND       10
#define BIOME_WOODLAND                  11
#define BIOME_TEMPERATE_SEASONAL_FOREST 12
#define BIOME_TEMPERATE_RAINFOREST      13
#define BIOME_DESERT                    14
#define BIOME_SAVANNAH                  15
#define BIOME_TROPICAL_SEASONAL_FOREST  16
#define BIOME_TROPICAL_RAINFOREST       17
#define BIOME_VOLCANIC                  18
#define BIOME_MAX                       19
#endif

/* --- Multi-Octave Surface Heightfield & Water Table ----------------------- */

typedef struct {
	uint8_t shift;   /* Lattice size 2^shift */
	int16_t amp;     /* Amplitude contribution in world elevation units */
} nd_terrain_octave_t;

/* Declarative macro-to-micro elevation octave cascade */
static const nd_terrain_octave_t ND_TERRAIN_HEIGHT_OCTAVES[] = {
	{ .shift = 9, .amp = 110 }, /* Continental elevation sweeps & ocean basins */
	{ .shift = 7, .amp =  55 }, /* Mountain ridges and highland plateaus */
	{ .shift = 5, .amp =  25 }, /* Rolling hills and river valleys */
	{ .shift = 4, .amp =  12 }, /* Local crags, bluffs, and slopes */
	{ .shift = 3, .amp =   4 }, /* Micro ground undulation */
};
#define ND_TERRAIN_HEIGHT_OCTAVE_COUNT \
	(sizeof(ND_TERRAIN_HEIGHT_OCTAVES) / sizeof(ND_TERRAIN_HEIGHT_OCTAVES[0]))

/* The 8-octave planetary cascade */
static const noise_octave_t ND_PLANETARY_OCTAVES[] = {
	{ .shift = 10, .amp = 95, .skew_x = 4, .skew_y = 3, .contrast = 256 }, /* Continental */
	{ .shift =  9, .amp = 48, .skew_x = 5, .skew_y = 4, .contrast = 256 }, /* Sub-continental */
	{ .shift =  8, .amp = 26, .skew_x = 6, .skew_y = 5, .contrast = 256 }, /* Room-scale bays */
	{ .shift =  7, .amp = 14, .skew_x = 7, .skew_y = 6, .contrast = 256 }, /* Coves & hills */
	{ .shift =  6, .amp =  8, .skew_x = 8, .skew_y = 7, .contrast = 256 }, /* Lagoons */
	{ .shift =  5, .amp =  4, .skew_x = 0, .skew_y = 0, .contrast = 256 }, /* Sandbars */
	{ .shift =  4, .amp =  2, .skew_x = 0, .skew_y = 0, .contrast = 256 }, /* Dune crests */
	{ .shift =  3, .amp =  1, .skew_x = 0, .skew_y = 0, .contrast = 256 }, /* Micro-relief */
};
#define ND_PLANETARY_OCTAVE_COUNT \
	(sizeof(ND_PLANETARY_OCTAVES) / sizeof(ND_PLANETARY_OCTAVES[0]))

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
	uint32_t h = (0x57415452u ^ (uint32_t)world * 0x9E3779B1u);
	return (int16_t)((int)(h % 21) - 10);
}

static inline int16_t
nd_terrain_sea_level(uint16_t world)
{
	return nd_terrain_water_level(world);
}

/* High-definition 8-octave cascade spanning continental scales down to sub-tile relief */
static const nd_terrain_octave_t ND_TERRAIN_HD_OCTAVES[] = {
	{ .shift = 10, .amp = 95 }, /* 1024 units (~6 rooms): continental ocean basins and mountain massifs */
	{ .shift =  9, .amp = 48 }, /*  512 units (~3 rooms): sub-continental sweeps and wide bays */
	{ .shift =  8, .amp = 26 }, /*  256 units (~1.5 rooms): room-scale peninsulas, ridges, and valleys */
	{ .shift =  7, .amp = 14 }, /*  128 units (1 room): coves, hills, and coastal shelves */
	{ .shift =  6, .amp =  8 }, /*   64 units (half room): lagoons, wide river mouths, and reef lines */
	{ .shift =  5, .amp =  4 }, /*   32 units (2 cells): organic sandbars and underwater banks */
	{ .shift =  4, .amp =  2 }, /*   16 units (1 cell): sub-cell dune crests and rocky shelves */
	{ .shift =  3, .amp =  1 }, /*    8 units (half cell): micro-relief, water ripple, and fine grain */
};
#define ND_TERRAIN_HD_OCTAVE_COUNT \
	(sizeof(ND_TERRAIN_HD_OCTAVES) / sizeof(ND_TERRAIN_HD_OCTAVES[0]))

/* High-definition continuous surface elevation H_HD(wx, wy, world) in altitude units.
 * Evaluates the full 8-octave cascade at continuous world pixel/unit coordinates (wx, wy). */
static inline int16_t
nd_terrain_height_hd(int32_t wx, int32_t wy, uint16_t world)
{
	uint32_t base_seed = 0x48454947u ^ ((uint32_t)world * 0x9E3779B1u);
	int total_elev = 0;

	for (size_t i = 0; i < ND_TERRAIN_HD_OCTAVE_COUNT; i++) {
		uint32_t oct_seed = base_seed ^ (uint32_t)(i * 0x85EBCA77u + 1);
		int sx = (int)(i + 4);
		int sy = (int)(i + 3);
		int16_t px = (int16_t)(wx + wy / sx);
		int16_t py = (int16_t)(wy - wx / sy);
		int n = noise_point_2d(px, py, oct_seed, ND_TERRAIN_HD_OCTAVES[i].shift);
		int v = n - 128;
		total_elev += (v * ND_TERRAIN_HD_OCTAVES[i].amp) / 128;
	}

	return (int16_t)total_elev;
}

/* Directional surface slope normal & sunlight hillshading (sunlight from top-left, L = [-1, -1, 1]).
 * Returns a shading multiplier scaled around 256 (e.g. 180 = shadowed, 256 = flat, 330 = highlighted). */
static inline uint16_t
nd_terrain_hillshade(int32_t wx, int32_t wy, uint16_t world)
{
	int16_t h_e = nd_terrain_height_hd(wx + 2, wy, world);
	int16_t h_w = nd_terrain_height_hd(wx - 2, wy, world);
	int16_t h_s = nd_terrain_height_hd(wx, wy + 2, world);
	int16_t h_n = nd_terrain_height_hd(wx, wy - 2, world);

	int dh_dx = (h_e - h_w);
	int dh_dy = (h_s - h_n);

	/* Sunlight vector L = (-1, -1, 1):
	 * Sunward slopes (sloping down to East/South) receive more light.
	 * Leeward slopes receive less light. */
	int shade_delta = (-dh_dx - dh_dy) * 12;
	int shade_val = 256 + shade_delta;
	if (shade_val < 160) shade_val = 160;
	if (shade_val > 360) shade_val = 360;
	return (uint16_t)shade_val;
}

/* Single authoritative query for any continuous point on the planet */
static inline nd_terrain_query_t
nd_terrain_eval(uint16_t world, int32_t wx, int32_t wy, uint32_t anim)
{
	(void)anim;
	nd_terrain_query_t q;
	q.elevation = nd_terrain_height_hd(wx, wy, world);
	q.water_level = nd_terrain_sea_level(world);
	q.depth = (int16_t)(q.water_level - q.elevation);
	q.is_water = q.depth > 0 ? 1 : 0;

	int16_t h_e = nd_terrain_height_hd(wx + 2, wy, world);
	int16_t h_w = nd_terrain_height_hd(wx - 2, wy, world);
	int16_t h_s = nd_terrain_height_hd(wx, wy + 2, world);
	int16_t h_n = nd_terrain_height_hd(wx, wy - 2, world);
	q.slope_dx = (int16_t)(h_e - h_w);
	q.slope_dy = (int16_t)(h_s - h_n);

	return q;
}

/* Surface elevation H(x, y, w) in altitude units.
 * Evaluates continuous multi-octave fBm in sub-unit fixed-point coordinate space. */
static inline int16_t
nd_terrain_height_fine(int32_t x_sub, int32_t y_sub, uint16_t world)
{
	uint32_t base_seed = 0x48454947u ^ ((uint32_t)world * 0x9E3779B1u);
	int total_elev = 0;

	for (size_t i = 0; i < ND_TERRAIN_HEIGHT_OCTAVE_COUNT; i++) {
		uint32_t oct_seed = base_seed ^ (uint32_t)(i * 0x85EBCA77u + 1);
		int sx = (int)(i + 4);
		int sy = (int)(i + 3);
		int16_t px = (int16_t)(x_sub + y_sub / sx);
		int16_t py = (int16_t)(y_sub - x_sub / sy);
		int n = noise_point_2d(px, py, oct_seed, ND_TERRAIN_HEIGHT_OCTAVES[i].shift);
		int v = n - 128;
		total_elev += (v * ND_TERRAIN_HEIGHT_OCTAVES[i].amp) / 128;
	}

	return (int16_t)total_elev;
}

/* Surface elevation H(x, y, w) at integer room/tile coordinates */
static inline int16_t
nd_terrain_height(int16_t x, int16_t y, uint16_t world)
{
	return nd_terrain_height_fine((int32_t)x * ND_TERRAIN_SUB,
	                              (int32_t)y * ND_TERRAIN_SUB, world);
}

/* Surface gradient / slope (dH/dx, dH/dy) */
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

/* Cavern cavity threshold: when cavity value is below this, rock is hollow */
#define ND_TERRAIN_CAVE_THRESHOLD 46

/* Evaluates 3D cavern hollow cavity value using 3D continuous noise.
 * Range 0..255. Lower values indicate hollow cave volume. */
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

/* Returns 1 if coordinate (x, y, z) is solid ground/rock, 0 if air/cavern */
static inline int
nd_terrain_is_solid(int16_t x, int16_t y, int16_t z, uint16_t world)
{
	int16_t h = nd_terrain_height(x, y, world);
	if (z > h)
		return 0; /* Open air above the surface */
	if (z == h)
		return 1; /* Surface turf / crust */

	/* Underground (z < h): test 3D cavity noise */
	int cavity = nd_terrain_cavity((int32_t)x * ND_TERRAIN_SUB,
	                               (int32_t)y * ND_TERRAIN_SUB,
	                               (int32_t)z * ND_TERRAIN_SUB, world);
	return cavity >= ND_TERRAIN_CAVE_THRESHOLD;
}

/* Returns 1 if coordinate (x, y, z) is an open subterranean cavern */
static inline int
nd_terrain_is_cave(int16_t x, int16_t y, int16_t z, uint16_t world)
{
	int16_t h = nd_terrain_height(x, y, world);
	if (z >= h)
		return 0; /* Not underground */

	int cavity = nd_terrain_cavity((int32_t)x * ND_TERRAIN_SUB,
	                               (int32_t)y * ND_TERRAIN_SUB,
	                               (int32_t)z * ND_TERRAIN_SUB, world);
	return cavity < ND_TERRAIN_CAVE_THRESHOLD;
}

/* Returns 1 if surface position (x, y) has a natural cave entrance / sinkhole */
static inline int
nd_terrain_is_entrance(int16_t x, int16_t y, uint16_t world)
{
	int16_t h = nd_terrain_height(x, y, world);
	int16_t w = nd_terrain_water_level(world);
	if (h <= w)
		return 0; /* Submerged under water */

	/* Entrance exists if a subterranean tunnel breaches just below the surface */
	return nd_terrain_is_cave(x, y, h - 1, world);
}

/* --- Climate & Biome Resolution ------------------------------------------ */

static inline uint8_t
nd_terrain_biome(int16_t x, int16_t y, int16_t z, uint16_t world)
{
	int16_t h = nd_terrain_height(x, y, world);
	int16_t w = nd_terrain_water_level(world);

	if (z <= h && h < w)
		return BIOME_WATER;

	/* If deep underground inside a cavern, return subterranean/volcanic */
	if (z < h && nd_terrain_is_cave(x, y, z, world)) {
		if (z < -60) return BIOME_VOLCANIC;
		return BIOME_SHRUBLAND; /* Underdark fungal cavern */
	}

	uint32_t t_seed = 0x54454D50u ^ ((uint32_t)world * 0x9E3779B1u);
	uint32_t r_seed = 0x5241494Eu ^ ((uint32_t)world * 0x85EBCA77u);

	/* Latitude temperature: colder towards positive y (north) */
	/* Altitude drop: higher elevation cools by ~1 degree per 2 height units */
	int lat_drop = (int)y / 4;
	int altitude_drop = (h > w ? (h - w) : 0) / 2;

	int temp_noise = (noise_point_2d(x * 2, y * 2, t_seed, 7) - 128) / 4;
	int temp = 65 - lat_drop - altitude_drop + temp_noise;

	int rain_noise = noise_point_2d(x * 2, y * 2, r_seed, 6);
	int rain = rain_noise;

	if (temp < -15) return BIOME_PERMANENT_ICE;
	if (temp < 5)   return BIOME_TUNDRA;
	if (temp < 25) {
		if (rain < 80)  return BIOME_COLD_DESERT;
		if (rain < 160) return BIOME_CONIFEROUS_FOREST;
		return BIOME_BOREAL_FOREST;
	}
	if (temp < 50) {
		if (rain < 60)  return BIOME_TEMPERATE_GRASSLAND;
		if (rain < 140) return BIOME_WOODLAND;
		if (rain < 200) return BIOME_TEMPERATE_SEASONAL_FOREST;
		return BIOME_TEMPERATE_RAINFOREST;
	}
	/* Hot */
	if (rain < 50)  return BIOME_DESERT;
	if (rain < 120) return BIOME_SAVANNAH;
	if (rain < 200) return BIOME_TROPICAL_SEASONAL_FOREST;
	return BIOME_TROPICAL_RAINFOREST;
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
	cell.elevation = nd_terrain_height(x, y, world);
	cell.water_level = nd_terrain_water_level(world);
	nd_terrain_slope(x, y, world, &cell.slope_x, &cell.slope_y);
	cell.biome = nd_terrain_biome(x, y, z, world);
	cell.is_solid = (uint8_t)nd_terrain_is_solid(x, y, z, world);
	cell.is_cave = (uint8_t)nd_terrain_is_cave(x, y, z, world);
	cell.is_entrance = (uint8_t)nd_terrain_is_entrance(x, y, world);
	cell.is_water = (uint8_t)(cell.elevation < cell.water_level && z <= cell.water_level);
	return cell;
}

#endif /* ND_TERRAIN_H */
