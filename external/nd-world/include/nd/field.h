#ifndef ND_FIELD_H
#define ND_FIELD_H

/*
 * nd/field.h — Canonical Multi-Resolution Procedural Field & Chunk Type.
 *
 * Pure C, header-only, zero heap allocations in core evaluators.
 * Shared by server worldgen, 2D mini-map, and Seven 3D renderer.
 *
 * Coordinates:
 *   - Room coordinates: integer lattice (X, Y) on server.
 *   - World coordinates: continuous (wx, wy) in world units.
 *     1 room = 160 world units wide, 128 world units high.
 *     Room (X, Y) is sampled at its tile centre (px=80, py=64).
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <ttypt/noise.h>

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

/* Room tile dimensions in world units */
#define ND_FIELD_ROOM_W 160
#define ND_FIELD_ROOM_H 128

/* Elevation range and climate constants from server worldgen */
#define ND_FIELD_NOISE_FOURTH (NOISE_MAX >> 2)
#define ND_FIELD_TEMP_MAX     ((int16_t)170)
#define ND_FIELD_TEMP_MIN     ((int16_t)-41)
#define ND_FIELD_RAIN_DIV     (NOISE_MAX >> 9)
#define ND_FIELD_RAIN_MAX     (NOISE_MAX / ND_FIELD_RAIN_DIV)

/* Coarse server octave specifications (noise.c:119) */
static const octave_t ND_FIELD_COARSE_HE_OCTAVES[] = {
	{ 7, 2 }, { 6, 2 }, { 5, 2 }, { 4, 3 }, { 3, 3 }, { 2, 3 }
};
#define ND_FIELD_COARSE_HE_COUNT (sizeof(ND_FIELD_COARSE_HE_OCTAVES)/sizeof(ND_FIELD_COARSE_HE_OCTAVES[0]))

static const octave_t ND_FIELD_COARSE_CL_OCTAVES[] = {
	{ 5, 1 }, { 2, 1 }
};
#define ND_FIELD_COARSE_CL_COUNT (sizeof(ND_FIELD_COARSE_CL_OCTAVES)/sizeof(ND_FIELD_COARSE_CL_OCTAVES[0]))

static const octave_t ND_FIELD_COARSE_TM_OCTAVES[] = {
	{ 5, 1 }, { 8, 1 }
};
#define ND_FIELD_COARSE_TM_COUNT (sizeof(ND_FIELD_COARSE_TM_OCTAVES)/sizeof(ND_FIELD_COARSE_TM_OCTAVES[0]))

/* Fine client octave specifications (shifts in world units: 128, 64, 32, 16, 8) */
typedef struct {
	uint8_t shift;
	int16_t amp;
} nd_field_fine_octave_t;

static const nd_field_fine_octave_t ND_FIELD_FINE_OCTAVES[] = {
	{ 7, 14 },
	{ 6,  8 },
	{ 5,  4 },
	{ 4,  2 },
	{ 3,  1 },
};
#define ND_FIELD_FINE_COUNT (sizeof(ND_FIELD_FINE_OCTAVES)/sizeof(ND_FIELD_FINE_OCTAVES[0]))
#define ND_FIELD_FINE_AMP_BUDGET 29

/* Climate primitives matching noise.c */
static inline uint32_t
nd_field_water_noise(uint16_t obits)
{
	return (((obits >> 2) + 2) & 3) * ND_FIELD_NOISE_FOURTH;
}

static inline int16_t
nd_field_water_level(uint16_t obits)
{
	uint32_t w = nd_field_water_noise(obits);
	return (int16_t)((int64_t)w * 200 / NOISE_MAX - 100);
}

static inline uint32_t
nd_field_sun_dist(uint16_t obits)
{
	return ((obits + 1) & 3);
}

static inline int16_t
nd_field_temp(uint16_t obits, uint32_t he, uint32_t tm, int16_t pos_y)
{
	int64_t x = (int64_t)he - (int64_t)nd_field_water_noise(obits) + ((int64_t)tm - (int64_t)NOISE_MAX) / 3;
	x /= 4 * (int64_t)65535 * 260;
	x += (ND_FIELD_TEMP_MAX << 1) - ND_FIELD_TEMP_MIN - (ND_FIELD_TEMP_MAX - ND_FIELD_TEMP_MIN) * nd_field_sun_dist(obits);
	x += (pos_y * ND_FIELD_TEMP_MIN) / 6300;
	return (int16_t)(ND_FIELD_TEMP_MIN + x);
}

static inline uint32_t
nd_field_rain(uint32_t cl)
{
	return cl / ND_FIELD_RAIN_DIV;
}

static inline unsigned
_nd_field_bio_idx_calc(int16_t tmp_floor, int16_t tmp_ceil,
                       uint32_t rn_floor, uint32_t rn_ceil,
                       int16_t tmp, uint32_t rn)
{
	uint32_t rn_bit = (4 * (rn - rn_floor) / (rn_ceil - rn_floor)) & 3;
	int32_t tmp_bit = (4 * (tmp - tmp_floor) / (tmp_ceil - tmp_floor)) & 3;
	return (unsigned)(rn_bit + (tmp_bit << 2));
}

static inline uint8_t
nd_field_bio_idx(uint32_t rn, int16_t tmp)
{
	if (tmp < ND_FIELD_TEMP_MIN)
		return BIOME_PERMANENT_ICE;
	else if (tmp >= ND_FIELD_TEMP_MAX)
		return BIOME_VOLCANIC;
	return (uint8_t)(2 + _nd_field_bio_idx_calc(ND_FIELD_TEMP_MIN, ND_FIELD_TEMP_MAX, 0, ND_FIELD_RAIN_MAX, tmp, rn));
}

/* Biome blending: calculates primary biome, secondary biome, and blend factor */
static inline void
nd_field_bio_blend(uint32_t rn, int16_t tmp, uint8_t *bio_out, uint8_t *bio2_out, uint8_t *blend_out)
{
	if (tmp < ND_FIELD_TEMP_MIN) {
		if (bio_out) *bio_out = BIOME_PERMANENT_ICE;
		if (bio2_out) *bio2_out = BIOME_PERMANENT_ICE;
		if (blend_out) *blend_out = 0;
		return;
	}
	if (tmp >= ND_FIELD_TEMP_MAX) {
		if (bio_out) *bio_out = BIOME_VOLCANIC;
		if (bio2_out) *bio2_out = BIOME_VOLCANIC;
		if (blend_out) *blend_out = 0;
		return;
	}

	uint32_t rn_step = ND_FIELD_RAIN_MAX / 4;
	int32_t tmp_step = (ND_FIELD_TEMP_MAX - ND_FIELD_TEMP_MIN) / 4;
	if (rn_step == 0) rn_step = 1;
	if (tmp_step == 0) tmp_step = 1;

	uint32_t rn_bit = (rn / rn_step);
	if (rn_bit > 3) rn_bit = 3;
	int32_t tmp_bit = ((tmp - ND_FIELD_TEMP_MIN) / tmp_step);
	if (tmp_bit > 3) tmp_bit = 3;
	if (tmp_bit < 0) tmp_bit = 0;

	uint8_t primary = (uint8_t)(2 + rn_bit + (tmp_bit << 2));

	uint32_t rn_rem = rn - (rn_bit * rn_step);
	uint32_t rn_frac = (rn_rem * 256) / rn_step;

	int32_t tmp_rem = (tmp - ND_FIELD_TEMP_MIN) - (tmp_bit * tmp_step);
	uint32_t tmp_frac = (uint32_t)((tmp_rem * 256) / tmp_step);

	int32_t d_rn = (rn_frac < 128) ? (int32_t)rn_frac : (int32_t)(256 - rn_frac);
	int32_t d_tmp = (tmp_frac < 128) ? (int32_t)tmp_frac : (int32_t)(256 - tmp_frac);

	uint8_t secondary = primary;
	int32_t min_d = 128;

	if (d_rn < d_tmp) {
		min_d = d_rn;
		if (rn_frac < 128 && rn_bit > 0)
			secondary = (uint8_t)(2 + (rn_bit - 1) + (tmp_bit << 2));
		else if (rn_frac >= 128 && rn_bit < 3)
			secondary = (uint8_t)(2 + (rn_bit + 1) + (tmp_bit << 2));
	} else {
		min_d = d_tmp;
		if (tmp_frac < 128 && tmp_bit > 0)
			secondary = (uint8_t)(2 + rn_bit + ((tmp_bit - 1) << 2));
		else if (tmp_frac >= 128 && tmp_bit < 3)
			secondary = (uint8_t)(2 + rn_bit + ((tmp_bit + 1) << 2));
	}

	uint8_t blend = 0;
	if (min_d < 48 && secondary != primary) {
		blend = (uint8_t)(((48 - min_d) * 128) / 48);
	}

	if (bio_out) *bio_out = primary;
	if (bio2_out) *bio2_out = secondary;
	if (blend_out) *blend_out = blend;
}

/* Converts continuous world pixel/unit coordinates (wx, wy)
 * to fractional room coordinates (room_x, room_y) in 16.16 fixed-point.
 *
 * Screen coordinates in NeverDark:
 *   wx = room_X * 160 + px, with px in 0..159 (centre at 80)
 *   wy = -room_Y * 128 + py, with py in 0..127 (centre at 64)
 * Inverting:
 *   room_x = (wx - 80) / 160
 *   room_y = (64 - wy) / 128
 */
static inline void
nd_field_world_to_room_q16(int32_t wx, int32_t wy, int32_t *rx_q16, int32_t *ry_q16)
{
	int64_t ox = (int64_t)(wx - 80) * 65536LL;
	int64_t oy = (int64_t)(64 - wy) * 65536LL;
	if (rx_q16) *rx_q16 = (int32_t)(ox / 160);
	if (ry_q16) *ry_q16 = (int32_t)(oy / 128);
}

/* Continuous interpolation of coarse server octaves at fractional room coordinates */
static inline uint32_t
nd_field_eval_coarse_oct_q16(int32_t ry_q16, int32_t rx_q16, unsigned x, unsigned w, unsigned seed)
{
	int16_t cell_y = (int16_t)(ry_q16 >> 16);
	uint16_t frac_y = (uint16_t)(ry_q16 & 0xffff);
	int16_t cell_x = (int16_t)(rx_q16 >> 16);
	uint16_t frac_x = (uint16_t)(rx_q16 & 0xffff);

	if (frac_y == 0 && frac_x == 0) {
		int16_t p[2] = { cell_y, cell_x };
		return noise_wpoint_n(p, x, w, seed, 2, 0);
	}

	uint16_t mask = COORDMASK_LSHIFT(x);
	int16_t s[2] = { (int16_t)(cell_y & mask), (int16_t)(cell_x & mask) };
	uint32_t fiy = (uint16_t)cell_y & (uint16_t)~mask;
	uint32_t fix = (uint16_t)cell_x & (uint16_t)~mask;

	uint32_t uy = ((uint32_t)fiy << 16) | (uint32_t)frac_y;
	uint32_t ux = ((uint32_t)fix << 16) | (uint32_t)frac_x;

	uint32_t v[4];
	noise_get_v(v, s, x, w, seed, 2);

	int32_t s0 = noise_wstep(v[0], v[2], x);
	int32_t s1 = noise_wstep(v[1], v[3], x);
	uint32_t b0 = v[0] + (uint32_t)(((int64_t)s0 * uy) >> 16);
	uint32_t b1 = v[1] + (uint32_t)(((int64_t)s1 * uy) >> 16);
	int32_t st = noise_wstep(b0, b1, x);
	return b0 + (uint32_t)(((int64_t)st * ux) >> 16);
}

typedef struct {
	int16_t elevation;      /* Total elevation H = coarse + fine */
	int16_t coarse_elev;    /* Coarse elevation */
	int16_t fine_elev;      /* Fine detail elevation */
	int16_t water_level;    /* W */
	int16_t depth;          /* max(0, W - H) */
	uint8_t is_water;       /* H < W */
	int16_t temp;           /* Temperature */
	uint32_t rain;          /* Rain */
	uint8_t bio;            /* Primary biome */
	uint8_t bio2;           /* Secondary biome */
	uint8_t blend;          /* Biome blend factor (0..128) */
} nd_field_sample_t;

static inline nd_field_sample_t
nd_field_sample(uint16_t world, uint8_t lod, int32_t wx, int32_t wy)
{
	nd_field_sample_t s;
	memset(&s, 0, sizeof(s));

	int32_t rx_q16, ry_q16;
	nd_field_world_to_room_q16(wx, wy, &rx_q16, &ry_q16);

	int16_t room_y = (int16_t)(ry_q16 >> 16);

	uint32_t he_raw = 0;
	for (size_t o = 0; o < ND_FIELD_COARSE_HE_COUNT; o++) {
		he_raw += nd_field_eval_coarse_oct_q16(ry_q16, rx_q16,
		                                      ND_FIELD_COARSE_HE_OCTAVES[o].x,
		                                      ND_FIELD_COARSE_HE_OCTAVES[o].w,
		                                      55 + world);
	}

	uint32_t cl_raw = 0;
	for (size_t o = 0; o < ND_FIELD_COARSE_CL_COUNT; o++) {
		cl_raw += nd_field_eval_coarse_oct_q16(ry_q16, rx_q16,
		                                      ND_FIELD_COARSE_CL_OCTAVES[o].x,
		                                      ND_FIELD_COARSE_CL_OCTAVES[o].w,
		                                      1 + world);
	}

	uint32_t tm_raw = 0;
	for (size_t o = 0; o < ND_FIELD_COARSE_TM_COUNT; o++) {
		tm_raw += nd_field_eval_coarse_oct_q16(ry_q16, rx_q16,
		                                      ND_FIELD_COARSE_TM_OCTAVES[o].x,
		                                      ND_FIELD_COARSE_TM_OCTAVES[o].w,
		                                      53 + world);
	}

	s.coarse_elev = (int16_t)((int64_t)he_raw * 200 / NOISE_MAX - 100);

	int16_t fine_h = 0;
	if (lod > 0) {
		uint32_t fine_seed = 0x48454947u ^ ((uint32_t)world * 0x9E3779B1u);
		for (size_t i = 0; i < ND_FIELD_FINE_COUNT; i++) {
			uint32_t oct_seed = fine_seed ^ (uint32_t)(i * 0x85EBCA77u + 1);
			int16_t p[2] = { (int16_t)wy, (int16_t)wx };
			uint32_t raw = noise_wpoint_n(p, ND_FIELD_FINE_OCTAVES[i].shift, 8, oct_seed, 2, NOISE_FADE);
			int32_t val = (int32_t)(raw >> 16) - 128;
			fine_h += (int16_t)((val * ND_FIELD_FINE_OCTAVES[i].amp) / 128);
		}
	}
	s.fine_elev = fine_h;
	s.elevation = (int16_t)(s.coarse_elev + fine_h);

	s.water_level = nd_field_water_level(world);
	if (s.elevation < s.water_level) {
		s.is_water = 1;
		s.depth = (int16_t)(s.water_level - s.elevation);
	} else {
		s.is_water = 0;
		s.depth = 0;
	}

	s.temp = nd_field_temp(world, he_raw, tm_raw, room_y);
	s.rain = nd_field_rain(cl_raw);
	nd_field_bio_blend(s.rain, s.temp, &s.bio, &s.bio2, &s.blend);

	return s;
}


/* Shared multi-resolution field chunk type */
typedef struct {
	uint16_t world;
	uint8_t  lod;      /* 0 = room (server), 1 = 16 units (minimap), 2 = 4 units, 3 = 1 unit (Seven) */
	int32_t  ox, oy;   /* origin: in rooms for lod 0; in world units for lod > 0 */
	uint16_t w, h;     /* width, height in samples */
	int16_t  *height;  /* surface elevation H(x, y) */
	uint8_t  *bio;     /* primary biome index */
	uint8_t  *bio2;    /* secondary biome index for blending */
	uint8_t  *blend;   /* blend factor: 0 = 100% bio, 255 = 100% bio2 */
	uint8_t  *depth;   /* water depth: max(0, W - H) */
} nd_field_chunk_t;

static inline void
nd_field_fill(nd_field_chunk_t *chunk)
{
	if (!chunk || !chunk->height) return;
	uint16_t w = chunk->w, h = chunk->h;
	int step_world = 1;
	if (chunk->lod == 0) step_world = 1;
	else if (chunk->lod == 1) step_world = 16;
	else if (chunk->lod == 2) step_world = 4;
	else if (chunk->lod == 3) step_world = 1;

	for (uint16_t r = 0; r < h; r++) {
		for (uint16_t c = 0; c < w; c++) {
			int32_t wx, wy;
			if (chunk->lod == 0) {
				int16_t rx = (int16_t)(chunk->ox + c);
				int16_t ry = (int16_t)(chunk->oy + r);
				wx = (int32_t)rx * ND_FIELD_ROOM_W + (ND_FIELD_ROOM_W / 2);
				wy = -(int32_t)ry * ND_FIELD_ROOM_H + (ND_FIELD_ROOM_H / 2);
			} else {
				wx = chunk->ox + (int32_t)c * step_world;
				wy = chunk->oy + (int32_t)r * step_world;
			}

			nd_field_sample_t s = nd_field_sample(chunk->world, chunk->lod, wx, wy);
			size_t idx = (size_t)r * w + c;
			chunk->height[idx] = s.elevation;
			if (chunk->bio)   chunk->bio[idx] = s.bio;
			if (chunk->bio2)  chunk->bio2[idx] = s.bio2;
			if (chunk->blend) chunk->blend[idx] = s.blend;
			if (chunk->depth) chunk->depth[idx] = (uint8_t)(s.depth > 255 ? 255 : s.depth);
		}
	}
}

/* Chunk manager / LRU cache */
#define ND_FIELD_CACHE_MAX_ENTRIES 64

typedef struct {
	nd_field_chunk_t chunk;
	uint32_t last_used_tick;
	uint8_t  state; /* 0 = free, 1 = ready, 2 = building */
	uint8_t  pinned;
	int16_t  *height_buf;
	uint8_t  *bio_buf;
	uint8_t  *bio2_buf;
	uint8_t  *blend_buf;
	uint8_t  *depth_buf;
	size_t   buf_capacity;
} nd_field_cache_entry_t;

typedef struct {
	uint32_t current_tick;
	uint16_t capacity;
	uint16_t count;
	nd_field_cache_entry_t entries[ND_FIELD_CACHE_MAX_ENTRIES];
	void *(*alloc_fn)(size_t size);
	void (*free_fn)(void *ptr);
} nd_field_cache_t;

static inline void
nd_field_cache_init(nd_field_cache_t *cache, uint16_t capacity,
                    void *(*alloc_fn)(size_t), void (*free_fn)(void *))
{
	if (!cache) return;
	memset(cache, 0, sizeof(*cache));
	cache->capacity = (capacity > ND_FIELD_CACHE_MAX_ENTRIES) ? ND_FIELD_CACHE_MAX_ENTRIES : capacity;
	cache->alloc_fn = alloc_fn ? alloc_fn : malloc;
	cache->free_fn = free_fn ? free_fn : free;
}

static inline void
nd_field_cache_destroy(nd_field_cache_t *cache)
{
	if (!cache || !cache->free_fn) return;
	for (uint16_t i = 0; i < cache->capacity; i++) {
		nd_field_cache_entry_t *e = &cache->entries[i];
		if (e->height_buf) cache->free_fn(e->height_buf);
		if (e->bio_buf)    cache->free_fn(e->bio_buf);
		if (e->bio2_buf)   cache->free_fn(e->bio2_buf);
		if (e->blend_buf)  cache->free_fn(e->blend_buf);
		if (e->depth_buf)  cache->free_fn(e->depth_buf);
	}
	memset(cache, 0, sizeof(*cache));
}

static inline const nd_field_chunk_t *
nd_field_get(nd_field_cache_t *cache, uint16_t world, uint8_t lod, int32_t ox, int32_t oy)
{
	if (!cache) return NULL;
	for (uint16_t i = 0; i < cache->capacity; i++) {
		nd_field_cache_entry_t *e = &cache->entries[i];
		if (e->state == 1 && e->chunk.world == world && e->chunk.lod == lod &&
		    e->chunk.ox == ox && e->chunk.oy == oy) {
			e->last_used_tick = cache->current_tick;
			return &e->chunk;
		}
	}
	return NULL;
}

static inline int
nd_field_request(nd_field_cache_t *cache, uint16_t world, uint8_t lod,
                 int32_t ox, int32_t oy, uint16_t w, uint16_t h)
{
	if (!cache || w == 0 || h == 0) return -1;
	for (uint16_t i = 0; i < cache->capacity; i++) {
		nd_field_cache_entry_t *e = &cache->entries[i];
		if (e->state != 0 && e->chunk.world == world && e->chunk.lod == lod &&
		    e->chunk.ox == ox && e->chunk.oy == oy) {
			e->last_used_tick = cache->current_tick;
			return i;
		}
	}

	int slot = -1;
	for (uint16_t i = 0; i < cache->capacity; i++) {
		if (cache->entries[i].state == 0) {
			slot = (int)i;
			break;
		}
	}

	if (slot < 0) {
		uint32_t oldest_tick = 0xffffffff;
		for (uint16_t i = 0; i < cache->capacity; i++) {
			if (!cache->entries[i].pinned && cache->entries[i].last_used_tick < oldest_tick) {
				oldest_tick = cache->entries[i].last_used_tick;
				slot = (int)i;
			}
		}
	}
	if (slot < 0) return -1;

	nd_field_cache_entry_t *e = &cache->entries[slot];
	size_t needed_samples = (size_t)w * h;
	if (e->buf_capacity < needed_samples) {
		if (e->height_buf) cache->free_fn(e->height_buf);
		if (e->bio_buf)    cache->free_fn(e->bio_buf);
		if (e->bio2_buf)   cache->free_fn(e->bio2_buf);
		if (e->blend_buf)  cache->free_fn(e->blend_buf);
		if (e->depth_buf)  cache->free_fn(e->depth_buf);

		e->height_buf = (int16_t *)cache->alloc_fn(needed_samples * sizeof(int16_t));
		e->bio_buf    = (uint8_t *)cache->alloc_fn(needed_samples * sizeof(uint8_t));
		e->bio2_buf   = (uint8_t *)cache->alloc_fn(needed_samples * sizeof(uint8_t));
		e->blend_buf  = (uint8_t *)cache->alloc_fn(needed_samples * sizeof(uint8_t));
		e->depth_buf  = (uint8_t *)cache->alloc_fn(needed_samples * sizeof(uint8_t));
		e->buf_capacity = needed_samples;
	}

	e->chunk.world  = world;
	e->chunk.lod    = lod;
	e->chunk.ox     = ox;
	e->chunk.oy     = oy;
	e->chunk.w      = w;
	e->chunk.h      = h;
	e->chunk.height = e->height_buf;
	e->chunk.bio    = e->bio_buf;
	e->chunk.bio2   = e->bio2_buf;
	e->chunk.blend  = e->blend_buf;
	e->chunk.depth  = e->depth_buf;
	e->state        = 2; /* building */
	e->last_used_tick = cache->current_tick;
	return slot;
}

static inline int
nd_field_pump(nd_field_cache_t *cache, int budget)
{
	if (!cache || budget <= 0) return 0;
	int built = 0;
	for (uint16_t i = 0; i < cache->capacity && built < budget; i++) {
		nd_field_cache_entry_t *e = &cache->entries[i];
		if (e->state == 2) {
			nd_field_fill(&e->chunk);
			e->state = 1; /* ready */
			built++;
		}
	}
	return built;
}

static inline void
nd_field_pin_frame(nd_field_cache_t *cache)
{
	if (!cache) return;
	for (uint16_t i = 0; i < cache->capacity; i++)
		cache->entries[i].pinned = 0;
}

static inline void
nd_field_pin(nd_field_cache_t *cache, uint16_t world, uint8_t lod, int32_t ox, int32_t oy)
{
	if (!cache) return;
	for (uint16_t i = 0; i < cache->capacity; i++) {
		nd_field_cache_entry_t *e = &cache->entries[i];
		if (e->state != 0 && e->chunk.world == world && e->chunk.lod == lod &&
		    e->chunk.ox == ox && e->chunk.oy == oy) {
			e->pinned = 1;
			break;
		}
	}
}

static inline void
nd_field_forget(nd_field_cache_t *cache, uint16_t world)
{
	if (!cache) return;
	for (uint16_t i = 0; i < cache->capacity; i++) {
		if (cache->entries[i].chunk.world == world)
			cache->entries[i].state = 0;
	}
}

static inline void
nd_field_tick(nd_field_cache_t *cache)
{
	if (cache) cache->current_tick++;
}

#endif /* ND_FIELD_H */
