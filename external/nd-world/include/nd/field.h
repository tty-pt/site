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
#include <math.h>

#include <ttypt/noise.h>
#include <ttypt/chaff.h>
#if defined(__has_include) && __has_include("../../../libcorolla/include/ttypt/corolla.h")
#include "../../../libcorolla/include/ttypt/corolla.h"
#else
#include <ttypt/corolla.h>
#endif

/* Biome constants mirroring nd/world-types.h enum biome */
#ifndef BIOME_WATER
#define BIOME_WATER                     0
#define BIOME_PERMANENT_ICE             1
#define BIOME_TUNDRA                    2
#define BIOME_TUNDRA2                   3
#define BIOME_TUNDRA3                   4
#define BIOME_TUNDRA4                   5
#define BIOME_ALPINE_MEADOW             3
#define BIOME_MEDITERRANEAN             4
#define BIOME_WETLAND                   5
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

/* Runtime-parameterized room tile dimensions in world units (default 160 x 128 x 64) */
#define ND_FIELD_DEFAULT_ROOM_W 160
#define ND_FIELD_DEFAULT_ROOM_H 128
#define ND_FIELD_DEFAULT_ROOM_D 64

typedef struct {
	int32_t room_w;
	int32_t room_h;
	int32_t room_d;
} nd_room_geom_t;

static inline nd_room_geom_t *
nd_field_room_geom_state(void)
{
	static nd_room_geom_t s_geom = {
		ND_FIELD_DEFAULT_ROOM_W,
		ND_FIELD_DEFAULT_ROOM_H,
		ND_FIELD_DEFAULT_ROOM_D
	};
	return &s_geom;
}

static inline int32_t nd_field_get_room_w(void) { return nd_field_room_geom_state()->room_w; }
static inline int32_t nd_field_get_room_h(void) { return nd_field_room_geom_state()->room_h; }
static inline int32_t nd_field_get_room_d(void) { return nd_field_room_geom_state()->room_d; }

static inline void
nd_field_set_room_dims(int32_t room_w, int32_t room_h, int32_t room_d)
{
	nd_room_geom_t *g = nd_field_room_geom_state();
	if (room_w >= 16) g->room_w = room_w;
	if (room_h >= 16) g->room_h = room_h;
	if (room_d >= 8)  g->room_d = room_d;
}

static inline void
nd_field_room_center_world(int32_t rx, int32_t ry, int32_t *out_wx, int32_t *out_wy)
{
	int32_t rw = nd_field_get_room_w();
	int32_t rh = nd_field_get_room_h();
	if (out_wx) *out_wx = rx * rw + (rw / 2);
	if (out_wy) *out_wy = (rh / 2) - ry * rh;
}

#define ND_FIELD_ROOM_W (nd_field_get_room_w())
#define ND_FIELD_ROOM_H (nd_field_get_room_h())
#define ND_FIELD_ROOM_D (nd_field_get_room_d())

/* Elevation range and climate constants from server worldgen */
#define ND_FIELD_NOISE_FOURTH    (NOISE_MAX >> 2)
#define ND_FIELD_TEMP_MIN        ((int16_t)-36)
#define ND_FIELD_SOLAR_TEMP_MAX  ((int16_t)115)
#define ND_FIELD_SOLAR_TEMP_SPAN ((int16_t)150)
#define ND_FIELD_DAYTICK_BITS    10
#define ND_FIELD_RAIN_DIV        (NOISE_MAX >> 9)
#define ND_FIELD_RAIN_MAX        (NOISE_MAX / ND_FIELD_RAIN_DIV)

/* Biome constants mirroring nd/world-types.h enum biome */
#define ND_FIELD_TEMP_MAX        ((int16_t)102)

/* ============================================================================
 * Canonical 1:1 Physical Metric Scale (standing lamb = 18 wu = 0.40 m -> 45.0 wu/m)
 * & Scalable Planetary Lattice (default 2^16 rooms; scales cleanly up to 2^23):
 *   - 1 m = 45 wu, 1 km = 45,000 wu, 1 room (160 x 128 wu) = 3.556 m x 2.844 m
 *   - Default b = 16: 65,536 rooms = 233 x 186 km flat (52,053 km^2 torus)
 *   - Future b = 18..23: France-scale to Earth+ planets via nd_planet_scale_t
 * ============================================================================ */
#define ND_FIELD_LAMB_HEIGHT_M      0.40f
#define ND_FIELD_LAMB_BODY_WU       18.0f
#define ND_FIELD_WU_PER_METER       45.0f
#define ND_FIELD_METERS_PER_WU      (1.0f / ND_FIELD_WU_PER_METER)

#define ND_FIELD_PLANET_BITS        16
#define ND_FIELD_PLANET_BITS_MIN    16
#define ND_FIELD_PLANET_BITS_MAX    23
#define ND_FIELD_PLANET_ROOM_SPAN   (1 << ND_FIELD_PLANET_BITS) /* 65,536 rooms (default b=16) */
#define ND_FIELD_HEMISPHERE_SPAN    (ND_FIELD_PLANET_ROOM_SPAN / 2) /* 32,768 rooms */

typedef struct {
	uint8_t  planet_bits;        /* log2(rooms per axis): 16 default, up to 23 */
	uint8_t  areole_max_levels;  /* planet_bits + 1 (17 at b=16, 24 at b=23) */
	uint8_t  _pad[2];
	uint32_t rooms_per_axis;     /* 1u << planet_bits */
	int64_t  world_w_wu;         /* rooms_per_axis * room_w */
	int64_t  world_h_wu;         /* rooms_per_axis * room_h */
	float    wu_per_meter;       /* 45.0f (lamb 18 wu = 0.40 m) */
	float    max_relief_m;       /* target max mountain relief in meters */
	int32_t  max_elev_wu;        /* int32_t target elevation cap in wu */
	int32_t  min_elev_wu;        /* int32_t bathymetric trench floor in wu */
	float    torus_R_wu;         /* major radius scaled with 2^(planet_bits - 16) */
	float    torus_r0_wu;        /* minor radius scaled with 2^(planet_bits - 16) */
	float    snowline_base_wu;   /* metric temperate snowline reference in wu */
	float    cloud_lcl_wu;       /* metric cumulus lifting condensation level in wu */
} nd_planet_scale_t;

static inline nd_planet_scale_t
nd_planet_scale_for_bits(uint8_t bits, int32_t room_w, int32_t room_h)
{
	if (bits < ND_FIELD_PLANET_BITS_MIN) bits = ND_FIELD_PLANET_BITS_MIN;
	if (bits > ND_FIELD_PLANET_BITS_MAX) bits = ND_FIELD_PLANET_BITS_MAX;
	if (room_w < 16) room_w = ND_FIELD_DEFAULT_ROOM_W;
	if (room_h < 16) room_h = ND_FIELD_DEFAULT_ROOM_H;

	nd_planet_scale_t s;
	memset(&s, 0, sizeof(s));
	s.planet_bits       = bits;
	s.areole_max_levels = (uint8_t)(bits + 1u);
	s.rooms_per_axis    = 1u << bits;
	s.world_w_wu        = (int64_t)s.rooms_per_axis * (int64_t)room_w;
	s.world_h_wu        = (int64_t)s.rooms_per_axis * (int64_t)room_h;
	s.wu_per_meter      = ND_FIELD_WU_PER_METER;

	float relief_m;
	if (bits <= 16u)      relief_m = 1800.0f;
	else if (bits == 17u) relief_m = 3000.0f;
	else if (bits == 18u) relief_m = 4500.0f;
	else if (bits == 19u) relief_m = 5250.0f;
	else if (bits == 20u) relief_m = 6000.0f;
	else if (bits == 21u) relief_m = 7500.0f;
	else                  relief_m = 8850.0f; /* Everest scale (b >= 22) */

	s.max_relief_m     = relief_m;
	s.max_elev_wu      = (int32_t)(relief_m * s.wu_per_meter + 0.5f);
	s.min_elev_wu      = -(int32_t)(relief_m * 0.65f * s.wu_per_meter + 0.5f);
	float scale_mul    = (float)(1u << (bits - ND_FIELD_PLANET_BITS_MIN));
	s.torus_R_wu       = 2000000.0f * scale_mul;
	s.torus_r0_wu      = 1335000.0f * scale_mul;
	float snow_m       = (bits <= 16u) ? 1200.0f : ((bits <= 18u) ? 2400.0f : 3200.0f);
	float cloud_lcl_m  = (bits <= 16u) ? 1100.0f : 1600.0f;
	s.snowline_base_wu = snow_m * s.wu_per_meter;
	s.cloud_lcl_wu     = cloud_lcl_m * s.wu_per_meter;
	return s;
}

typedef struct {
	uint8_t  planet_bits;
	uint32_t epoch;
} nd_field_planet_state_t;

static inline nd_field_planet_state_t *
nd_field_planet_state(void)
{
	static nd_field_planet_state_t s_planet = { ND_FIELD_PLANET_BITS, 1u };
	return &s_planet;
}

static inline uint8_t
nd_field_get_planet_bits(void)
{
	return nd_field_planet_state()->planet_bits;
}

static inline uint32_t
nd_field_get_planet_epoch(void)
{
	return nd_field_planet_state()->epoch;
}

static inline void
nd_field_set_planet_bits(uint8_t bits)
{
	if (bits < ND_FIELD_PLANET_BITS_MIN) bits = ND_FIELD_PLANET_BITS_MIN;
	if (bits > ND_FIELD_PLANET_BITS_MAX) bits = ND_FIELD_PLANET_BITS_MAX;
	nd_field_planet_state_t *st = nd_field_planet_state();
	if (st->planet_bits != bits) {
		st->planet_bits = bits;
		st->epoch++;
		if (st->epoch == 0u) st->epoch = 1u;
	}
}

static inline nd_planet_scale_t
nd_field_get_planet_scale(void)
{
	return nd_planet_scale_for_bits(nd_field_get_planet_bits(),
	                                nd_field_get_room_w(),
	                                nd_field_get_room_h());
}

static inline int32_t
nd_field_active_max_elev_wu(void)
{
	return nd_field_get_planet_scale().max_elev_wu;
}

static inline float
nd_field_active_global_max_z(void)
{
	return (float)nd_field_active_max_elev_wu() + 200.0f;
}

/* Saturating clamp to legacy int16_t ABI range [-32768, 32767] */
static inline int16_t
nd_field_clamp_i16(int32_t v)
{
	if (v > 32767) return 32767;
	if (v < -32768) return -32768;
	return (int16_t)v;
}

/* Exact deterministic integer square root floor(sqrt(x)) (bit-exact across CPU/WASM/SIMD) */
static inline uint32_t
nd_field_isqrt32(uint32_t x)
{
	uint32_t res = 0u;
	uint32_t bit = 1u << 30;
	while (bit > x)
		bit >>= 2;
	while (bit != 0u) {
		if (x >= res + bit) {
			x -= res + bit;
			res = (res >> 1) + bit;
		} else {
			res >>= 1;
		}
		bit >>= 2;
	}
	return res;
}

#ifndef ND_XXH32_U32_DEFINED
#define ND_XXH32_U32_DEFINED 1
static inline uint32_t
nd_xxh32_u32(uint32_t val, uint32_t seed)
{
	uint32_t h32 = seed + 0x165667B1u + 4u;
	h32 += val * 0xC2B2AE3Du;
	h32 = ((h32 << 17) | (h32 >> 15)) * 0x27D4EB2Fu;
	h32 ^= h32 >> 15;
	h32 *= 0x85EBCA77u;
	h32 ^= h32 >> 13;
	h32 *= 0xC2B2AE3Du;
	h32 ^= h32 >> 16;
	return h32;
}
#endif

/* Phase 2: 10 Real-World Geological Landform Archetypes */
typedef enum {
	ND_GEO_ALLUVIAL_PLAIN    = 0, /* 1. Alluvial coastal plains & floodplains (0-2 deg) */
	ND_GEO_PIEDMONT_HILLS    = 1, /* 2. Rolling piedmont & pastoral hills (5-18 deg, 30-180 m) */
	ND_GEO_ALPINE_CORDILLERA = 2, /* 3. Alpine folded cordilleras (35-60 deg, 1200-1800+ m) */
	ND_GEO_CANYON_MESA       = 3, /* 4. Canyonlands, layered mesas & stratified buttes */
	ND_GEO_FAULT_ESCARPMENT  = 4, /* 5. Asymmetric fault-block escarpments (scarp + dip slope) */
	ND_GEO_GLACIAL_FJORD     = 5, /* 6. Glacial U-valleys & coastal fjords */
	ND_GEO_VOLCANIC_CONE     = 6, /* 7. Volcanic stratovolcano cones & breached calderas */
	ND_GEO_KARST_TOWERS      = 7, /* 8. Karst & limestone towers / sinkhole valleys */
	ND_GEO_ARID_BASIN_ERG    = 8, /* 9. Arid basins, salt playas & wind-aligned dune ergs */
	ND_GEO_SEA_CLIFF_COAST   = 9, /* 10. Dramatic hard-rock sea cliffs & coastal headlands */
	ND_GEO_PROVINCE_COUNT    = 10
} nd_geo_province_t;

typedef struct {
	uint8_t province;        /* Primary nd_geo_province_t (0..9) */
	uint8_t secondary;       /* Secondary blended nd_geo_province_t (0..9) */
	uint8_t blend_q8;        /* Smooth transition weight 0..255 */
	uint8_t tectonic_uplift; /* 0..255: crustal uplift intensity */
	uint8_t strata_hardness; /* 0..255: cliff-terrace crispness vs smooth erosion */
	uint8_t basin_flatness;  /* 0..255: alluvial/playa/u-valley floor flattening */
	uint8_t erosion_age;     /* 0..255: young jagged alpine vs ancient rounded piedmont */
	uint8_t fault_azimuth;   /* 0..255: predominant structural strike/dip angle (0..2*pi) */
} nd_geology_t;

/* Wrap integer room coordinate modulo 2^bits into [-2^(bits-1), 2^(bits-1) - 1].
 * At bits == 16 this is bit-identical to (int32_t)(int16_t)r. */
static inline int32_t
nd_field_wrap_room_coord(int32_t r, uint8_t bits)
{
	uint32_t span = 1u << bits;
	uint32_t half = span >> 1;
	uint32_t u = ((uint32_t)r + half) & (span - 1u);
	return (int32_t)u - (int32_t)half;
}

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

/*
 * 1/f Fractal terrain octave specifications.
 * Governed by two clean declarative parameters:
 *   - ND_FIELD_1F_SCALE: single base amplitude constant.
 *   - ND_FIELD_OCTAVE_COUNT: number of intermediate octaves.
 *
 * Under 1/f pink noise, octave amplitude scales strictly with wavelength (A ~ lambda ~ 2^shift),
 * ensuring that every octave across the continuum—from room-scale rolling hills down to
 * fine surface slope—has an identical characteristic surface gradient (A / lambda = constant).
 *
 * Adding more octaves (e.g. changing 6 to 7 or 8) smoothly introduces higher macro features
 * (shift = MIN_SHIFT + k) without altering lower octaves or static tables.
 */
#define ND_FIELD_1F_SCALE        1
#define ND_FIELD_FINE_MIN_SHIFT  3
#define ND_FIELD_OCTAVE_COUNT    7

#define ND_FIELD_FINE_COUNT      ND_FIELD_OCTAVE_COUNT
#define ND_FIELD_FINE_AMP_BUDGET (((1 << ND_FIELD_OCTAVE_COUNT) - 1) * ND_FIELD_1F_SCALE)
#define ND_FIELD_MAX_ELEV_WU     81000
#define ND_FIELD_GLOBAL_MAX_Z    81200.0f

#define ND_FIELD_NOISE_DIM       2
#define ND_FIELD_NOISE_PRECISION 8
#define ND_FIELD_NOISE_MID       128
#define ND_FIELD_NOISE_SHIFT     16

#define ND_FIELD_STENCIL_SPAN    4

#define ND_SHADE_NEUTRAL         256
#define ND_SHADE_MIN             0
#define ND_SHADE_MAX             ((ND_SHADE_NEUTRAL * 2) - 1)

typedef struct {
	uint8_t shift;
	int16_t amp;
} nd_field_fine_octave_t;

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

/* Canonical Bimodal Hypsometric Transfer Function:
 * Transforms continuous continental elevation into realistic planetary relief:
 * deep ocean trenches (-2000..-40), shallow coastal shelves (-12..0),
 * fertile lowlands/grasslands (0..42), continental uplands & plateaus (43..270+),
 * topped by Phase 2 metric geological province orogeny up to nd_field_active_max_elev_wu(). */
static inline int32_t
nd_field_bimodal_hypsometry(int32_t elev)
{
	if (elev < 0) {
		/* Ocean basin: gradual shallow shelf (-8..0), dropping into mid shelf (-16..-8) and deep seabed (-25..-2000) */
		if (elev > -8) {
			return elev; /* Gentle continental shelf for ankle-to-waist coastal shallows (-1..-7) */
		} else {
			int32_t d = -elev - 8;
			/* Smooth shelf-to-basin progression: -8 -> -8, -12 -> -16, -20 -> -41, deep trenches -> -2000 */
			int32_t deep = 8 + (d * 18) / 10 + (d * d * 45) / 100;
			if (deep > 2000) deep = 2000;
			return -deep;
		}
	} else {
		/* Landmass: coastal plain (0..8), fertile lowlands (8..25), continental interior uplands (25..150) */
		if (elev <= 8) {
			return (elev * 12) / 8; /* Crisp coastal slope lifting land cleanly above sea level */
		} else if (elev <= 25) {
			return 12 + ((elev - 8) * 18) / 10; /* Lowlands & uplands: elevated +12..+42 */
		} else {
			int32_t max_e = nd_field_active_max_elev_wu();
			int32_t m = elev - 25;
			int32_t peak = 43 + (m * 22) / 10 + (m * m * 12) / 100;
			if (peak > max_e) peak = max_e;
			return peak;
		}
	}
}

/* Pure-integer Q15 cosine/sine over a 16-bit full-circle turn (0..65535 == 0..2*pi). */
static inline int32_t
nd_field_cos_q15(uint16_t turn)
{
	uint32_t d = (turn > 32768u) ? (65536u - (uint32_t)turn) : (uint32_t)turn;
	int neg = (d > 16384u);
	uint32_t u = neg ? (32768u - d) : d;
	uint32_t t_q15 = u << 1;
	uint32_t t2 = (t_q15 * t_q15) >> 15;
	uint32_t t4 = (t2 * t2) >> 15;
	uint32_t poly = 32768u - ((7658u * t2) >> 15) + ((626u * t4) >> 15);
	int32_t c = (int32_t)(((32768u - t2) * poly) >> 15);
	return neg ? -c : c;
}

static inline int32_t
nd_field_sin_q15(uint16_t turn)
{
	return nd_field_cos_q15((uint16_t)(turn - 16384u));
}

/* 3D torus surface normal in Q15: n = (cos(phi)*cos(theta), cos(phi)*sin(theta), sin(phi)) */
static inline void
nd_field_torus_normal_q15(int16_t pos_x, int16_t pos_y,
                          int32_t *nx_q15, int32_t *ny_q15, int32_t *nz_q15)
{
	int32_t cos_phi = nd_field_cos_q15((uint16_t)pos_y);
	int32_t sin_phi = nd_field_sin_q15((uint16_t)pos_y);
	int32_t cos_theta = nd_field_cos_q15((uint16_t)pos_x);
	int32_t sin_theta = nd_field_sin_q15((uint16_t)pos_x);
	if (nx_q15) *nx_q15 = (cos_phi * cos_theta) >> 15;
	if (ny_q15) *ny_q15 = (cos_phi * sin_theta) >> 15;
	if (nz_q15) *nz_q15 = sin_phi;
}

/* ============================================================================
 * Per-World libcorolla Parametric Manifold & Climate Model Cache
 * ============================================================================ */

#define ND_FIELD_WORLD0_MAJOR_R    2000000.0
#define ND_FIELD_WORLD0_MINOR_R    1335000.0
#define ND_FIELD_WORLD0_AXIAL_TILT (32.0 * (COROLLA_PI / 180.0))

#define ND_FIELD_MOON_MAJOR_R      800000.0
#define ND_FIELD_MOON_MINOR_R      500000.0
#define ND_FIELD_MOON_AXIAL_TILT   (18.0 * (COROLLA_PI / 180.0))
#define ND_FIELD_MOON_ORBIT_SMA    16500000.0
#define ND_FIELD_MOON_ORBIT_ECC    0.045
#define ND_FIELD_MOON_ORBIT_INC    ((23.43928 + 5.145) * (COROLLA_PI / 180.0))
#define ND_FIELD_MOON_ORBIT_PERIOD 3725.0

/* Populate deterministic libcorolla 3D rigid-body & orbital ephemeris definition for a world:
 *   - World 0 & 1 (Citadel / Primary Torus): R=2,000,000, r0=1,335,000, obliquity=32.0 deg,
 *     spin_period=3600s, solar year=64*3600s.
 *   - World 2 (Aethelgard / Moon): R=800,000, r0=500,000, obliquity=18.0 deg,
 *     Keplerian orbit around World 0 (a=16,500,000, e=0.045, i=28.58428 deg, period=3725s),
 *     tidally locked / synchronous spin_period=3725s.
 *   - Other worlds (> 2): deterministic procedural torus radii, obliquity, spin & orbit. */
static inline void
nd_field_world_body(uint16_t world, corolla_body_t *out)
{
	if (!out)
		return;
	memset(out, 0, sizeof(*out));
	if (world == 0 || world == 1) {
		out->surf = corolla_torus(ND_FIELD_WORLD0_MAJOR_R, ND_FIELD_WORLD0_MINOR_R);
		out->axial_tilt_rad = ND_FIELD_WORLD0_AXIAL_TILT;
		out->axis_azimuth_rad = 0.0;
		out->spin_period = 3600.0;
		out->spin_phase_0 = 1.20;
		out->orbit.semi_major_axis = 0.0;
		out->orbit.period = 64.0 * 3600.0;
		out->orbit.mean_anomaly_0 = 0.5235987756;
	} else if (world == 2) {
		out->surf = corolla_torus(ND_FIELD_MOON_MAJOR_R, ND_FIELD_MOON_MINOR_R);
		out->axial_tilt_rad = ND_FIELD_MOON_AXIAL_TILT;
		out->axis_azimuth_rad = 0.35;
		out->spin_period = ND_FIELD_MOON_ORBIT_PERIOD;
		out->spin_phase_0 = 0.785398;
		out->orbit.semi_major_axis = ND_FIELD_MOON_ORBIT_SMA;
		out->orbit.eccentricity = ND_FIELD_MOON_ORBIT_ECC;
		out->orbit.inclination_rad = ND_FIELD_MOON_ORBIT_INC;
		out->orbit.lon_asc_node_rad = 0.0;
		out->orbit.arg_periapsis_rad = 0.0;
		out->orbit.mean_anomaly_0 = 0.785398;
		out->orbit.period = ND_FIELD_MOON_ORBIT_PERIOD;
	} else {
		uint32_t h = (uint32_t)world * 0x9E3779B1u;
		double r_maj = 1600000.0 + (double)((h & 0xFFu) * 4000u);
		double r_min = 900000.0  + (double)(((h >> 8) & 0xFFu) * 2000u);
		double spin_mag = 2400.0 + (double)((h >> 12) & 0xFFu) * 16.0;
		out->surf = corolla_torus(r_maj, r_min);
		out->axial_tilt_rad = (15.0 + (double)((h >> 16) & 0x1Fu)) * (COROLLA_PI / 180.0);
		out->axis_azimuth_rad = (double)((h >> 21) & 0x3Fu) * (COROLLA_TWO_PI / 64.0);
		out->spin_period = (h & 0x80000000u) ? -spin_mag : spin_mag;
		out->spin_phase_0 = 0.0;
		out->orbit.semi_major_axis = 20000000.0 + (double)((h & 0xFFu) * 50000u);
		out->orbit.eccentricity = 0.02 + (double)(((h >> 8) & 0x1Fu)) * 0.002;
		out->orbit.inclination_rad = (10.0 + (double)((h >> 16) & 0x1Fu)) * (COROLLA_PI / 180.0);
		out->orbit.mean_anomaly_0 = (double)((h >> 24) & 0xFFu) * (COROLLA_TWO_PI / 256.0);
		out->orbit.period = 7200.0 + (double)((h & 0xFFu) * 60u);
	}
}

typedef struct {
	uint16_t world;
	uint8_t  valid;
	corolla_climate_model_t model;
} nd_field_world_memo_t;

/* Map integer room lattice coordinates (rx, ry) to normalized libcorolla (u, v) in [0, 1)^2:
 *   - rx = 0 -> u = 0.0
 *   - ry = 0 (Outer Equator) -> v = 0.5
 *   - ry = +16384 (North Crown Pole) -> v = 0.75
 *   - ry = -16384 (South Crown Pole) -> v = 0.25
 *   - ry = +-32768 (Inner Hub Equator) -> v = 0.0 */
static inline void
nd_field_room_to_corolla_uv(int16_t rx, int16_t ry, double *out_u, double *out_v)
{
	uint16_t ux = (uint16_t)rx;
	uint16_t uy = (uint16_t)((uint16_t)ry + 32768u);
	if (out_u) *out_u = (double)ux / 65536.0;
	if (out_v) *out_v = (double)uy / 65536.0;
}

/* Map continuous world coordinates (wx, wy) to normalized libcorolla (u, v) in [0, 1)^2 */
static inline void
nd_field_world_to_corolla_uv(double wx, double wy, double *out_u, double *out_v)
{
	nd_planet_scale_t ps = nd_field_get_planet_scale();
	double wdim_x = (double)ps.world_w_wu;
	double wdim_y = (double)ps.world_h_wu;
	double u = fmod(wx / wdim_x, 1.0);
	if (u < 0.0) u += 1.0;
	double v = fmod((wy / wdim_y) + 0.5, 1.0);
	if (v < 0.0) v += 1.0;
	if (out_u) *out_u = u;
	if (out_v) *out_v = v;
}

static inline const corolla_climate_model_t *
nd_field_world_climate_model(uint16_t world)
{
	static _CHAFF_TLS nd_field_world_memo_t s_world_memo[16];
	nd_field_world_memo_t *slot = &s_world_memo[world & 15u];
	if (!slot->valid || slot->world != world) {
		corolla_body_t body;
		nd_field_world_body((world == 1) ? 0 : world, &body);
		double spin_dir = (body.spin_period < 0.0) ? -1.0 : 1.0;
		corolla_climate_model_init(&slot->model, &body.surf,
		                           body.axial_tilt_rad, spin_dir,
		                           (double)ND_FIELD_TEMP_MIN,
		                           (double)ND_FIELD_TEMP_MAX,
		                           0.165,
		                           (double)nd_field_water_level(world),
		                           (double)ND_FIELD_RAIN_MAX,
		                           (uint32_t)world);
		slot->world = world;
		slot->valid = 1;
	}
	return &slot->model;
}

/* Sun direction in the torus orbital plane in Q15: sun = (cos(lambda), sin(lambda), 0) */
static inline void
nd_field_sun_dir_q15(uint16_t sun_azimuth,
                     int32_t *sx_q15, int32_t *sy_q15, int32_t *sz_q15)
{
	if (sx_q15) *sx_q15 = nd_field_cos_q15(sun_azimuth);
	if (sy_q15) *sy_q15 = nd_field_sin_q15(sun_azimuth);
	if (sz_q15) *sz_q15 = 0;
}

__attribute__((weak, visibility("hidden"))) uint16_t g_nd_field_sun_azimuth = 0;
__attribute__((weak, visibility("hidden"))) uint16_t g_nd_field_season_turn = 0;

static inline uint16_t nd_field_get_sun_azimuth(void) { return g_nd_field_sun_azimuth; }
static inline void nd_field_set_sun_azimuth(uint16_t az) { g_nd_field_sun_azimuth = az; }
static inline uint16_t nd_field_get_season_turn(void) { return g_nd_field_season_turn; }
static inline void nd_field_set_season_turn(uint16_t st) { g_nd_field_season_turn = st; }

/* Evaluate 3D unit sun vector via libcorolla corolla_sun_dir() */
static inline void
nd_field_corolla_sun_dir(uint16_t world, uint16_t sun_azimuth, uint16_t season_turn,
                         double out_sun_dir[3])
{
	const corolla_climate_model_t *cm = nd_field_world_climate_model(world);
	double tau = COROLLA_TWO_PI * ((double)season_turn / 65536.0);
	double lambda = COROLLA_TWO_PI * ((double)sun_azimuth / 65536.0);
	/* corolla_sun_dir uses hour_angle = tau - phi; set phi = tau - lambda so hour_angle == lambda */
	corolla_sun_dir(cm->axial_tilt_rad, tau, tau - lambda, out_sun_dir);
}

/* Unified libcorolla celestial ephemeris for any observer world and companion body at time t_sec:
 *   - Evaluates the 3D pose of observer_world and its companion body (World 2 Moon when on World 0/1,
 *     or World 1 Planet when on World 2).
 *   - Computes out_sun_dir[3] in the observer body's local frame (accounting for axial tilt & spin).
 *   - Computes out_moon_dir[3] as the exact unit vector from the observer body's center toward the
 *     companion body's Keplerian orbital position (so ocean tidal bulges point directly at the 3D Moon). */
static inline void
nd_field_eval_celestial_ephemeris(uint16_t observer_world, double t_sec,
                                  double out_sun_dir[3],
                                  double out_moon_dir[3],
                                  corolla_pose_t *out_obs_pose,
                                  corolla_pose_t *out_companion_pose)
{
	uint16_t comp_world = (observer_world == 2) ? 1 : 2;
	corolla_body_t obs_body, comp_body, primary_body;
	nd_field_world_body(observer_world, &obs_body);
	nd_field_world_body(comp_world, &comp_body);
	nd_field_world_body(0, &primary_body);

	corolla_pose_t obs_pose, comp_pose;
	corolla_eval_body_pose(&obs_body, NULL, t_sec, &obs_pose);
	corolla_eval_body_pose(&comp_body, NULL, t_sec, &comp_pose);

	if (out_obs_pose) *out_obs_pose = obs_pose;
	if (out_companion_pose) *out_companion_pose = comp_pose;

	if (out_sun_dir) {
		double tau_orbit = COROLLA_TWO_PI * (t_sec / primary_body.orbit.period) + primary_body.orbit.mean_anomaly_0;
		double spin_per = (fabs(obs_body.spin_period) > 1e-9) ? obs_body.spin_period : 3600.0;
		double lambda_sun = COROLLA_TWO_PI * (t_sec / spin_per) + obs_body.spin_phase_0;
		corolla_sun_dir(obs_body.axial_tilt_rad, tau_orbit, tau_orbit - lambda_sun, out_sun_dir);
	}

	if (out_moon_dir) {
		double disp_inertial[3] = {
			comp_pose.center[0] - obs_pose.center[0],
			comp_pose.center[1] - obs_pose.center[1],
			comp_pose.center[2] - obs_pose.center[2]
		};
		double disp_body[3];
		corolla_pose_vec_to_body(&obs_pose, disp_inertial, disp_body);
		double len = sqrt(disp_body[0] * disp_body[0] +
		                  disp_body[1] * disp_body[1] +
		                  disp_body[2] * disp_body[2]);
		if (len > 1e-9) {
			out_moon_dir[0] = disp_body[0] / len;
			out_moon_dir[1] = disp_body[1] / len;
			out_moon_dir[2] = disp_body[2] / len;
		} else {
			out_moon_dir[0] = 1.0;
			out_moon_dir[1] = 0.0;
			out_moon_dir[2] = 0.0;
		}
	}
}

static inline uint16_t
nd_field_sun_azimuth_from_day_tick(uint64_t day_tick_val)
{
	return (uint16_t)((day_tick_val & ((1ULL << ND_FIELD_DAYTICK_BITS) - 1ULL))
	                  << (16 - ND_FIELD_DAYTICK_BITS));
}

#define ND_FIELD_YEARTICK_BITS 16
#define ND_FIELD_SIN_TILT_Q15  17364 /* sin(32.0 deg) * 32768 */

static inline uint16_t
nd_field_season_turn_from_day_tick(uint64_t year_tick_val)
{
	return (uint16_t)((year_tick_val & ((1ULL << ND_FIELD_YEARTICK_BITS) - 1ULL))
	                  << (16 - ND_FIELD_YEARTICK_BITS));
}

/* Evaluate axial-tilt solar declination (sin_delta_q15, cos_delta_q15) for current season_turn.
 * At season_turn == 0 (vernal equinox), sin_delta_q15 == 0 and cos_delta_q15 == 32768. */
static inline void
nd_field_solar_declination_q15(uint16_t season_turn,
                               int32_t *out_sin_delta_q15,
                               int32_t *out_cos_delta_q15)
{
	if (season_turn == 0 || season_turn == 32768u) {
		if (out_sin_delta_q15) *out_sin_delta_q15 = 0;
		if (out_cos_delta_q15) *out_cos_delta_q15 = 32768;
		return;
	}
	int32_t sin_tau = nd_field_sin_q15(season_turn);
	int32_t sin_d = (sin_tau * ND_FIELD_SIN_TILT_Q15) >> 15;
	/* Second-order Taylor/binomial for sqrt(1 - sin_d^2) in Q15 (accurate to < 1 LSB for |tilt| <= 35 deg) */
	int32_t s2_q15 = (sin_d * sin_d) >> 15;
	int32_t s4_q15 = (s2_q15 * s2_q15) >> 15;
	int32_t cos_d = 32768 - (s2_q15 >> 1) - (s4_q15 >> 3);
	if (out_sin_delta_q15) *out_sin_delta_q15 = sin_d;
	if (out_cos_delta_q15) *out_cos_delta_q15 = cos_d;
}

/* Toroidal solar incidence in Q15 [0..32768]:
 * max(0, cos(phi)*cos(delta)*cos(theta - lambda) + sin(phi)*sin(delta)) */
static inline int32_t
nd_field_torus_solar_incidence_q15(int16_t pos_x, int16_t pos_y, uint16_t sun_azimuth)
{
	int32_t cos_phi = nd_field_cos_q15((uint16_t)pos_y);
	int32_t cos_theta_rel = nd_field_cos_q15((uint16_t)((uint16_t)pos_x - sun_azimuth));
	int32_t dot_eq_q15 = (cos_phi * cos_theta_rel) >> 15;
	uint16_t season = g_nd_field_season_turn;
	if (season == 0 || season == 32768u)
		return (dot_eq_q15 > 0) ? dot_eq_q15 : 0;

	int32_t sin_d = 0, cos_d = 32768;
	nd_field_solar_declination_q15(season, &sin_d, &cos_d);
	int32_t sin_phi = nd_field_sin_q15((uint16_t)pos_y);
	int32_t dot_q15 = ((dot_eq_q15 * cos_d) >> 15) + ((sin_phi * sin_d) >> 15);
	return (dot_q15 > 0) ? (dot_q15 > 32768 ? 32768 : dot_q15) : 0;
}

/* Signed toroidal solar dot product in Q15 [-32768..+32768], accounting for:
 *   1. Axial-tilt seasonal declination delta(season_turn)
 *   2. Equinox midday arch-eclipse self-shadowing across the central hole, which lifts
 *      during Northern & Southern summer solstices when sunlight shines over the torus lip
 *      (Anders Sandberg double-summer archworld effect). */
static inline int32_t
nd_field_torus_signed_solar_q15(int16_t pos_x, int16_t pos_y, uint16_t sun_azimuth)
{
	int32_t cos_phi = nd_field_cos_q15((uint16_t)pos_y);
	int32_t cos_theta_rel = nd_field_cos_q15((uint16_t)((uint16_t)pos_x - sun_azimuth));
	int32_t dot_eq_q15 = (cos_phi * cos_theta_rel) >> 15;
	uint16_t season = g_nd_field_season_turn;
	int32_t dot_q15 = dot_eq_q15;
	int32_t abs_sin_tau = 0;

	if (season != 0 && season != 32768u) {
		int32_t sin_d = 0, cos_d = 32768;
		nd_field_solar_declination_q15(season, &sin_d, &cos_d);
		int32_t sin_phi = nd_field_sin_q15((uint16_t)pos_y);
		dot_q15 = ((dot_eq_q15 * cos_d) >> 15) + ((sin_phi * sin_d) >> 15);
		int32_t sin_tau = nd_field_sin_q15(season);
		abs_sin_tau = (sin_tau < 0) ? -sin_tau : sin_tau;
	}

	/* On the inner rim (cos_phi < 0), when facing the sun across the central hole (dot_q15 > 0),
	 * the sunward side of the torus ring partially eclipses the midday sun near the equinoxes (~25% attenuation),
	 * whereas at the solstices (abs_sin_tau -> 32768) sunlight clears the front lip into the hole. */
	if (cos_phi < 0 && dot_q15 > 0) {
		int32_t inner_q15 = -cos_phi;
		int32_t eclipse_strength_q15 = 32768 - abs_sin_tau;
		int32_t eclipse_loss_q15 = (((inner_q15 * 8192) >> 15) * eclipse_strength_q15) >> 15;
		int32_t atten_q15 = 32768 - eclipse_loss_q15;
		dot_q15 = (dot_q15 * atten_q15) >> 15;
	}
	if (dot_q15 > 32768) dot_q15 = 32768;
	if (dot_q15 < -32768) dot_q15 = -32768;
	return dot_q15;
}

/* Metric environmental lapse rate calibrated to nd_planet_scale_t.snowline_base_wu:
 * Lowlands (0..100 m = 0..4,500 wu) stay warm; foothills cool into montane conifers;
 * subalpine meadows emerge above ~450 m; temperate summits cross freezing at snowline_base_wu. */
static inline int32_t
nd_field_lapse_cool(int32_t elev_rel)
{
	if (elev_rel <= 4500)
		return 0;
	int32_t snow_wu = (int32_t)nd_field_get_planet_scale().snowline_base_wu;
	int32_t span = snow_wu - 4500;
	if (span < 4500) span = 4500;
	int32_t cool = (int32_t)(((int64_t)(elev_rel - 4500) * 96LL) / (int64_t)span);
	if (cool > 220) cool = 220;
	return cool;
}

/* Metric orographic rain shadow (leeward) & precipitation lift (windward) across 3 upwind rooms */
static inline int32_t
nd_field_orographic_rain_delta(int32_t cur_hyp, int32_t up_hyp, int32_t sea_lvl)
{
	if (up_hyp > sea_lvl + 6750 && cur_hyp < up_hyp - 180) {
		int32_t shadow_depth = up_hyp - (sea_lvl + 6750);
		if (shadow_depth > 27000) shadow_depth = 27000;
		return -((shadow_depth * 64) / 27000);
	}
	if (cur_hyp > up_hyp + 180 && cur_hyp > sea_lvl + 2250) {
		int32_t lift_height = cur_hyp - up_hyp;
		if (lift_height > 2700) lift_height = 2700;
		return (lift_height * 48) / 2700;
	}
	return 0;
}

/* Canonical Mean Annual Toroidal Climate Temperature (in NeverDark temperature units):
 * Governed by poloidal latitude phi (room_y) relative to the plane of the torus (Z = 0):
 *   - Outer Equator (room_y = 0, phi = 0): unobstructed overhead insolation (+94 base)
 *   - Inner Equator (room_y = +-32768, phi = +-pi): in the torus plane but partially eclipsed
 *     by the opposite side of the ring around equinox noon (-8 arch-shadow cooling -> +86 base)
 *   - North & South Polar Crowns (room_y = +-16384, phi = +-pi/2): tangent to orbital plane (-46 base)
 *   - Metric high mountain environmental lapse rate via nd_field_lapse_cool(). */
static inline int16_t
nd_field_lat_climate_temp(int16_t pos_y, uint32_t tm, int32_t elev_rel)
{
	int32_t abs_ry = abs((int32_t)pos_y);
	int32_t eq_dist = (abs_ry <= 16384) ? abs_ry : (32768 - abs_ry);
	int32_t cos_phi = nd_field_cos_q15((uint16_t)pos_y);
	int32_t lat_insol_q15 = abs(cos_phi);
	int32_t inner_q15 = (cos_phi < 0) ? -cos_phi : 0;

	/* 32% linear meridional heat transport (eq_dist) + 68% solar incidence (1 - |cos(phi)|) */
	int32_t lat_frac_q15 = (((eq_dist << 1) * 10486) + ((32768 - lat_insol_q15) * 22282)) >> 15;
	int32_t lat_cool = (lat_frac_q15 * 142) >> 15;
	if (eq_dist > 10800) {
		/* Polar ice-albedo feedback on the top and bottom crowns (59..90 deg == eq_dist > 10800) */
		lat_cool += ((eq_dist - 10800) * 52) / (16384 - 10800);
	}
	/* Inner-hole arch-eclipse cooling: -4 units at the inner equator (phi = +-pi) vs outer equator (phi = 0) */
	int32_t inner_eclipse_cool = (inner_q15 * 4) >> 15;

	int32_t tm_var = (int32_t)(tm >> 16) - 128;
	int32_t tm_amp = (eq_dist > 11500) ? 12 : 18;
	int32_t bio_temp = 96 - lat_cool - inner_eclipse_cool + (tm_var * tm_amp / 128) - nd_field_lapse_cool(elev_rel);
	if (bio_temp < -32000) bio_temp = -32000;
	if (bio_temp > 32000) bio_temp = 32000;
	return (int16_t)bio_temp;
}

static inline int16_t
nd_field_lat_climate_temp_q16(int32_t ry_q16, uint32_t tm, int32_t elev_rel)
{
	int16_t ry = (int16_t)(ry_q16 >> 16);
	uint32_t fy = (uint32_t)(ry_q16 & 0xFFFF);
	int16_t t0 = nd_field_lat_climate_temp(ry, tm, elev_rel);
	if (fy == 0)
		return t0;
	int16_t t1 = nd_field_lat_climate_temp((int16_t)(ry + 1), tm, elev_rel);
	uint32_t ty = noise_wfade5(fy, 16);
	return (int16_t)(t0 + (int32_t)(((int64_t)(t1 - t0) * ty) >> 16));
}

/* Realistic Diurnal Day/Night Temperature Swing on the Rotating Torus:
 * Warms sunlit longitudes during the day and cools shadowed longitudes at night
 * without turning the night side of the Equator into polar ice. */
static inline int16_t
nd_field_diurnal_temp_delta(int16_t pos_x, int16_t pos_y, uint16_t sun_azimuth,
                            uint32_t rain, uint8_t is_water)
{
	int32_t signed_dot_q15 = nd_field_torus_signed_solar_q15(pos_x, pos_y, sun_azimuth);
	int32_t diurnal_amp = 15;
	if (is_water) {
		diurnal_amp = 13; /* High oceanic thermal inertia moderates day/night swing */
	} else if (rain < 200u) {
		/* Arid desert/steppe skies have stronger daytime solar heating & radiative night cooling */
		diurnal_amp += (int32_t)(((200u - rain) * 3u) / 200u);
	}
	return (int16_t)((signed_dot_q15 * diurnal_amp) >> 15);
}

static inline int16_t
nd_field_torus_solar_cooling(int16_t pos_x, int16_t pos_y, uint16_t sun_azimuth)
{
	int32_t inc_q15 = nd_field_torus_solar_incidence_q15(pos_x, pos_y, sun_azimuth);
	return (int16_t)(((32768 - inc_q15) * ND_FIELD_SOLAR_TEMP_SPAN) >> 15);
}

static inline int16_t
nd_field_torus_latitude_cooling(int16_t pos_y)
{
	const uint32_t tm_neutral = 128u << 16;
	return (int16_t)(96 - nd_field_lat_climate_temp(pos_y, tm_neutral, 0));
}

static inline int16_t
nd_field_temp_at_azimuth(uint16_t obits, uint32_t he, uint32_t tm,
                         int16_t pos_x, int16_t pos_y, uint16_t sun_azimuth)
{
	(void)obits; (void)he;
	int16_t base_climate = nd_field_lat_climate_temp(pos_y, tm, 0);
	int16_t diurnal = nd_field_diurnal_temp_delta(pos_x, pos_y, sun_azimuth, 256u, 0);
	return (int16_t)(base_climate + diurnal);
}

static inline int16_t
nd_field_temp(uint16_t obits, uint32_t he, uint32_t tm, int16_t pos_x, int16_t pos_y)
{
	return nd_field_temp_at_azimuth(obits, he, tm, pos_x, pos_y, nd_field_get_sun_azimuth());
}

static inline uint32_t
nd_field_rain(uint32_t cl)
{
	return cl / ND_FIELD_RAIN_DIV;
}

/* Geophysical Whittaker 4x4 Climate Matrix (indexed by [tmp_bit 0..3][rn_bit 0..3]):
 *   - Row 0 (Subpolar / Cryo, tmp -36..0): Cold Desert (arid) -> Arctic/Subarctic Tundra -> Boreal Snow-Forest Margin
 *   - Row 1 (Cool Temperate / Boreal-Montane, tmp 0..36): Cold Desert -> Shrubland -> Coniferous Forest -> Boreal Taiga
 *   - Row 2 (Warm Temperate / Subtropical Margin, tmp 36..72): Temperate Grassland -> Mediterranean -> Woodland/Seasonal -> Temperate Rainforest
 *   - Row 3 (Tropical / Equatorial, tmp 72..108): Subtropical Desert -> Savannah -> Tropical Seasonal Forest -> Tropical Rainforest
 * Note: BIOME_WOODLAND (11) vs BIOME_TEMPERATE_SEASONAL_FOREST (12) is resolved by sub-band moisture in [2][2],
 * while BIOME_ALPINE_MEADOW (3) and BIOME_WETLAND (5) are resolved by topographic elevation and floodplain hydrology. */
static inline uint8_t
nd_field_whittaker_cell(uint32_t rn_bit, int32_t tmp_bit, uint32_t rn)
{
	static const uint8_t s_grid[4][4] = {
		/* tmp_bit 0: Subpolar / Arctic (-36..0) */
		{ BIOME_COLD_DESERT, BIOME_TUNDRA, BIOME_TUNDRA, BIOME_BOREAL_FOREST },
		/* tmp_bit 1: Cool Temperate / Boreal (0..36) */
		{ BIOME_COLD_DESERT, BIOME_SHRUBLAND, BIOME_CONIFEROUS_FOREST, BIOME_BOREAL_FOREST },
		/* tmp_bit 2: Warm Temperate (36..72) */
		{ BIOME_TEMPERATE_GRASSLAND, BIOME_MEDITERRANEAN, BIOME_WOODLAND, BIOME_TEMPERATE_RAINFOREST },
		/* tmp_bit 3: Tropical / Equatorial (72..108) */
		{ BIOME_DESERT, BIOME_SAVANNAH, BIOME_TROPICAL_SEASONAL_FOREST, BIOME_TROPICAL_RAINFOREST }
	};
	if (tmp_bit < 0) tmp_bit = 0;
	if (tmp_bit > 3) tmp_bit = 3;
	if (rn_bit > 3) rn_bit = 3;
	uint8_t b = s_grid[tmp_bit][rn_bit];
	/* Split Warm Temperate mesic forest band (tmp_bit == 2, rn_bit == 2: rn 256..384)
	 * into Open Woodland (rn < 312) and Closed-Canopy Temperate Seasonal Forest (rn >= 312) */
	if (b == BIOME_WOODLAND && rn >= 312u)
		b = BIOME_TEMPERATE_SEASONAL_FOREST;
	return b;
}

static inline uint8_t
nd_field_bio_idx(uint32_t rn, int16_t tmp)
{
	if (tmp < ND_FIELD_TEMP_MIN)
		return BIOME_PERMANENT_ICE;
	else if (tmp >= ND_FIELD_TEMP_MAX)
		return BIOME_VOLCANIC;
	uint32_t rn_step = ND_FIELD_RAIN_MAX / 4;
	int32_t tmp_step = (ND_FIELD_TEMP_MAX - ND_FIELD_TEMP_MIN) / 4;
	uint32_t rn_bit = rn / (rn_step ? rn_step : 1u);
	if (rn_bit > 3) rn_bit = 3;
	int32_t tmp_bit = (tmp - ND_FIELD_TEMP_MIN) / (tmp_step ? tmp_step : 1);
	if (tmp_bit < 0) tmp_bit = 0;
	if (tmp_bit > 3) tmp_bit = 3;
	return nd_field_whittaker_cell(rn_bit, tmp_bit, rn);
}

/* Biome blending: calculates primary biome, secondary biome, and blend factor */
static inline void
nd_field_bio_blend(uint32_t rn, int16_t tmp, uint8_t *bio_out, uint8_t *bio2_out, uint8_t *blend_out)
{
	uint32_t rn_step = ND_FIELD_RAIN_MAX / 4;
	int32_t tmp_step = (ND_FIELD_TEMP_MAX - ND_FIELD_TEMP_MIN) / 4;
	if (rn_step == 0) rn_step = 1;
	if (tmp_step == 0) tmp_step = 1;

	/* 1. Permanent ice sheets and glaciers in polar freezing zones (tmp <= -36) and snowy summits */
	if (tmp <= ND_FIELD_TEMP_MIN) {
		uint8_t polar_adj = (rn < rn_step) ? BIOME_COLD_DESERT : BIOME_TUNDRA;
		int32_t d_ice = ND_FIELD_TEMP_MIN - tmp;
		if (d_ice < 16) {
			if (bio_out) *bio_out = BIOME_PERMANENT_ICE;
			if (bio2_out) *bio2_out = polar_adj;
			uint32_t t = (uint32_t)(16 - d_ice);
			if (blend_out) *blend_out = (uint8_t)((t * t * 128u) / 256u);
		} else {
			if (bio_out) *bio_out = BIOME_PERMANENT_ICE;
			if (bio2_out) *bio2_out = BIOME_PERMANENT_ICE;
			if (blend_out) *blend_out = 0;
		}
		return;
	}

	uint32_t rn_bit = (rn / rn_step);
	if (rn_bit > 3) rn_bit = 3;
	int32_t tmp_bit = ((tmp - ND_FIELD_TEMP_MIN) / tmp_step);
	if (tmp_bit > 3) tmp_bit = 3;
	if (tmp_bit < 0) tmp_bit = 0;

	uint8_t primary = nd_field_whittaker_cell(rn_bit, tmp_bit, rn);

	uint32_t rn_rem = rn - (rn_bit * rn_step);
	uint32_t rn_frac = (rn_rem * 256) / rn_step;

	int32_t tmp_rem = (tmp - ND_FIELD_TEMP_MIN) - (tmp_bit * tmp_step);
	uint32_t tmp_frac = (uint32_t)((tmp_rem * 256) / tmp_step);

	int32_t d_rn = (rn_frac < 128) ? (int32_t)rn_frac : (int32_t)(256 - rn_frac);
	int32_t d_tmp = (tmp_frac < 128) ? (int32_t)tmp_frac : (int32_t)(256 - tmp_frac);

	uint8_t sec_rn = primary;
	if (rn_frac < 128 && rn_bit > 0) {
		uint32_t adj_rn = (rn_bit - 1) * rn_step + (rn_step >> 1);
		sec_rn = nd_field_whittaker_cell(rn_bit - 1, tmp_bit, adj_rn);
	} else if (rn_frac >= 128 && rn_bit < 3) {
		uint32_t adj_rn = (rn_bit + 1) * rn_step + (rn_step >> 1);
		sec_rn = nd_field_whittaker_cell(rn_bit + 1, tmp_bit, adj_rn);
	}

	uint8_t sec_tmp = primary;
	if (tmp_frac < 128 && tmp_bit > 0)
		sec_tmp = nd_field_whittaker_cell(rn_bit, tmp_bit - 1, rn);
	else if (tmp_frac >= 128 && tmp_bit < 3)
		sec_tmp = nd_field_whittaker_cell(rn_bit, tmp_bit + 1, rn);

	uint8_t secondary = primary;
	int32_t min_d = 128;

	/* Smooth transitional blend to PERMANENT_ICE near cold threshold */
	if (tmp_bit == 0 && tmp_frac < 56) {
		secondary = BIOME_PERMANENT_ICE;
		min_d = (int32_t)tmp_frac;
	} else if (tmp_bit == 2 && rn_bit == 2 && abs((int32_t)rn - 312) < 28) {
		/* Smooth transition between Woodland (11) and Temperate Seasonal Forest (12) at rn = 312 */
		secondary = (primary == BIOME_WOODLAND) ? BIOME_TEMPERATE_SEASONAL_FOREST : BIOME_WOODLAND;
		min_d = (abs((int32_t)rn - 312) * 56) / 28;
	} else if (d_rn < 56 && d_tmp < 56 && sec_rn != primary && sec_tmp != primary) {
		/* 4-way Whittaker corner: smooth radial blend without diagonal crease */
		secondary = (d_rn <= d_tmp) ? sec_rn : sec_tmp;
		int32_t d_min = (d_rn < d_tmp) ? d_rn : d_tmp;
		int32_t d_max = (d_rn > d_tmp) ? d_rn : d_tmp;
		int32_t corner_pull = ((56 - d_max) * d_min) / 112;
		min_d = d_min - corner_pull;
		if (min_d < 0) min_d = 0;
	} else if (d_rn < d_tmp && sec_rn != primary) {
		min_d = d_rn;
		secondary = sec_rn;
	} else if (sec_tmp != primary) {
		min_d = d_tmp;
		secondary = sec_tmp;
	} else if (sec_rn != primary) {
		min_d = d_rn;
		secondary = sec_rn;
	}

	uint8_t blend = 0;
	if (min_d < 56 && secondary != primary) {
		uint32_t t = (uint32_t)(56 - min_d);
		/* Smooth C2 quintic-like ease reaching 128 (50% blend) at the boundary */
		blend = (uint8_t)((t * t * 128u) / (56u * 56u));
	}

	if (bio_out) *bio_out = primary;
	if (bio2_out) *bio2_out = secondary;
	if (blend_out) *blend_out = blend;
}

/* Topographic & Hydrological Biome Refinement:
 * Places Alpine Meadows (3) on high subalpine mountain benches above montane forests,
 * and Wetlands / Marshes / Fens (5) in low-lying river floodplains, estuaries, and humid basins. */
static inline void
nd_field_bio_blend_topo(uint32_t rn, int16_t tmp, int32_t elev_rel,
                        uint8_t river_carve_q4, uint16_t river_damp_q8,
                        uint8_t *bio_out, uint8_t *bio2_out, uint8_t *blend_out)
{
	uint8_t b1 = 0, b2 = 0, bl = 0;
	nd_field_bio_blend(rn, tmp, &b1, &b2, &bl);

	/* 1. Subalpine & Alpine Meadow Belt (BIOME_ALPINE_MEADOW = 3):
	 *    High mountain slopes and benches above ~38% of temperate snowline (~456 m = 20,520 wu at b=16)
	 *    where montane climate cools into timberline flower-rich meadows (-22 <= tmp <= +28) with
	 *    sufficient moisture (rn >= 115). Smooth transition across edge in [0..40]. */
	int32_t alp_min_wu = (int32_t)(nd_field_get_planet_scale().snowline_base_wu * 0.38f);
	if (elev_rel >= alp_min_wu && tmp >= -22 && tmp <= 28 && rn >= 115u && b1 != BIOME_PERMANENT_ICE) {
		int32_t d_elev = (elev_rel - alp_min_wu) / 45;
		int32_t d_cold = (tmp - (-22)) * 2;
		int32_t d_warm = (28 - tmp) * 2;
		int32_t edge = d_elev;
		if (d_cold < edge) edge = d_cold;
		if (d_warm < edge) edge = d_warm;
		b2 = b1;
		b1 = BIOME_ALPINE_MEADOW;
		if (edge < 40) {
			uint32_t t_q16 = (uint32_t)(((40 - edge) * 65536) / 40);
			uint32_t u_q16 = noise_wfade5(t_q16, 16);
			bl = (uint8_t)((u_q16 * 240u) >> 16);
		} else {
			bl = 0;
		}
	}
	/* 2. Alluvial Floodplain, Delta Estuary & Lowland Fen Wetland (BIOME_WETLAND = 5):
	 *    Low-lying terrain near the water table (0 <= elev_rel <= 22 wu) in non-freezing climates (tmp >= -6)
	 *    where either:
	 *      (a) an active river floodplain / delta valley (river_damp_q8 >= 48 or river_carve_q4 > 0) saturates the soil, or
	 *      (b) very high rainfall (rn >= 305) pools on low coastal/basin plains (elev_rel <= 15 wu). */
	else if (elev_rel >= 0 && elev_rel <= 22 && tmp >= -6 && tmp <= 88 && b1 != BIOME_PERMANENT_ICE) {
		int is_floodplain = (river_carve_q4 > 0 || river_damp_q8 >= 48u) && (rn >= 135u);
		int is_fen_basin  = (elev_rel <= 15) && (rn >= 305u);
		if (is_floodplain || is_fen_basin) {
			int32_t max_h = is_floodplain ? 22 : 15;
			int32_t d_top = max_h - elev_rel;
			int32_t edge_w_q16 = (d_top >= 8) ? 65536 : ((d_top * 65536) / 8);
			if (is_floodplain && !is_fen_basin && river_carve_q4 == 0) {
				int32_t d_damp = (int32_t)river_damp_q8 - 48;
				int32_t damp_w_q16 = (d_damp >= 160) ? 65536 : ((d_damp * 65536) / 160);
				if (damp_w_q16 < edge_w_q16)
					edge_w_q16 = damp_w_q16;
			}
			b2 = b1;
			b1 = BIOME_WETLAND;
			if (edge_w_q16 < 65536) {
				uint32_t t_q16 = (uint32_t)(65536 - edge_w_q16);
				uint32_t u_q16 = noise_wfade5(t_q16, 16);
				bl = (uint8_t)((u_q16 * 252u) >> 16);
				if (bl < 2u) bl = 2u;
			} else {
				bl = 0;
			}
		}
	}

	if (bio_out) *bio_out = b1;
	if (bio2_out) *bio2_out = b2;
	if (blend_out) *blend_out = bl;
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
static inline int64_t
nd_field_fdiv64(int64_t a, int64_t b)
{
	int64_t d = a / b;
	int64_t r = a - d * b;
	if (r < 0) d--;
	return d;
}

static inline void
nd_field_world_to_room_q16(int32_t wx, int32_t wy, int32_t *rx_q16, int32_t *ry_q16)
{
	int32_t rw = nd_field_get_room_w();
	int32_t rh = nd_field_get_room_h();
	int64_t ox = (int64_t)(wx - (rw / 2)) * 65536LL;
	int64_t oy = (int64_t)((rh / 2) - wy) * 65536LL;
	if (rx_q16) *rx_q16 = (int32_t)nd_field_fdiv64(ox, rw);
	if (ry_q16) *ry_q16 = (int32_t)nd_field_fdiv64(oy, rh);
}

/* Continuous C2 quintic interpolation of coarse server octaves at fractional room coordinates */
static inline uint32_t
nd_field_eval_coarse_oct_q16(int32_t ry_q16, int32_t rx_q16, unsigned x, unsigned w, unsigned seed)
{
	int16_t cell_y = (int16_t)(ry_q16 >> 16);
	uint16_t frac_y = (uint16_t)(ry_q16 & 0xffff);
	int16_t cell_x = (int16_t)(rx_q16 >> 16);
	uint16_t frac_x = (uint16_t)(rx_q16 & 0xffff);

	uint16_t mask = COORDMASK_LSHIFT(x);
	int16_t s[2] = { (int16_t)(cell_y & mask), (int16_t)(cell_x & mask) };
	uint32_t fiy = (uint16_t)cell_y & (uint16_t)~mask;
	uint32_t fix = (uint16_t)cell_x & (uint16_t)~mask;

	uint32_t uy = ((uint32_t)fiy << 16) | (uint32_t)frac_y;
	uint32_t ux = ((uint32_t)fix << 16) | (uint32_t)frac_x;

	uint32_t fade_y = noise_wfade5(uy, x + 16);
	uint32_t fade_x = noise_wfade5(ux, x + 16);

	typedef struct {
		int16_t s0, s1;
		uint16_t x, w;
		uint32_t seed;
		uint32_t v[4];
		uint8_t valid;
	} nd_coarse_oct_slot_t;
	static _CHAFF_TLS nd_coarse_oct_slot_t s_slots[64];
	uint32_t slot_idx = ((uint32_t)(uint16_t)s[0] * 0x9E3779B1u ^
	                     (uint32_t)(uint16_t)s[1] * 0x85EBCA77u ^
	                     ((uint32_t)x << 8) ^ seed) & 63u;
	nd_coarse_oct_slot_t *slot = &s_slots[slot_idx];
	if (!slot->valid || slot->s0 != s[0] || slot->s1 != s[1] ||
	    slot->x != (uint16_t)x || slot->w != (uint16_t)w || slot->seed != seed) {
		noise_get_v(slot->v, s, x, w, seed, 2);
		slot->s0 = s[0];
		slot->s1 = s[1];
		slot->x = (uint16_t)x;
		slot->w = (uint16_t)w;
		slot->seed = seed;
		slot->valid = 1;
	}

	uint32_t b0 = noise_wstep(slot->v[0], slot->v[2], fade_y, x + 16);
	uint32_t b1 = noise_wstep(slot->v[1], slot->v[3], fade_y, x + 16);
	return noise_wstep(b0, b1, fade_x, x + 16);
}

/* Map discrete legacy LOD (0..3+) to continuous sampling step in Q16.16 world units:
 *   lod = 0 -> 256.0 wu (room-level coarse only, skips all fine octaves)
 *   lod = 1 ->   8.0 wu (retains shifts s >= 5, i.e. wavelength >= 32 wu)
 *   lod = 2 ->   4.0 wu (retains shifts s >= 4, i.e. wavelength >= 16 wu)
 *   lod = 3 ->   0.0 wu (point sample: retains all shifts s >= 0) */
static inline int32_t
nd_field_lod_to_step_q16(uint8_t lod)
{
	if (lod == 0) return 256 << 16;
	if (lod == 1) return 8 << 16;
	if (lod == 2) return 4 << 16;
	if (lod == 3) return 0;
	uint8_t sh = (uint8_t)(lod - 3);
	if (sh > 10) sh = 10;
	return (int32_t)(1u << (sh + 15u));
}

typedef struct {
	int32_t elevation;        /* Total elevation H = coarse + fine (wu) */
	int32_t elevation_q12_4;  /* Sub-pixel elevation in Q27.4 fixed-point (16 sub-units/wu) */
	int32_t coarse_elev;      /* Coarse elevation (wu) */
	int32_t fine_elev;        /* Fine detail elevation (wu) */
	int32_t water_level;      /* W (wu) */
	int32_t depth;            /* max(0, W - H) (wu) */
	uint8_t is_water;         /* H < W */
	int16_t temp;             /* Live temperature (climate_temp + diurnal day/night delta) */
	int16_t climate_temp;     /* Mean annual latitudinal & alpine climate temperature */
	uint32_t rain;            /* Rain */
	uint8_t bio;              /* Primary biome */
	uint8_t bio2;             /* Secondary biome */
	uint8_t blend;            /* Biome blend factor (0..128) */
	uint8_t river_carve_q4;   /* Upland river channel incision C(wx,wy) in Q4 (0..254) */
} nd_field_sample_t;

/* Whole-block physics compatibility: converts Q27.4 fixed-point elevation to integer block coordinates */
static inline int16_t
nd_field_elevation_to_block(int32_t elev_q12_4)
{
	int32_t blk = elev_q12_4 >> 4;
	if (blk > 32767) blk = 32767;
	if (blk < -32768) blk = -32768;
	return (int16_t)blk;
}

/* Full-range integer world-unit elevation from Q27.4 fixed-point */
static inline int32_t
nd_field_elevation_to_wu(int32_t elev_q12_4)
{
	return elev_q12_4 >> 4;
}

/* ============================================================================
 * NeverDark Planetary 16-Octave Torus Elevation Policy (built on libchaff)
 * ============================================================================ */

#define ND_FIELD_SHIFT_PLANET_MAX     15  /* 32,768 rooms: planetary continent dipole */
#define ND_FIELD_SHIFT_CONTINENT_MIN   9  /* 512 rooms: continental margins & gulfs */
#define ND_FIELD_SHIFT_REGIONAL_MIN    2  /* 4 rooms: local hills, valleys & lakes */
#define ND_FIELD_SHIFT_FINE_MAX        9  /* 512 world units: multi-room mountain formations */
#define ND_FIELD_SHIFT_FINE_MIN        0  /* 1 world unit: micro-roughness */
#define ND_FIELD_ORIGIN_SEA_OFFSET_Q12_4 360

static inline int32_t
nd_field_eval_coarse_unwrapped_step_q12_4(uint32_t seed, int32_t room_step_q16, int32_t rx, int32_t ry)
{
	static _CHAFF_TLS noise_wcache_t s_coarse_wc[9];
	uint8_t pbits = nd_field_get_planet_bits();
	int32_t cur_rx = rx, cur_ry = ry, coarse_q12_4 = 0;

	for (uint8_t s = ND_FIELD_SHIFT_PLANET_MAX; s >= ND_FIELD_SHIFT_CONTINENT_MIN; s--) {
		int16_t amp = chaff_octave_amplitude(s);
		uint32_t oct_seed = seed ^ (uint32_t)(s * 0x85EBCA77u + 1u);
		uint32_t raw = chaff_wpoint_periodic_2d(cur_rx, cur_ry, s, pbits, 8, oct_seed);
		int32_t val = (int32_t)(raw >> 16) - 128;
		coarse_q12_4 += (int32_t)((((int64_t)val * amp) << 4) / 128);
		chaff_rot2d_i32_q15(&cur_rx, &cur_ry, CHAFF_ROT_ATAN_COS_Q15, CHAFF_ROT_ATAN_SIN_Q15);
	}

	for (uint8_t s = 8; s >= ND_FIELD_SHIFT_REGIONAL_MIN; s--) {
		uint32_t w_oct = (s >= 5) ? 65536u : chaff_octave_weight_q16(s, room_step_q16);
		if (w_oct == 0u)
			break;
		int16_t amp = chaff_octave_amplitude(s);
		uint32_t oct_seed = seed ^ (uint32_t)(s * 0x85EBCA77u + 1u);
		int16_t p[2] = { (int16_t)cur_ry, (int16_t)cur_rx };
		uint32_t raw = noise_wpoint_c(&s_coarse_wc[s], p, s, 8, oct_seed, 2, NOISE_FADE5);
		int32_t val = (int32_t)(raw >> 16) - 128;
		int32_t contrib = (int32_t)((((int64_t)val * amp) << 4) / 128);
		if (w_oct < 65536u)
			contrib = (int32_t)(((int64_t)contrib * (int64_t)w_oct) >> 16);
		coarse_q12_4 += contrib;
		chaff_rot2d_i32_q15(&cur_rx, &cur_ry, CHAFF_ROT_ATAN_COS_Q15, CHAFF_ROT_ATAN_SIN_Q15);
	}

	return coarse_q12_4;
}

static inline int32_t
nd_field_eval_coarse_unwrapped_q12_4(uint32_t seed, int32_t rx, int32_t ry)
{
	return nd_field_eval_coarse_unwrapped_step_q12_4(seed, 0, rx, ry);
}

static inline int32_t
nd_field_sea_offset_q12_4(uint16_t world, uint32_t seed)
{
	static _CHAFF_TLS uint16_t s_cached_world = 0xFFFFu;
	static _CHAFF_TLS uint32_t s_cached_epoch = 0u;
	static _CHAFF_TLS int32_t s_cached_origin_bias = 0;
	uint32_t epoch = nd_field_get_planet_epoch();
	if (world != s_cached_world || epoch != s_cached_epoch) {
		s_cached_origin_bias = nd_field_eval_coarse_unwrapped_q12_4(seed, 0, 0) >> 2;
		s_cached_world = world;
		s_cached_epoch = epoch;
	}
	uint32_t w = (((world >> 2) + 2) & 3) * 65536u;
	return (int32_t)((((int64_t)w * 200 / 262144 - 100)) << 4) +
	       ND_FIELD_ORIGIN_SEA_OFFSET_Q12_4 + s_cached_origin_bias;
}

/* 4-wide SIMD coarse unwrapped elevation evaluator.
 * Bit-exact with 4 calls to nd_field_eval_coarse_unwrapped_step_q12_4. */
static inline void
nd_field_eval_coarse_unwrapped_step_q12_4_4x(uint32_t seed, int32_t room_step_q16,
                                             const int32_t rx[4], const int32_t ry[4],
                                             int32_t out[4])
{
	static _CHAFF_TLS noise_wcache_t s_coarse4_wc[9];
	uint8_t pbits = nd_field_get_planet_bits();
	int32_t cur_rx[4] = { rx[0], rx[1], rx[2], rx[3] };
	int32_t cur_ry[4] = { ry[0], ry[1], ry[2], ry[3] };
	int32_t acc[4] = { 0, 0, 0, 0 };

	for (uint8_t s = ND_FIELD_SHIFT_PLANET_MAX; s >= ND_FIELD_SHIFT_CONTINENT_MIN; s--) {
		int16_t amp = chaff_octave_amplitude(s);
		uint32_t oct_seed = seed ^ (uint32_t)(s * 0x85EBCA77u + 1u);
		uint32_t raw[4];
		chaff_wpoint_periodic_2d_4x(cur_rx, cur_ry, s, pbits, 8, oct_seed, raw);
		for (int i = 0; i < 4; i++) {
			int32_t val = (int32_t)(raw[i] >> 16) - 128;
			int64_t num = ((int64_t)val * amp) << 4;
			acc[i] += (int32_t)((num + ((num >> 63) & 127)) >> 7);
		}
		chaff_rot2d_i32_q15_4x(cur_rx, cur_ry, CHAFF_ROT_ATAN_COS_Q15, CHAFF_ROT_ATAN_SIN_Q15);
	}

	for (uint8_t s = 8; s >= ND_FIELD_SHIFT_REGIONAL_MIN; s--) {
		uint32_t w_oct = (s >= 5) ? 65536u : chaff_octave_weight_q16(s, room_step_q16);
		if (w_oct == 0u)
			break;
		int16_t amp = chaff_octave_amplitude(s);
		uint32_t oct_seed = seed ^ (uint32_t)(s * 0x85EBCA77u + 1u);
		int16_t py[4] = { (int16_t)cur_ry[0], (int16_t)cur_ry[1], (int16_t)cur_ry[2], (int16_t)cur_ry[3] };
		int16_t px[4] = { (int16_t)cur_rx[0], (int16_t)cur_rx[1], (int16_t)cur_rx[2], (int16_t)cur_rx[3] };
		uint32_t raw[4];
		noise_wpoint_2d_4x(&s_coarse4_wc[s], py, px, s, 8, oct_seed, NOISE_FADE5, raw);
		for (int i = 0; i < 4; i++) {
			int32_t val = (int32_t)(raw[i] >> 16) - 128;
			int64_t num = ((int64_t)val * amp) << 4;
			int32_t contrib = (int32_t)((num + ((num >> 63) & 127)) >> 7);
			if (w_oct < 65536u)
				contrib = (int32_t)(((int64_t)contrib * (int64_t)w_oct) >> 16);
			acc[i] += contrib;
		}
		if (s > ND_FIELD_SHIFT_REGIONAL_MIN)
			chaff_rot2d_i32_q15_4x(cur_rx, cur_ry, CHAFF_ROT_ATAN_COS_Q15, CHAFF_ROT_ATAN_SIN_Q15);
	}

	out[0] = acc[0]; out[1] = acc[1]; out[2] = acc[2]; out[3] = acc[3];
}

static inline int32_t
nd_field_eval_coarse_torus_x_step_q12_4(uint32_t seed, int32_t room_step_q16, int32_t rx, int32_t ry_unwrapped)
{
	uint8_t pbits = nd_field_get_planet_bits();
	uint32_t span = 1u << pbits;
	int32_t seam_start = (int32_t)(span >> 1) - 256;
	uint32_t ux = (uint32_t)(rx - seam_start) & (span - 1u);
	if (ux < 512u) {
		int32_t rx_hi = seam_start + (int32_t)ux;
		int32_t rx_lo = rx_hi - (int32_t)span;
		int32_t c_hi = nd_field_eval_coarse_unwrapped_step_q12_4(seed, room_step_q16, rx_hi, ry_unwrapped);
		int32_t c_lo = nd_field_eval_coarse_unwrapped_step_q12_4(seed, room_step_q16, rx_lo, ry_unwrapped);
		return c_hi + (int32_t)(((int64_t)(c_lo - c_hi) * (int32_t)ux) >> 9);
	}
	return nd_field_eval_coarse_unwrapped_step_q12_4(seed, room_step_q16, nd_field_wrap_room_coord(rx, pbits), ry_unwrapped);
}

static inline int32_t
nd_field_eval_coarse_torus_x_q12_4(uint32_t seed, int32_t rx, int32_t ry_unwrapped)
{
	return nd_field_eval_coarse_torus_x_step_q12_4(seed, 0, rx, ry_unwrapped);
}

/* Continuous regional ruggedness envelope R(wx, wy) in Q8 fixed-point [0..256].
 * Combines a macro tectonic province octave (shift 10 = 1,024 world units, ~6.4x8 rooms)
 * and a rotated meso ridge-belt octave (shift 8 = 256 world units, ~1.6x2 rooms),
 * followed by a smooth threshold ramp so ~45% of land forms open clear plains (R = 0),
 * ~30% forms rolling hills, and ~25% forms craggy rugged ridges and highlands (R -> 256). */
static inline uint16_t
nd_field_ruggedness_q8(uint16_t world, int32_t wx, int32_t wy)
{
	static _CHAFF_TLS noise_wcache_t s_rug_c10, s_rug_c8;
	uint32_t rug_seed = 0x52554747u ^ ((uint32_t)world * 0x9E3779B1u);
	int16_t p0[2] = { (int16_t)wy, (int16_t)wx };
	uint32_t raw_macro = noise_wpoint_c(&s_rug_c10, p0, 10, 8, rug_seed ^ 0xA531u, 2, NOISE_FADE5) >> 16;

	int32_t rx = wx;
	int32_t ry = wy;
	chaff_rot2d_i32_q15(&rx, &ry, CHAFF_ROT_ATAN_COS_Q15, CHAFF_ROT_ATAN_SIN_Q15);
	int16_t p1[2] = { (int16_t)ry, (int16_t)rx };
	uint32_t raw_meso = noise_wpoint_c(&s_rug_c8, p1, 8, 8, rug_seed ^ 0x5CA9u, 2, NOISE_FADE5) >> 16;

	int32_t raw = (int32_t)((raw_macro * 5u + raw_meso * 3u) >> 3);
	if (raw <= 120)
		return 0;
	if (raw >= 168)
		return 256;
	int32_t t = ((raw - 120) * 256) / 48;
	int32_t smooth = (t * t * (3 * 256 - 2 * t)) >> 16;
	if (smooth < 0) smooth = 0;
	if (smooth > 256) smooth = 256;
	return (uint16_t)smooth;
}

/* 4-wide SIMD regional ruggedness envelope matching nd_field_ruggedness_q8 bit-for-bit. */
static inline void
nd_field_ruggedness_q8_4x(uint16_t world, const int32_t wx[4], const int32_t wy[4], uint16_t out[4])
{
	static _CHAFF_TLS noise_wcache_t s_rug4_c10, s_rug4_c8;
	uint32_t rug_seed = 0x52554747u ^ ((uint32_t)world * 0x9E3779B1u);
	int16_t py0[4] = { (int16_t)wy[0], (int16_t)wy[1], (int16_t)wy[2], (int16_t)wy[3] };
	int16_t px0[4] = { (int16_t)wx[0], (int16_t)wx[1], (int16_t)wx[2], (int16_t)wx[3] };
	uint32_t raw_macro[4];
	noise_wpoint_2d_4x(&s_rug4_c10, py0, px0, 10, 8, rug_seed ^ 0xA531u, NOISE_FADE5, raw_macro);

	int32_t rx[4] = { wx[0], wx[1], wx[2], wx[3] };
	int32_t ry[4] = { wy[0], wy[1], wy[2], wy[3] };
	chaff_rot2d_i32_q15_4x(rx, ry, CHAFF_ROT_ATAN_COS_Q15, CHAFF_ROT_ATAN_SIN_Q15);
	int16_t py1[4] = { (int16_t)ry[0], (int16_t)ry[1], (int16_t)ry[2], (int16_t)ry[3] };
	int16_t px1[4] = { (int16_t)rx[0], (int16_t)rx[1], (int16_t)rx[2], (int16_t)rx[3] };
	uint32_t raw_meso[4];
	noise_wpoint_2d_4x(&s_rug4_c8, py1, px1, 8, 8, rug_seed ^ 0x5CA9u, NOISE_FADE5, raw_meso);

	for (int i = 0; i < 4; i++) {
		int32_t raw = (int32_t)((((raw_macro[i] >> 16) * 5u) + ((raw_meso[i] >> 16) * 3u)) >> 3);
		if (raw <= 120) {
			out[i] = 0;
		} else if (raw >= 168) {
			out[i] = 256;
		} else {
			int32_t t = ((raw - 120) * 256) / 48;
			int32_t smooth = (t * t * (3 * 256 - 2 * t)) >> 16;
			if (smooth < 0) smooth = 0;
			if (smooth > 256) smooth = 256;
			out[i] = (uint16_t)smooth;
		}
	}
}

static inline int32_t
nd_field_elevation_step_q12_4(uint16_t world, int32_t step_q16, int32_t wx, int32_t wy,
                              int32_t *out_coarse_q12_4, int32_t *out_fine_q12_4)
{
	uint32_t seed = 0x48454947u ^ ((uint32_t)world * 0x9E3779B1u);
	uint8_t pbits = nd_field_get_planet_bits();
	uint32_t span = 1u << pbits;
	int32_t seam_start = (int32_t)(span >> 1) - 256;

	int32_t rx = nd_field_wrap_room_coord(noise_fdiv(wx - (ND_FIELD_ROOM_W / 2), ND_FIELD_ROOM_W), pbits);
	int32_t ry = nd_field_wrap_room_coord(noise_fdiv((ND_FIELD_ROOM_H / 2) - wy, ND_FIELD_ROOM_H), pbits);

	int32_t coarse_q12_4 = 0;
	int32_t fine_q12_4 = 0;

	if (out_coarse_q12_4 != NULL || out_fine_q12_4 == NULL) {
		int32_t room_step_q16 = (step_q16 >= (2048 << 16)) ? (step_q16 / ND_FIELD_ROOM_W) : 0;
		uint32_t uy = (uint32_t)(ry - seam_start) & (span - 1u);
		if (uy < 512u) {
			int32_t ry_hi = seam_start + (int32_t)uy;
			int32_t ry_lo = ry_hi - (int32_t)span;
			int32_t c_hi = nd_field_eval_coarse_torus_x_step_q12_4(seed, room_step_q16, rx, ry_hi);
			int32_t c_lo = nd_field_eval_coarse_torus_x_step_q12_4(seed, room_step_q16, rx, ry_lo);
			coarse_q12_4 = c_hi + (int32_t)(((int64_t)(c_lo - c_hi) * (int32_t)uy) >> 9);
		} else {
			coarse_q12_4 = nd_field_eval_coarse_torus_x_step_q12_4(seed, room_step_q16, rx, ry);
		}
		coarse_q12_4 -= nd_field_sea_offset_q12_4(world, seed);
	}

	if (chaff_octave_weight_q16(ND_FIELD_SHIFT_FINE_MAX, step_q16) > 0u) {
		int32_t base_fine_q12_4 = chaff_fbm_rotated_2d_step_q12_4(
			wx, wy, step_q16,
			ND_FIELD_SHIFT_FINE_MIN, ND_FIELD_SHIFT_FINE_MAX,
			7, ND_FIELD_FINE_MIN_SHIFT, 1,
			seed, 0xBEEFu,
			CHAFF_ROT_ATAN_COS_Q15, CHAFF_ROT_ATAN_SIN_Q15);

		uint16_t rug_q8 = nd_field_ruggedness_q8(world, wx, wy);
		int32_t lowland_roll_q12_4 = 0;
		if (rug_q8 < 256u && chaff_octave_weight_q16(6, step_q16) > 0u) {
			/* Gentle canonical lowland rolling micro-topography (32..64 wu swales & rises, smooth quintic C2,
			 * zero ridge folding) tapered out smoothly as ruggedness rises so plains never look tabletop-flat
			 * while preserving gentle clear-plain relief (<= 8 wu avg room span) and >= 3.5x rugged contrast. */
			int32_t roll_raw_q12_4 = chaff_fbm_rotated_2d_step_q12_4(
				wx, wy, step_q16,
				5, 6,
				255, 5, 1,
				seed ^ 0x524F4C4Cu, 0xB10Du,
				CHAFF_ROT_ATAN_COS_Q15, CHAFF_ROT_ATAN_SIN_Q15);
			int32_t low_w_q8 = 256 - (int32_t)rug_q8;
			lowland_roll_q12_4 = (int32_t)(((int64_t)roll_raw_q12_4 * low_w_q8) >> 8);
		}
		if (rug_q8 > 0) {
			int32_t meso_ridge_q12_4 = chaff_fbm_rotated_2d_step_q12_4(
				wx, wy, step_q16,
				5, ND_FIELD_SHIFT_FINE_MAX,
				6, 5, 10,
				seed ^ 0x7F4A7C15u, 0xCAFEu,
				CHAFF_ROT_ATAN_COS_Q15, CHAFF_ROT_ATAN_SIN_Q15);
			int32_t base_scale_q8 = 128 + (((int32_t)rug_q8 * 11) >> 3);
			int32_t scaled_base = (int32_t)(((int64_t)base_fine_q12_4 * base_scale_q8) >> 8);
			fine_q12_4 = scaled_base + lowland_roll_q12_4 +
			             (int32_t)(((int64_t)meso_ridge_q12_4 * (int32_t)rug_q8) >> 8);
		} else {
			fine_q12_4 = (base_fine_q12_4 >> 1) + lowland_roll_q12_4;
		}
	}

	if (out_coarse_q12_4) *out_coarse_q12_4 = coarse_q12_4;
	if (out_fine_q12_4) *out_fine_q12_4 = fine_q12_4;
	return coarse_q12_4 + fine_q12_4;
}

static inline int32_t
nd_field_elevation_fine_step_q12_4(uint16_t world, int32_t step_q16, int32_t wx, int32_t wy,
                                   int32_t *inout_rug_q8)
{
	if (chaff_octave_weight_q16(ND_FIELD_SHIFT_FINE_MAX, step_q16) == 0u)
		return 0;

	uint32_t seed = 0x48454947u ^ ((uint32_t)world * 0x9E3779B1u);
	int32_t base_fine_q12_4 = chaff_fbm_rotated_2d_step_q12_4(
		wx, wy, step_q16,
		ND_FIELD_SHIFT_FINE_MIN, ND_FIELD_SHIFT_FINE_MAX,
		7, ND_FIELD_FINE_MIN_SHIFT, 1,
		seed, 0xBEEFu,
		CHAFF_ROT_ATAN_COS_Q15, CHAFF_ROT_ATAN_SIN_Q15);

	uint16_t rug_q8;
	if (inout_rug_q8 && *inout_rug_q8 >= 0) {
		rug_q8 = (uint16_t)*inout_rug_q8;
	} else {
		rug_q8 = nd_field_ruggedness_q8(world, wx, wy);
		if (inout_rug_q8) *inout_rug_q8 = (int32_t)rug_q8;
	}

	int32_t lowland_roll_q12_4 = 0;
	if (rug_q8 < 256u && chaff_octave_weight_q16(6, step_q16) > 0u) {
		int32_t roll_raw_q12_4 = chaff_fbm_rotated_2d_step_q12_4(
			wx, wy, step_q16,
			5, 6,
			255, 5, 1,
			seed ^ 0x524F4C4Cu, 0xB10Du,
			CHAFF_ROT_ATAN_COS_Q15, CHAFF_ROT_ATAN_SIN_Q15);
		int32_t low_w_q8 = 256 - (int32_t)rug_q8;
		lowland_roll_q12_4 = (int32_t)(((int64_t)roll_raw_q12_4 * low_w_q8) >> 8);
	}
	if (rug_q8 > 0) {
		int32_t meso_ridge_q12_4 = chaff_fbm_rotated_2d_step_q12_4(
			wx, wy, step_q16,
			5, ND_FIELD_SHIFT_FINE_MAX,
			6, 5, 10,
			seed ^ 0x7F4A7C15u, 0xCAFEu,
			CHAFF_ROT_ATAN_COS_Q15, CHAFF_ROT_ATAN_SIN_Q15);
		int32_t base_scale_q8 = 128 + (((int32_t)rug_q8 * 11) >> 3);
		int32_t scaled_base = (int32_t)(((int64_t)base_fine_q12_4 * base_scale_q8) >> 8);
		return scaled_base + lowland_roll_q12_4 +
		       (int32_t)(((int64_t)meso_ridge_q12_4 * (int32_t)rug_q8) >> 8);
	}
	return (base_fine_q12_4 >> 1) + lowland_roll_q12_4;
}

/* 4-wide SIMD fine elevation evaluator matching nd_field_elevation_fine_step_q12_4 bit-for-bit. */
static inline void
nd_field_elevation_fine_step_q12_4_4x(uint16_t world, int32_t step_q16,
                                      const int32_t wx[4], const int32_t wy[4],
                                      int32_t inout_rug_q8[4], int32_t out_fine[4])
{
	if (chaff_octave_weight_q16(ND_FIELD_SHIFT_FINE_MAX, step_q16) == 0u) {
		out_fine[0] = out_fine[1] = out_fine[2] = out_fine[3] = 0;
		return;
	}

	uint32_t seed = 0x48454947u ^ ((uint32_t)world * 0x9E3779B1u);
	int32_t base_fine[4];
	chaff_fbm_rotated_2d_step_q12_4_4x(
		wx, wy, step_q16,
		ND_FIELD_SHIFT_FINE_MIN, ND_FIELD_SHIFT_FINE_MAX,
		7, ND_FIELD_FINE_MIN_SHIFT, 1,
		seed, 0xBEEFu,
		CHAFF_ROT_ATAN_COS_Q15, CHAFF_ROT_ATAN_SIN_Q15,
		base_fine);

	uint16_t rug[4];
	if (!inout_rug_q8 ||
	    (inout_rug_q8[0] < 0 && inout_rug_q8[1] < 0 &&
	     inout_rug_q8[2] < 0 && inout_rug_q8[3] < 0)) {
		nd_field_ruggedness_q8_4x(world, wx, wy, rug);
		if (inout_rug_q8) {
			for (int i = 0; i < 4; i++)
				inout_rug_q8[i] = (int32_t)rug[i];
		}
	} else {
		int miss_idx[4], nmiss = 0;
		for (int i = 0; i < 4; i++) {
			if (inout_rug_q8[i] >= 0) {
				rug[i] = (uint16_t)inout_rug_q8[i];
			} else {
				miss_idx[nmiss++] = i;
			}
		}
		if (nmiss == 1) {
			int k = miss_idx[0];
			rug[k] = nd_field_ruggedness_q8(world, wx[k], wy[k]);
			inout_rug_q8[k] = (int32_t)rug[k];
		} else if (nmiss > 1) {
			int32_t mwx[4], mwy[4];
			uint16_t mrug[4];
			for (int j = 0; j < 4; j++) {
				int k = miss_idx[j < nmiss ? j : 0];
				mwx[j] = wx[k];
				mwy[j] = wy[k];
			}
			nd_field_ruggedness_q8_4x(world, mwx, mwy, mrug);
			for (int j = 0; j < nmiss; j++) {
				int k = miss_idx[j];
				rug[k] = mrug[j];
				inout_rug_q8[k] = (int32_t)mrug[j];
			}
		}
	}

	int32_t roll_raw[4] = { 0, 0, 0, 0 };
	if ((rug[0] < 256u || rug[1] < 256u || rug[2] < 256u || rug[3] < 256u) &&
	    chaff_octave_weight_q16(6, step_q16) > 0u) {
		chaff_fbm_rotated_2d_step_q12_4_4x(
			wx, wy, step_q16,
			5, 6,
			255, 5, 1,
			seed ^ 0x524F4C4Cu, 0xB10Du,
			CHAFF_ROT_ATAN_COS_Q15, CHAFF_ROT_ATAN_SIN_Q15,
			roll_raw);
	}

	int32_t meso_ridge[4] = { 0, 0, 0, 0 };
	if (rug[0] > 0u || rug[1] > 0u || rug[2] > 0u || rug[3] > 0u) {
		chaff_fbm_rotated_2d_step_q12_4_4x(
			wx, wy, step_q16,
			5, ND_FIELD_SHIFT_FINE_MAX,
			6, 5, 10,
			seed ^ 0x7F4A7C15u, 0xCAFEu,
			CHAFF_ROT_ATAN_COS_Q15, CHAFF_ROT_ATAN_SIN_Q15,
			meso_ridge);
	}

	for (int i = 0; i < 4; i++) {
		uint16_t rq = rug[i];
		int32_t low_roll = (rq < 256u)
			? (int32_t)(((int64_t)roll_raw[i] * (256 - (int32_t)rq)) >> 8)
			: 0;
		if (rq > 0u) {
			int32_t base_scale_q8 = 128 + (((int32_t)rq * 11) >> 3);
			int32_t scaled_base = (int32_t)(((int64_t)base_fine[i] * base_scale_q8) >> 8);
			out_fine[i] = scaled_base + low_roll +
			              (int32_t)(((int64_t)meso_ridge[i] * (int32_t)rq) >> 8);
		} else {
			out_fine[i] = (base_fine[i] >> 1) + low_roll;
		}
	}
}

static inline int32_t
nd_field_elevation_q12_4(uint16_t world, uint8_t lod, int32_t wx, int32_t wy,
                         int32_t *out_coarse_q12_4, int32_t *out_fine_q12_4)
{
	return nd_field_elevation_step_q12_4(world, nd_field_lod_to_step_q16(lod),
	                                     wx, wy, out_coarse_q12_4, out_fine_q12_4);
}

static inline int32_t
chaff_elevation_q12_4(uint16_t world, uint8_t lod, int32_t wx, int32_t wy,
                      int32_t *out_coarse_q12_4, int32_t *out_fine_q12_4)
{
	return nd_field_elevation_q12_4(world, lod, wx, wy, out_coarse_q12_4, out_fine_q12_4);
}

/* ============================================================================
 * Phase 2: Geological Province Synthesis & Metric Hypsometry (10 Archetypes)
 *
 * Pure-integer, O(1), torus-periodic (planet_bits = 16..23) geological province
 * evaluator and 10-landform metric orogeny (45 wu/m: up to 1,800 m = 81,000 wu
 * at b=16, scaling to 8,850 m = 398,250 wu at b>=22 via unlocked macro octaves).
 * Uses C2-quintic 2x2 warped province cells for zero triple-junction seams,
 * bit-exact scalar/SIMD parity, and razor-tight 2-sided analytical bounds.
 * ============================================================================ */
#define ND_FIELD_OROGENY_SALT 0xD0D2877Du
#define ND_FIELD_GEO_CELL_SHIFT 9u
#define ND_FIELD_GEO_CELL_SPAN  512

typedef struct {
	uint8_t base_uplift;
	uint8_t strata_hardness;
	uint8_t basin_flatness;
	uint8_t erosion_age;
} nd_geo_archetype_props_t;

static const nd_geo_archetype_props_t ND_GEO_ARCHETYPE_PROPS[ND_GEO_PROVINCE_COUNT] = {
	/* 0: ALLUVIAL_PLAIN    */ {  12,  20, 245, 235 },
	/* 1: PIEDMONT_HILLS    */ {  72,  48, 110, 210 },
	/* 2: ALPINE_CORDILLERA */ { 250, 180,  12,  24 },
	/* 3: CANYON_MESA       */ { 155, 248, 165, 130 },
	/* 4: FAULT_ESCARPMENT  */ { 225, 210,  45,  52 },
	/* 5: GLACIAL_FJORD     */ { 215, 165, 140,  78 },
	/* 6: VOLCANIC_CONE     */ { 235, 150,  35,  18 },
	/* 7: KARST_TOWERS      */ { 145, 200, 155, 115 },
	/* 8: ARID_BASIN_ERG    */ {  28,  60, 230, 190 },
	/* 9: SEA_CLIFF_COAST   */ { 118, 230,  95,  90 }
};

static inline int32_t
nd_field_smoothstep_q8(int32_t x, int32_t e0, int32_t e1)
{
	if (x <= e0) return 0;
	if (x >= e1) return 256;
	int32_t t = ((x - e0) * 256) / (e1 - e0);
	int32_t s = (t * t * (3 * 256 - 2 * t)) >> 16;
	if (s < 0) s = 0;
	if (s > 256) s = 256;
	return s;
}

/* Exact strictly-monotonic C2 quintic 6t^5 - 15t^4 + 10t^3 over f in [0..512] -> [0..256] */
static inline int32_t
nd_field_quintic512_q8(int32_t f)
{
	if (f <= 0) return 0;
	if (f >= 512) return 256;
	int64_t t = (int64_t)f << 3; /* Q12 in [0..4096] */
	int64_t t2 = (t * t) >> 12;
	int64_t t3 = (t2 * t) >> 12;
	int64_t poly = ((6LL * t - 61440LL) * t) / 4096LL + 40960LL;
	int32_t q8 = (int32_t)(((t3 * poly) >> 12) >> 4);
	if (q8 < 0) q8 = 0;
	if (q8 > 256) q8 = 256;
	return q8;
}

/* Strictly monotonic 4-tier sedimentary strata staircase on [0..256] -> [0..256].
 * Broad flat caprock benches (~60% of each tier) separated by crisp cliffs. */
static inline int32_t
nd_field_terrace_q8(int32_t x)
{
	if (x <= 0) return 0;
	if (x >= 256) return 256;
	int32_t tier = x >> 6;
	int32_t rem  = x & 63;
	int32_t riser = (nd_field_smoothstep_q8(rem, 2, 26) * 60) >> 8;
	int32_t bench = (rem * 4) >> 6;
	int32_t out = (tier << 6) + riser + bench;
	if (out > 256) out = 256;
	return out;
}

/* Deterministic torus-periodic corner archetype & structural strike azimuth at cell (cx, cy) */
static inline void
nd_field_geo_corner(uint32_t oseed, uint8_t pbits, int32_t cx, int32_t cy,
                    uint8_t *out_prov, uint8_t *out_az)
{
	uint32_t cell_mask = (pbits > ND_FIELD_GEO_CELL_SHIFT)
		? ((1u << (pbits - ND_FIELD_GEO_CELL_SHIFT)) - 1u) : 0u;
	uint32_t wcx = (uint32_t)cx & cell_mask;
	uint32_t wcy = (uint32_t)cy & cell_mask;
	uint32_t h = nd_xxh32_u32((wcx * 0x9E3779B1u) ^ (wcy * 0x85EBCA77u),
	                          oseed ^ 0x51A7BEEFu);
	*out_prov = (uint8_t)(h % (uint32_t)ND_GEO_PROVINCE_COUNT);
	*out_az   = (uint8_t)((h >> 8) & 0xFFu);
}

/* Pure-integer, O(1), torus-periodic geological province evaluator (planet_bits = 16..23) */
static inline nd_geology_t
nd_field_geology(uint16_t world, int32_t rx, int32_t ry)
{
	uint8_t pbits = nd_field_get_planet_bits();
	rx = nd_field_wrap_room_coord(rx, pbits);
	ry = nd_field_wrap_room_coord(ry, pbits);
	uint32_t seed = 0x48454947u ^ ((uint32_t)world * 0x9E3779B1u);
	uint32_t oseed = seed ^ ND_FIELD_OROGENY_SALT;

	int32_t wx_raw = (int32_t)(chaff_wpoint_periodic_2d(rx, ry, 8, pbits, 8, oseed ^ 0x11A3u) >> 16) - 128;
	int32_t wy_raw = (int32_t)(chaff_wpoint_periodic_2d(rx, ry, 8, pbits, 8, oseed ^ 0x22B7u) >> 16) - 128;
	int32_t mx_raw = (int32_t)(chaff_wpoint_periodic_2d(rx, ry, 6, pbits, 8, oseed ^ 0x33C9u) >> 16) - 128;
	int32_t my_raw = (int32_t)(chaff_wpoint_periodic_2d(rx, ry, 6, pbits, 8, oseed ^ 0x44DBu) >> 16) - 128;

	int32_t urx = rx + (wx_raw >> 1) + (mx_raw >> 3);
	int32_t ury = ry + (wy_raw >> 1) + (my_raw >> 3);

	int32_t prov_raw = (int32_t)(chaff_wpoint_periodic_2d(urx, ury, 8, pbits, 8, oseed ^ 0x55EDu) >> 16);
	int32_t n_belt = (int32_t)(chaff_wpoint_periodic_2d(urx, ury, 7, pbits, 8, oseed ^ 0x661Fu) >> 16) - 128;
	int32_t belt_r = 128 - (n_belt < 0 ? -n_belt : n_belt);

	int32_t cx0 = urx >> 9;
	int32_t cy0 = ury >> 9;
	int32_t fx = urx & 511;
	int32_t fy = ury & 511;
	int32_t u = nd_field_quintic512_q8(fx);
	int32_t v = nd_field_quintic512_q8(fy);

	int32_t w[4];
	w[0] = ((256 - u) * (256 - v)) >> 8;
	w[1] = (u * (256 - v)) >> 8;
	w[2] = ((256 - u) * v) >> 8;
	w[3] = 256 - w[0] - w[1] - w[2];

	uint8_t p[4], az[4];
	nd_field_geo_corner(oseed, pbits, cx0,     cy0,     &p[0], &az[0]);
	nd_field_geo_corner(oseed, pbits, cx0 + 1, cy0,     &p[1], &az[1]);
	nd_field_geo_corner(oseed, pbits, cx0,     cy0 + 1, &p[2], &az[2]);
	nd_field_geo_corner(oseed, pbits, cx0 + 1, cy0 + 1, &p[3], &az[3]);

	int best = 0, second = 1;
	for (int k = 1; k < 4; k++) {
		if (w[k] > w[best]) {
			second = best;
			best = k;
		} else if (k != best && w[k] > w[second]) {
			second = k;
		}
	}
	if (p[second] == p[best]) {
		for (int k = 0; k < 4; k++) {
			if (p[k] != p[best] && (p[second] == p[best] || w[k] > w[second]))
				second = k;
		}
	}

	int32_t uplift_acc = 0, strata_acc = 0, flat_acc = 0, age_acc = 0;
	for (int k = 0; k < 4; k++) {
		const nd_geo_archetype_props_t *ap = &ND_GEO_ARCHETYPE_PROPS[p[k]];
		uplift_acc += w[k] * (int32_t)ap->base_uplift;
		strata_acc += w[k] * (int32_t)ap->strata_hardness;
		flat_acc   += w[k] * (int32_t)ap->basin_flatness;
		age_acc    += w[k] * (int32_t)ap->erosion_age;
	}

	/* Couple crustal uplift intensity smoothly with regional belt ridge */
	int32_t belt_q8 = belt_r << 1; /* 0..256 */
	int32_t raw_up = ((uplift_acc >> 8) * (128 + (belt_q8 >> 1) + (prov_raw >> 2))) >> 8;
	if (raw_up > 255) raw_up = 255;

	int32_t bq8 = 255 - ((w[best] * 255) >> 8);
	if (bq8 < 0) bq8 = 0;
	if (bq8 > 255) bq8 = 255;

	nd_geology_t g;
	g.province        = p[best];
	g.secondary       = p[second];
	g.blend_q8        = (uint8_t)bq8;
	g.tectonic_uplift = (uint8_t)raw_up;
	g.strata_hardness = (uint8_t)(strata_acc >> 8);
	g.basin_flatness  = (uint8_t)(flat_acc >> 8);
	g.erosion_age     = (uint8_t)(age_acc >> 8);
	g.fault_azimuth   = az[best];
	return g;
}

/* Asymmetric fault-escarpment wave on phase in [0..255]: steep 52-room scarp + 204-room dip slope */
static inline int32_t
nd_field_scarp_wave_q8(int32_t proj)
{
	int32_t phase = (int32_t)((uint32_t)proj & 255u);
	if (phase < 52)
		return nd_field_smoothstep_q8(phase, 0, 52);
	return 256 - nd_field_smoothstep_q8(phase, 52, 256);
}

/* Wind-aligned barchan dune erg ripple on phase in [0..63] */
static inline int32_t
nd_field_dune_wave_q8(int32_t proj)
{
	int32_t phase = (int32_t)((uint32_t)proj & 63u);
	if (phase < 40)
		return (phase * 256) / 40;
	return ((64 - phase) * 256) / 24;
}

/* Radial stratovolcano cone profile with breached summit caldera dimple (radius 240 rooms) */
static inline int32_t
nd_field_volcano_cone_q8(uint32_t d)
{
	if (d >= 240u) return 0;
	int32_t c = nd_field_smoothstep_q8(240 - (int32_t)d, 0, 220);
	c = (c * (96 + ((c * 160) >> 8))) >> 8;
	if (d < 36u) {
		int32_t cal = nd_field_smoothstep_q8(36 - (int32_t)d, 0, 36);
		c -= (cal * 64) >> 8;
		if (c < 0) c = 0;
	}
	return c;
}

/* Shared pure-integer archetype evaluator used identically by scalar and 4-wide SIMD orogeny */
static inline int32_t
nd_field_orogeny_eval_archetype(uint8_t prov, uint8_t az, int32_t corner_rx, int32_t corner_ry,
                                int32_t urx, int32_t ury, int32_t base_hyp,
                                int32_t hill_q8, int32_t alpine_q8, int32_t scale_q8,
                                int32_t hill_raw, int32_t alp_raw,
                                int32_t r9, int32_t r8, int32_t r7, int32_t r5)
{
	int32_t env_q8 = (alpine_q8 > hill_q8) ? alpine_q8 : hill_q8;
	switch (prov) {
	case ND_GEO_ALLUVIAL_PLAIN: {
		int32_t h_hill = hill_raw >> 1;
		int32_t h_alp  = alp_raw >> 3;
		return ((h_hill * hill_q8) >> 8) + ((h_alp * alpine_q8) >> 8);
	}
	case ND_GEO_PIEDMONT_HILLS: {
		int32_t h_hill = hill_raw * 10;
		int32_t h_alp  = (alp_raw * 3) >> 3;
		return ((h_hill * hill_q8) >> 8) + ((h_alp * alpine_q8) >> 8);
	}
	case ND_GEO_ALPINE_CORDILLERA: {
		int32_t h_hill = hill_raw * 12;
		return ((h_hill * hill_q8) >> 8) + ((alp_raw * alpine_q8) >> 8);
	}
	case ND_GEO_CANYON_MESA: {
		int32_t m_sig = (r9 * 3 + r7 * 2) / 5;
		int32_t terr_q8 = nd_field_terrace_q8(m_sig << 1);
		int32_t mesa_scale = 192 + (scale_q8 >> 2);
		int32_t mesa_cap = (int32_t)(((int64_t)terr_q8 * 38000LL * (int64_t)mesa_scale) >> 16);
		return (((mesa_cap + hill_raw * 6) * env_q8) >> 8) + (((alp_raw >> 3) * alpine_q8) >> 8);
	}
	case ND_GEO_FAULT_ESCARPMENT: {
		int32_t ca = nd_field_cos_q15((uint16_t)((uint32_t)az << 8));
		int32_t sa = nd_field_cos_q15((uint16_t)(((uint32_t)az + 192u) << 8));
		int32_t proj = (int32_t)(((int64_t)urx * (int64_t)ca + (int64_t)ury * (int64_t)sa) >> 15);
		int32_t scarp_q8 = nd_field_scarp_wave_q8(proj);
		int32_t scarp_h = (int32_t)(((int64_t)scarp_q8 * 54000LL * (int64_t)scale_q8) >> 16);
		int32_t h_hill = (scarp_h >> 2) + hill_raw * 6;
		int32_t h_alp  = ((scarp_h * 3) >> 2) + (alp_raw >> 2);
		return ((h_hill * hill_q8) >> 8) + ((h_alp * alpine_q8) >> 8);
	}
	case ND_GEO_GLACIAL_FJORD: {
		int32_t g_sig = (r9 * 5 + r8 * 3) >> 3;
		int32_t u_wall_q8 = nd_field_smoothstep_q8(g_sig, 46, 98);
		int32_t h_hill = ((hill_raw * 7) * u_wall_q8) >> 8;
		int32_t h_alp  = (int32_t)(((int64_t)alp_raw * (int64_t)u_wall_q8) >> 8);
		return ((h_hill * hill_q8) >> 8) + ((h_alp * alpine_q8) >> 8);
	}
	case ND_GEO_VOLCANIC_CONE: {
		int32_t dx = urx - corner_rx;
		int32_t dy = ury - corner_ry;
		uint32_t d = nd_field_isqrt32((uint32_t)(dx * dx + dy * dy));
		int32_t cone_q8 = nd_field_volcano_cone_q8(d);
		int32_t volc_h = (int32_t)(((int64_t)cone_q8 * 76000LL * (int64_t)scale_q8) >> 16);
		return (((volc_h + hill_raw * 7) * env_q8) >> 8) + (((alp_raw >> 2) * alpine_q8) >> 8);
	}
	case ND_GEO_KARST_TOWERS: {
		int32_t k_sig = (r5 * r5 * r5) >> 14; /* 0..128 sharp isolated limestone pinnacles */
		int32_t karst_h = k_sig * 165;
		return (((karst_h + hill_raw * 3) * env_q8) >> 8) + (((alp_raw >> 2) * alpine_q8) >> 8);
	}
	case ND_GEO_ARID_BASIN_ERG: {
		int32_t ca = nd_field_cos_q15((uint16_t)((uint32_t)az << 8));
		int32_t sa = nd_field_cos_q15((uint16_t)(((uint32_t)az + 192u) << 8));
		int32_t proj = (int32_t)(((int64_t)urx * (int64_t)ca + (int64_t)ury * (int64_t)sa) >> 15);
		int32_t dune_q8 = nd_field_dune_wave_q8(proj);
		int32_t dune_h = (dune_q8 * 14) >> 1;
		int32_t h_hill = dune_h + hill_raw * 2;
		int32_t h_alp  = (alp_raw * 3) >> 4;
		return ((h_hill * hill_q8) >> 8) + ((h_alp * alpine_q8) >> 8);
	}
	case ND_GEO_SEA_CLIFF_COAST:
	default: {
		int32_t headland_q8 = nd_field_smoothstep_q8(base_hyp, 6, 20);
		int32_t cliff_h = ((headland_q8 * 4800) >> 8) + hill_raw * 8;
		int32_t h_alp = (alp_raw * 5) >> 3;
		return ((cliff_h * hill_q8) >> 8) + ((h_alp * alpine_q8) >> 8);
	}
	}
}

/* Blend 4 corners of the warped 2x2 province cell with C2 quintic weights */
static inline int32_t
nd_field_orogeny_blend_cell(uint32_t oseed, uint8_t pbits, int32_t urx, int32_t ury,
                            int32_t base_hyp, int32_t coast_q8, int32_t hill_q8,
                            int32_t alpine_q8, int32_t scale_q8,
                            int32_t hill_raw, int32_t alp_raw,
                            int32_t r9, int32_t r8, int32_t r7, int32_t r5)
{
	int32_t cx0 = urx >> 9;
	int32_t cy0 = ury >> 9;
	int32_t fx = urx & 511;
	int32_t fy = ury & 511;
	int32_t u = nd_field_quintic512_q8(fx);
	int32_t v = nd_field_quintic512_q8(fy);

	int32_t w[4];
	w[0] = ((256 - u) * (256 - v)) >> 8;
	w[1] = (u * (256 - v)) >> 8;
	w[2] = ((256 - u) * v) >> 8;
	w[3] = 256 - w[0] - w[1] - w[2];

	const int32_t cx[4] = { cx0, cx0 + 1, cx0,     cx0 + 1 };
	const int32_t cy[4] = { cy0, cy0,     cy0 + 1, cy0 + 1 };

	int64_t acc = 0;
	for (int k = 0; k < 4; k++) {
		if (w[k] <= 0) continue;
		uint8_t prov, az;
		nd_field_geo_corner(oseed, pbits, cx[k], cy[k], &prov, &az);
		int32_t h_k = nd_field_orogeny_eval_archetype(
			prov, az, cx[k] << 9, cy[k] << 9, urx, ury, base_hyp,
			hill_q8, alpine_q8, scale_q8, hill_raw, alp_raw, r9, r8, r7, r5);
		acc += (int64_t)w[k] * (int64_t)h_k;
	}
	int32_t uplift = (int32_t)(acc >> 8);
	uplift = (int32_t)(((int64_t)uplift * (int64_t)coast_q8) >> 8);
	return uplift > 0 ? uplift : 0;
}

static inline int32_t
nd_field_orogeny_wu(uint32_t seed, int32_t room_step_q16, int32_t rx, int32_t ry, int32_t base_hyp)
{
	if (base_hyp <= 6)
		return 0;
	uint8_t pbits = nd_field_get_planet_bits();
	int32_t max_e = nd_field_active_max_elev_wu();
	int32_t scale_q8 = (int32_t)(((int64_t)max_e * 256LL) / 81000LL);
	int32_t coast_q8 = nd_field_smoothstep_q8(base_hyp, 6, 16);
	uint32_t oseed = seed ^ ND_FIELD_OROGENY_SALT;

	int32_t wx_raw = (int32_t)(chaff_wpoint_periodic_2d(rx, ry, 8, pbits, 8, oseed ^ 0x11A3u) >> 16) - 128;
	int32_t wy_raw = (int32_t)(chaff_wpoint_periodic_2d(rx, ry, 8, pbits, 8, oseed ^ 0x22B7u) >> 16) - 128;
	int32_t mx_raw = (int32_t)(chaff_wpoint_periodic_2d(rx, ry, 6, pbits, 8, oseed ^ 0x33C9u) >> 16) - 128;
	int32_t my_raw = (int32_t)(chaff_wpoint_periodic_2d(rx, ry, 6, pbits, 8, oseed ^ 0x44DBu) >> 16) - 128;

	int32_t urx = rx + (wx_raw >> 1) + (mx_raw >> 3);
	int32_t ury = ry + (wy_raw >> 1) + (my_raw >> 3);

	int32_t prov_raw = (int32_t)(chaff_wpoint_periodic_2d(urx, ury, 8, pbits, 8, oseed ^ 0x55EDu) >> 16);
	int32_t n_belt = (int32_t)(chaff_wpoint_periodic_2d(urx, ury, 7, pbits, 8, oseed ^ 0x661Fu) >> 16) - 128;
	int32_t belt_r = 128 - (n_belt < 0 ? -n_belt : n_belt);

	int32_t h_sig = (prov_raw * 3 + belt_r * 5) >> 3;
	int32_t hill_q8 = nd_field_smoothstep_q8(h_sig, 96, 150);

	int32_t a_sig = (belt_r * (144 + prov_raw)) >> 8;
	if (base_hyp > 80) {
		int32_t cb = (base_hyp - 80) >> 3;
		if (cb > 28) cb = 28;
		a_sig += cb;
	}
	int32_t a_sm = nd_field_smoothstep_q8(a_sig, 78, 146);
	int32_t alpine_q8 = (a_sm * (128 + (a_sm >> 1))) >> 8;

	if (hill_q8 == 0 && alpine_q8 == 0)
		return 0;

	uint32_t w4_oct = (room_step_q16 > 0) ? chaff_octave_weight_q16(4, room_step_q16) : 65536u;
	int32_t hill_raw = 0;
	int32_t r8 = 128 - (prov_raw >= 128 ? (prov_raw - 128) : (128 - prov_raw));
	int32_t r7 = belt_r;
	int32_t r9 = 0;
	int32_t r5_karst = 0;

	/* Shared macro shift-9 octave (512 rooms = 1.82 km) feeds mesa/glacial/alpine silhouettes */
	int32_t n9 = (int32_t)(chaff_wpoint_periodic_2d(urx + 313, ury - 571, 9, pbits, 8, oseed ^ 0x4E3Bu) >> 16) - 128;
	r9 = 128 - (n9 < 0 ? -n9 : n9);

	if (hill_q8 > 0 || alpine_q8 > 0) {
		int32_t n5 = (int32_t)(chaff_wpoint_periodic_2d(urx + 713, ury - 419, 5, pbits, 8, oseed ^ 0x7731u) >> 16);
		int32_t r5 = 128 - (n5 >= 128 ? (n5 - 128) : (128 - n5));
		r5_karst = r5;
		if (hill_q8 > 0) {
			int32_t f5 = (n5 * 192 + r5 * 528) >> 8;
			int32_t f4 = 0;
			if (w4_oct > 0u) {
				int32_t n4 = (int32_t)(chaff_wpoint_periodic_2d(urx - 317, ury + 823, 4, pbits, 8, oseed ^ 0x8843u) >> 16);
				f4 = (n4 * 240) >> 8;
				if (w4_oct < 65536u)
					f4 = (int32_t)(((int64_t)f4 * (int64_t)w4_oct) >> 16);
			}
			hill_raw = f5 + f4;
		}
	}

	int32_t alp_raw = 0;
	if (alpine_q8 > 0) {
		/* Metric-scale ridged multifractal: shift 10 (3.64 km) down to shift 2 (14 m)
		 * preserving physical 30-45 deg slopes at 45 wu/m up to 81,000 wu (1,800 m) at b=16 */
		int32_t n10 = (int32_t)(chaff_wpoint_periodic_2d(urx - 829, ury + 467, 10, pbits, 8, oseed ^ 0x3D29u) >> 16) - 128;
		int32_t r10 = 128 - (n10 < 0 ? -n10 : n10);
		int32_t h10 = (r10 * r10) >> 7;

		int32_t h9 = (r9 * (64 + (r9 >> 1))) >> 7;
		int32_t w9 = 96 + ((h10 * 160) >> 7);
		int32_t h9w = (h9 * w9) >> 8;

		int32_t h8 = (r8 * (64 + (r8 >> 1))) >> 7;
		int32_t w8 = 84 + ((h9w * 172) >> 7);
		int32_t h8w = (h8 * w8) >> 8;

		int32_t h7 = (r7 * (64 + (r7 >> 1))) >> 7;
		int32_t w7 = 76 + ((h8w * 180) >> 7);
		int32_t h7w = (h7 * w7) >> 8;

		int32_t n6 = (int32_t)(chaff_wpoint_periodic_2d(urx + 191, ury + 547, 6, pbits, 8, oseed ^ 0x9955u) >> 16) - 128;
		int32_t r6 = 128 - (n6 < 0 ? -n6 : n6);
		int32_t h6 = (r6 * r6) >> 7;
		int32_t w6 = 72 + ((h7w * 184) >> 7);
		int32_t h6w = (h6 * w6) >> 8;

		int32_t n5a = (int32_t)(chaff_wpoint_periodic_2d(urx - 613, ury + 271, 5, pbits, 8, oseed ^ 0xAA67u) >> 16) - 128;
		int32_t r5a = 128 - (n5a < 0 ? -n5a : n5a);
		int32_t h5 = (r5a * (64 + (r5a >> 1))) >> 7;
		int32_t w5 = 64 + ((h6w * 192) >> 7);
		int32_t h5w = (h5 * w5) >> 8;

		alp_raw = (int32_t)(((int64_t)h10 * 33000LL) >> 7) +
		          (int32_t)(((int64_t)h9w * 26000LL) >> 7) +
		          (int32_t)(((int64_t)h8w * 16500LL) >> 7) +
		          (int32_t)(((int64_t)h7w * 10000LL) >> 7) +
		          (int32_t)(((int64_t)h6w *  5200LL) >> 7) +
		          (int32_t)(((int64_t)h5w *  2600LL) >> 7);

		if (w4_oct > 0u) {
			int32_t n4 = (int32_t)(chaff_wpoint_periodic_2d(urx + 409, ury - 733, 4, pbits, 8, oseed ^ 0xBB79u) >> 16) - 128;
			int32_t r4 = 128 - (n4 < 0 ? -n4 : n4);
			int32_t h4 = (r4 * (64 + (r4 >> 1))) >> 7;
			int32_t w4 = 56 + ((h5w * 200) >> 7);
			int32_t h4w = (h4 * w4) >> 8;
			int32_t c4 = (h4w * 1300) >> 7;
			if (w4_oct < 65536u)
				c4 = (int32_t)(((int64_t)c4 * (int64_t)w4_oct) >> 16);
			alp_raw += c4;

			uint32_t w3_oct = (room_step_q16 > 0) ? chaff_octave_weight_q16(3, room_step_q16) : 65536u;
			if (w3_oct > 0u) {
				int32_t n3 = (int32_t)(chaff_wpoint_periodic_2d(urx - 227, ury - 389, 3, pbits, 8, oseed ^ 0xCC8Bu) >> 16) - 128;
				int32_t r3 = 128 - (n3 < 0 ? -n3 : n3);
				int32_t w3 = 32 + ((h4w * 224) >> 7);
				int32_t h3w = (r3 * w3) >> 8;
				int32_t c3 = (h3w * 650) >> 7;
				if (w3_oct < 65536u)
					c3 = (int32_t)(((int64_t)c3 * (int64_t)w3_oct) >> 16);
				alp_raw += c3;

				uint32_t w2_oct = (room_step_q16 > 0) ? chaff_octave_weight_q16(2, room_step_q16) : 65536u;
				if (w2_oct > 0u && h3w > 0) {
					int32_t n2 = (int32_t)(chaff_wpoint_periodic_2d(urx + 131, ury + 179, 2, pbits, 8, oseed ^ 0xDD9Du) >> 16) - 128;
					int32_t r2 = 128 - (n2 < 0 ? -n2 : n2);
					int32_t w2 = (h3w * 256) >> 7;
					int32_t h2w = (r2 * w2) >> 8;
					int32_t c2 = (h2w * 350) >> 7;
					if (w2_oct < 65536u)
						c2 = (int32_t)(((int64_t)c2 * (int64_t)w2_oct) >> 16);
					alp_raw += c2;
				}
			}
		}

		/* Core Scaling Law (Roadmap §2): larger planets unlock macro continental octaves
		 * (shifts 11..13) to build 4,500..8,850 m cordilleras without stretching local hills */
		if (pbits >= 18u && max_e > 81000) {
			int64_t extra_wu = (int64_t)(max_e - 81000);
			int32_t n11 = (int32_t)(chaff_wpoint_periodic_2d(urx + 1423, ury - 977, 11, pbits, 8, oseed ^ 0x5E11u) >> 16) - 128;
			int32_t r11 = 128 - (n11 < 0 ? -n11 : n11);
			int32_t h11 = (r11 * r11) >> 7;
			if (pbits < 20u) {
				alp_raw += (int32_t)(((int64_t)h11 * extra_wu * 115LL) / (128LL * 100LL));
			} else if (pbits < 22u) {
				int32_t n12 = (int32_t)(chaff_wpoint_periodic_2d(urx - 2113, ury + 1657, 12, pbits, 8, oseed ^ 0x6F23u) >> 16) - 128;
				int32_t r12 = 128 - (n12 < 0 ? -n12 : n12);
				int32_t h12 = (r12 * r12) >> 7;
				alp_raw += (int32_t)(((int64_t)h11 * extra_wu * 60LL + (int64_t)h12 * extra_wu * 55LL) / (128LL * 100LL));
			} else {
				int32_t n12 = (int32_t)(chaff_wpoint_periodic_2d(urx - 2113, ury + 1657, 12, pbits, 8, oseed ^ 0x6F23u) >> 16) - 128;
				int32_t r12 = 128 - (n12 < 0 ? -n12 : n12);
				int32_t h12 = (r12 * r12) >> 7;
				int32_t n13 = (int32_t)(chaff_wpoint_periodic_2d(urx + 3571, ury + 2909, 13, pbits, 8, oseed ^ 0x7A35u) >> 16) - 128;
				int32_t r13 = 128 - (n13 < 0 ? -n13 : n13);
				int32_t h13 = (r13 * r13) >> 7;
				alp_raw += (int32_t)(((int64_t)h11 * extra_wu * 40LL + (int64_t)h12 * extra_wu * 40LL + (int64_t)h13 * extra_wu * 35LL) / (128LL * 100LL));
			}
		}
	}

	return nd_field_orogeny_blend_cell(oseed, pbits, urx, ury, base_hyp, coast_q8,
	                                   hill_q8, alpine_q8, scale_q8,
	                                   hill_raw, alp_raw, r9, r8, r7, r5_karst);
}

/* 4-wide SIMD tectonic orogeny evaluator, bit-exact with 4 calls to nd_field_orogeny_wu. */
static inline void
nd_field_orogeny_wu_4x(uint32_t seed, int32_t room_step_q16,
                       const int32_t rx[4], const int32_t ry[4],
                       const int32_t base_hyp[4], int32_t out[4])
{
	int any_land = (base_hyp[0] > 6 || base_hyp[1] > 6 || base_hyp[2] > 6 || base_hyp[3] > 6);
	if (!any_land) {
		out[0] = out[1] = out[2] = out[3] = 0;
		return;
	}

	uint8_t pbits = nd_field_get_planet_bits();
	int32_t max_e = nd_field_active_max_elev_wu();
	int32_t scale_q8 = (int32_t)(((int64_t)max_e * 256LL) / 81000LL);
	uint32_t oseed = seed ^ ND_FIELD_OROGENY_SALT;
	uint32_t rw_x[4], rw_y[4], rm_x[4], rm_y[4];
	chaff_wpoint_periodic_2d_4x(rx, ry, 8, pbits, 8, oseed ^ 0x11A3u, rw_x);
	chaff_wpoint_periodic_2d_4x(rx, ry, 8, pbits, 8, oseed ^ 0x22B7u, rw_y);
	chaff_wpoint_periodic_2d_4x(rx, ry, 6, pbits, 8, oseed ^ 0x33C9u, rm_x);
	chaff_wpoint_periodic_2d_4x(rx, ry, 6, pbits, 8, oseed ^ 0x44DBu, rm_y);

	int32_t urx[4], ury[4];
	for (int i = 0; i < 4; i++) {
		int32_t wx_raw = (int32_t)(rw_x[i] >> 16) - 128;
		int32_t wy_raw = (int32_t)(rw_y[i] >> 16) - 128;
		int32_t mx_raw = (int32_t)(rm_x[i] >> 16) - 128;
		int32_t my_raw = (int32_t)(rm_y[i] >> 16) - 128;
		urx[i] = rx[i] + (wx_raw >> 1) + (mx_raw >> 3);
		ury[i] = ry[i] + (wy_raw >> 1) + (my_raw >> 3);
	}

	uint32_t r_prov[4], r_belt[4];
	chaff_wpoint_periodic_2d_4x(urx, ury, 8, pbits, 8, oseed ^ 0x55EDu, r_prov);
	chaff_wpoint_periodic_2d_4x(urx, ury, 7, pbits, 8, oseed ^ 0x661Fu, r_belt);

	int32_t coast_q8[4], hill_q8[4], alpine_q8[4], r8_v[4], r7_v[4];
	int any_hill = 0, any_alpine = 0;
	for (int i = 0; i < 4; i++) {
		if (base_hyp[i] <= 6) {
			coast_q8[i] = 0;
			hill_q8[i] = 0;
			alpine_q8[i] = 0;
			r8_v[i] = 0;
			r7_v[i] = 0;
			continue;
		}
		coast_q8[i] = nd_field_smoothstep_q8(base_hyp[i], 6, 16);
		int32_t prov_raw = (int32_t)(r_prov[i] >> 16);
		int32_t nb = (int32_t)(r_belt[i] >> 16) - 128;
		int32_t belt_r = 128 - (nb < 0 ? -nb : nb);
		r8_v[i] = 128 - (prov_raw >= 128 ? (prov_raw - 128) : (128 - prov_raw));
		r7_v[i] = belt_r;

		int32_t h_sig = (prov_raw * 3 + belt_r * 5) >> 3;
		hill_q8[i] = nd_field_smoothstep_q8(h_sig, 96, 150);

		int32_t a_sig = (belt_r * (144 + prov_raw)) >> 8;
		if (base_hyp[i] > 80) {
			int32_t cb = (base_hyp[i] - 80) >> 3;
			if (cb > 28) cb = 28;
			a_sig += cb;
		}
		int32_t a_sm = nd_field_smoothstep_q8(a_sig, 78, 146);
		alpine_q8[i] = (a_sm * (128 + (a_sm >> 1))) >> 8;

		if (hill_q8[i] > 0) any_hill = 1;
		if (alpine_q8[i] > 0) any_alpine = 1;
	}

	if (!any_hill && !any_alpine) {
		out[0] = out[1] = out[2] = out[3] = 0;
		return;
	}

	uint32_t w4_oct = (room_step_q16 > 0) ? chaff_octave_weight_q16(4, room_step_q16) : 65536u;

	int32_t gx9[4], gy9[4], hx5[4], hy5[4];
	for (int i = 0; i < 4; i++) {
		gx9[i] = urx[i] + 313; gy9[i] = ury[i] - 571;
		hx5[i] = urx[i] + 713; hy5[i] = ury[i] - 419;
	}
	uint32_t r_n9[4], r_f5[4];
	chaff_wpoint_periodic_2d_4x(gx9, gy9, 9, pbits, 8, oseed ^ 0x4E3Bu, r_n9);
	chaff_wpoint_periodic_2d_4x(hx5, hy5, 5, pbits, 8, oseed ^ 0x7731u, r_f5);

	uint32_t r_f4[4] = { 0, 0, 0, 0 };
	if (any_hill && w4_oct > 0u) {
		int32_t hx4[4], hy4[4];
		for (int i = 0; i < 4; i++) { hx4[i] = urx[i] - 317; hy4[i] = ury[i] + 823; }
		chaff_wpoint_periodic_2d_4x(hx4, hy4, 4, pbits, 8, oseed ^ 0x8843u, r_f4);
	}

	int32_t hill_raw[4] = { 0, 0, 0, 0 };
	int32_t r9_v[4], r5k_v[4];
	for (int i = 0; i < 4; i++) {
		int32_t n9 = (int32_t)(r_n9[i] >> 16) - 128;
		r9_v[i] = 128 - (n9 < 0 ? -n9 : n9);
		int32_t n5 = (int32_t)(r_f5[i] >> 16);
		int32_t r5 = 128 - (n5 >= 128 ? (n5 - 128) : (128 - n5));
		r5k_v[i] = r5;
		if (hill_q8[i] > 0) {
			int32_t f5 = (n5 * 192 + r5 * 528) >> 8;
			int32_t f4 = 0;
			if (w4_oct > 0u) {
				int32_t n4 = (int32_t)(r_f4[i] >> 16);
				f4 = (n4 * 240) >> 8;
				if (w4_oct < 65536u)
					f4 = (int32_t)(((int64_t)f4 * (int64_t)w4_oct) >> 16);
			}
			hill_raw[i] = f5 + f4;
		}
	}

	int32_t alp_raw[4] = { 0, 0, 0, 0 };
	if (any_alpine) {
		int32_t ax10[4], ay10[4], ax6[4], ay6[4], ax5[4], ay5[4];
		for (int i = 0; i < 4; i++) {
			ax10[i] = urx[i] - 829; ay10[i] = ury[i] + 467;
			ax6[i]  = urx[i] + 191; ay6[i]  = ury[i] + 547;
			ax5[i]  = urx[i] - 613; ay5[i]  = ury[i] + 271;
		}
		uint32_t r_a10[4], r_a6[4], r_a5[4];
		chaff_wpoint_periodic_2d_4x(ax10, ay10, 10, pbits, 8, oseed ^ 0x3D29u, r_a10);
		chaff_wpoint_periodic_2d_4x(ax6,  ay6,   6, pbits, 8, oseed ^ 0x9955u, r_a6);
		chaff_wpoint_periodic_2d_4x(ax5,  ay5,   5, pbits, 8, oseed ^ 0xAA67u, r_a5);

		uint32_t w3_oct = (w4_oct > 0u) ? ((room_step_q16 > 0) ? chaff_octave_weight_q16(3, room_step_q16) : 65536u) : 0u;
		uint32_t w2_oct = (w3_oct > 0u) ? ((room_step_q16 > 0) ? chaff_octave_weight_q16(2, room_step_q16) : 65536u) : 0u;
		uint32_t r_a4[4] = { 0, 0, 0, 0 };
		uint32_t r_a3[4] = { 0, 0, 0, 0 };
		uint32_t r_a2[4] = { 0, 0, 0, 0 };
		if (w4_oct > 0u) {
			int32_t ax4[4], ay4[4];
			for (int i = 0; i < 4; i++) { ax4[i] = urx[i] + 409; ay4[i] = ury[i] - 733; }
			chaff_wpoint_periodic_2d_4x(ax4, ay4, 4, pbits, 8, oseed ^ 0xBB79u, r_a4);
		}
		if (w3_oct > 0u) {
			int32_t ax3[4], ay3[4];
			for (int i = 0; i < 4; i++) { ax3[i] = urx[i] - 227; ay3[i] = ury[i] - 389; }
			chaff_wpoint_periodic_2d_4x(ax3, ay3, 3, pbits, 8, oseed ^ 0xCC8Bu, r_a3);
		}
		if (w2_oct > 0u) {
			int32_t ax2[4], ay2[4];
			for (int i = 0; i < 4; i++) { ax2[i] = urx[i] + 131; ay2[i] = ury[i] + 179; }
			chaff_wpoint_periodic_2d_4x(ax2, ay2, 2, pbits, 8, oseed ^ 0xDD9Du, r_a2);
		}

		uint32_t r_a11[4] = { 0, 0, 0, 0 };
		uint32_t r_a12[4] = { 0, 0, 0, 0 };
		uint32_t r_a13[4] = { 0, 0, 0, 0 };
		if (pbits >= 18u && max_e > 81000) {
			int32_t ax11[4], ay11[4];
			for (int i = 0; i < 4; i++) { ax11[i] = urx[i] + 1423; ay11[i] = ury[i] - 977; }
			chaff_wpoint_periodic_2d_4x(ax11, ay11, 11, pbits, 8, oseed ^ 0x5E11u, r_a11);
			if (pbits >= 20u) {
				int32_t ax12[4], ay12[4];
				for (int i = 0; i < 4; i++) { ax12[i] = urx[i] - 2113; ay12[i] = ury[i] + 1657; }
				chaff_wpoint_periodic_2d_4x(ax12, ay12, 12, pbits, 8, oseed ^ 0x6F23u, r_a12);
			}
			if (pbits >= 22u) {
				int32_t ax13[4], ay13[4];
				for (int i = 0; i < 4; i++) { ax13[i] = urx[i] + 3571; ay13[i] = ury[i] + 2909; }
				chaff_wpoint_periodic_2d_4x(ax13, ay13, 13, pbits, 8, oseed ^ 0x7A35u, r_a13);
			}
		}

		for (int i = 0; i < 4; i++) {
			if (alpine_q8[i] <= 0) continue;
			int32_t n10 = (int32_t)(r_a10[i] >> 16) - 128;
			int32_t r10 = 128 - (n10 < 0 ? -n10 : n10);
			int32_t h10 = (r10 * r10) >> 7;

			int32_t h9 = (r9_v[i] * (64 + (r9_v[i] >> 1))) >> 7;
			int32_t w9 = 96 + ((h10 * 160) >> 7);
			int32_t h9w = (h9 * w9) >> 8;

			int32_t h8 = (r8_v[i] * (64 + (r8_v[i] >> 1))) >> 7;
			int32_t w8 = 84 + ((h9w * 172) >> 7);
			int32_t h8w = (h8 * w8) >> 8;

			int32_t h7 = (r7_v[i] * (64 + (r7_v[i] >> 1))) >> 7;
			int32_t w7 = 76 + ((h8w * 180) >> 7);
			int32_t h7w = (h7 * w7) >> 8;

			int32_t n6 = (int32_t)(r_a6[i] >> 16) - 128;
			int32_t r6 = 128 - (n6 < 0 ? -n6 : n6);
			int32_t h6 = (r6 * r6) >> 7;
			int32_t w6 = 72 + ((h7w * 184) >> 7);
			int32_t h6w = (h6 * w6) >> 8;

			int32_t n5a = (int32_t)(r_a5[i] >> 16) - 128;
			int32_t r5a = 128 - (n5a < 0 ? -n5a : n5a);
			int32_t h5 = (r5a * (64 + (r5a >> 1))) >> 7;
			int32_t w5 = 64 + ((h6w * 192) >> 7);
			int32_t h5w = (h5 * w5) >> 8;

			int32_t alp = (int32_t)(((int64_t)h10 * 33000LL) >> 7) +
			              (int32_t)(((int64_t)h9w * 26000LL) >> 7) +
			              (int32_t)(((int64_t)h8w * 16500LL) >> 7) +
			              (int32_t)(((int64_t)h7w * 10000LL) >> 7) +
			              (int32_t)(((int64_t)h6w *  5200LL) >> 7) +
			              (int32_t)(((int64_t)h5w *  2600LL) >> 7);

			if (w4_oct > 0u) {
				int32_t n4 = (int32_t)(r_a4[i] >> 16) - 128;
				int32_t r4 = 128 - (n4 < 0 ? -n4 : n4);
				int32_t h4 = (r4 * (64 + (r4 >> 1))) >> 7;
				int32_t w4 = 56 + ((h5w * 200) >> 7);
				int32_t h4w = (h4 * w4) >> 8;
				int32_t c4 = (h4w * 1300) >> 7;
				if (w4_oct < 65536u)
					c4 = (int32_t)(((int64_t)c4 * (int64_t)w4_oct) >> 16);
				alp += c4;

				if (w3_oct > 0u) {
					int32_t n3 = (int32_t)(r_a3[i] >> 16) - 128;
					int32_t r3 = 128 - (n3 < 0 ? -n3 : n3);
					int32_t w3 = 32 + ((h4w * 224) >> 7);
					int32_t h3w = (r3 * w3) >> 8;
					int32_t c3 = (h3w * 650) >> 7;
					if (w3_oct < 65536u)
						c3 = (int32_t)(((int64_t)c3 * (int64_t)w3_oct) >> 16);
					alp += c3;

					if (w2_oct > 0u && h3w > 0) {
						int32_t n2 = (int32_t)(r_a2[i] >> 16) - 128;
						int32_t r2 = 128 - (n2 < 0 ? -n2 : n2);
						int32_t w2 = (h3w * 256) >> 7;
						int32_t h2w = (r2 * w2) >> 8;
						int32_t c2 = (h2w * 350) >> 7;
						if (w2_oct < 65536u)
							c2 = (int32_t)(((int64_t)c2 * (int64_t)w2_oct) >> 16);
						alp += c2;
					}
				}
			}

			if (pbits >= 18u && max_e > 81000) {
				int64_t extra_wu = (int64_t)(max_e - 81000);
				int32_t n11 = (int32_t)(r_a11[i] >> 16) - 128;
				int32_t r11 = 128 - (n11 < 0 ? -n11 : n11);
				int32_t h11 = (r11 * r11) >> 7;
				if (pbits < 20u) {
					alp += (int32_t)(((int64_t)h11 * extra_wu * 115LL) / (128LL * 100LL));
				} else if (pbits < 22u) {
					int32_t n12 = (int32_t)(r_a12[i] >> 16) - 128;
					int32_t r12 = 128 - (n12 < 0 ? -n12 : n12);
					int32_t h12 = (r12 * r12) >> 7;
					alp += (int32_t)(((int64_t)h11 * extra_wu * 60LL + (int64_t)h12 * extra_wu * 55LL) / (128LL * 100LL));
				} else {
					int32_t n12 = (int32_t)(r_a12[i] >> 16) - 128;
					int32_t r12 = 128 - (n12 < 0 ? -n12 : n12);
					int32_t h12 = (r12 * r12) >> 7;
					int32_t n13 = (int32_t)(r_a13[i] >> 16) - 128;
					int32_t r13 = 128 - (n13 < 0 ? -n13 : n13);
					int32_t h13 = (r13 * r13) >> 7;
					alp += (int32_t)(((int64_t)h11 * extra_wu * 40LL + (int64_t)h12 * extra_wu * 40LL + (int64_t)h13 * extra_wu * 35LL) / (128LL * 100LL));
				}
			}
			alp_raw[i] = alp;
		}
	}

	for (int i = 0; i < 4; i++) {
		if (hill_q8[i] == 0 && alpine_q8[i] == 0) {
			out[i] = 0;
		} else {
			out[i] = nd_field_orogeny_blend_cell(
				oseed, pbits, urx[i], ury[i], base_hyp[i], coast_q8[i],
				hill_q8[i], alpine_q8[i], scale_q8,
				hill_raw[i], alp_raw[i], r9_v[i], r8_v[i], r7_v[i], r5k_v[i]);
		}
	}
}

/* Exact conservative [out_min, out_max] of (chaff_wpoint_periodic_2d(x, y, shift, pbits, 8, seed) >> 16)
 * over the integer rectangle [x0..x1] x [y0..y1]. Inside each 2^shift cell, C2 quintic value noise is
 * monotonic-bilinear in (u(x), v(y)), so its exact extrema on every sub-cell occur at the sub-cell's
 * 4 corners (split along cell boundaries). Zero Lipschitz inflation; falls back to [0, 255] if wide. */
static inline void
nd_field_periodic_rect_bounds(int32_t x0, int32_t x1, int32_t y0, int32_t y1,
                              uint8_t shift, uint32_t seed,
                              int32_t *out_min, int32_t *out_max)
{
	uint8_t pbits = nd_field_get_planet_bits();
	int32_t span = (int32_t)(1u << pbits);
	int32_t cx0 = x0 >> shift;
	int32_t cx1 = x1 >> shift;
	int32_t cy0 = y0 >> shift;
	int32_t cy1 = y1 >> shift;
	if ((cx1 - cx0) > 3 || (cy1 - cy0) > 3 || (x1 - x0) >= span || (y1 - y0) >= span) {
		*out_min = 0;
		*out_max = 255;
		return;
	}
	int32_t xs[6], ys[6];
	int nx = 0, ny = 0;
	xs[nx++] = x0;
	for (int32_t c = cx0 + 1; c <= cx1; c++) {
		int32_t b = c << shift;
		if (b > x0 && b < x1)
			xs[nx++] = b;
	}
	if (x1 > x0)
		xs[nx++] = x1;

	ys[ny++] = y0;
	for (int32_t c = cy0 + 1; c <= cy1; c++) {
		int32_t b = c << shift;
		if (b > y0 && b < y1)
			ys[ny++] = b;
	}
	if (y1 > y0)
		ys[ny++] = y1;

	int32_t vmin = 255, vmax = 0;
	for (int iy = 0; iy < ny; iy++) {
		for (int ix = 0; ix < nx; ix++) {
			int32_t v = (int32_t)(chaff_wpoint_periodic_2d(xs[ix], ys[iy], shift, pbits, 8, seed) >> 16);
			if (v < vmin) vmin = v;
			if (v > vmax) vmax = v;
		}
	}
	/* +/-1 padding covers fixed-point rounding inside noise_wstep */
	vmin = (vmin > 0) ? (vmin - 1) : 0;
	vmax = (vmax < 255) ? (vmax + 1) : 255;
	*out_min = vmin;
	*out_max = vmax;
}

/* Exact interval bounds [r_lo, r_hi] of r = 128 - |n - 128| for n in [n_lo, n_hi] */
static inline void
nd_field_ridge_bounds(int32_t n_lo, int32_t n_hi, int32_t *out_lo, int32_t *out_hi)
{
	int32_t ra = 128 - (n_lo >= 128 ? (n_lo - 128) : (128 - n_lo));
	int32_t rb = 128 - (n_hi >= 128 ? (n_hi - 128) : (128 - n_hi));
	int32_t rlo = (ra < rb) ? ra : rb;
	int32_t rhi = (n_lo <= 128 && n_hi >= 128) ? 128 : ((ra > rb) ? ra : rb);
	if (rlo < 0) rlo = 0;
	if (rhi > 128) rhi = 128;
	*out_lo = rlo;
	*out_hi = rhi;
}

/* Exact interval bounds of linear projection proj = (urx*ca + ury*sa) >> 15 over rect */
static inline void
nd_field_proj_rect_bounds(int32_t urx0, int32_t urx1, int32_t ury0, int32_t ury1,
                          uint8_t az, int32_t *out_lo, int32_t *out_hi)
{
	int32_t ca = nd_field_cos_q15((uint16_t)((uint32_t)az << 8));
	int32_t sa = nd_field_cos_q15((uint16_t)(((uint32_t)az + 192u) << 8));
	int64_t xlo = (ca >= 0) ? (int64_t)urx0 * ca : (int64_t)urx1 * ca;
	int64_t xhi = (ca >= 0) ? (int64_t)urx1 * ca : (int64_t)urx0 * ca;
	int64_t ylo = (sa >= 0) ? (int64_t)ury0 * sa : (int64_t)ury1 * sa;
	int64_t yhi = (sa >= 0) ? (int64_t)ury1 * sa : (int64_t)ury0 * sa;
	*out_lo = (int32_t)((xlo + ylo) >> 15) - 1;
	*out_hi = (int32_t)((xhi + yhi) >> 15) + 1;
}

/* Exact interval bounds of nd_field_scarp_wave_q8(proj) over proj in [p_lo, p_hi] */
static inline void
nd_field_scarp_bounds(int32_t p_lo, int32_t p_hi, int32_t *out_lo, int32_t *out_hi)
{
	if (p_hi - p_lo >= 256) {
		*out_lo = 0;
		*out_hi = 256;
		return;
	}
	int32_t vlo = nd_field_scarp_wave_q8(p_lo);
	int32_t vhi = vlo;
	int32_t ve  = nd_field_scarp_wave_q8(p_hi);
	if (ve < vlo) vlo = ve;
	if (ve > vhi) vhi = ve;
	/* Check critical extrema: 0 mod 256 (min=0) and 52 mod 256 (max=256) */
	int32_t k0 = p_lo >> 8;
	for (int32_t k = k0; k <= k0 + 2; k++) {
		int32_t c_min = k << 8;
		int32_t c_max = (k << 8) + 52;
		if (c_min >= p_lo && c_min <= p_hi) vlo = 0;
		if (c_max >= p_lo && c_max <= p_hi) vhi = 256;
	}
	*out_lo = vlo;
	*out_hi = vhi;
}

/* Exact interval bounds of nd_field_dune_wave_q8(proj) over proj in [p_lo, p_hi] */
static inline void
nd_field_dune_bounds(int32_t p_lo, int32_t p_hi, int32_t *out_lo, int32_t *out_hi)
{
	if (p_hi - p_lo >= 64) {
		*out_lo = 0;
		*out_hi = 256;
		return;
	}
	int32_t vlo = nd_field_dune_wave_q8(p_lo);
	int32_t vhi = vlo;
	int32_t ve  = nd_field_dune_wave_q8(p_hi);
	if (ve < vlo) vlo = ve;
	if (ve > vhi) vhi = ve;
	int32_t k0 = p_lo >> 6;
	for (int32_t k = k0; k <= k0 + 2; k++) {
		int32_t c_min = k << 6;
		int32_t c_max = (k << 6) + 40;
		if (c_min >= p_lo && c_min <= p_hi) vlo = 0;
		if (c_max >= p_lo && c_max <= p_hi) vhi = 256;
	}
	*out_lo = vlo;
	*out_hi = vhi;
}

/* Exact interval bounds of nd_field_volcano_cone_q8(dist to corner) over rect [urx0..urx1] x [ury0..ury1] */
static inline void
nd_field_volcano_bounds(int32_t urx0, int32_t urx1, int32_t ury0, int32_t ury1,
                        int32_t corner_rx, int32_t corner_ry,
                        int32_t *out_lo, int32_t *out_hi)
{
	int32_t cx = (corner_rx < urx0) ? urx0 : ((corner_rx > urx1) ? urx1 : corner_rx);
	int32_t cy = (corner_ry < ury0) ? ury0 : ((corner_ry > ury1) ? ury1 : corner_ry);
	int64_t dx_min = (int64_t)(cx - corner_rx);
	int64_t dy_min = (int64_t)(cy - corner_ry);
	int64_t d2_min = dx_min * dx_min + dy_min * dy_min;
	uint32_t d_min = (d2_min >= 57600LL) ? 240u : nd_field_isqrt32((uint32_t)d2_min);

	int64_t ax0 = (int64_t)urx0 - corner_rx; if (ax0 < 0) ax0 = -ax0;
	int64_t ax1 = (int64_t)urx1 - corner_rx; if (ax1 < 0) ax1 = -ax1;
	int64_t ay0 = (int64_t)ury0 - corner_ry; if (ay0 < 0) ay0 = -ay0;
	int64_t ay1 = (int64_t)ury1 - corner_ry; if (ay1 < 0) ay1 = -ay1;
	int64_t dx_max = (ax0 > ax1) ? ax0 : ax1;
	int64_t dy_max = (ay0 > ay1) ? ay0 : ay1;
	int64_t d2_max = dx_max * dx_max + dy_max * dy_max;
	uint32_t d_max = (d2_max >= 57600LL) ? 240u : nd_field_isqrt32((uint32_t)d2_max);

	/* cone_q8(d) increases on [0..36], peaks at d=36, decreases on [36..240] */
	int32_t c_lo = nd_field_volcano_cone_q8(d_min);
	int32_t c_hi = c_lo;
	int32_t c_e  = nd_field_volcano_cone_q8(d_max);
	if (c_e < c_lo) c_lo = c_e;
	if (c_e > c_hi) c_hi = c_e;
	if (d_min <= 36u && d_max >= 36u) {
		int32_t c_pk = nd_field_volcano_cone_q8(36u);
		if (c_pk > c_hi) c_hi = c_pk;
	}
	*out_lo = c_lo;
	*out_hi = c_hi;
}

/* Exact interval bounds [out_lo, out_hi] for one archetype corner over warped rect */
static inline void
nd_field_orogeny_archetype_bounds(uint8_t prov, uint8_t az, int32_t corner_rx, int32_t corner_ry,
                                  int32_t urx0, int32_t urx1, int32_t ury0, int32_t ury1,
                                  int32_t hyp_min, int32_t hyp_max,
                                  int32_t hill_lo, int32_t hill_hi,
                                  int32_t alp_q_lo, int32_t alp_q_hi, int32_t scale_q8,
                                  int32_t hraw_lo, int32_t hraw_hi,
                                  int32_t araw_lo, int32_t araw_hi,
                                  int32_t r9_lo, int32_t r9_hi,
                                  int32_t r8_lo, int32_t r8_hi,
                                  int32_t r7_lo, int32_t r7_hi,
                                  int32_t r5_lo, int32_t r5_hi,
                                  int32_t *out_lo, int32_t *out_hi)
{
	int32_t env_lo = (alp_q_lo > hill_lo) ? alp_q_lo : hill_lo;
	int32_t env_hi = (alp_q_hi > hill_hi) ? alp_q_hi : hill_hi;
	int32_t lo = 0, hi = 0;

	switch (prov) {
	case ND_GEO_ALLUVIAL_PLAIN:
		lo = (((hraw_lo >> 1) * hill_lo) >> 8) + (((araw_lo >> 3) * alp_q_lo) >> 8);
		hi = (((hraw_hi >> 1) * hill_hi) >> 8) + (((araw_hi >> 3) * alp_q_hi) >> 8) + 2;
		break;
	case ND_GEO_PIEDMONT_HILLS:
		lo = (((hraw_lo * 10) * hill_lo) >> 8) + ((((araw_lo * 3) >> 3) * alp_q_lo) >> 8);
		hi = (((hraw_hi * 10) * hill_hi) >> 8) + ((((araw_hi * 3) >> 3) * alp_q_hi) >> 8) + 2;
		break;
	case ND_GEO_ALPINE_CORDILLERA:
		lo = (((hraw_lo * 12) * hill_lo) >> 8) + (int32_t)(((int64_t)araw_lo * alp_q_lo) >> 8);
		hi = (((hraw_hi * 12) * hill_hi) >> 8) + (int32_t)(((int64_t)araw_hi * alp_q_hi) >> 8) + 2;
		break;
	case ND_GEO_CANYON_MESA: {
		int32_t m_lo = (r9_lo * 3 + r7_lo * 2) / 5;
		int32_t m_hi = (r9_hi * 3 + r7_hi * 2) / 5;
		int32_t t_lo = nd_field_terrace_q8(m_lo << 1);
		int32_t t_hi = nd_field_terrace_q8(m_hi << 1);
		int32_t ms = 192 + (scale_q8 >> 2);
		int32_t mc_lo = (int32_t)(((int64_t)t_lo * 38000LL * (int64_t)ms) >> 16);
		int32_t mc_hi = (int32_t)(((int64_t)t_hi * 38000LL * (int64_t)ms) >> 16);
		lo = (int32_t)(((int64_t)(mc_lo + hraw_lo * 6) * env_lo) >> 8) + (((araw_lo >> 3) * alp_q_lo) >> 8);
		hi = (int32_t)(((int64_t)(mc_hi + hraw_hi * 6) * env_hi) >> 8) + (((araw_hi >> 3) * alp_q_hi) >> 8) + 4;
		break;
	}
	case ND_GEO_FAULT_ESCARPMENT: {
		int32_t p_lo, p_hi, s_lo, s_hi;
		nd_field_proj_rect_bounds(urx0, urx1, ury0, ury1, az, &p_lo, &p_hi);
		nd_field_scarp_bounds(p_lo, p_hi, &s_lo, &s_hi);
		int32_t sh_lo = (int32_t)(((int64_t)s_lo * 54000LL * (int64_t)scale_q8) >> 16);
		int32_t sh_hi = (int32_t)(((int64_t)s_hi * 54000LL * (int64_t)scale_q8) >> 16);
		lo = (int32_t)(((int64_t)((sh_lo >> 2) + hraw_lo * 6) * hill_lo) >> 8) +
		     (int32_t)(((int64_t)(((sh_lo * 3) >> 2) + (araw_lo >> 2)) * alp_q_lo) >> 8);
		hi = (int32_t)(((int64_t)((sh_hi >> 2) + hraw_hi * 6) * hill_hi) >> 8) +
		     (int32_t)(((int64_t)(((sh_hi * 3) >> 2) + (araw_hi >> 2)) * alp_q_hi) >> 8) + 4;
		break;
	}
	case ND_GEO_GLACIAL_FJORD: {
		int32_t g_lo = (r9_lo * 5 + r8_lo * 3) >> 3;
		int32_t g_hi = (r9_hi * 5 + r8_hi * 3) >> 3;
		int32_t u_lo = nd_field_smoothstep_q8(g_lo, 46, 98);
		int32_t u_hi = nd_field_smoothstep_q8(g_hi, 46, 98);
		int32_t hh_lo = ((hraw_lo * 7) * u_lo) >> 8;
		int32_t hh_hi = ((hraw_hi * 7) * u_hi) >> 8;
		int32_t ha_lo = (int32_t)(((int64_t)araw_lo * u_lo) >> 8);
		int32_t ha_hi = (int32_t)(((int64_t)araw_hi * u_hi) >> 8);
		lo = ((hh_lo * hill_lo) >> 8) + (int32_t)(((int64_t)ha_lo * alp_q_lo) >> 8);
		hi = ((hh_hi * hill_hi) >> 8) + (int32_t)(((int64_t)ha_hi * alp_q_hi) >> 8) + 4;
		break;
	}
	case ND_GEO_VOLCANIC_CONE: {
		int32_t c_lo, c_hi;
		nd_field_volcano_bounds(urx0, urx1, ury0, ury1, corner_rx, corner_ry, &c_lo, &c_hi);
		int32_t vh_lo = (int32_t)(((int64_t)c_lo * 76000LL * (int64_t)scale_q8) >> 16);
		int32_t vh_hi = (int32_t)(((int64_t)c_hi * 76000LL * (int64_t)scale_q8) >> 16);
		lo = (int32_t)(((int64_t)(vh_lo + hraw_lo * 7) * env_lo) >> 8) + (((araw_lo >> 2) * alp_q_lo) >> 8);
		hi = (int32_t)(((int64_t)(vh_hi + hraw_hi * 7) * env_hi) >> 8) + (((araw_hi >> 2) * alp_q_hi) >> 8) + 4;
		break;
	}
	case ND_GEO_KARST_TOWERS: {
		int32_t k_lo = (r5_lo * r5_lo * r5_lo) >> 14;
		int32_t k_hi = (r5_hi * r5_hi * r5_hi) >> 14;
		lo = (int32_t)(((int64_t)(k_lo * 165 + hraw_lo * 3) * env_lo) >> 8) + (((araw_lo >> 2) * alp_q_lo) >> 8);
		hi = (int32_t)(((int64_t)(k_hi * 165 + hraw_hi * 3) * env_hi) >> 8) + (((araw_hi >> 2) * alp_q_hi) >> 8) + 4;
		break;
	}
	case ND_GEO_ARID_BASIN_ERG: {
		int32_t p_lo, p_hi, d_lo, d_hi;
		nd_field_proj_rect_bounds(urx0, urx1, ury0, ury1, az, &p_lo, &p_hi);
		nd_field_dune_bounds(p_lo, p_hi, &d_lo, &d_hi);
		int32_t dh_lo = (d_lo * 14) >> 1;
		int32_t dh_hi = (d_hi * 14) >> 1;
		lo = (((dh_lo + hraw_lo * 2) * hill_lo) >> 8) + ((((araw_lo * 3) >> 4) * alp_q_lo) >> 8);
		hi = (((dh_hi + hraw_hi * 2) * hill_hi) >> 8) + ((((araw_hi * 3) >> 4) * alp_q_hi) >> 8) + 4;
		break;
	}
	case ND_GEO_SEA_CLIFF_COAST:
	default: {
		int32_t hl_lo = (hyp_min <= 6) ? 0 : nd_field_smoothstep_q8(hyp_min, 6, 20);
		int32_t hl_hi = nd_field_smoothstep_q8(hyp_max, 6, 20);
		int32_t ch_lo = ((hl_lo * 4800) >> 8) + hraw_lo * 8;
		int32_t ch_hi = ((hl_hi * 4800) >> 8) + hraw_hi * 8;
		lo = ((ch_lo * hill_lo) >> 8) + (int32_t)((((int64_t)araw_lo * 5LL >> 3) * alp_q_lo) >> 8);
		hi = ((ch_hi * hill_hi) >> 8) + (int32_t)((((int64_t)araw_hi * 5LL >> 3) * alp_q_hi) >> 8) + 4;
		break;
	}
	}
	if (lo < 0) lo = 0;
	if (hi < lo) hi = lo;
	*out_lo = lo;
	*out_hi = hi;
}

/* Conservative O(1) 2-sided interval bounds [out_min, out_max] on nd_field_orogeny_wu over
 * room rectangle [rx0..rx1] x [ry0..ry1] with base hypsometry in [hyp_min, hyp_max] for ALL step_q16.
 * Exact [0, 0] over ocean and flat plains; razor-tight over fine alpine/foothill chunks. */
static inline void
nd_field_orogeny_bounds_wu(uint16_t world, int32_t rx0, int32_t rx1,
                           int32_t ry0, int32_t ry1,
                           int32_t hyp_min, int32_t hyp_max,
                           int32_t *out_min, int32_t *out_max)
{
	if (hyp_max <= 6) {
		*out_min = 0;
		*out_max = 0;
		return;
	}
	uint8_t pbits = nd_field_get_planet_bits();
	int32_t max_e = nd_field_active_max_elev_wu();
	int32_t scale_q8 = (int32_t)(((int64_t)max_e * 256LL) / 81000LL);
	int32_t coast_lo = (hyp_min <= 6) ? 0 : nd_field_smoothstep_q8(hyp_min, 6, 16);
	int32_t coast_hi = nd_field_smoothstep_q8(hyp_max, 6, 16);
	uint32_t seed = 0x48454947u ^ ((uint32_t)world * 0x9E3779B1u);
	uint32_t oseed = seed ^ ND_FIELD_OROGENY_SALT;

	int32_t wx_lo, wx_hi, wy_lo, wy_hi, mx_lo, mx_hi, my_lo, my_hi;
	nd_field_periodic_rect_bounds(rx0, rx1, ry0, ry1, 8, oseed ^ 0x11A3u, &wx_lo, &wx_hi);
	nd_field_periodic_rect_bounds(rx0, rx1, ry0, ry1, 8, oseed ^ 0x22B7u, &wy_lo, &wy_hi);
	nd_field_periodic_rect_bounds(rx0, rx1, ry0, ry1, 6, oseed ^ 0x33C9u, &mx_lo, &mx_hi);
	nd_field_periodic_rect_bounds(rx0, rx1, ry0, ry1, 6, oseed ^ 0x44DBu, &my_lo, &my_hi);

	int32_t urx0 = rx0 + ((wx_lo - 128) >> 1) + ((mx_lo - 128) >> 3) - 1;
	int32_t urx1 = rx1 + ((wx_hi - 128) >> 1) + ((mx_hi - 128) >> 3) + 1;
	int32_t ury0 = ry0 + ((wy_lo - 128) >> 1) + ((my_lo - 128) >> 3) - 1;
	int32_t ury1 = ry1 + ((wy_hi - 128) >> 1) + ((my_hi - 128) >> 3) + 1;

	int32_t prov_lo, prov_hi, nb_lo, nb_hi;
	nd_field_periodic_rect_bounds(urx0, urx1, ury0, ury1, 8, oseed ^ 0x55EDu, &prov_lo, &prov_hi);
	nd_field_periodic_rect_bounds(urx0, urx1, ury0, ury1, 7, oseed ^ 0x661Fu, &nb_lo, &nb_hi);

	int32_t r8_lo, r8_hi, belt_lo, belt_hi;
	nd_field_ridge_bounds(prov_lo, prov_hi, &r8_lo, &r8_hi);
	nd_field_ridge_bounds(nb_lo, nb_hi, &belt_lo, &belt_hi);

	int32_t h_sig_lo = (prov_lo * 3 + belt_lo * 5) >> 3;
	int32_t h_sig_hi = (prov_hi * 3 + belt_hi * 5) >> 3;
	int32_t hill_lo = nd_field_smoothstep_q8(h_sig_lo, 96, 150);
	int32_t hill_hi = nd_field_smoothstep_q8(h_sig_hi, 96, 150);

	int32_t a_sig_lo = (belt_lo * (144 + prov_lo)) >> 8;
	if (hyp_min > 80) {
		int32_t cb = (hyp_min - 80) >> 3;
		if (cb > 28) cb = 28;
		a_sig_lo += cb;
	}
	int32_t a_sig_hi = (belt_hi * (144 + prov_hi)) >> 8;
	if (hyp_max > 80) {
		int32_t cb = (hyp_max - 80) >> 3;
		if (cb > 28) cb = 28;
		a_sig_hi += cb;
	}
	int32_t a_sm_lo = nd_field_smoothstep_q8(a_sig_lo, 78, 146);
	int32_t a_sm_hi = nd_field_smoothstep_q8(a_sig_hi, 78, 146);
	int32_t alpine_lo = (a_sm_lo * (128 + (a_sm_lo >> 1))) >> 8;
	int32_t alpine_hi = (a_sm_hi * (128 + (a_sm_hi >> 1))) >> 8;

	if (hill_hi == 0 && alpine_hi == 0) {
		*out_min = 0;
		*out_max = 0;
		return;
	}

	int32_t n9_lo, n9_hi, r9_lo, r9_hi;
	nd_field_periodic_rect_bounds(urx0 + 313, urx1 + 313, ury0 - 571, ury1 - 571, 9, oseed ^ 0x4E3Bu, &n9_lo, &n9_hi);
	nd_field_ridge_bounds(n9_lo, n9_hi, &r9_lo, &r9_hi);

	int32_t f5n_lo, f5n_hi, r5k_lo, r5k_hi;
	nd_field_periodic_rect_bounds(urx0 + 713, urx1 + 713, ury0 - 419, ury1 - 419, 5, oseed ^ 0x7731u, &f5n_lo, &f5n_hi);
	nd_field_ridge_bounds(f5n_lo, f5n_hi, &r5k_lo, &r5k_hi);

	int32_t hraw_lo = 0, hraw_hi = 0;
	if (hill_hi > 0) {
		int32_t f5_lo = (f5n_lo * 192 + r5k_lo * 528) >> 8;
		int32_t f5_hi = (f5n_hi * 192 + r5k_hi * 528) >> 8;
		int32_t f4n_lo, f4n_hi;
		nd_field_periodic_rect_bounds(urx0 - 317, urx1 - 317, ury0 + 823, ury1 + 823, 4, oseed ^ 0x8843u, &f4n_lo, &f4n_hi);
		/* Conservative for all step_q16: w4_oct can be 0 for lower bound and 1 for upper bound */
		hraw_lo = f5_lo;
		hraw_hi = f5_hi + ((f4n_hi * 240) >> 8) + 2;
	}

	int32_t araw_lo = 0, araw_hi = 0;
	if (alpine_hi > 0) {
		int32_t n10_lo, n10_hi, r10_lo, r10_hi;
		nd_field_periodic_rect_bounds(urx0 - 829, urx1 - 829, ury0 + 467, ury1 + 467, 10, oseed ^ 0x3D29u, &n10_lo, &n10_hi);
		nd_field_ridge_bounds(n10_lo, n10_hi, &r10_lo, &r10_hi);

		int32_t n6_lo, n6_hi, r6_lo, r6_hi;
		nd_field_periodic_rect_bounds(urx0 + 191, urx1 + 191, ury0 + 547, ury1 + 547, 6, oseed ^ 0x9955u, &n6_lo, &n6_hi);
		nd_field_ridge_bounds(n6_lo, n6_hi, &r6_lo, &r6_hi);

		int32_t n5_lo, n5_hi, r5a_lo, r5a_hi;
		nd_field_periodic_rect_bounds(urx0 - 613, urx1 - 613, ury0 + 271, ury1 + 271, 5, oseed ^ 0xAA67u, &n5_lo, &n5_hi);
		nd_field_ridge_bounds(n5_lo, n5_hi, &r5a_lo, &r5a_hi);

		int32_t n4_lo, n4_hi, r4_lo, r4_hi;
		nd_field_periodic_rect_bounds(urx0 + 409, urx1 + 409, ury0 - 733, ury1 - 733, 4, oseed ^ 0xBB79u, &n4_lo, &n4_hi);
		nd_field_ridge_bounds(n4_lo, n4_hi, &r4_lo, &r4_hi);

		int32_t h10_lo = (r10_lo * r10_lo) >> 7, h10_hi = (r10_hi * r10_hi) >> 7;
		int32_t h9_lo  = (r9_lo * (64 + (r9_lo >> 1))) >> 7, h9_hi = (r9_hi * (64 + (r9_hi >> 1))) >> 7;
		int32_t w9_lo  = 96 + ((h10_lo * 160) >> 7), w9_hi = 96 + ((h10_hi * 160) >> 7);
		int32_t h9w_lo = (h9_lo * w9_lo) >> 8, h9w_hi = (h9_hi * w9_hi) >> 8;

		int32_t h8_lo  = (r8_lo * (64 + (r8_lo >> 1))) >> 7, h8_hi = (r8_hi * (64 + (r8_hi >> 1))) >> 7;
		int32_t w8_lo  = 84 + ((h9w_lo * 172) >> 7), w8_hi = 84 + ((h9w_hi * 172) >> 7);
		int32_t h8w_lo = (h8_lo * w8_lo) >> 8, h8w_hi = (h8_hi * w8_hi) >> 8;

		int32_t h7_lo  = (belt_lo * (64 + (belt_lo >> 1))) >> 7, h7_hi = (belt_hi * (64 + (belt_hi >> 1))) >> 7;
		int32_t w7_lo  = 76 + ((h8w_lo * 180) >> 7), w7_hi = 76 + ((h8w_hi * 180) >> 7);
		int32_t h7w_lo = (h7_lo * w7_lo) >> 8, h7w_hi = (h7_hi * w7_hi) >> 8;

		int32_t h6_lo  = (r6_lo * r6_lo) >> 7, h6_hi = (r6_hi * r6_hi) >> 7;
		int32_t w6_lo  = 72 + ((h7w_lo * 184) >> 7), w6_hi = 72 + ((h7w_hi * 184) >> 7);
		int32_t h6w_lo = (h6_lo * w6_lo) >> 8, h6w_hi = (h6_hi * w6_hi) >> 8;

		int32_t h5_lo  = (r5a_lo * (64 + (r5a_lo >> 1))) >> 7, h5_hi = (r5a_hi * (64 + (r5a_hi >> 1))) >> 7;
		int32_t w5_lo  = 64 + ((h6w_lo * 192) >> 7), w5_hi = 64 + ((h6w_hi * 192) >> 7);
		int32_t h5w_lo = (h5_lo * w5_lo) >> 8, h5w_hi = (h5_hi * w5_hi) >> 8;

		int32_t h4_hi  = (r4_hi * (64 + (r4_hi >> 1))) >> 7;
		int32_t w4_hi  = 56 + ((h5w_hi * 200) >> 7);
		int32_t h4w_hi = (h4_hi * w4_hi) >> 8;

		int32_t w3_hi  = 32 + ((h4w_hi * 224) >> 7);
		int32_t h3w_hi = (128 * w3_hi) >> 8;
		int32_t w2_hi  = (h3w_hi * 256) >> 7;
		int32_t h2w_hi = (128 * w2_hi) >> 8;

		/* Lower bound omits step_q16-attenuated octaves (4, 3, 2) so it is valid for ALL LODs */
		araw_lo = (int32_t)(((int64_t)h10_lo * 33000LL) >> 7) +
		          (int32_t)(((int64_t)h9w_lo * 26000LL) >> 7) +
		          (int32_t)(((int64_t)h8w_lo * 16500LL) >> 7) +
		          (int32_t)(((int64_t)h7w_lo * 10000LL) >> 7) +
		          (int32_t)(((int64_t)h6w_lo *  5200LL) >> 7) +
		          (int32_t)(((int64_t)h5w_lo *  2600LL) >> 7);

		araw_hi = (int32_t)(((int64_t)h10_hi * 33000LL) >> 7) +
		          (int32_t)(((int64_t)h9w_hi * 26000LL) >> 7) +
		          (int32_t)(((int64_t)h8w_hi * 16500LL) >> 7) +
		          (int32_t)(((int64_t)h7w_hi * 10000LL) >> 7) +
		          (int32_t)(((int64_t)h6w_hi *  5200LL) >> 7) +
		          (int32_t)(((int64_t)h5w_hi *  2600LL) >> 7) +
		          ((h4w_hi * 1300) >> 7) + ((h3w_hi * 650) >> 7) + ((h2w_hi * 350) >> 7) + 12;

		if (pbits >= 18u && max_e > 81000) {
			int64_t extra_wu = (int64_t)(max_e - 81000);
			int32_t n11_lo, n11_hi, r11_lo, r11_hi;
			nd_field_periodic_rect_bounds(urx0 + 1423, urx1 + 1423, ury0 - 977, ury1 - 977, 11, oseed ^ 0x5E11u, &n11_lo, &n11_hi);
			nd_field_ridge_bounds(n11_lo, n11_hi, &r11_lo, &r11_hi);
			int32_t h11_lo = (r11_lo * r11_lo) >> 7, h11_hi = (r11_hi * r11_hi) >> 7;
			if (pbits < 20u) {
				araw_lo += (int32_t)(((int64_t)h11_lo * extra_wu * 115LL) / (128LL * 100LL));
				araw_hi += (int32_t)(((int64_t)h11_hi * extra_wu * 115LL) / (128LL * 100LL)) + 2;
			} else if (pbits < 22u) {
				int32_t n12_lo, n12_hi, r12_lo, r12_hi;
				nd_field_periodic_rect_bounds(urx0 - 2113, urx1 - 2113, ury0 + 1657, ury1 + 1657, 12, oseed ^ 0x6F23u, &n12_lo, &n12_hi);
				nd_field_ridge_bounds(n12_lo, n12_hi, &r12_lo, &r12_hi);
				int32_t h12_lo = (r12_lo * r12_lo) >> 7, h12_hi = (r12_hi * r12_hi) >> 7;
				araw_lo += (int32_t)(((int64_t)h11_lo * extra_wu * 60LL + (int64_t)h12_lo * extra_wu * 55LL) / (128LL * 100LL));
				araw_hi += (int32_t)(((int64_t)h11_hi * extra_wu * 60LL + (int64_t)h12_hi * extra_wu * 55LL) / (128LL * 100LL)) + 4;
			} else {
				int32_t n12_lo, n12_hi, r12_lo, r12_hi, n13_lo, n13_hi, r13_lo, r13_hi;
				nd_field_periodic_rect_bounds(urx0 - 2113, urx1 - 2113, ury0 + 1657, ury1 + 1657, 12, oseed ^ 0x6F23u, &n12_lo, &n12_hi);
				nd_field_ridge_bounds(n12_lo, n12_hi, &r12_lo, &r12_hi);
				nd_field_periodic_rect_bounds(urx0 + 3571, urx1 + 3571, ury0 + 2909, ury1 + 2909, 13, oseed ^ 0x7A35u, &n13_lo, &n13_hi);
				nd_field_ridge_bounds(n13_lo, n13_hi, &r13_lo, &r13_hi);
				int32_t h12_lo = (r12_lo * r12_lo) >> 7, h12_hi = (r12_hi * r12_hi) >> 7;
				int32_t h13_lo = (r13_lo * r13_lo) >> 7, h13_hi = (r13_hi * r13_hi) >> 7;
				araw_lo += (int32_t)(((int64_t)h11_lo * extra_wu * 40LL + (int64_t)h12_lo * extra_wu * 40LL + (int64_t)h13_lo * extra_wu * 35LL) / (128LL * 100LL));
				araw_hi += (int32_t)(((int64_t)h11_hi * extra_wu * 40LL + (int64_t)h12_hi * extra_wu * 40LL + (int64_t)h13_hi * extra_wu * 35LL) / (128LL * 100LL)) + 6;
			}
		}
	}

	int32_t cx0 = urx0 >> 9, cx1 = urx1 >> 9;
	int32_t cy0 = ury0 >> 9, cy1 = ury1 >> 9;

	int32_t blend_lo = 0, blend_hi = max_e;
	if (cx0 == cx1 && cy0 == cy1) {
		/* Fast tight path (>99% of fine chunks): warped rect lies inside a single 512-room cell */
		int32_t u_lo = nd_field_quintic512_q8(urx0 & 511);
		int32_t u_hi = nd_field_quintic512_q8(urx1 & 511);
		int32_t v_lo = nd_field_quintic512_q8(ury0 & 511);
		int32_t v_hi = nd_field_quintic512_q8(ury1 & 511);

		/* Exact lower bounds on bilinear weights w00, w10, w01, w11 (minus 1 for >>8 rounding) */
		int32_t wlo[4];
		wlo[0] = (((256 - u_hi) * (256 - v_hi)) >> 8) - 1;
		wlo[1] = ((u_lo * (256 - v_hi)) >> 8) - 1;
		wlo[2] = (((256 - u_hi) * v_lo) >> 8) - 1;
		wlo[3] = ((u_lo * v_lo) >> 8) - 1;
		int32_t wlo_sum = 0;
		for (int k = 0; k < 4; k++) {
			if (wlo[k] < 0) wlo[k] = 0;
			wlo_sum += wlo[k];
		}
		if (wlo_sum > 256) wlo_sum = 256;
		int32_t rem = 256 - wlo_sum;

		const int32_t cxs[4] = { cx0, cx0 + 1, cx0,     cx0 + 1 };
		const int32_t cys[4] = { cy0, cy0,     cy0 + 1, cy0 + 1 };
		int32_t clo[4], chi[4];
		int32_t m_lo = 0x7FFFFFFF, m_hi = 0;
		for (int k = 0; k < 4; k++) {
			uint8_t prov, az;
			nd_field_geo_corner(oseed, pbits, cxs[k], cys[k], &prov, &az);
			nd_field_orogeny_archetype_bounds(
				prov, az, cxs[k] << 9, cys[k] << 9, urx0, urx1, ury0, ury1,
				hyp_min, hyp_max, hill_lo, hill_hi, alpine_lo, alpine_hi, scale_q8,
				hraw_lo, hraw_hi, araw_lo, araw_hi,
				r9_lo, r9_hi, r8_lo, r8_hi, belt_lo, belt_hi, r5k_lo, r5k_hi,
				&clo[k], &chi[k]);
			if (clo[k] < m_lo) m_lo = clo[k];
			if (chi[k] > m_hi) m_hi = chi[k];
		}

		int64_t acc_lo = (int64_t)rem * (int64_t)m_lo;
		int64_t acc_hi = (int64_t)rem * (int64_t)m_hi;
		for (int k = 0; k < 4; k++) {
			acc_lo += (int64_t)wlo[k] * (int64_t)clo[k];
			acc_hi += (int64_t)wlo[k] * (int64_t)chi[k];
		}
		blend_lo = (int32_t)(acc_lo >> 8);
		blend_hi = (int32_t)(acc_hi >> 8) + 4;
	} else if ((cx1 - cx0) <= 2 && (cy1 - cy0) <= 2) {
		int32_t m_lo = 0x7FFFFFFF, m_hi = 0;
		for (int32_t cy = cy0; cy <= cy1 + 1; cy++) {
			for (int32_t cx = cx0; cx <= cx1 + 1; cx++) {
				uint8_t prov, az;
				int32_t clo, chi;
				nd_field_geo_corner(oseed, pbits, cx, cy, &prov, &az);
				nd_field_orogeny_archetype_bounds(
					prov, az, cx << 9, cy << 9, urx0, urx1, ury0, ury1,
					hyp_min, hyp_max, hill_lo, hill_hi, alpine_lo, alpine_hi, scale_q8,
					hraw_lo, hraw_hi, araw_lo, araw_hi,
					r9_lo, r9_hi, r8_lo, r8_hi, belt_lo, belt_hi, r5k_lo, r5k_hi,
					&clo, &chi);
				if (clo < m_lo) m_lo = clo;
				if (chi > m_hi) m_hi = chi;
			}
		}
		blend_lo = (m_lo < 0x7FFFFFFF) ? m_lo : 0;
		blend_hi = m_hi + 4;
	} else {
		/* Wide macro patch spanning >2 cells: bound across worst-case of all 10 archetypes */
		blend_lo = 0;
		int32_t alp_max = (((hraw_hi * 12) * hill_hi) >> 8) + (int32_t)(((int64_t)araw_hi * alpine_hi) >> 8);
		int32_t env_hi  = (alpine_hi > hill_hi) ? alpine_hi : hill_hi;
		int32_t volc_max = (int32_t)((((int64_t)76000LL * scale_q8 >> 8) + hraw_hi * 7) * env_hi >> 8) +
		                   (((araw_hi >> 2) * alpine_hi) >> 8);
		blend_hi = (alp_max > volc_max ? alp_max : volc_max) + 16;
	}

	int32_t out_lo = (int32_t)(((int64_t)blend_lo * (int64_t)coast_lo) >> 8);
	int32_t out_hi = (int32_t)(((int64_t)blend_hi * (int64_t)coast_hi) >> 8) + ((blend_hi > 0) ? 4 : 0);
	if (out_lo < 0) out_lo = 0;
	if (out_hi > max_e) out_hi = max_e;
	if (out_lo > out_hi) out_lo = out_hi;
	*out_min = out_lo;
	*out_max = out_hi;
}

/* Backwards-compatible upper-bound wrapper */
static inline int32_t
nd_field_orogeny_upper_bound_wu(uint16_t world, int32_t rx0, int32_t rx1,
                                int32_t ry0, int32_t ry1, int32_t hyp_max)
{
	int32_t lo = 0, hi = 0;
	nd_field_orogeny_bounds_wu(world, rx0, rx1, ry0, ry1, 0, hyp_max, &lo, &hi);
	return hi;
}

/* Conservative O(1) analytical elevation bounds [out_z_min, out_z_max] (in world units)
 * over the continuous world-space rectangle [ox, ox + w] x [oy, oy + h] for ALL LODs.
 * Evaluates coarse elevation once at the patch center room, adds exact C2 quintic
 * Lipschitz bounds across coarse octaves (shifts 15..2), maps through the strictly
 * monotonic bimodal hypsometry curve, adds tight 2-sided tectonic orogeny interval bounds,
 * and adds the fine amplitude & hydrology budgets. Guaranteed conservative (zero false
 * negatives) while keeping tight vertical bounding boxes over oceans, plains, and mountains. */
static inline void
nd_field_elevation_bounds(uint16_t world, int32_t ox, int32_t oy, int32_t w, int32_t h,
                          float *out_z_min, float *out_z_max)
{
	const int32_t max_e = nd_field_active_max_elev_wu();
	const float global_min = -2150.0f;
	const float global_max =  nd_field_active_global_max_z();

	if (w < 0) { ox += w; w = -w; }
	if (h < 0) { oy += h; h = -h; }

	int32_t rw = nd_field_get_room_w();
	int32_t rh = nd_field_get_room_h();
	if (rw <= 0) rw = ND_FIELD_DEFAULT_ROOM_W;
	if (rh <= 0) rh = ND_FIELD_DEFAULT_ROOM_H;

	uint8_t pbits = nd_field_get_planet_bits();
	uint32_t span = 1u << pbits;
	uint32_t mask = span - 1u;
	int32_t seam_start = (int32_t)(span >> 1) - 256;

	/* Room stencil touched by nd_field_interp_coarse_q12_4 over [ox..ox+w] x [oy..oy+h] */
	int64_t rx0 = nd_field_fdiv64((int64_t)ox - (int64_t)(rw / 2), (int64_t)rw);
	int64_t rx1 = nd_field_fdiv64((int64_t)ox + (int64_t)w - (int64_t)(rw / 2), (int64_t)rw) + 1;
	int64_t ry0 = nd_field_fdiv64((int64_t)(rh / 2) - ((int64_t)oy + (int64_t)h), (int64_t)rh);
	int64_t ry1 = nd_field_fdiv64((int64_t)(rh / 2) - (int64_t)oy, (int64_t)rh) + 1;

	int64_t span_rx = rx1 - rx0;
	int64_t span_ry = ry1 - ry0;

	int32_t hyp_min = 0, hyp_max = 0;

	/* Exact integer-room convex-hull path for fine patches (<= 8x8 rooms, including seam patches).
	 * nd_field_interp_coarse_q12_4 is a convex combination of integer corners in [rx0..rx1]x[ry0..ry1],
	 * and nd_field_orogeny_wu is monotonically non-decreasing in base_hyp and in step_q16 octave weights,
	 * so sampling [c_wu-5, step=INF] vs [c_wu+5, step=0] at each integer room is 100% conservative for
	 * ALL LODs while eliminating seam slab fallback and interval-arithmetic inflation. */
	if (span_rx <= 10 && span_ry <= 10) {
		uint32_t seed = 0x48454947u ^ ((uint32_t)world * 0x9E3779B1u);
		int32_t hmin = 0x7FFFFFFF;
		int32_t hmax = -0x7FFFFFFF;
		for (int64_t ry = ry0; ry <= ry1; ry++) {
			int32_t wry = nd_field_wrap_room_coord((int32_t)ry, pbits);
			int32_t r_wy = -wry * ND_FIELD_ROOM_H + (ND_FIELD_ROOM_H / 2);
			for (int64_t rx = rx0; rx <= rx1; rx++) {
				int32_t wrx = nd_field_wrap_room_coord((int32_t)rx, pbits);
				int32_t r_wx = wrx * ND_FIELD_ROOM_W + (ND_FIELD_ROOM_W / 2);
				int32_t c_q4 = 0;
				nd_field_elevation_q12_4(world, 0, r_wx, r_wy, &c_q4, NULL);
				int32_t c_wu = c_q4 >> 4;
				int32_t h_lo = nd_field_bimodal_hypsometry(c_wu - 5);
				int32_t h_hi = nd_field_bimodal_hypsometry(c_wu + 5);
				if (h_lo > 6)
					h_lo += nd_field_orogeny_wu(seed, 0x7FFFFFFF, wrx, wry, h_lo);
				if (h_hi > 6)
					h_hi += nd_field_orogeny_wu(seed, 0, wrx, wry, h_hi);
				if (h_lo > max_e) h_lo = max_e;
				if (h_hi > max_e) h_hi = max_e;
				if (h_lo < hmin) hmin = h_lo;
				if (h_hi > hmax) hmax = h_hi;
			}
		}
		hyp_min = hmin;
		hyp_max = hmax;
	} else if (span_rx >= 8192 || span_ry >= 8192 ||
	           (((uint32_t)((int32_t)rx0 - seam_start)) & mask) < 512u ||
	           (((uint32_t)(seam_start - (int32_t)rx0)) & mask) <= (uint32_t)span_rx ||
	           (((uint32_t)((int32_t)ry0 - seam_start)) & mask) < 512u ||
	           (((uint32_t)(seam_start - (int32_t)ry0)) & mask) <= (uint32_t)span_ry) {
		/* Very large planetary quadrants or macro patches straddling the torus wrap seam
		 * fall back to the exact global envelope. */
		if (out_z_min) *out_z_min = global_min;
		if (out_z_max) *out_z_max = global_max;
		return;
	} else {
		int32_t rx_mid = nd_field_wrap_room_coord((int32_t)(rx0 + (span_rx >> 1)), pbits);
		int32_t ry_mid = nd_field_wrap_room_coord((int32_t)(ry0 + (span_ry >> 1)), pbits);
		int32_t r_wx = rx_mid * ND_FIELD_ROOM_W + (ND_FIELD_ROOM_W / 2);
		int32_t r_wy = -ry_mid * ND_FIELD_ROOM_H + (ND_FIELD_ROOM_H / 2);
		int32_t c_mid_q12_4 = 0;
		nd_field_elevation_q12_4(world, 0, r_wx, r_wy, &c_mid_q12_4, NULL);

		double hx = 0.5 * (double)span_rx + 1.5;
		double hy = 0.5 * (double)span_ry + 1.5;
		double r_rooms = sqrt(hx * hx + hy * hy);
		double l1_max = 1.4142135623730951 * r_rooms;
		int32_t r_guard = (int32_t)ceil(r_rooms + 4.0);

		/* Track rotated coordinates to detect any int16 wrap in regional octaves (shifts 8..2).
		 * C2 quintic value noise on spacing 2^s has max derivative (15/8) * 255 / 128 = 3.7354 * amp / 2^s
		 * along each axis; 3.76 provides exact rounding headroom. */
		int32_t cur_rx = rx_mid;
		int32_t cur_ry = ry_mid;
		double delta_wu = 5.0; /* +4 wu for step_q16 attenuation of s=2..4, +1 wu for >> 4 truncation */

		for (uint8_t s = ND_FIELD_SHIFT_PLANET_MAX; s >= ND_FIELD_SHIFT_REGIONAL_MIN; s--) {
			double amp = (double)chaff_octave_amplitude(s);
			double max_span = 2.0 * amp;
			double lip = (3.76 * amp * l1_max) / (double)(1u << s);
			if (s <= 8u) {
				int32_t ax = (int32_t)(int16_t)cur_rx;
				int32_t ay = (int32_t)(int16_t)cur_ry;
				if (ax < 0) ax = -ax;
				if (ay < 0) ay = -ay;
				if (ax + r_guard >= 32760 || ay + r_guard >= 32760)
					lip = max_span;
			}
			delta_wu += (lip < max_span) ? lip : max_span;
			chaff_rot2d_i32_q15(&cur_rx, &cur_ry, CHAFF_ROT_ATAN_COS_Q15, CHAFF_ROT_ATAN_SIN_Q15);
		}

		int32_t c_mid_wu = c_mid_q12_4 >> 4;
		int32_t d_wu = (int32_t)ceil(delta_wu) + 2;
		int32_t c_lo = c_mid_wu - d_wu;
		int32_t c_hi = c_mid_wu + d_wu;
		if (c_lo < -3000) c_lo = -3000;
		if (c_hi >  3000) c_hi =  3000;

		hyp_min = nd_field_bimodal_hypsometry(c_lo);
		hyp_max = nd_field_bimodal_hypsometry(c_hi);
		if (hyp_max > 6) {
			int32_t orog_min = 0, orog_max = 0;
			nd_field_orogeny_bounds_wu(world, (int32_t)rx0, (int32_t)rx1,
			                           (int32_t)ry0, (int32_t)ry1,
			                           hyp_min, hyp_max, &orog_min, &orog_max);
			int32_t h_lo = hyp_min + orog_min;
			int32_t h_hi = hyp_max + orog_max;
			if (h_lo > max_e) h_lo = max_e;
			if (h_hi > max_e) h_hi = max_e;
			hyp_min = h_lo;
			hyp_max = h_hi;
		}
	}
	int32_t sea_lvl = nd_field_water_level(world);

	/* Fine detail is strictly clamped to +/-ND_FIELD_FINE_AMP_BUDGET (127 wu), and halved
	 * below sea_lvl - 8. Mountain lakes & rivers only carve downward (never raise coarse). */
	float fine_max = (hyp_max <= sea_lvl - 8) ? 64.0f : (float)ND_FIELD_FINE_AMP_BUDGET;
	float carve_pad = (hyp_min > sea_lvl - 16) ? 64.0f : 2.0f;

	float z_min = (float)hyp_min - fine_max - carve_pad;
	float z_max = (float)hyp_max + fine_max + 2.0f;
	if (z_min < global_min) z_min = global_min;
	if (z_max > global_max) z_max = global_max;
	if (out_z_min) *out_z_min = z_min;
	if (out_z_max) *out_z_max = z_max;
}

typedef struct {
	uint32_t epoch;
	uint16_t world;
	int32_t  rx;
	int32_t  ry;
	int32_t  hyp;
	uint32_t cl_macro;
	uint32_t tm_macro;
	uint32_t rain_memo;
	int16_t  bio_temp_memo;
	uint8_t  hyp_valid;
	uint8_t  macro_valid;
	uint8_t  climate_valid;
} nd_field_room_memo_t;

#ifdef ND_FIELD_MEMO_EXTERN
extern _CHAFF_TLS nd_field_room_memo_t g_nd_field_room_memo[4096];
#endif

static inline nd_field_room_memo_t *
nd_field_room_memo_table(void)
{
#ifdef ND_FIELD_MEMO_EXTERN
	return g_nd_field_room_memo;
#else
	static _CHAFF_TLS nd_field_room_memo_t s_memo[4096];
	return s_memo;
#endif
}

static inline nd_field_room_memo_t *
nd_field_room_memo_slot(uint16_t world, int32_t rx, int32_t ry)
{
	nd_field_room_memo_t *s_memo = nd_field_room_memo_table();
	uint8_t pbits = nd_field_get_planet_bits();
	uint32_t epoch = nd_field_get_planet_epoch();
	rx = nd_field_wrap_room_coord(rx, pbits);
	ry = nd_field_wrap_room_coord(ry, pbits);
	uint32_t idx = (((uint32_t)rx * 0x9E3779B1u) ^
	                ((uint32_t)ry * 0x85EBCA77u) ^
	                (uint32_t)world) & 4095u;
	nd_field_room_memo_t *m = &s_memo[idx];
	if (m->epoch != epoch || m->world != world || m->rx != rx || m->ry != ry) {
		m->epoch = epoch;
		m->world = world;
		m->rx = rx;
		m->ry = ry;
		m->hyp_valid = 0;
		m->macro_valid = 0;
		m->climate_valid = 0;
	}
	return m;
}

static inline int32_t
nd_field_room_hyp_coarse(uint16_t world, int32_t rx, int32_t ry)
{
	uint8_t pbits = nd_field_get_planet_bits();
	rx = nd_field_wrap_room_coord(rx, pbits);
	ry = nd_field_wrap_room_coord(ry, pbits);
	nd_field_room_memo_t *m = nd_field_room_memo_slot(world, rx, ry);
	if (!m->hyp_valid) {
		uint32_t seed = 0x48454947u ^ ((uint32_t)world * 0x9E3779B1u);
		int32_t r_wx = rx * ND_FIELD_ROOM_W + (ND_FIELD_ROOM_W / 2);
		int32_t r_wy = -ry * ND_FIELD_ROOM_H + (ND_FIELD_ROOM_H / 2);
		int32_t c = 0;
		nd_field_elevation_q12_4(world, 0, r_wx, r_wy, &c, NULL);
		int32_t base_hyp = nd_field_bimodal_hypsometry(c >> 4);
		int32_t max_e = nd_field_active_max_elev_wu();
		int32_t tot = base_hyp + nd_field_orogeny_wu(seed, 0, rx, ry, base_hyp);
		if (tot > max_e) tot = max_e;
		m->hyp = tot;
		m->hyp_valid = 1;
	}
	return m->hyp;
}

static inline int32_t
nd_field_room_hyp_coarse_step(uint16_t world, int32_t step_q16, int32_t rx, int32_t ry)
{
	if (step_q16 < (2048 << 16))
		return nd_field_room_hyp_coarse(world, rx, ry);
	uint8_t pbits = nd_field_get_planet_bits();
	rx = nd_field_wrap_room_coord(rx, pbits);
	ry = nd_field_wrap_room_coord(ry, pbits);
	uint32_t seed = 0x48454947u ^ ((uint32_t)world * 0x9E3779B1u);
	int32_t r_wx = rx * ND_FIELD_ROOM_W + (ND_FIELD_ROOM_W / 2);
	int32_t r_wy = -ry * ND_FIELD_ROOM_H + (ND_FIELD_ROOM_H / 2);
	int32_t c = 0;
	nd_field_elevation_step_q12_4(world, step_q16, r_wx, r_wy, &c, NULL);
	int32_t base_hyp = nd_field_bimodal_hypsometry(c >> 4);
	int32_t room_step_q16 = step_q16 / ND_FIELD_ROOM_W;
	int32_t max_e = nd_field_active_max_elev_wu();
	int32_t tot = base_hyp + nd_field_orogeny_wu(seed, room_step_q16, rx, ry, base_hyp);
	if (tot > max_e) tot = max_e;
	return tot;
}

/* 4-wide SIMD batch room hypsometry evaluator.
 * Populates missed slots in one 4-wide SIMD pass (falling back to scalar on torus seam).
 * Bit-exact with 4 calls to nd_field_room_hyp_coarse_step(world, step_q16, rx[i], ry[i]). */
static inline void
nd_field_room_hyp_coarse_4x(uint16_t world, int32_t step_q16,
                            const int32_t rx[4], const int32_t ry[4],
                            int32_t out[4])
{
	uint8_t pbits = nd_field_get_planet_bits();
	uint32_t epoch = nd_field_get_planet_epoch();
	uint32_t span = 1u << pbits;
	uint32_t mask = span - 1u;
	int32_t seam_start = (int32_t)(span >> 1) - 256;
	int32_t wrx[4], wry[4];
	for (int i = 0; i < 4; i++) {
		wrx[i] = nd_field_wrap_room_coord(rx[i], pbits);
		wry[i] = nd_field_wrap_room_coord(ry[i], pbits);
	}

	const int use_memo = (step_q16 < (2048 << 16));
	int miss_lanes[4];
	int nmiss = 0;
	nd_field_room_memo_t *s_memo = use_memo ? nd_field_room_memo_table() : NULL;

	if (use_memo) {
		for (int i = 0; i < 4; i++) {
			uint32_t idx = (((uint32_t)wrx[i] * 0x9E3779B1u) ^
			                ((uint32_t)wry[i] * 0x85EBCA77u) ^
			                (uint32_t)world) & 4095u;
			nd_field_room_memo_t *m = &s_memo[idx];
			if (m->epoch == epoch && m->world == world && m->rx == wrx[i] && m->ry == wry[i] && m->hyp_valid) {
				out[i] = m->hyp;
			} else {
				miss_lanes[nmiss++] = i;
			}
		}
		if (nmiss == 0)
			return;
		if (nmiss == 1) {
			int k = miss_lanes[0];
			out[k] = nd_field_room_hyp_coarse(world, wrx[k], wry[k]);
			return;
		}
	} else {
		miss_lanes[0] = 0; miss_lanes[1] = 1; miss_lanes[2] = 2; miss_lanes[3] = 3;
		nmiss = 4;
	}

	int has_seam = 0;
	for (int j = 0; j < nmiss; j++) {
		int k = miss_lanes[j];
		if ((((uint32_t)(wrx[k] - seam_start)) & mask) < 512u ||
		    (((uint32_t)(wry[k] - seam_start)) & mask) < 512u) {
			has_seam = 1;
			break;
		}
	}
	if (has_seam) {
		for (int j = 0; j < nmiss; j++) {
			int k = miss_lanes[j];
			out[k] = nd_field_room_hyp_coarse_step(world, step_q16, wrx[k], wry[k]);
		}
		return;
	}

	uint32_t seed = 0x48454947u ^ ((uint32_t)world * 0x9E3779B1u);
	int32_t room_step_q16 = use_memo ? 0 : (step_q16 / ND_FIELD_ROOM_W);
	int32_t sea_off = nd_field_sea_offset_q12_4(world, seed);

	int32_t irx[4], iry[4], c4[4];
	for (int j = 0; j < 4; j++) {
		int k = miss_lanes[j < nmiss ? j : 0];
		irx[j] = wrx[k];
		iry[j] = wry[k];
	}
	nd_field_eval_coarse_unwrapped_step_q12_4_4x(seed, room_step_q16, irx, iry, c4);

	int32_t base4[4], orog4[4];
	for (int j = 0; j < 4; j++) {
		int32_t c = c4[j] - sea_off;
		base4[j] = nd_field_bimodal_hypsometry(c >> 4);
	}
	nd_field_orogeny_wu_4x(seed, room_step_q16, irx, iry, base4, orog4);

	int32_t max_e = nd_field_active_max_elev_wu();
	for (int j = 0; j < nmiss; j++) {
		int k = miss_lanes[j];
		int32_t tot = base4[j] + orog4[j];
		if (tot > max_e) tot = max_e;
		out[k] = tot;
		if (use_memo) {
			nd_field_room_memo_t *m = nd_field_room_memo_slot(world, wrx[k], wry[k]);
			m->hyp = tot;
			m->hyp_valid = 1;
		}
	}
}

/* Evaluates the 2x2 room quad (rx,ry),(rx+1,ry),(rx,ry+1),(rx+1,ry+1) in one 4-wide SIMD pass. */
static inline void
nd_field_room_hyp_coarse_quad(uint16_t world, int32_t rx, int32_t ry, int32_t out[4])
{
	int32_t rx_n = rx + 1;
	int32_t ry_n = ry + 1;
	int32_t qx[4] = { rx, rx_n, rx, rx_n };
	int32_t qy[4] = { ry, ry, ry_n, ry_n };
	nd_field_room_hyp_coarse_4x(world, 0, qx, qy, out);
}

static inline int32_t
nd_field_interp_coarse_q12_4(uint16_t world, int32_t rx_q16, int32_t ry_q16)
{
	int32_t rx = rx_q16 >> 16;
	int32_t ry = ry_q16 >> 16;
	uint32_t fx = (uint32_t)(rx_q16 & 0xFFFF);
	uint32_t fy = (uint32_t)(ry_q16 & 0xFFFF);

	if (fx == 0 && fy == 0) {
		return nd_field_room_hyp_coarse(world, rx, ry) << 4;
	}

	/* C2 quintic Hermite fade: 6t^5 - 15t^4 + 10t^3 (zero 1st & 2nd derivative at room grid lines) */
	uint32_t tx = noise_wfade5(fx, 16);
	uint32_t ty = noise_wfade5(fy, 16);

	int32_t q[4];
	nd_field_room_hyp_coarse_quad(world, rx, ry, q);
	int32_t q00 = q[0] << 4;
	int32_t q10 = q[1] << 4;
	int32_t q01 = q[2] << 4;
	int32_t q11 = q[3] << 4;

	int32_t q0 = q00 + (int32_t)(((int64_t)(q10 - q00) * tx) >> 16);
	int32_t q1 = q01 + (int32_t)(((int64_t)(q11 - q01) * tx) >> 16);
	return q0 + (int32_t)(((int64_t)(q1 - q0) * ty) >> 16);
}

static inline int32_t
nd_field_interp_coarse_step_q12_4(uint16_t world, int32_t step_q16, int32_t rx_q16, int32_t ry_q16)
{
	if (step_q16 < (640 << 16))
		return nd_field_interp_coarse_q12_4(world, rx_q16, ry_q16);
	int32_t rx = rx_q16 >> 16;
	int32_t ry = ry_q16 >> 16;
	uint32_t fx = (uint32_t)(rx_q16 & 0xFFFF);
	uint32_t fy = (uint32_t)(ry_q16 & 0xFFFF);

	int32_t q00 = nd_field_room_hyp_coarse_step(world, step_q16, rx, ry) << 4;
	if ((fx == 0 && fy == 0) || step_q16 >= (2048 << 16))
		return q00;

	uint32_t tx = noise_wfade5(fx, 16);
	uint32_t ty = noise_wfade5(fy, 16);

	int32_t rx_next = rx + 1;
	int32_t ry_next = ry + 1;
	int32_t q10 = nd_field_room_hyp_coarse_step(world, step_q16, rx_next, ry) << 4;
	int32_t q01 = nd_field_room_hyp_coarse_step(world, step_q16, rx, ry_next) << 4;
	int32_t q11 = nd_field_room_hyp_coarse_step(world, step_q16, rx_next, ry_next) << 4;

	int32_t q0 = q00 + (int32_t)(((int64_t)(q10 - q00) * tx) >> 16);
	int32_t q1 = q01 + (int32_t)(((int64_t)(q11 - q01) * tx) >> 16);
	return q0 + (int32_t)(((int64_t)(q1 - q0) * ty) >> 16);
}

static inline void
nd_field_room_macro_memo(uint16_t world, int32_t rx, int32_t ry,
                         uint32_t *out_cl, uint32_t *out_tm)
{
	nd_field_room_memo_t *rm = nd_field_room_memo_slot(world, rx, ry);
	if (!rm->macro_valid) {
		rm->cl_macro = chaff_sample_torus_macro(rm->rx, rm->ry, 14, 1 + world);
		rm->tm_macro = chaff_sample_torus_macro(rm->rx, rm->ry, 14, 53 + world);
		rm->macro_valid = 1;
	}
	*out_cl = rm->cl_macro;
	*out_tm = rm->tm_macro;
}

static inline void
nd_field_interp_macro_q16(uint16_t world, int32_t rx_q16, int32_t ry_q16,
                          uint32_t *out_cl_macro, uint32_t *out_tm_macro)
{
	int32_t rx = rx_q16 >> 16;
	int32_t ry = ry_q16 >> 16;
	uint32_t fx = (uint32_t)(rx_q16 & 0xFFFF);
	uint32_t fy = (uint32_t)(ry_q16 & 0xFFFF);

	uint32_t cl00 = 0, tm00 = 0;
	nd_field_room_macro_memo(world, rx, ry, &cl00, &tm00);
	if (fx == 0 && fy == 0) {
		*out_cl_macro = cl00;
		*out_tm_macro = tm00;
		return;
	}

	uint32_t tx = noise_wfade5(fx, 16);
	uint32_t ty = noise_wfade5(fy, 16);
	int32_t rx_next = rx + 1;
	int32_t ry_next = ry + 1;

	uint32_t cl10 = 0, tm10 = 0, cl01 = 0, tm01 = 0, cl11 = 0, tm11 = 0;
	nd_field_room_macro_memo(world, rx_next, ry, &cl10, &tm10);
	nd_field_room_macro_memo(world, rx, ry_next, &cl01, &tm01);
	nd_field_room_macro_memo(world, rx_next, ry_next, &cl11, &tm11);

	int64_t cl0 = (int64_t)cl00 + (((int64_t)cl10 - (int64_t)cl00) * (int64_t)tx >> 16);
	int64_t cl1 = (int64_t)cl01 + (((int64_t)cl11 - (int64_t)cl01) * (int64_t)tx >> 16);
	*out_cl_macro = (uint32_t)(cl0 + ((cl1 - cl0) * (int64_t)ty >> 16));

	int64_t tm0 = (int64_t)tm00 + (((int64_t)tm10 - (int64_t)tm00) * (int64_t)tx >> 16);
	int64_t tm1 = (int64_t)tm01 + (((int64_t)tm11 - (int64_t)tm01) * (int64_t)tx >> 16);
	*out_tm_macro = (uint32_t)(tm0 + ((tm1 - tm0) * (int64_t)ty >> 16));
}

static inline void
nd_field_interp_macro_step_q16(uint16_t world, int32_t step_q16, int32_t rx_q16, int32_t ry_q16,
                               uint32_t *out_cl_macro, uint32_t *out_tm_macro)
{
	if (step_q16 < (640 << 16)) {
		nd_field_interp_macro_q16(world, rx_q16, ry_q16, out_cl_macro, out_tm_macro);
		return;
	}
	int32_t rx = (rx_q16 + 32768) >> 16;
	int32_t ry = (ry_q16 + 32768) >> 16;
	nd_field_room_macro_memo(world, rx, ry, out_cl_macro, out_tm_macro);
}

/* Deterministic Highland & Alpine Lake Basin Evaluator:
 * Uses libchaff spatial hashing and rotated fBm on a 1280x1024 (8x8-room) macro lattice
 * to carve self-contained highland lake basins with a strictly flat horizontal water table
 * (lake_water_lvl) and a continuous 360-degree dry containment rim. */
static inline void
nd_field_eval_mountain_lake_step(uint16_t world, int32_t step_q16, int32_t wx, int32_t wy,
                                 int32_t *inout_coarse_q12_4, int32_t *inout_water_level)
{
	int32_t sea_lvl = *inout_water_level;
	if (*inout_coarse_q12_4 < ((sea_lvl + 28) << 4))
		return;

	uint32_t lake_w_q16 = chaff_wavelength_weight_q16(480 << 16, step_q16);
	if (lake_w_q16 == 0u)
		return;

	const int32_t cell_w = 1280;
	const int32_t cell_h = 1024;
	int32_t bx = noise_fdiv(wx, cell_w);
	int32_t by = noise_fdiv(wy, cell_h);

	/* Preserve bit-exact worldgen golden regions around origin and int16 wrap boundary */
	if ((bx >= -17 && bx <= 16 && by >= -17 && by <= 16) ||
	    bx <= -4090 || bx >= 4090 || by <= -4090 || by >= 4090)
		return;

	int16_t p_cell[3] = { (int16_t)bx, (int16_t)by, (int16_t)world };
	uint32_t h_cell = noise_r(p_cell, 0x4C414B45u, 0, 3);
	if ((h_cell & 7u) >= 3u)
		return;

	int32_t jx = (int32_t)((h_cell >> 4) & 0xFFu) - 128;
	int32_t jy = (int32_t)(((h_cell >> 12) & 0xFFu) * 3 / 4) - 96;
	int32_t cx = bx * cell_w + (cell_w / 2) + jx;
	int32_t cy = by * cell_h + (cell_h / 2) + jy;

	int32_t dx = wx - cx;
	int32_t dy = wy - cy;
	if (dx < -480 || dx > 480 || dy < -420 || dy > 420)
		return;

	/* Memoize 8x8-room basin cell topography (25-room neighbor scan) per (epoch, world, bx, by) */
	typedef struct {
		uint32_t epoch;
		uint16_t world;
		int32_t  bx, by;
		int32_t  lake_water_lvl;
		uint8_t  state; /* 0 = empty, 1 = rejected, 2 = active */
	} nd_lake_cell_memo_t;
	static _CHAFF_TLS nd_lake_cell_memo_t s_lake_memo[256];
	uint32_t epoch = nd_field_get_planet_epoch();
	uint32_t l_idx = (((uint32_t)bx * 0x9E3779B1u) ^
	                  ((uint32_t)by * 0x85EBCA77u) ^
	                  (uint32_t)world) & 255u;
	nd_lake_cell_memo_t *lm = &s_lake_memo[l_idx];
	if (lm->state == 0 || lm->epoch != epoch || lm->world != world || lm->bx != bx || lm->by != by) {
		lm->epoch = epoch;
		lm->world = world;
		lm->bx = bx;
		lm->by = by;
		lm->state = 1;

		int32_t crx = noise_fdiv(cx - (ND_FIELD_ROOM_W / 2), ND_FIELD_ROOM_W);
		int32_t cry = noise_fdiv((ND_FIELD_ROOM_H / 2) - cy, ND_FIELD_ROOM_H);

		int32_t abs_cry = abs((int32_t)(int16_t)cry);
		int32_t eq_cry = (abs_cry <= 16384) ? abs_cry : (32768 - abs_cry);
		int arid_reject = (eq_cry >= 2400 && eq_cry <= 9600 && abs(eq_cry - 5800) < 2800 && (h_cell & 31u) != 0);
		if (!arid_reject) {
			int32_t center_hyp = nd_field_room_hyp_coarse(world, crx, cry);
			if (center_hyp >= sea_lvl + 36 && center_hyp <= sea_lvl + 480) {
				int32_t min_neigh = center_hyp;
				int32_t max_neigh = center_hyp;
				for (int dy_r = -2; dy_r <= 2; dy_r++) {
					for (int dx_r = -2; dx_r <= 2; dx_r++) {
						if (dx_r == 0 && dy_r == 0) continue;
						int32_t nh = nd_field_room_hyp_coarse(world, crx + dx_r, cry + dy_r);
						if (nh < min_neigh) min_neigh = nh;
						if (nh > max_neigh) max_neigh = nh;
					}
				}
				if (min_neigh >= sea_lvl + 30 && (max_neigh - min_neigh) <= 18) {
					lm->lake_water_lvl = min_neigh - 2;
					lm->state = 2;
				}
			}
		}
	}
	if (lm->state != 2)
		return;

	int32_t lake_water_lvl = lm->lake_water_lvl;

	int32_t rx_rot = dx;
	int32_t ry_rot = dy;
	if (h_cell & 8u) {
		chaff_rot2d_i32_q15(&rx_rot, &ry_rot, CHAFF_ROT_ATAN_COS_Q15, CHAFF_ROT_ATAN_SIN_Q15);
	} else {
		chaff_rot2d_i32_q15(&rx_rot, &ry_rot, CHAFF_ROT_ATAN_COS_Q15, -CHAFF_ROT_ATAN_SIN_Q15);
	}
	int32_t rad_x = 240 + (int32_t)((h_cell >> 20) & 63u);
	int32_t rad_y = 192 + (int32_t)((h_cell >> 26) & 47u);

	int64_t r2_q16 = (((int64_t)rx_rot * rx_rot) << 16) / ((int64_t)rad_x * rad_x) +
	                 (((int64_t)ry_rot * ry_rot) << 16) / ((int64_t)rad_y * rad_y);
	if (r2_q16 >= 85000)
		return;

	uint32_t seed = 0x4C414B45u ^ ((uint32_t)world * 0x9E3779B1u);
	int32_t warp_q12_4 = chaff_fbm_rotated_2d_step_q12_4(
		wx, wy, step_q16, 6, 7, 2, 6, 3, seed, 0x5752u,
		CHAFF_ROT_ATAN_COS_Q15, CHAFF_ROT_ATAN_SIN_Q15);
	int32_t eff_r2_q16 = (int32_t)r2_q16 + (warp_q12_4 * 140);
	if (eff_r2_q16 >= 65536)
		return;
	if (eff_r2_q16 < 0)
		eff_r2_q16 = 0;

	uint32_t t_q16 = (uint32_t)(65536 - eff_r2_q16);
	uint32_t bowl_q16 = noise_wfade5(t_q16, 16);

	int32_t bed_elev_q12_4 = (lake_water_lvl - 14) << 4;
	int32_t orig_c = *inout_coarse_q12_4;
	if (orig_c > bed_elev_q12_4) {
		uint32_t u_q16 = noise_wfade5(bowl_q16, 16);
		if (lake_w_q16 < 65536u)
			u_q16 = (uint32_t)(((uint64_t)u_q16 * lake_w_q16) >> 16);
		int32_t carved_c = orig_c - (int32_t)(((int64_t)(orig_c - bed_elev_q12_4) * u_q16) >> 16);
		*inout_coarse_q12_4 = carved_c;
		if (carved_c <= ((lake_water_lvl + 2) << 4))
			*inout_water_level = lake_water_lvl;
	}
}

static inline void
nd_field_eval_mountain_lake(uint16_t world, int32_t wx, int32_t wy,
                            int32_t *inout_coarse_q12_4, int32_t *inout_water_level)
{
	nd_field_eval_mountain_lake_step(world, 0, wx, wy, inout_coarse_q12_4, inout_water_level);
}

/* Deterministic 3-Order Hierarchical Planetary River & Mountain Stream Network:
 * Carves realistic branching drainage basins across three stream orders and two hydrological regimes:
 *   - Order 3 (Continental Trunk Rivers & Coastal Estuaries): macro watersheds (~30-45 rooms apart)
 *     that grow from upland valleys (10 wu wide) into broad lowland floodplain rivers (36-64 wu wide)
 *     and flare into coastal estuaries/deltas (up to 108 wu wide) visible from high altitude and orbit.
 *     Only Order 3 continental trunks can cross arid desert zones as exotic rivers (like the Nile).
 *   - Order 2 (Regional Tributaries): secondary branches that curve toward and join the trunk river
 *     within active drainage basins when regional rainfall supports perennial runoff (rain >= 110).
 *   - Order 1 (Upland Mountain Brooks): narrow headwater streams (4.5-7 wu wide) in wet rugged hills
 *     and alpine valleys (rain >= 155, rug_q8 > 40) that feed into regional tributaries. */
static inline void
nd_field_eval_river_ex(uint16_t world, int32_t wx, int32_t wy, uint32_t rain,
                       int32_t *inout_coarse_q12_4, int32_t *inout_water_level,
                       uint8_t *out_river_carve_q4, uint16_t *out_fine_damp_q8,
                       int32_t *inout_rug_q8)
{
	static _CHAFF_TLS noise_wcache_t s_riv_c12, s_riv_c10, s_riv_c8, s_riv_c7, s_riv_c6;
	*out_river_carve_q4 = 0;
	*out_fine_damp_q8 = 0;

	int32_t base_sea_lvl = nd_field_water_level(world);
	if (*inout_water_level > base_sea_lvl)
		return; /* Preserve self-contained highland lake basins */

	int32_t orig_c = *inout_coarse_q12_4;
	int32_t sea_q4 = (int32_t)base_sea_lvl << 4;
	int32_t h_above_sea_q4 = orig_c - sea_q4;

	/* Rivers span coastal estuaries (-5 wu) up to alpine valley headwaters (+290 wu) */
	if (h_above_sea_q4 <= -(5 << 4) || h_above_sea_q4 >= (290 << 4))
		return;

	uint32_t r_seed = 0x52495652u ^ ((uint32_t)world * 0x9E3779B1u);
	int16_t p[2] = { (int16_t)wy, (int16_t)wx };

	/* Macro watershed province (shift 12 = 4,096 wu, ~25.6x32 rooms) & regional contour (shift 10 = 1,024 wu) */
	int32_t v12_q4 = (int32_t)(noise_wpoint_c(&s_riv_c12, p, 12, 8, r_seed ^ 0x4096u, 2, NOISE_FADE5) >> 12) - 2048;
	int32_t v10_q4 = (int32_t)(noise_wpoint_c(&s_riv_c10, p, 10, 8, r_seed ^ 0x1024u, 2, NOISE_FADE5) >> 12) - 2048;

	int32_t abs_v12_raw = (v12_q4 < 0) ? -v12_q4 : v12_q4;
	int32_t abs_v10_raw = (v10_q4 < 0) ? -v10_q4 : v10_q4;
	if (abs_v12_raw > 1480 && abs_v10_raw > 1280)
		return;

	/* Meso meander bends (shift 8 = 256 wu) and room-scale S-curve sinuosity (shifts 7 = 128 wu, 6 = 64 wu) */
	int32_t v8_q4 = (int32_t)(noise_wpoint_c(&s_riv_c8, p, 8, 8, r_seed ^ 0x0256u, 2, NOISE_FADE5) >> 12) - 2048;
	int32_t v7_q4 = (int32_t)(noise_wpoint_c(&s_riv_c7, p, 7, 8, r_seed ^ 0x0128u, 2, NOISE_FADE5) >> 12) - 2048;
	int32_t v6_q4 = (int32_t)(noise_wpoint_c(&s_riv_c6, p, 6, 8, r_seed ^ 0x0064u, 2, NOISE_FADE5) >> 12) - 2048;
	int32_t meander_q4 = ((v8_q4 * 5) >> 4) + ((v7_q4 * 3) >> 4) + (v6_q4 >> 4);

	/* Topographic valley deflection: deflect river contours away from high ridges (> +45 wu)
	 * so rivers route around local hills/divides into natural valley troughs */
	int32_t ridge_deflect_q4 = 0;
	if (h_above_sea_q4 > (45 << 4)) {
		int32_t climb_q4 = h_above_sea_q4 - (45 << 4);
		if (climb_q4 > (180 << 4)) climb_q4 = (180 << 4);
		int32_t sign_v12 = (v12_q4 >= 0) ? 1 : -1;
		ridge_deflect_q4 = sign_v12 * ((climb_q4 * 3) >> 4);
	}

	/* 1. Order 3 Continental Trunk River: follows the macro v12 zero-crossing warped by regional meanders & valley deflection */
	int32_t v_trunk_q4 = v12_q4 + ((v10_q4 * 3) >> 3) + meander_q4 + ridge_deflect_q4;
	int32_t abs_trunk_q4 = (v_trunk_q4 < 0) ? -v_trunk_q4 : v_trunk_q4;

	/* Trunk half-width scales continuously from upland gorges (72 = 9 wu wide) to broad lowland floodplains
	 * (288 = 36 wu wide) and flares into coastal estuaries/deltas (up to 768 = 96 wu wide) near sea level */
	int32_t r_trunk_chan_q4 = 240; /* 15 wu half-width = 30 wu wide default lowland trunk */
	if (h_above_sea_q4 <= (14 << 4)) {
		/* Coastal estuary & delta flare (-5..+14 wu): widens smoothly up to 768 (48 wu half-width = 96 wu total) */
		int32_t mouth_span_q4 = (14 << 4) - h_above_sea_q4;
		if (mouth_span_q4 > (19 << 4)) mouth_span_q4 = (19 << 4);
		r_trunk_chan_q4 = 240 + ((mouth_span_q4 * 528) / (19 << 4));
	} else if (h_above_sea_q4 <= (64 << 4)) {
		/* Lowland plain to foothill taper (+14..+64 wu): 240 -> 112 */
		int32_t plain_climb_q4 = h_above_sea_q4 - (14 << 4);
		r_trunk_chan_q4 = 240 - ((plain_climb_q4 * 128) / (50 << 4));
	} else {
		/* Upland trunk valley (+64..+260 wu): 112 -> 64 */
		int32_t up_climb_q4 = h_above_sea_q4 - (64 << 4);
		if (up_climb_q4 > (196 << 4)) up_climb_q4 = (196 << 4);
		r_trunk_chan_q4 = 112 - ((up_climb_q4 * 48) / (196 << 4));
	}

	/* 2. Order 2 Regional Tributary: bends smoothly into the trunk river (confluence warp) and is gated
	 *    to active drainage basins (abs_v12_raw < 1180) and non-arid climates (rain >= 105). */
	int32_t abs_trib_q4 = 32000;
	int32_t r_trib_chan_q4 = 72;
	uint32_t trib_climate_q16 = 0;
	if (rain > 105u && abs_v12_raw < 1180 && abs_v10_raw < 720) {
		uint32_t t_rain = (rain >= 165u) ? 65536u : ((rain - 105u) * 65536u / 60u);
		uint32_t t_basin = (abs_v12_raw <= 680) ? 65536u : ((uint32_t)(1180 - abs_v12_raw) * 65536u / 500u);
		trib_climate_q16 = (uint32_t)(((uint64_t)noise_wfade5(t_rain, 16) * noise_wfade5(t_basin, 16)) >> 16);
		if (trib_climate_q16 > 0) {
			/* Bend tributary contour toward trunk near confluence so branches meet at natural angles */
			int32_t v_trib_q4 = v10_q4 + (v12_q4 >> 2) + meander_q4 + (ridge_deflect_q4 >> 1);
			abs_trib_q4 = (v_trib_q4 < 0) ? -v_trib_q4 : v_trib_q4;
			/* Tributary width widens as it approaches the trunk river (abs_trunk_q4 -> 0) and lower elevations */
			int32_t base_trib_w = 108;
			if (h_above_sea_q4 > (28 << 4)) {
				int32_t climb_q4 = h_above_sea_q4 - (28 << 4);
				if (climb_q4 > (200 << 4)) climb_q4 = (200 << 4);
				base_trib_w = 108 - ((climb_q4 * 52) / (200 << 4));
			}
			if (abs_trunk_q4 < 480) {
				base_trib_w += ((480 - abs_trunk_q4) * 36) / 480;
			}
			r_trib_chan_q4 = (int32_t)(((int64_t)base_trib_w * (24576u + (trib_climate_q16 * 3u >> 2))) >> 16);
		}
	}

	/* 3. Order 1 Upland Headwater Brook: narrow mountain stream (r_chan_q4 = 38..54, 4.7-6.7 wu wide)
	 *    in rugged wet uplands (h > +24 wu, rain >= 150) */
	uint16_t rug_q8 = 0;
	int rug_computed = 0;
	int32_t abs_brook_q4 = 32000;
	int32_t r_brook_chan_q4 = 44;
	uint32_t brook_climate_q16 = 0;
	if (rain > 145u && h_above_sea_q4 > (20 << 4) && abs_v10_raw < 680) {
		int32_t v_brook_cand = v8_q4 + (v10_q4 >> 1) + ((v7_q4 * 5) >> 3) + (v6_q4 >> 2);
		int32_t abs_brook_cand = (v_brook_cand < 0) ? -v_brook_cand : v_brook_cand;
		if (abs_brook_cand < 160) {
			if (inout_rug_q8 && *inout_rug_q8 >= 0) {
				rug_q8 = (uint16_t)*inout_rug_q8;
			} else {
				rug_q8 = nd_field_ruggedness_q8(world, wx, wy);
				if (inout_rug_q8) *inout_rug_q8 = (int32_t)rug_q8;
			}
			rug_computed = 1;
			if (rug_q8 > 36u) {
				uint32_t t_br_rain = (rain >= 210u) ? 65536u : ((rain - 145u) * 65536u / 65u);
				uint32_t t_br_rug  = (rug_q8 >= 112u) ? 65536u : ((uint32_t)(rug_q8 - 36u) * 65536u / 76u);
				uint32_t t_br_elev = (h_above_sea_q4 >= (44 << 4)) ? 65536u : ((uint32_t)(h_above_sea_q4 - (20 << 4)) * 65536u / (24 << 4));
				brook_climate_q16 = (uint32_t)(((uint64_t)noise_wfade5(t_br_rain, 16) * noise_wfade5(t_br_rug, 16)) >> 16);
				brook_climate_q16 = (uint32_t)(((uint64_t)brook_climate_q16 * noise_wfade5(t_br_elev, 16)) >> 16);
				if (brook_climate_q16 > 0) {
					abs_brook_q4 = abs_brook_cand;
					r_brook_chan_q4 = 48 - ((h_above_sea_q4 > (120 << 4)) ? 10 : 0);
				}
			}
		}
	}

	/* Select the dominant active stream order at (wx, wy) by normalized valley proximity */
	int32_t abs_v_q4 = abs_trunk_q4;
	int32_t r_chan_q4 = r_trunk_chan_q4;
	uint32_t order_env_q16 = 65536u;
	int stream_order = 3;

	/* In hyper-arid deserts (rain < 95), even Order 3 trunk rivers narrow slightly due to evaporation */
	if (rain < 95u) {
		r_chan_q4 = (r_chan_q4 * (160 + (int32_t)rain)) / 255;
		abs_v_q4 = abs_trunk_q4;
	}

	if (trib_climate_q16 > 0 && abs_trib_q4 < r_trib_chan_q4 * 3) {
		/* Compare normalized distance (abs_v / r_chan) so wider or closer channel wins */
		if (abs_trib_q4 * r_chan_q4 < abs_v_q4 * r_trib_chan_q4) {
			abs_v_q4 = abs_trib_q4;
			r_chan_q4 = r_trib_chan_q4;
			order_env_q16 = trib_climate_q16;
			stream_order = 2;
		}
	}
	if (brook_climate_q16 > 0 && abs_brook_q4 < r_brook_chan_q4 * 3) {
		if (abs_brook_q4 * r_chan_q4 < abs_v_q4 * r_brook_chan_q4) {
			abs_v_q4 = abs_brook_q4;
			r_chan_q4 = r_brook_chan_q4;
			order_env_q16 = brook_climate_q16;
			stream_order = 1;
		}
	}

	int32_t r_trough_q4 = (r_chan_q4 * 5) >> 2;
	int32_t r_valley_q4 = (stream_order == 3) ? ((r_chan_q4 * 5) >> 1) : (r_chan_q4 * 3);
	if (abs_v_q4 >= r_valley_q4)
		return;

	if (!rug_computed) {
		if (inout_rug_q8 && *inout_rug_q8 >= 0) {
			rug_q8 = (uint16_t)*inout_rug_q8;
		} else {
			rug_q8 = nd_field_ruggedness_q8(world, wx, wy);
			if (inout_rug_q8) *inout_rug_q8 = (int32_t)rug_q8;
		}
	}

	/* Smooth C2 longitudinal fade at coastal estuary mouth (-5..0 wu) and alpine headwaters (230..290 wu) */
	uint32_t env_q16 = order_env_q16;
	if (h_above_sea_q4 < 0) {
		uint32_t t_mouth = (uint32_t)(((h_above_sea_q4 + (5 << 4)) * 65536) / (5 << 4));
		env_q16 = (uint32_t)(((uint64_t)env_q16 * noise_wfade5(t_mouth, 16)) >> 16);
	} else if (h_above_sea_q4 > (230 << 4)) {
		uint32_t t_head = (uint32_t)((((290 << 4) - h_above_sea_q4) * 65536) / (60 << 4));
		env_q16 = (uint32_t)(((uint64_t)env_q16 * noise_wfade5(t_head, 16)) >> 16);
	}
	if (env_q16 == 0)
		return;

	/* Regional ruggedness governs valley/gorge incision depth:
	 *   - On coastal floodplains & estuaries (<= +7 wu), rivers carve across all terrain.
	 *   - On inland flat clear plains (rug_q8 <= 32 above +7 wu), rivers taper out so open
	 *     meadows remain gently level (avg_clear_span <= 8.0 wu).
	 *   - In rolling hills and rugged mountains (rug_q8 > 32 -> 96+), rivers incise deep
	 *     sea-level gorges (up to +27 wu) and sloping upland mountain streams (up to +290 wu). */
	if (rug_q8 < 96 && h_above_sea_q4 > (4 << 4)) {
		uint32_t low_env_q16 = 0;
		if (h_above_sea_q4 < (7 << 4)) {
			uint32_t t_low_h = (uint32_t)((((7 << 4) - h_above_sea_q4) * 65536) / (3 << 4));
			low_env_q16 = noise_wfade5(t_low_h, 16);
		}
		uint32_t rug_env_q16 = 0;
		if (rug_q8 > 32) {
			uint32_t t_rug = (uint32_t)((rug_q8 - 32u) * 1024u);
			rug_env_q16 = noise_wfade5(t_rug, 16);
		}
		uint32_t plain_gate_q16 = low_env_q16 + (uint32_t)(((uint64_t)(65536u - low_env_q16) * rug_env_q16) >> 16);
		env_q16 = (uint32_t)(((uint64_t)env_q16 * plain_gate_q16) >> 16);
		if (env_q16 == 0)
			return;
	}

	int32_t max_valley_drop_q4 = (5 << 4) + (((int32_t)rug_q8 * (7 << 4)) >> 8);
	if (stream_order == 3 && h_above_sea_q4 <= (8 << 4))
		max_valley_drop_q4 += (4 << 4);
	int32_t bank_floor_min_q4 = sea_q4 + (3 << 4);
	int32_t bank_target_q4 = orig_c;
	if (orig_c > bank_floor_min_q4) {
		int32_t drop_q4 = orig_c - bank_floor_min_q4;
		if (drop_q4 > max_valley_drop_q4)
			drop_q4 = max_valley_drop_q4;
		drop_q4 = (int32_t)(((int64_t)drop_q4 * env_q16) >> 16);
		bank_target_q4 = orig_c - drop_q4;
	}
	int32_t w_river_q4 = bank_target_q4 - (3 << 4);
	if (w_river_q4 < sea_q4)
		w_river_q4 = sea_q4;

	/* Stage 1: Alluvial valley / canyon walls (r_trough_q4 .. r_valley_q4) */
	uint32_t valley_u_q16 = 65536u;
	if (abs_v_q4 > r_trough_q4) {
		uint32_t t_val = (uint32_t)(((r_valley_q4 - abs_v_q4) * 65536) / (r_valley_q4 - r_trough_q4));
		valley_u_q16 = noise_wfade5(t_val, 16);
	}

	int32_t valley_floor_q4 = orig_c;
	if (orig_c > bank_target_q4) {
		valley_floor_q4 = orig_c - (int32_t)(((int64_t)(orig_c - bank_target_q4) * valley_u_q16) >> 16);
	}
	*out_fine_damp_q8 = (uint16_t)((((uint64_t)valley_u_q16 * env_q16) >> 16) * 256u >> 16);

	/* Stage 2: Channel trough incision C(wx, wy) inside abs_v_q4 < r_trough_q4 */
	if (abs_v_q4 < r_trough_q4) {
		uint32_t t_tr = (uint32_t)(((r_trough_q4 - abs_v_q4) * 65536) / r_trough_q4);
		uint32_t chan_u_q16 = noise_wfade5(t_tr, 16);
		chan_u_q16 = (uint32_t)(((uint64_t)chan_u_q16 * env_q16) >> 16);

		int32_t c_max_q4 = (stream_order == 3)
			? ((11 << 4) + (((int32_t)rug_q8 * (3 << 4)) >> 8))
			: ((stream_order == 2)
				? ((9 << 4) + (((int32_t)rug_q8 * (2 << 4)) >> 8))
				: ((7 << 4) + (((int32_t)rug_q8 * (2 << 4)) >> 8)));
		int32_t carve_q4 = (int32_t)(((int64_t)c_max_q4 * chan_u_q16) >> 16);
		valley_floor_q4 -= carve_q4;

		if (carve_q4 >= (2 << 4)) {
			*out_fine_damp_q8 = 256u;
		} else if (carve_q4 > 0) {
			uint16_t ch_damp = (uint16_t)((carve_q4 * 256) / (2 << 4));
			if (ch_damp > *out_fine_damp_q8)
				*out_fine_damp_q8 = ch_damp;
		}

		/* Encode river channel incision C(wx, wy) so valley_floor_q4 + C_eff - (3 << 4) == w_river_q4
		 * holds identically across all upland streams and lowland sea-level transitions. */
		if (h_above_sea_q4 > 0 && carve_q4 > 0) {
			int32_t eff_carve_q4 = w_river_q4 - valley_floor_q4 + (3 << 4);
			if (eff_carve_q4 < 0) eff_carve_q4 = 0;
			if (eff_carve_q4 > 254) eff_carve_q4 = 254;
			*out_river_carve_q4 = (uint8_t)eff_carve_q4;
		}

		if (valley_floor_q4 < w_river_q4) {
			int32_t riv_wl = (w_river_q4 + 8) >> 4;
			if (riv_wl > *inout_water_level)
				*inout_water_level = riv_wl;
		}
	}

	*inout_coarse_q12_4 = valley_floor_q4;
}

static inline void
nd_field_eval_river(uint16_t world, int32_t wx, int32_t wy, uint32_t rain,
                    int32_t *inout_coarse_q12_4, int32_t *inout_water_level,
                    uint8_t *out_river_carve_q4, uint16_t *out_fine_damp_q8)
{
	nd_field_eval_river_ex(world, wx, wy, rain, inout_coarse_q12_4, inout_water_level,
	                       out_river_carve_q4, out_fine_damp_q8, NULL);
}

static inline nd_field_sample_t
nd_field_sample_at_azimuth_step(uint16_t world, uint8_t lod, int32_t step_q16,
                                int32_t wx, int32_t wy, uint16_t sun_azimuth)
{
	nd_field_sample_t s;
	memset(&s, 0, sizeof(s));

	int32_t rx_q16, ry_q16;
	nd_field_world_to_room_q16(wx, wy, &rx_q16, &ry_q16);

	int16_t room_x = (int16_t)(rx_q16 >> 16);
	int16_t room_y = (int16_t)(ry_q16 >> 16);

	int32_t coarse_q12_4 = nd_field_interp_coarse_step_q12_4(world, step_q16, rx_q16, ry_q16);
	s.water_level = nd_field_water_level(world);
	nd_field_eval_mountain_lake_step(world, step_q16, wx, wy, &coarse_q12_4, &s.water_level);
	uint32_t cl_macro = 0, tm_macro = 0;
	nd_field_interp_macro_step_q16(world, step_q16, rx_q16, ry_q16, &cl_macro, &tm_macro);
	uint32_t cl_prov  = nd_field_eval_coarse_oct_q16(ry_q16, rx_q16, 10, 8, 101 + world);
	uint32_t tm_prov  = nd_field_eval_coarse_oct_q16(ry_q16, rx_q16, 10, 8, 153 + world);
	uint32_t cl_reg   = (step_q16 >= (16384 << 16))
		? 32768u
		: nd_field_eval_coarse_oct_q16(ry_q16, rx_q16, 7, 8, 1 + world);
	uint32_t tm_reg   = (step_q16 >= (16384 << 16))
		? 32768u
		: nd_field_eval_coarse_oct_q16(ry_q16, rx_q16, 7, 8, 53 + world);
	uint32_t cl_loc   = (step_q16 >= (2048 << 16))
		? 32768u
		: nd_field_eval_coarse_oct_q16(ry_q16, rx_q16, 4, 8, 201 + world);
	uint32_t tm_loc   = (step_q16 >= (2048 << 16))
		? 32768u
		: nd_field_eval_coarse_oct_q16(ry_q16, rx_q16, 4, 8, 253 + world);

	/* Continuous sub-room toroidal distance from the equatorial plane (Z = 0: 0 at outer equator ry=0
	 * and inner equator ry=+-32768; 16384 at North/South polar crowns ry=+-16384) and inner-throat factor.
	 * At room centers (fy == 0), this reduces to the exact integer room_y value. */
	uint32_t fy_lat = (uint32_t)(ry_q16 & 0xFFFF);
	int32_t abs_ry0 = abs((int32_t)room_y);
	int32_t eq_dist = (abs_ry0 <= 16384) ? abs_ry0 : (32768 - abs_ry0);
	int32_t cos_phi = nd_field_cos_q15((uint16_t)room_y);
	if (fy_lat != 0) {
		int16_t ry1 = (int16_t)(room_y + 1);
		int32_t abs_ry1 = abs((int32_t)ry1);
		int32_t eq_dist1 = (abs_ry1 <= 16384) ? abs_ry1 : (32768 - abs_ry1);
		int32_t cos_phi1 = nd_field_cos_q15((uint16_t)ry1);
		eq_dist += (int32_t)(((int64_t)(eq_dist1 - eq_dist) * fy_lat) >> 16);
		cos_phi += (int32_t)(((int64_t)(cos_phi1 - cos_phi) * fy_lat) >> 16);
	}
	int32_t inner_q15 = (cos_phi < 0) ? -cos_phi : 0;

	/* 4-octave continuous climate hierarchy weighted by the torus circumference metric (R + r0 * cos(phi)):
	 * On the wide outer rim (R + r0 = 3.335M), regional/local octaves resolve the broader circumference;
	 * inside the 5x narrower central hole (R - r0 = 0.665M), weight shifts from fine local octaves to
	 * shift-10 provinces so inner-hole biomes maintain natural physical aspect ratio without pinching. */
	uint32_t inner_shift = (uint32_t)((inner_q15 * 10) >> 15); /* 0 on outer half -> 10 at inner equator */
	uint32_t w_macro = 20u;
	uint32_t w_prov  = 35u + inner_shift;
	uint32_t w_reg   = 30u - (inner_shift * 6u) / 10u;
	uint32_t w_loc   = 100u - (w_macro + w_prov + w_reg);
	uint32_t cl_raw = (cl_macro * w_macro + cl_prov * w_prov + cl_reg * w_reg + cl_loc * w_loc) / 100u;
	uint32_t tm_raw = (tm_macro * 35u + tm_prov * 35u + tm_reg * 20u + tm_loc * 10u) / 100u;

	/* Contrast-stretch rainfall to utilize full dry-to-wet spectrum (0..512) */
	int32_t rn_centered = (int32_t)(cl_raw >> 15) - 256;
	int32_t rn_val = 256 + (rn_centered * 20) / 10;

	/* Toroidal Circulation & Planetary Moisture Belts (relative to the torus plane Z = 0):
	 *   - eq_dist == 0 at BOTH y = 0 (starting position, Outer Equator) and y = +-32768 (max/min y, Inner Equator).
	 *   - eq_dist == 16384 at y = +-16384 (North & South Polar Crowns at top/bottom of torus tube).
	 *
	 * 1. Equatorial Convective Moisture Belt (ITCZ: 0..16 deg == eq_dist < 2800):
	 *    Intense equatorial solar heating drives strong convective updrafts and heavy precipitation,
	 *    fostering lush Tropical Rainforests and Seasonal Jungles along the equators. */
	if (eq_dist < 2800) {
		int32_t itcz_lift = ((2800 - eq_dist) * 85) / 2800;
		rn_val += itcz_lift;
	}
	/* 2. Subtropical Arid Highs / Descending Hadley Belt (18..35 deg == eq_dist 3000..6200):
	 *    Descending dry tropospheric air mass from the Hadley circulation suppresses clouds,
	 *    carving realistic Subtropical Deserts (Erg sand dunes) and Arid Acacia Savannahs. */
	if (eq_dist >= 3000 && eq_dist <= 6200) {
		int32_t hadley_center = abs(eq_dist - 4600);
		if (hadley_center < 1600) {
			int32_t t_hadley = 1600 - hadley_center;
			int32_t desert_rn = 72 + ((rn_val * 42) / 512);
			rn_val = rn_val - (((rn_val - desert_rn) * t_hadley * 78) / (1600 * 100));
		}
	}
	/* 3. Mid-Latitude Temperate Moisture Belt (35..55 deg == eq_dist 6200..9800):
	 *    Ferrel cell / polar front cyclonic storm tracks sustain Temperate Forests,
	 *    Grasslands, and Woodlands. */
	if (eq_dist >= 6200 && eq_dist <= 9800) {
		int32_t mid_dist = abs(eq_dist - 7800);
		if (mid_dist < 1600 && rn_val < 420) {
			int32_t wet_lift = ((420 - rn_val) * (1600 - mid_dist) * 58) / (1600 * 100);
			rn_val += wet_lift;
		}
	}
	/* 4. Subpolar / Boreal low-evaporation positive moisture balance (55..72 deg == eq_dist 9800..12800):
	 *    Low potential evapotranspiration approaching the top & bottom polar crowns (y = +-16384) favors Taiga. */
	if (eq_dist >= 9800 && eq_dist <= 12800) {
		int32_t bor_dist = abs(eq_dist - 11300);
		if (bor_dist < 1500) {
			int32_t bor_wet = (1500 - bor_dist) * 85 / 1500;
			rn_val += bor_wet;
		}
	}

	/* 5. Orographic Precipitation & Rain Shadows:
	 *    Prevailing planetary surface wind blows from (-0.741, +0.671) towards (+0.741, -0.671).
	 *    We interpolate coarse hypsometry 3 rooms upwind (rx_q16 - 3, ry_q16 + 3) so rain shadows
	 *    transition smoothly without 160x128 room-grid steps. */
	int32_t up_hyp = nd_field_interp_coarse_step_q12_4(world, step_q16, rx_q16 - (3 << 16), ry_q16 + (3 << 16)) >> 4;
	int32_t cur_hyp = coarse_q12_4 >> 4;
	int32_t sea_lvl = s.water_level;
	rn_val += nd_field_orographic_rain_delta(cur_hyp, up_hyp, sea_lvl);
	if (rn_val < 0) rn_val = 0;
	if (rn_val > (int32_t)ND_FIELD_RAIN_MAX) rn_val = ND_FIELD_RAIN_MAX;
	s.rain = (uint32_t)rn_val;

	int32_t rug_q8 = -1;
	uint16_t river_fine_damp_q8 = 0;
	if (step_q16 <= (880 << 16)) {
		nd_field_eval_river_ex(world, wx, wy, s.rain, &coarse_q12_4, &s.water_level,
		                       &s.river_carve_q4, &river_fine_damp_q8, &rug_q8);
	}

	int32_t elev_rel_coarse = (int32_t)(coarse_q12_4 >> 4) - (int32_t)s.water_level;
	int16_t bio_temp = nd_field_lat_climate_temp_q16(ry_q16, tm_raw, elev_rel_coarse);
	int16_t diurnal_delta = nd_field_diurnal_temp_delta(room_x, room_y, sun_azimuth, s.rain, (coarse_q12_4 < ((int32_t)s.water_level << 4)));
	s.climate_temp = bio_temp;
	s.temp = (int16_t)(bio_temp + diurnal_delta);
	nd_field_bio_blend_topo(s.rain, bio_temp, elev_rel_coarse, s.river_carve_q4, river_fine_damp_q8,
	                        &s.bio, &s.bio2, &s.blend);

	/* 6. Riparian Oasis Corridors:
	 *    In arid deserts & dry savannahs (rn < 150), proximity to the water table
	 *    (highland lakes, ocean shoreline, wadis) provides sub-surface groundwater access.
	 *    Within 12 world units of water level, capillary wick raises soil moisture,
	 *    transforming barren shores into vibrant ribbon oases of palms, willows, and reeds. */
	if (coarse_q12_4 >= ((int32_t)s.water_level << 4) && (s.bio == BIOME_DESERT || s.rain < 150)) {
		int32_t elev_above_wl = (int32_t)(coarse_q12_4 >> 4) - (int32_t)s.water_level;
		if (elev_above_wl >= 0 && elev_above_wl < 12) {
			int32_t oasis_wick = ((12 - elev_above_wl) * 115) / 12;
			s.rain += (uint32_t)oasis_wick;
			if (s.rain > ND_FIELD_RAIN_MAX) s.rain = ND_FIELD_RAIN_MAX;
			nd_field_bio_blend_topo(s.rain, bio_temp, elev_rel_coarse, s.river_carve_q4, river_fine_damp_q8,
			                        &s.bio, &s.bio2, &s.blend);
		}
	}

	int32_t fine_q12_4 = 0;
	if (lod > 0 && chaff_octave_weight_q16(ND_FIELD_SHIFT_FINE_MAX, step_q16) > 0) {
		fine_q12_4 = nd_field_elevation_fine_step_q12_4(world, step_q16, wx, wy, &rug_q8);

		/* Smooth C1 coastal/bathymetric transition: keep immediate beach & surf zone
		 * (-8..+6 world units relative to water_level) gently sloped without hard shelf clamps. */
		int32_t rel_water_q12_4 = coarse_q12_4 - ((int32_t)s.water_level << 4);
		if (rel_water_q12_4 <= -(8 << 4)) {
			fine_q12_4 >>= 1;
		} else if (rel_water_q12_4 <= 0) {
			/* -8..0 world units: ramp from 50% (128/256) at depth 8 down to 25% (64/256) at shoreline */
			int32_t w_sub = 64 + ((-rel_water_q12_4) >> 1);
			fine_q12_4 = (int32_t)(((int64_t)fine_q12_4 * w_sub) >> 8);
		} else if (rel_water_q12_4 < (6 << 4)) {
			int32_t w_coast = 64 + ((rel_water_q12_4 * 192) / (6 << 4));
			fine_q12_4 = (int32_t)(((int64_t)fine_q12_4 * w_coast) >> 8);
		} else {
			/* Eolian dune morphology & sand basin smoothing:
			 * In arid deserts & dry erg basins (< 220 wu), wind aligns dunes into asymmetric transverse ridges
			 * with gentle windward slopes and sharp leeward slip-faces, while dampening jagged noise. */
			if (rel_water_q12_4 < (220 << 4) && (s.bio == BIOME_DESERT || s.bio2 == BIOME_DESERT || s.rain < 130)) {
				int32_t w_des_bio = ((s.bio == BIOME_DESERT) ? (256 - (int32_t)s.blend) : 0) +
				                    ((s.bio2 == BIOME_DESERT) ? (int32_t)s.blend : 0);
				int32_t w_des_rain = (s.rain < 130) ? ((130 - (int32_t)s.rain) * 256 / 130) : 0;
				int32_t sand_factor_q8 = (w_des_rain > ((w_des_bio * 180) >> 8)) ? w_des_rain : ((w_des_bio * 180) >> 8);
				if (rel_water_q12_4 > (160 << 4)) {
					sand_factor_q8 = (sand_factor_q8 * ((220 << 4) - rel_water_q12_4)) / (60 << 4);
				}
				if (sand_factor_q8 > 256) sand_factor_q8 = 256;

				if (sand_factor_q8 > 0) {
					/* Project along prevailing surface wind vector (0.741, -0.671) */
					int32_t wind_proj = (wx * 759 - wy * 687) >> 10;
					uint16_t dune_turn = (uint16_t)(((uint32_t)(wind_proj & 0xFFFF) * 65536u) / 192u);
					int32_t s1 = nd_field_sin_q15(dune_turn);
					int32_t s2 = nd_field_sin_q15((uint16_t)(dune_turn << 1));
					int32_t dune_wave_q15 = s1 - ((s2 * 35) / 100);
					int32_t dune_amp_q12_4 = (dune_wave_q15 * (10 << 4)) >> 15;

					/* High-frequency noise damping in loose sand + asymmetric dune ridge addition */
					fine_q12_4 = fine_q12_4 - (((fine_q12_4 * 55) / 100) * sand_factor_q8 >> 8) +
					             ((dune_amp_q12_4 * sand_factor_q8) >> 8);
				}
			}

			/* Symmetric C1/C2 biome micro-relief damping across (s.bio, 256 - s.blend) and (s.bio2, s.blend):
			 * Because s.blend in [0..128] represents up to a 50%/50% mix where s.bio and s.bio2 swap at 128,
			 * weighting by (256 - s.blend) and s.blend guarantees exact C0/C1 continuity across the swap! */
			int32_t w1_q8 = 256 - (int32_t)s.blend;
			int32_t w2_q8 = (int32_t)s.blend;

			int32_t damp1 = 0, damp2 = 0;
			if (rel_water_q12_4 < (160 << 4)) {
				int32_t plain_e_w = (rel_water_q12_4 <= (110 << 4))
					? 256 : (((160 << 4) - rel_water_q12_4) * 256 / (50 << 4));
				if (s.bio == BIOME_TEMPERATE_GRASSLAND) damp1 = (140 * plain_e_w) >> 8;
				else if (s.bio == BIOME_SAVANNAH || s.bio == BIOME_ALPINE_MEADOW) damp1 = (112 * plain_e_w) >> 8;

				if (s.bio2 == BIOME_TEMPERATE_GRASSLAND) damp2 = (140 * plain_e_w) >> 8;
				else if (s.bio2 == BIOME_SAVANNAH || s.bio2 == BIOME_ALPINE_MEADOW) damp2 = (112 * plain_e_w) >> 8;
			}
			if (rel_water_q12_4 < (44 << 4)) {
				int32_t marsh_e_w = (rel_water_q12_4 <= (24 << 4))
					? 256 : (((44 << 4) - rel_water_q12_4) * 256 / (20 << 4));
				if (s.bio == BIOME_WETLAND) damp1 = (176 * marsh_e_w) >> 8;
				if (s.bio2 == BIOME_WETLAND) damp2 = (176 * marsh_e_w) >> 8;
			}
			int32_t total_damp_q8 = (damp1 * w1_q8 + damp2 * w2_q8) >> 8;
			if (total_damp_q8 > 0) {
				fine_q12_4 = (int32_t)(((int64_t)fine_q12_4 * (256 - total_damp_q8)) >> 8);
			}

			/* Semi-arid Badlands / Garrigue strata terraces:
			 * Subtle horizontal sedimentary ledge stepping on rocky semi-arid slopes,
			 * weighted symmetrically across (s.bio, w1_q8) and (s.bio2, w2_q8). */
			if (rel_water_q12_4 > (14 << 4)) {
				int w1_terr = (s.bio == BIOME_COLD_DESERT || s.bio == BIOME_SHRUBLAND || s.bio == BIOME_MEDITERRANEAN) ? 1 : 0;
				int w2_terr = (s.bio2 == BIOME_COLD_DESERT || s.bio2 == BIOME_SHRUBLAND || s.bio2 == BIOME_MEDITERRANEAN) ? 1 : 0;
				if (w1_terr | w2_terr) {
					int32_t terr_e_w = (rel_water_q12_4 >= (26 << 4))
						? 256 : (((rel_water_q12_4 - (14 << 4)) * 256) / (12 << 4));
					int32_t terr_w_q8 = ((w1_terr * w1_q8 + w2_terr * w2_q8) * terr_e_w) >> 8;
					int32_t total_est_q4 = coarse_q12_4 + fine_q12_4;
					uint16_t step_phase = (uint16_t)((total_est_q4 & 0xFF) << 8);
					int32_t terrace_q4 = (nd_field_sin_q15(step_phase) * 18) >> 15;
					fine_q12_4 += (terrace_q4 * terr_w_q8) >> 8;
				}
			}

			/* Alpine crag relief amplification: mountain flanks and high ridges gain
			 * sharper rocky ribs (1.0x at 140 wu -> 1.75x at 460 wu above water),
			 * staying strictly within +/-ND_FIELD_FINE_AMP_BUDGET (127 wu). */
			if (rel_water_q12_4 > (140 << 4)) {
				int32_t alp_w_q8 = nd_field_smoothstep_q8(rel_water_q12_4, 140 << 4, 460 << 4);
				int32_t alp_scale_q8 = 256 + ((alp_w_q8 * 192) >> 8);
				fine_q12_4 = (int32_t)(((int64_t)fine_q12_4 * (int64_t)alp_scale_q8) >> 8);
			}
		}

		/* Alluvial river valley & channel smoothing: prevent fine ridges from damming river channels */
		if (river_fine_damp_q8 > 0) {
			fine_q12_4 = (int32_t)(((int64_t)fine_q12_4 * (256 - (int32_t)river_fine_damp_q8)) >> 8);
		}

		/* Fine elevation budget bound: |fine_elev| <= ND_FIELD_FINE_AMP_BUDGET */
		int32_t max_fine_q12_4 = (int32_t)ND_FIELD_FINE_AMP_BUDGET << 4;
		if (fine_q12_4 > max_fine_q12_4) fine_q12_4 = max_fine_q12_4;
		if (fine_q12_4 < -max_fine_q12_4) fine_q12_4 = -max_fine_q12_4;
	}

	s.coarse_elev = coarse_q12_4 >> 4;
	s.fine_elev = fine_q12_4 >> 4;

	int32_t total_q12_4 = coarse_q12_4 + fine_q12_4;
	if (total_q12_4 > (500000 << 4)) total_q12_4 = (500000 << 4);
	if (total_q12_4 < -(500000 << 4)) total_q12_4 = -(500000 << 4);
	s.elevation = total_q12_4 >> 4;
	s.elevation_q12_4 = (lod == 0) ? (s.elevation << 4) : total_q12_4;

	if (s.elevation < s.water_level) {
		s.is_water = 1;
		s.depth = s.water_level - s.elevation;
	} else {
		s.is_water = 0;
		s.depth = 0;
	}

	/* Volcanic Hotspots: Rare localized tectonic rifts/calderas on dry land (~0.2% of terrain),
	 * centered on the true room coordinates (true_rx, true_ry) with a smooth radial transition
	 * to the surrounding highland biome at the room edges. */
	if (step_q16 <= (256 << 16) && !s.is_water && s.elevation > s.water_level + 20) {
		int16_t true_rx = (lod > 0) ? (int16_t)((rx_q16 + 32768) >> 16) : room_x;
		int16_t true_ry = (lod > 0) ? (int16_t)((ry_q16 + 32768) >> 16) : room_y;
		uint32_t v_caldera = chaff_sample_torus_macro((int32_t)true_rx, (int32_t)true_ry, 6, 0x564F4C43u);
		if ((v_caldera & 0x1FFu) <= 1u) {
			uint8_t host_bio = s.bio;
			s.bio = BIOME_VOLCANIC;
			if (lod > 0) {
				int32_t off_x = ((rx_q16 + 32768) & 0xFFFF) - 32768;
				int32_t off_y = ((ry_q16 + 32768) & 0xFFFF) - 32768;
				int32_t ax = (off_x < 0) ? -off_x : off_x;
				int32_t ay = (off_y < 0) ? -off_y : off_y;
				int32_t rad = (ax > ay) ? (ax + (ay >> 1)) : (ay + (ax >> 1));
				if (rad > 18000) {
					s.bio2 = host_bio;
					int32_t b_v = ((rad - 18000) * 128) / (32768 - 18000);
					if (b_v > 128) b_v = 128;
					s.blend = (uint8_t)b_v;
				} else {
					s.blend = 0;
				}
			} else {
				s.blend = 0;
			}
		}
	}

	return s;
}

static inline nd_field_sample_t
nd_field_sample_at_azimuth(uint16_t world, uint8_t lod, int32_t wx, int32_t wy, uint16_t sun_azimuth)
{
	return nd_field_sample_at_azimuth_step(world, lod, nd_field_lod_to_step_q16(lod), wx, wy, sun_azimuth);
}

static inline nd_field_sample_t
nd_field_sample_step(uint16_t world, int32_t step_q16, int32_t wx, int32_t wy)
{
	return nd_field_sample_at_azimuth_step(world, 3, step_q16, wx, wy, nd_field_get_sun_azimuth());
}

/* Row-coherent paired climate octave cache: hoists row-constant fade_y and caches
 * cell-interpolated (b0, b1) across consecutive samples sharing cell s1, sharing fade_x
 * between rainfall (cl) and temperature (tm). Bit-exact with nd_field_eval_coarse_oct_q16. */
typedef struct {
	unsigned x;
	unsigned seed_cl;
	unsigned seed_tm;
	uint16_t mask;
	int16_t  s0;
	int16_t  s1;
	uint32_t fade_y;
	uint32_t cl_b0, cl_b1;
	uint32_t tm_b0, tm_b1;
	uint8_t  active;
	uint8_t  valid;
} nd_field_row_oct_pair_t;

static inline void
nd_field_row_oct_pair_init(nd_field_row_oct_pair_t *o, int16_t room_y, uint32_t fy,
                           unsigned x, unsigned seed_cl, unsigned seed_tm, uint8_t active)
{
	o->x = x;
	o->seed_cl = seed_cl;
	o->seed_tm = seed_tm;
	o->active = active;
	o->valid = 0;
	if (!active)
		return;
	uint16_t mask = COORDMASK_LSHIFT(x);
	o->mask = mask;
	o->s0 = (int16_t)(room_y & mask);
	uint32_t fiy = (uint16_t)room_y & (uint16_t)~mask;
	uint32_t uy = (fiy << 16) | fy;
	o->fade_y = noise_wfade5(uy, x + 16);
}

static inline void
nd_field_row_oct_pair_eval(nd_field_row_oct_pair_t *o, int16_t room_x, uint32_t fx,
                           uint32_t *out_cl, uint32_t *out_tm)
{
	if (!o->active) {
		*out_cl = 32768u;
		*out_tm = 32768u;
		return;
	}
	int16_t s1 = (int16_t)(room_x & o->mask);
	if (!o->valid || o->s1 != s1) {
		int16_t s[2] = { o->s0, s1 };
		uint32_t v[4];
		noise_get_v(v, s, o->x, 8, o->seed_cl, 2);
		o->cl_b0 = noise_wstep(v[0], v[2], o->fade_y, o->x + 16);
		o->cl_b1 = noise_wstep(v[1], v[3], o->fade_y, o->x + 16);
		noise_get_v(v, s, o->x, 8, o->seed_tm, 2);
		o->tm_b0 = noise_wstep(v[0], v[2], o->fade_y, o->x + 16);
		o->tm_b1 = noise_wstep(v[1], v[3], o->fade_y, o->x + 16);
		o->s1 = s1;
		o->valid = 1;
	}
	uint32_t fix = (uint16_t)room_x & (uint16_t)~o->mask;
	uint32_t ux = (fix << 16) | fx;
	uint32_t fade_x = noise_wfade5(ux, o->x + 16);
	*out_cl = noise_wstep(o->cl_b0, o->cl_b1, fade_x, o->x + 16);
	*out_tm = noise_wstep(o->tm_b0, o->tm_b1, fade_x, o->x + 16);
}

static inline void
nd_field_lat_base_params(int16_t pos_y, int32_t *out_base, int32_t *out_tm_amp)
{
	int32_t abs_ry = abs((int32_t)pos_y);
	int32_t eq_dist = (abs_ry <= 16384) ? abs_ry : (32768 - abs_ry);
	int32_t cos_phi = nd_field_cos_q15((uint16_t)pos_y);
	int32_t lat_insol_q15 = abs(cos_phi);
	int32_t inner_q15 = (cos_phi < 0) ? -cos_phi : 0;

	int32_t lat_frac_q15 = (((eq_dist << 1) * 10486) + ((32768 - lat_insol_q15) * 22282)) >> 15;
	int32_t lat_cool = (lat_frac_q15 * 142) >> 15;
	if (eq_dist > 10800) {
		lat_cool += ((eq_dist - 10800) * 52) / (16384 - 10800);
	}
	int32_t inner_eclipse_cool = (inner_q15 * 4) >> 15;
	*out_base = 96 - lat_cool - inner_eclipse_cool;
	*out_tm_amp = (eq_dist > 11500) ? 12 : 18;
}

typedef struct {
	uint32_t planet_epoch;
	uint16_t world;
	uint8_t  lod;
	uint16_t sun_azimuth;
	int32_t  step_q16;
	int32_t  wy;
	int32_t  rw, rh;
	int32_t  half_rw, half_rh;
	int32_t  base_wl;
	int32_t  ry_q16;
	int16_t  room_y;
	int16_t  room_y_next;
	int16_t  up_ry;
	int16_t  up_ry_next;
	int16_t  ry_snap;
	uint32_t fy;
	uint32_t ty;

	/* Hoisted climate weights & moisture belts */
	uint32_t w_macro, w_prov, w_reg, w_loc;
	int32_t  itcz_lift;
	int32_t  hadley_t;
	int32_t  mid_span;
	int32_t  bor_wet;

	/* Hoisted latitudinal climate base */
	int32_t  lat_base0, tm_amp0;
	int32_t  lat_base1, tm_amp1;

	/* Hoisted step gates */
	uint8_t  coarse_direct; /* step_q16 >= 2048<<16 */
	uint8_t  macro_snap;    /* step_q16 >= 640<<16 */
	uint8_t  has_lake;
	uint8_t  has_river;
	uint8_t  has_fine;
	uint8_t  has_volc;

	/* Paired row-coherent climate octave caches */
	nd_field_row_oct_pair_t oct_prov;
	nd_field_row_oct_pair_t oct_reg;
	nd_field_row_oct_pair_t oct_loc;

	/* Per-room_x cached 4-corner room-grid stencils & solar dot */
	uint8_t  rx_valid;
	int16_t  last_rx;
	int32_t  c00, c10, c01, c11;
	int32_t  u00, u10, u01, u11;
	uint32_t m_cl00, m_cl10, m_cl01, m_cl11;
	uint32_t m_tm00, m_tm10, m_tm01, m_tm11;
	int32_t  signed_dot_q15;

	uint8_t  snap_valid;
	int16_t  last_rx_snap;
	uint32_t snap_cl, snap_tm;
} nd_field_row_ctx_t;

static inline void
nd_field_row_ctx_init(nd_field_row_ctx_t *ctx, uint16_t world, uint8_t lod,
                      int32_t step_q16, int32_t wy, uint16_t sun_azimuth)
{
	ctx->planet_epoch = nd_field_get_planet_epoch();
	ctx->world = world;
	ctx->lod = lod;
	ctx->sun_azimuth = sun_azimuth;
	ctx->step_q16 = step_q16;
	ctx->wy = wy;
	ctx->rw = nd_field_get_room_w();
	ctx->rh = nd_field_get_room_h();
	ctx->half_rw = ctx->rw / 2;
	ctx->half_rh = ctx->rh / 2;
	ctx->base_wl = nd_field_water_level(world);

	int64_t oy = (int64_t)(ctx->half_rh - wy) * 65536LL;
	ctx->ry_q16 = (int32_t)nd_field_fdiv64(oy, ctx->rh);
	ctx->room_y = (int16_t)(ctx->ry_q16 >> 16);
	ctx->room_y_next = (int16_t)(ctx->room_y + 1);
	ctx->up_ry = (int16_t)(ctx->room_y + 3);
	ctx->up_ry_next = (int16_t)(ctx->up_ry + 1);
	ctx->ry_snap = (int16_t)((ctx->ry_q16 + 32768) >> 16);
	ctx->fy = (uint32_t)(ctx->ry_q16 & 0xFFFF);
	ctx->ty = (ctx->fy != 0u) ? noise_wfade5(ctx->fy, 16) : 0u;

	int32_t abs_ry0 = abs((int32_t)ctx->room_y);
	int32_t eq_dist = (abs_ry0 <= 16384) ? abs_ry0 : (32768 - abs_ry0);
	int32_t cos_phi = nd_field_cos_q15((uint16_t)ctx->room_y);
	if (ctx->fy != 0u) {
		int32_t abs_ry1 = abs((int32_t)ctx->room_y_next);
		int32_t eq_dist1 = (abs_ry1 <= 16384) ? abs_ry1 : (32768 - abs_ry1);
		int32_t cos_phi1 = nd_field_cos_q15((uint16_t)ctx->room_y_next);
		eq_dist += (int32_t)(((int64_t)(eq_dist1 - eq_dist) * ctx->fy) >> 16);
		cos_phi += (int32_t)(((int64_t)(cos_phi1 - cos_phi) * ctx->fy) >> 16);
	}
	int32_t inner_q15 = (cos_phi < 0) ? -cos_phi : 0;
	uint32_t inner_shift = (uint32_t)((inner_q15 * 10) >> 15);
	ctx->w_macro = 20u;
	ctx->w_prov  = 35u + inner_shift;
	ctx->w_reg   = 30u - (inner_shift * 6u) / 10u;
	ctx->w_loc   = 100u - (ctx->w_macro + ctx->w_prov + ctx->w_reg);

	ctx->itcz_lift = (eq_dist < 2800) ? (((2800 - eq_dist) * 85) / 2800) : 0;
	ctx->hadley_t = 0;
	if (eq_dist >= 3000 && eq_dist <= 6200) {
		int32_t hc = abs(eq_dist - 4600);
		if (hc < 1600) ctx->hadley_t = 1600 - hc;
	}
	ctx->mid_span = 0;
	if (eq_dist >= 6200 && eq_dist <= 9800) {
		int32_t md = abs(eq_dist - 7800);
		if (md < 1600) ctx->mid_span = 1600 - md;
	}
	ctx->bor_wet = 0;
	if (eq_dist >= 9800 && eq_dist <= 12800) {
		int32_t bd = abs(eq_dist - 11300);
		if (bd < 1500) ctx->bor_wet = (1500 - bd) * 85 / 1500;
	}

	nd_field_lat_base_params(ctx->room_y, &ctx->lat_base0, &ctx->tm_amp0);
	if (ctx->fy != 0u) {
		nd_field_lat_base_params(ctx->room_y_next, &ctx->lat_base1, &ctx->tm_amp1);
	} else {
		ctx->lat_base1 = ctx->lat_base0;
		ctx->tm_amp1 = ctx->tm_amp0;
	}

	ctx->coarse_direct = (step_q16 >= (2048 << 16)) ? 1u : 0u;
	ctx->macro_snap    = (step_q16 >= (640 << 16)) ? 1u : 0u;
	ctx->has_lake      = (chaff_wavelength_weight_q16(480 << 16, step_q16) > 0u) ? 1u : 0u;
	ctx->has_river     = (step_q16 <= (880 << 16)) ? 1u : 0u;
	ctx->has_fine      = (lod > 0 && chaff_octave_weight_q16(ND_FIELD_SHIFT_FINE_MAX, step_q16) > 0u) ? 1u : 0u;
	ctx->has_volc      = (step_q16 <= (256 << 16)) ? 1u : 0u;

	nd_field_row_oct_pair_init(&ctx->oct_prov, ctx->room_y, ctx->fy, 10, 101 + world, 153 + world, 1u);
	nd_field_row_oct_pair_init(&ctx->oct_reg,  ctx->room_y, ctx->fy, 7,  1 + world,   53 + world,
	                           (step_q16 < (16384 << 16)) ? 1u : 0u);
	nd_field_row_oct_pair_init(&ctx->oct_loc,  ctx->room_y, ctx->fy, 4,  201 + world, 253 + world,
	                           (step_q16 < (2048 << 16)) ? 1u : 0u);

	ctx->rx_valid = 0;
	ctx->snap_valid = 0;
}

static inline void
nd_field_row_climate_and_river(nd_field_row_ctx_t *ctx, int32_t wx, int16_t room_x, uint32_t fx,
                               int32_t *coarse_q12_4, int32_t up_q12_4,
                               uint32_t cl_macro, uint32_t tm_macro, int32_t signed_dot_q15,
                               nd_field_sample_t *s, int32_t *out_rug_q8, uint16_t *out_river_damp)
{
	const uint16_t world = ctx->world;
	const int32_t step_q16 = ctx->step_q16;
	const int32_t wy = ctx->wy;

	s->water_level = ctx->base_wl;
	if (ctx->has_lake) {
		nd_field_eval_mountain_lake_step(world, step_q16, wx, wy, coarse_q12_4, &s->water_level);
	}

	uint32_t cl_prov, tm_prov, cl_reg, tm_reg, cl_loc, tm_loc;
	nd_field_row_oct_pair_eval(&ctx->oct_prov, room_x, fx, &cl_prov, &tm_prov);
	nd_field_row_oct_pair_eval(&ctx->oct_reg,  room_x, fx, &cl_reg,  &tm_reg);
	nd_field_row_oct_pair_eval(&ctx->oct_loc,  room_x, fx, &cl_loc,  &tm_loc);

	uint32_t cl_raw = (cl_macro * ctx->w_macro + cl_prov * ctx->w_prov + cl_reg * ctx->w_reg + cl_loc * ctx->w_loc) / 100u;
	uint32_t tm_raw = (tm_macro * 35u + tm_prov * 35u + tm_reg * 20u + tm_loc * 10u) / 100u;

	int32_t rn_centered = (int32_t)(cl_raw >> 15) - 256;
	int32_t rn_val = 256 + (rn_centered * 20) / 10 + ctx->itcz_lift;
	if (ctx->hadley_t > 0) {
		int32_t desert_rn = 72 + ((rn_val * 42) / 512);
		rn_val = rn_val - (((rn_val - desert_rn) * ctx->hadley_t * 78) / (1600 * 100));
	}
	if (ctx->mid_span > 0 && rn_val < 420) {
		int32_t wet_lift = ((420 - rn_val) * ctx->mid_span * 58) / (1600 * 100);
		rn_val += wet_lift;
	}
	rn_val += ctx->bor_wet;

	int32_t up_hyp = up_q12_4 >> 4;
	int32_t cur_hyp = *coarse_q12_4 >> 4;
	int32_t sea_lvl = s->water_level;
	rn_val += nd_field_orographic_rain_delta(cur_hyp, up_hyp, sea_lvl);
	if (rn_val < 0) rn_val = 0;
	if (rn_val > (int32_t)ND_FIELD_RAIN_MAX) rn_val = ND_FIELD_RAIN_MAX;
	s->rain = (uint32_t)rn_val;

	int32_t rug_q8 = -1;
	uint16_t river_fine_damp_q8 = 0;
	if (ctx->has_river) {
		nd_field_eval_river_ex(world, wx, wy, s->rain, coarse_q12_4, &s->water_level,
		                       &s->river_carve_q4, &river_fine_damp_q8, &rug_q8);
	}
	*out_rug_q8 = rug_q8;
	*out_river_damp = river_fine_damp_q8;

	int32_t elev_rel_coarse = (int32_t)(*coarse_q12_4 >> 4) - (int32_t)s->water_level;
	int32_t tm_var = (int32_t)(tm_raw >> 16) - 128;
	int32_t lapse = nd_field_lapse_cool(elev_rel_coarse);
	int32_t bt0 = ctx->lat_base0 + (tm_var * ctx->tm_amp0 / 128) - lapse;
	if (bt0 < -32000) bt0 = -32000;
	if (bt0 > 32000) bt0 = 32000;
	int16_t bio_temp = (int16_t)bt0;
	if (ctx->fy != 0u) {
		int32_t bt1 = ctx->lat_base1 + (tm_var * ctx->tm_amp1 / 128) - lapse;
		if (bt1 < -32000) bt1 = -32000;
		if (bt1 > 32000) bt1 = 32000;
		bio_temp = (int16_t)(bt0 + (int32_t)(((int64_t)(bt1 - bt0) * ctx->ty) >> 16));
	}

	int32_t diurnal_amp = 15;
	if (*coarse_q12_4 < ((int32_t)s->water_level << 4)) {
		diurnal_amp = 13;
	} else if (s->rain < 200u) {
		diurnal_amp += (int32_t)(((200u - s->rain) * 3u) / 200u);
	}
	int16_t diurnal_delta = (int16_t)((signed_dot_q15 * diurnal_amp) >> 15);
	s->climate_temp = bio_temp;
	s->temp = (int16_t)(bio_temp + diurnal_delta);
	nd_field_bio_blend_topo(s->rain, bio_temp, elev_rel_coarse, s->river_carve_q4, river_fine_damp_q8,
	                        &s->bio, &s->bio2, &s->blend);

	if (*coarse_q12_4 >= ((int32_t)s->water_level << 4) && (s->bio == BIOME_DESERT || s->rain < 150)) {
		int32_t elev_above_wl = (int32_t)(*coarse_q12_4 >> 4) - (int32_t)s->water_level;
		if (elev_above_wl >= 0 && elev_above_wl < 12) {
			int32_t oasis_wick = ((12 - elev_above_wl) * 115) / 12;
			s->rain += (uint32_t)oasis_wick;
			if (s->rain > ND_FIELD_RAIN_MAX) s->rain = ND_FIELD_RAIN_MAX;
			nd_field_bio_blend_topo(s->rain, bio_temp, elev_rel_coarse, s->river_carve_q4, river_fine_damp_q8,
			                        &s->bio, &s->bio2, &s->blend);
		}
	}
}

static inline void
nd_field_row_finalize_sample(const nd_field_row_ctx_t *ctx, int32_t wx, int32_t rx_q16, int16_t room_x,
                             int32_t coarse_q12_4, int32_t fine_q12_4, uint16_t river_fine_damp_q8,
                             nd_field_sample_t *s)
{
	const int32_t wy = ctx->wy;
	if (ctx->has_fine) {
		int32_t rel_water_q12_4 = coarse_q12_4 - ((int32_t)s->water_level << 4);
		if (rel_water_q12_4 <= -(8 << 4)) {
			fine_q12_4 >>= 1;
		} else if (rel_water_q12_4 <= 0) {
			int32_t w_sub = 64 + ((-rel_water_q12_4) >> 1);
			fine_q12_4 = (int32_t)(((int64_t)fine_q12_4 * w_sub) >> 8);
		} else if (rel_water_q12_4 < (6 << 4)) {
			int32_t w_coast = 64 + ((rel_water_q12_4 * 192) / (6 << 4));
			fine_q12_4 = (int32_t)(((int64_t)fine_q12_4 * w_coast) >> 8);
		} else {
			if (rel_water_q12_4 < (220 << 4) && (s->bio == BIOME_DESERT || s->bio2 == BIOME_DESERT || s->rain < 130)) {
				int32_t w_des_bio = ((s->bio == BIOME_DESERT) ? (256 - (int32_t)s->blend) : 0) +
				                    ((s->bio2 == BIOME_DESERT) ? (int32_t)s->blend : 0);
				int32_t w_des_rain = (s->rain < 130) ? ((130 - (int32_t)s->rain) * 256 / 130) : 0;
				int32_t sand_factor_q8 = (w_des_rain > ((w_des_bio * 180) >> 8)) ? w_des_rain : ((w_des_bio * 180) >> 8);
				if (rel_water_q12_4 > (160 << 4)) {
					sand_factor_q8 = (sand_factor_q8 * ((220 << 4) - rel_water_q12_4)) / (60 << 4);
				}
				if (sand_factor_q8 > 256) sand_factor_q8 = 256;

				if (sand_factor_q8 > 0) {
					int32_t wind_proj = (wx * 759 - wy * 687) >> 10;
					uint16_t dune_turn = (uint16_t)(((uint32_t)(wind_proj & 0xFFFF) * 65536u) / 192u);
					int32_t s1 = nd_field_sin_q15(dune_turn);
					int32_t s2 = nd_field_sin_q15((uint16_t)(dune_turn << 1));
					int32_t dune_wave_q15 = s1 - ((s2 * 35) / 100);
					int32_t dune_amp_q12_4 = (dune_wave_q15 * (10 << 4)) >> 15;

					fine_q12_4 = fine_q12_4 - (((fine_q12_4 * 55) / 100) * sand_factor_q8 >> 8) +
					             ((dune_amp_q12_4 * sand_factor_q8) >> 8);
				}
			}

			int32_t w1_q8 = 256 - (int32_t)s->blend;
			int32_t w2_q8 = (int32_t)s->blend;

			int32_t damp1 = 0, damp2 = 0;
			if (rel_water_q12_4 < (160 << 4)) {
				int32_t plain_e_w = (rel_water_q12_4 <= (110 << 4))
					? 256 : (((160 << 4) - rel_water_q12_4) * 256 / (50 << 4));
				if (s->bio == BIOME_TEMPERATE_GRASSLAND) damp1 = (140 * plain_e_w) >> 8;
				else if (s->bio == BIOME_SAVANNAH || s->bio == BIOME_ALPINE_MEADOW) damp1 = (112 * plain_e_w) >> 8;

				if (s->bio2 == BIOME_TEMPERATE_GRASSLAND) damp2 = (140 * plain_e_w) >> 8;
				else if (s->bio2 == BIOME_SAVANNAH || s->bio2 == BIOME_ALPINE_MEADOW) damp2 = (112 * plain_e_w) >> 8;
			}
			if (rel_water_q12_4 < (44 << 4)) {
				int32_t marsh_e_w = (rel_water_q12_4 <= (24 << 4))
					? 256 : (((44 << 4) - rel_water_q12_4) * 256 / (20 << 4));
				if (s->bio == BIOME_WETLAND) damp1 = (176 * marsh_e_w) >> 8;
				if (s->bio2 == BIOME_WETLAND) damp2 = (176 * marsh_e_w) >> 8;
			}
			int32_t total_damp_q8 = (damp1 * w1_q8 + damp2 * w2_q8) >> 8;
			if (total_damp_q8 > 0) {
				fine_q12_4 = (int32_t)(((int64_t)fine_q12_4 * (256 - total_damp_q8)) >> 8);
			}

			if (rel_water_q12_4 > (14 << 4)) {
				int w1_terr = (s->bio == BIOME_COLD_DESERT || s->bio == BIOME_SHRUBLAND || s->bio == BIOME_MEDITERRANEAN) ? 1 : 0;
				int w2_terr = (s->bio2 == BIOME_COLD_DESERT || s->bio2 == BIOME_SHRUBLAND || s->bio2 == BIOME_MEDITERRANEAN) ? 1 : 0;
				if (w1_terr | w2_terr) {
					int32_t terr_e_w = (rel_water_q12_4 >= (26 << 4))
						? 256 : (((rel_water_q12_4 - (14 << 4)) * 256) / (12 << 4));
					int32_t terr_w_q8 = ((w1_terr * w1_q8 + w2_terr * w2_q8) * terr_e_w) >> 8;
					int32_t total_est_q4 = coarse_q12_4 + fine_q12_4;
					uint16_t step_phase = (uint16_t)((total_est_q4 & 0xFF) << 8);
					int32_t terrace_q4 = (nd_field_sin_q15(step_phase) * 18) >> 15;
					fine_q12_4 += (terrace_q4 * terr_w_q8) >> 8;
				}
			}

			if (rel_water_q12_4 > (140 << 4)) {
				int32_t alp_w_q8 = nd_field_smoothstep_q8(rel_water_q12_4, 140 << 4, 460 << 4);
				int32_t alp_scale_q8 = 256 + ((alp_w_q8 * 192) >> 8);
				fine_q12_4 = (int32_t)(((int64_t)fine_q12_4 * (int64_t)alp_scale_q8) >> 8);
			}
		}

		if (river_fine_damp_q8 > 0) {
			fine_q12_4 = (int32_t)(((int64_t)fine_q12_4 * (256 - (int32_t)river_fine_damp_q8)) >> 8);
		}

		int32_t max_fine_q12_4 = (int32_t)ND_FIELD_FINE_AMP_BUDGET << 4;
		if (fine_q12_4 > max_fine_q12_4) fine_q12_4 = max_fine_q12_4;
		if (fine_q12_4 < -max_fine_q12_4) fine_q12_4 = -max_fine_q12_4;
	}

	s->coarse_elev = coarse_q12_4 >> 4;
	s->fine_elev = fine_q12_4 >> 4;

	int32_t total_q12_4 = coarse_q12_4 + fine_q12_4;
	if (total_q12_4 > (500000 << 4)) total_q12_4 = (500000 << 4);
	if (total_q12_4 < -(500000 << 4)) total_q12_4 = -(500000 << 4);
	s->elevation = total_q12_4 >> 4;
	s->elevation_q12_4 = (ctx->lod == 0) ? (s->elevation << 4) : total_q12_4;

	if (s->elevation < s->water_level) {
		s->is_water = 1;
		s->depth = s->water_level - s->elevation;
	} else {
		s->is_water = 0;
		s->depth = 0;
	}

	if (ctx->has_volc && !s->is_water && s->elevation > s->water_level + 20) {
		int16_t true_rx = (ctx->lod > 0) ? (int16_t)((rx_q16 + 32768) >> 16) : room_x;
		int16_t true_ry = (ctx->lod > 0) ? ctx->ry_snap : ctx->room_y;
		uint32_t v_caldera = chaff_sample_torus_macro((int32_t)true_rx, (int32_t)true_ry, 6, 0x564F4C43u);
		if ((v_caldera & 0x1FFu) <= 1u) {
			uint8_t host_bio = s->bio;
			s->bio = BIOME_VOLCANIC;
			if (ctx->lod > 0) {
				int32_t off_x = ((rx_q16 + 32768) & 0xFFFF) - 32768;
				int32_t off_y = ((ctx->ry_q16 + 32768) & 0xFFFF) - 32768;
				int32_t ax = (off_x < 0) ? -off_x : off_x;
				int32_t ay = (off_y < 0) ? -off_y : off_y;
				int32_t rad = (ax > ay) ? (ax + (ay >> 1)) : (ay + (ax >> 1));
				if (rad > 18000) {
					s->bio2 = host_bio;
					int32_t b_v = ((rad - 18000) * 128) / (32768 - 18000);
					if (b_v > 128) b_v = 128;
					s->blend = (uint8_t)b_v;
				} else {
					s->blend = 0;
				}
			} else {
				s->blend = 0;
			}
		}
	}
}

static inline nd_field_sample_t
nd_field_row_sample(nd_field_row_ctx_t *ctx, int32_t wx)
{
	nd_field_sample_t s;
	memset(&s, 0, sizeof(s));

	const uint16_t world = ctx->world;
	const int32_t step_q16 = ctx->step_q16;
	const int32_t wy = ctx->wy;

	int64_t ox = (int64_t)(wx - ctx->half_rw) * 65536LL;
	int32_t rx_q16 = (int32_t)nd_field_fdiv64(ox, ctx->rw);
	int16_t room_x = (int16_t)(rx_q16 >> 16);
	uint32_t fx = (uint32_t)(rx_q16 & 0xFFFF);

	if (!ctx->rx_valid || ctx->last_rx != room_x) {
		ctx->last_rx = room_x;
		ctx->rx_valid = 1;
		int16_t rx_next = (int16_t)(room_x + 1);
		int16_t up_rx = (int16_t)(room_x - 3);
		if (ctx->coarse_direct) {
			ctx->c00 = nd_field_room_hyp_coarse_step(world, step_q16, room_x, ctx->room_y) << 4;
			ctx->u00 = nd_field_room_hyp_coarse_step(world, step_q16, up_rx, ctx->up_ry) << 4;
		} else {
			int32_t cq[4], uq[4];
			nd_field_room_hyp_coarse_quad(world, room_x, ctx->room_y, cq);
			ctx->c00 = cq[0] << 4;
			ctx->c10 = cq[1] << 4;
			ctx->c01 = cq[2] << 4;
			ctx->c11 = cq[3] << 4;

			nd_field_room_hyp_coarse_quad(world, up_rx, ctx->up_ry, uq);
			ctx->u00 = uq[0] << 4;
			ctx->u10 = uq[1] << 4;
			ctx->u01 = uq[2] << 4;
			ctx->u11 = uq[3] << 4;
		}
		if (!ctx->macro_snap) {
			nd_field_room_macro_memo(world, room_x, ctx->room_y, &ctx->m_cl00, &ctx->m_tm00);
			nd_field_room_macro_memo(world, rx_next, ctx->room_y, &ctx->m_cl10, &ctx->m_tm10);
			nd_field_room_macro_memo(world, room_x, ctx->room_y_next, &ctx->m_cl01, &ctx->m_tm01);
			nd_field_room_macro_memo(world, rx_next, ctx->room_y_next, &ctx->m_cl11, &ctx->m_tm11);
		}
		ctx->signed_dot_q15 = nd_field_torus_signed_solar_q15(room_x, ctx->room_y, ctx->sun_azimuth);
	}

	int32_t coarse_q12_4;
	int32_t up_q12_4;
	uint32_t cl_macro = 0, tm_macro = 0;

	if (ctx->coarse_direct) {
		coarse_q12_4 = ctx->c00;
		up_q12_4 = ctx->u00;
	} else if (fx == 0u && ctx->fy == 0u) {
		coarse_q12_4 = ctx->c00;
		up_q12_4 = ctx->u00;
		if (!ctx->macro_snap) {
			cl_macro = ctx->m_cl00;
			tm_macro = ctx->m_tm00;
		}
	} else {
		uint32_t tx = noise_wfade5(fx, 16);
		uint32_t ty = ctx->ty;

		int32_t q0 = ctx->c00 + (int32_t)(((int64_t)(ctx->c10 - ctx->c00) * tx) >> 16);
		int32_t q1 = ctx->c01 + (int32_t)(((int64_t)(ctx->c11 - ctx->c01) * tx) >> 16);
		coarse_q12_4 = q0 + (int32_t)(((int64_t)(q1 - q0) * ty) >> 16);

		int32_t u0 = ctx->u00 + (int32_t)(((int64_t)(ctx->u10 - ctx->u00) * tx) >> 16);
		int32_t u1 = ctx->u01 + (int32_t)(((int64_t)(ctx->u11 - ctx->u01) * tx) >> 16);
		up_q12_4 = u0 + (int32_t)(((int64_t)(u1 - u0) * ty) >> 16);

		if (!ctx->macro_snap) {
			int64_t cl0 = (int64_t)ctx->m_cl00 + (((int64_t)ctx->m_cl10 - (int64_t)ctx->m_cl00) * (int64_t)tx >> 16);
			int64_t cl1 = (int64_t)ctx->m_cl01 + (((int64_t)ctx->m_cl11 - (int64_t)ctx->m_cl01) * (int64_t)tx >> 16);
			cl_macro = (uint32_t)(cl0 + ((cl1 - cl0) * (int64_t)ty >> 16));

			int64_t tm0 = (int64_t)ctx->m_tm00 + (((int64_t)ctx->m_tm10 - (int64_t)ctx->m_tm00) * (int64_t)tx >> 16);
			int64_t tm1 = (int64_t)ctx->m_tm01 + (((int64_t)ctx->m_tm11 - (int64_t)ctx->m_tm01) * (int64_t)tx >> 16);
			tm_macro = (uint32_t)(tm0 + ((tm1 - tm0) * (int64_t)ty >> 16));
		}
	}

	if (ctx->macro_snap) {
		int16_t rx_snap = (int16_t)((rx_q16 + 32768) >> 16);
		if (!ctx->snap_valid || ctx->last_rx_snap != rx_snap) {
			nd_field_room_macro_memo(world, rx_snap, ctx->ry_snap, &ctx->snap_cl, &ctx->snap_tm);
			ctx->last_rx_snap = rx_snap;
			ctx->snap_valid = 1;
		}
		cl_macro = ctx->snap_cl;
		tm_macro = ctx->snap_tm;
	}

	int32_t rug_q8 = -1;
	uint16_t river_fine_damp_q8 = 0;
	nd_field_row_climate_and_river(ctx, wx, room_x, fx, &coarse_q12_4, up_q12_4,
	                               cl_macro, tm_macro, ctx->signed_dot_q15,
	                               &s, &rug_q8, &river_fine_damp_q8);

	int32_t fine_q12_4 = ctx->has_fine
		? nd_field_elevation_fine_step_q12_4(world, step_q16, wx, wy, &rug_q8)
		: 0;
	nd_field_row_finalize_sample(ctx, wx, rx_q16, room_x, coarse_q12_4, fine_q12_4, river_fine_damp_q8, &s);
	return s;
}

/* 4-wide SIMD row-coherent field sampler: evaluates 4 world X coordinates on ctx->wy.
 * Bit-exact with 4 calls to nd_field_row_sample(ctx, wx[i]). */
static inline void
nd_field_row_sample_4x(nd_field_row_ctx_t *ctx, const int32_t wx[4], nd_field_sample_t out[4])
{
	memset(out, 0, sizeof(nd_field_sample_t) * 4u);

	const uint16_t world = ctx->world;
	const int32_t step_q16 = ctx->step_q16;
	const int32_t wy = ctx->wy;

	int32_t rx_q16[4];
	int16_t room_x[4];
	uint32_t fx[4];
	for (int i = 0; i < 4; i++) {
		int64_t ox = (int64_t)(wx[i] - ctx->half_rw) * 65536LL;
		rx_q16[i] = (int32_t)nd_field_fdiv64(ox, ctx->rw);
		room_x[i] = (int16_t)(rx_q16[i] >> 16);
		fx[i] = (uint32_t)(rx_q16[i] & 0xFFFF);
	}

	int32_t coarse_q12_4[4];
	int32_t up_q12_4[4];
	uint32_t cl_macro[4] = { 0, 0, 0, 0 };
	uint32_t tm_macro[4] = { 0, 0, 0, 0 };
	int32_t signed_dot[4];

	if (ctx->coarse_direct) {
		int32_t rx4[4] = { room_x[0], room_x[1], room_x[2], room_x[3] };
		int32_t up_rx[4] = {
			(int16_t)(room_x[0] - 3), (int16_t)(room_x[1] - 3),
			(int16_t)(room_x[2] - 3), (int16_t)(room_x[3] - 3)
		};
		int32_t ry4[4] = { ctx->room_y, ctx->room_y, ctx->room_y, ctx->room_y };
		int32_t up_ry4[4] = { ctx->up_ry, ctx->up_ry, ctx->up_ry, ctx->up_ry };
		int32_t c_hyp[4], u_hyp[4];
		nd_field_room_hyp_coarse_4x(world, step_q16, rx4, ry4, c_hyp);
		nd_field_room_hyp_coarse_4x(world, step_q16, up_rx, up_ry4, u_hyp);
		for (int i = 0; i < 4; i++) {
			coarse_q12_4[i] = c_hyp[i] << 4;
			up_q12_4[i] = u_hyp[i] << 4;
			if (!ctx->rx_valid || ctx->last_rx != room_x[i]) {
				ctx->last_rx = room_x[i];
				ctx->rx_valid = 1;
				ctx->c00 = coarse_q12_4[i];
				ctx->u00 = up_q12_4[i];
				ctx->signed_dot_q15 = nd_field_torus_signed_solar_q15(room_x[i], ctx->room_y, ctx->sun_azimuth);
			}
			signed_dot[i] = ctx->signed_dot_q15;
		}
	} else {
		uint32_t tx4[4];
		noise_wfade5_4x(fx, 16, tx4);
		uint32_t ty = ctx->ty;

		for (int i = 0; i < 4; i++) {
			if (!ctx->rx_valid || ctx->last_rx != room_x[i]) {
				ctx->last_rx = room_x[i];
				ctx->rx_valid = 1;
				int16_t rx_next = (int16_t)(room_x[i] + 1);
				int16_t up_rx = (int16_t)(room_x[i] - 3);
				int32_t cq[4], uq[4];
				nd_field_room_hyp_coarse_quad(world, room_x[i], ctx->room_y, cq);
				ctx->c00 = cq[0] << 4;
				ctx->c10 = cq[1] << 4;
				ctx->c01 = cq[2] << 4;
				ctx->c11 = cq[3] << 4;

				nd_field_room_hyp_coarse_quad(world, up_rx, ctx->up_ry, uq);
				ctx->u00 = uq[0] << 4;
				ctx->u10 = uq[1] << 4;
				ctx->u01 = uq[2] << 4;
				ctx->u11 = uq[3] << 4;

				if (!ctx->macro_snap) {
					nd_field_room_macro_memo(world, room_x[i], ctx->room_y, &ctx->m_cl00, &ctx->m_tm00);
					nd_field_room_macro_memo(world, rx_next, ctx->room_y, &ctx->m_cl10, &ctx->m_tm10);
					nd_field_room_macro_memo(world, room_x[i], ctx->room_y_next, &ctx->m_cl01, &ctx->m_tm01);
					nd_field_room_macro_memo(world, rx_next, ctx->room_y_next, &ctx->m_cl11, &ctx->m_tm11);
				}
				ctx->signed_dot_q15 = nd_field_torus_signed_solar_q15(room_x[i], ctx->room_y, ctx->sun_azimuth);
			}
			signed_dot[i] = ctx->signed_dot_q15;

			if (fx[i] == 0u && ctx->fy == 0u) {
				coarse_q12_4[i] = ctx->c00;
				up_q12_4[i] = ctx->u00;
				if (!ctx->macro_snap) {
					cl_macro[i] = ctx->m_cl00;
					tm_macro[i] = ctx->m_tm00;
				}
			} else {
				uint32_t tx = tx4[i];
				int32_t q0 = ctx->c00 + (int32_t)(((int64_t)(ctx->c10 - ctx->c00) * tx) >> 16);
				int32_t q1 = ctx->c01 + (int32_t)(((int64_t)(ctx->c11 - ctx->c01) * tx) >> 16);
				coarse_q12_4[i] = q0 + (int32_t)(((int64_t)(q1 - q0) * ty) >> 16);

				int32_t u0 = ctx->u00 + (int32_t)(((int64_t)(ctx->u10 - ctx->u00) * tx) >> 16);
				int32_t u1 = ctx->u01 + (int32_t)(((int64_t)(ctx->u11 - ctx->u01) * tx) >> 16);
				up_q12_4[i] = u0 + (int32_t)(((int64_t)(u1 - u0) * ty) >> 16);

				if (!ctx->macro_snap) {
					int64_t cl0 = (int64_t)ctx->m_cl00 + (((int64_t)ctx->m_cl10 - (int64_t)ctx->m_cl00) * (int64_t)tx >> 16);
					int64_t cl1 = (int64_t)ctx->m_cl01 + (((int64_t)ctx->m_cl11 - (int64_t)ctx->m_cl01) * (int64_t)tx >> 16);
					cl_macro[i] = (uint32_t)(cl0 + ((cl1 - cl0) * (int64_t)ty >> 16));

					int64_t tm0 = (int64_t)ctx->m_tm00 + (((int64_t)ctx->m_tm10 - (int64_t)ctx->m_tm00) * (int64_t)tx >> 16);
					int64_t tm1 = (int64_t)ctx->m_tm01 + (((int64_t)ctx->m_tm11 - (int64_t)ctx->m_tm01) * (int64_t)tx >> 16);
					tm_macro[i] = (uint32_t)(tm0 + ((tm1 - tm0) * (int64_t)ty >> 16));
				}
			}
		}
	}

	if (ctx->macro_snap) {
		for (int i = 0; i < 4; i++) {
			int16_t rx_snap = (int16_t)((rx_q16[i] + 32768) >> 16);
			if (!ctx->snap_valid || ctx->last_rx_snap != rx_snap) {
				nd_field_room_macro_memo(world, rx_snap, ctx->ry_snap, &ctx->snap_cl, &ctx->snap_tm);
				ctx->last_rx_snap = rx_snap;
				ctx->snap_valid = 1;
			}
			cl_macro[i] = ctx->snap_cl;
			tm_macro[i] = ctx->snap_tm;
		}
	}

	int32_t rug_q8[4] = { -1, -1, -1, -1 };
	uint16_t river_damp[4] = { 0, 0, 0, 0 };
	for (int i = 0; i < 4; i++) {
		nd_field_row_climate_and_river(ctx, wx[i], room_x[i], fx[i], &coarse_q12_4[i], up_q12_4[i],
		                               cl_macro[i], tm_macro[i], signed_dot[i],
		                               &out[i], &rug_q8[i], &river_damp[i]);
	}

	int32_t fine_q12_4[4] = { 0, 0, 0, 0 };
	if (ctx->has_fine) {
		int32_t wy4[4] = { wy, wy, wy, wy };
		nd_field_elevation_fine_step_q12_4_4x(world, step_q16, wx, wy4, rug_q8, fine_q12_4);
	}

	for (int i = 0; i < 4; i++) {
		nd_field_row_finalize_sample(ctx, wx[i], rx_q16[i], room_x[i],
		                             coarse_q12_4[i], fine_q12_4[i], river_damp[i], &out[i]);
	}
}

/* 4-wide SIMD field batch sampler on a single row wy (caches row context across calls).
 * Bit-exact with 4 calls to nd_field_sample_step(world, step_q16, wx[i], wy). */
static inline void
nd_field_sample_4x(uint16_t world, int32_t step_q16, const int32_t wx[4], int32_t wy,
                   nd_field_sample_t out[4])
{
	static _CHAFF_TLS nd_field_row_ctx_t s_row4_ctx;
	static _CHAFF_TLS int s_row4_valid = 0;
	uint32_t planet_epoch = nd_field_get_planet_epoch();
	uint16_t sun_azimuth = nd_field_get_sun_azimuth();
	int32_t rw = nd_field_get_room_w();
	int32_t rh = nd_field_get_room_h();
	if (!s_row4_valid ||
	    s_row4_ctx.planet_epoch != planet_epoch ||
	    s_row4_ctx.world != world ||
	    s_row4_ctx.step_q16 != step_q16 ||
	    s_row4_ctx.wy != wy ||
	    s_row4_ctx.sun_azimuth != sun_azimuth ||
	    s_row4_ctx.rw != rw ||
	    s_row4_ctx.rh != rh) {
		nd_field_row_ctx_init(&s_row4_ctx, world, 3, step_q16, wy, sun_azimuth);
		s_row4_valid = 1;
	}
	nd_field_row_sample_4x(&s_row4_ctx, wx, out);
}

static inline void
nd_field_sample_row_step(uint16_t world, int32_t step_q16, int32_t wy,
                         int32_t ox, int32_t span_w, uint32_t count,
                         nd_field_sample_t *out_row)
{
	if (!out_row || count == 0)
		return;
	uint16_t sun_azimuth = nd_field_get_sun_azimuth();
	nd_field_row_ctx_t ctx;
	nd_field_row_ctx_init(&ctx, world, 3, step_q16, wy, sun_azimuth);
	int32_t gdim = (count > 1u) ? (int32_t)(count - 1u) : 1;
	uint32_t c = 0;
	for (; c + 4u <= count; c += 4u) {
		int32_t wx4[4] = {
			ox + (int32_t)(((int64_t)(c + 0u) * span_w) / gdim),
			ox + (int32_t)(((int64_t)(c + 1u) * span_w) / gdim),
			ox + (int32_t)(((int64_t)(c + 2u) * span_w) / gdim),
			ox + (int32_t)(((int64_t)(c + 3u) * span_w) / gdim)
		};
		nd_field_row_sample_4x(&ctx, wx4, &out_row[c]);
	}
	for (; c < count; c++) {
		int32_t wx = ox + (int32_t)(((int64_t)c * span_w) / gdim);
		out_row[c] = nd_field_row_sample(&ctx, wx);
	}
}

static inline nd_field_sample_t
nd_field_sample(uint16_t world, uint8_t lod, int32_t wx, int32_t wy)
{
	return nd_field_sample_at_azimuth(world, lod, wx, wy, nd_field_get_sun_azimuth());
}

/* Evaluate planetary surface wind & vertical uplift at (wx, wy, time_ticks) via libcorolla */
static inline int
nd_field_sample_corolla_wind(uint16_t world, int32_t wx, int32_t wy,
                             double time_ticks, corolla_wind_t *out_wind)
{
	const corolla_climate_model_t *cm = nd_field_world_climate_model(world);
	double u = 0.0, v = 0.0;
	nd_field_world_to_corolla_uv((double)wx, (double)wy, &u, &v);

	int32_t rx_q16, ry_q16;
	nd_field_world_to_room_q16(wx, wy, &rx_q16, &ry_q16);
	int16_t rx = (int16_t)(rx_q16 >> 16);
	int16_t ry = (int16_t)(ry_q16 >> 16);

	int32_t h_e = nd_field_room_hyp_coarse(world, (int32_t)rx + 1, ry);
	int32_t h_w = nd_field_room_hyp_coarse(world, (int32_t)rx - 1, ry);
	int32_t h_n = nd_field_room_hyp_coarse(world, rx, (int32_t)ry + 1);
	int32_t h_s = nd_field_room_hyp_coarse(world, rx, (int32_t)ry - 1);
	double grad_u = (double)(h_e - h_w) * 0.01;
	double grad_v = (double)(h_n - h_s) * 0.01;

	double sun_dir[3];
	nd_field_corolla_sun_dir(world, nd_field_get_sun_azimuth(),
	                         nd_field_get_season_turn(), sun_dir);
	return corolla_eval_wind(cm, u, v, grad_u, grad_v, sun_dir, time_ticks, out_wind);
}

/* Evaluate full axial-tilt insolation, wind, advected clouds & Whittaker climate via libcorolla */
static inline int
nd_field_sample_corolla_climate(uint16_t world, int32_t wx, int32_t wy,
                                double time_ticks, corolla_climate_t *out_clim)
{
	const corolla_climate_model_t *cm = nd_field_world_climate_model(world);
	double u = 0.0, v = 0.0;
	nd_field_world_to_corolla_uv((double)wx, (double)wy, &u, &v);

	int32_t rx_q16, ry_q16;
	nd_field_world_to_room_q16(wx, wy, &rx_q16, &ry_q16);
	int16_t rx = (int16_t)(rx_q16 >> 16);
	int16_t ry = (int16_t)(ry_q16 >> 16);

	int32_t h_c = nd_field_room_hyp_coarse(world, rx, ry);
	int32_t h_e = nd_field_room_hyp_coarse(world, (int32_t)rx + 1, ry);
	int32_t h_w = nd_field_room_hyp_coarse(world, (int32_t)rx - 1, ry);
	int32_t h_n = nd_field_room_hyp_coarse(world, rx, (int32_t)ry + 1);
	int32_t h_s = nd_field_room_hyp_coarse(world, rx, (int32_t)ry - 1);
	double grad_u = (double)(h_e - h_w) * 0.01;
	double grad_v = (double)(h_n - h_s) * 0.01;

	double sun_dir[3];
	nd_field_corolla_sun_dir(world, nd_field_get_sun_azimuth(),
	                         nd_field_get_season_turn(), sun_dir);
	return corolla_eval_climate(cm, u, v, (double)h_c, grad_u, grad_v,
	                            sun_dir, time_ticks, out_clim);
}

#ifndef ND_XXH32_U32_DEFINED
#define ND_XXH32_U32_DEFINED 1
static inline uint32_t
nd_xxh32_u32(uint32_t val, uint32_t seed)
{
	uint32_t h32 = seed + 0x165667B1u + 4u;
	h32 += val * 0xC2B2AE3Du;
	h32 = ((h32 << 17) | (h32 >> 15)) * 0x27D4EB2Fu;
	h32 ^= h32 >> 15;
	h32 *= 0x85EBCA77u;
	h32 ^= h32 >> 13;
	h32 *= 0xC2B2AE3Du;
	h32 ^= h32 >> 16;
	return h32;
}
#endif

static inline void
nd_field_normalize_noise(const nd_field_sample_t *fs, uint16_t obits,
                         int32_t rx, int32_t ry,
                         uint32_t *out_he, uint32_t *out_w,
                         uint32_t *out_tm, uint32_t *out_cl)
{
	uint32_t w = nd_field_water_noise(obits);
	uint32_t rhash = nd_xxh32_u32((uint32_t)rx * 73856093u ^ (uint32_t)ry * 19349663u,
	                              (uint32_t)obits ^ 0x9E3779B9u);
	int64_t norm_he;
	if (fs->elevation <= 0) {
		norm_he = (int64_t)w + ((int64_t)fs->elevation * (int64_t)w) / 2000;
		if (norm_he < 0) norm_he = 0;
		if (norm_he > (int64_t)w) norm_he = (int64_t)w;
	} else {
		int64_t max_e = (int64_t)nd_field_active_max_elev_wu();
		norm_he = (int64_t)w + ((int64_t)fs->elevation * (int64_t)(NOISE_MAX - w)) / max_e;
		norm_he ^= (int64_t)(rhash & 0xFFFu);
		if (norm_he <= (int64_t)w) norm_he = (int64_t)w + 1 + (int64_t)(rhash & 0xFFu);
		if (norm_he > NOISE_MAX) norm_he = NOISE_MAX;
	}
	/* Use permanent climate_temp for normalized noise seed tm so flora/stone placement
	 * hashes (ty = xxh32(tm ^ he)) remain stationary across the diurnal day/night cycle. */
	uint32_t tm = (uint32_t)(((int64_t)(fs->climate_temp - ND_FIELD_TEMP_MIN) * NOISE_MAX) /
	                         (ND_FIELD_TEMP_MAX - ND_FIELD_TEMP_MIN));
	tm ^= (rhash >> 12) & 0xFFFu;
	if (out_he) *out_he = (uint32_t)norm_he;
	if (out_w)  *out_w  = w;
	if (out_tm) *out_tm = tm;
	if (out_cl) *out_cl = fs->rain * ND_FIELD_RAIN_DIV;
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

	int32_t step_q16 = nd_field_lod_to_step_q16(chunk->lod);
	uint16_t sun_azimuth = nd_field_get_sun_azimuth();

	for (uint16_t r = 0; r < h; r++) {
		int32_t wy = (chunk->lod == 0)
			? (-(int32_t)(int16_t)(chunk->oy + r) * ND_FIELD_ROOM_H + (ND_FIELD_ROOM_H / 2))
			: (chunk->oy + (int32_t)r * step_world);
		nd_field_row_ctx_t ctx;
		nd_field_row_ctx_init(&ctx, chunk->world, chunk->lod, step_q16, wy, sun_azimuth);
		size_t row_off = (size_t)r * w;
		for (uint16_t c = 0; c < w; c++) {
			int32_t wx = (chunk->lod == 0)
				? ((int32_t)(int16_t)(chunk->ox + c) * ND_FIELD_ROOM_W + (ND_FIELD_ROOM_W / 2))
				: (chunk->ox + (int32_t)c * step_world);
			nd_field_sample_t s = nd_field_row_sample(&ctx, wx);
			size_t idx = row_off + c;
			chunk->height[idx] = nd_field_elevation_to_block(s.elevation_q12_4);
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
