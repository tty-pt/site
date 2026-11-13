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
#include <ttypt/chaff.h>

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
 * Planetary Scale (2^16 Room Coordinate Lattice):
 *   - Planet Width & Height: 65,536 rooms (int16 lattice range -32,768..+32,767)
 *   - 1 Room = 160 x 128 continuous world units
 *   - Planetary Dimensions: 10,485,760 x 8,388,608 world units
 *   - Octaves incorporate planetary macro wavelengths (shifts 14..8 = 256..16,384 rooms)
 *     down to local topography (shifts 7..2) with zero magic numbers.
 * ============================================================================ */
#define ND_FIELD_PLANET_BITS        16
#define ND_FIELD_PLANET_ROOM_SPAN   (1 << ND_FIELD_PLANET_BITS) /* 65,536 rooms */
#define ND_FIELD_HEMISPHERE_SPAN    (ND_FIELD_PLANET_ROOM_SPAN / 2) /* 32,768 rooms */

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
 * deep ocean trenches (-800..-40), shallow coastal shelves (-12..0),
 * fertile lowlands/grasslands (0..15), rolling highlands (15..30),
 * and soaring alpine mountain peaks (30..1200). Zero magic numbers. */
static inline int16_t
nd_field_bimodal_hypsometry(int16_t elev)
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
			return (int16_t)(-deep);
		}
	} else {
		/* Landmass: coastal plain (0..8), fertile lowlands (8..25), rolling plateaus (25..60), mountain ranges (60..1854) */
		if (elev <= 8) {
			return (int16_t)(elev * 12 / 8); /* Crisp coastal slope lifting land cleanly above sea level */
		} else if (elev <= 25) {
			return (int16_t)(12 + (elev - 8) * 18 / 10); /* Lowlands & uplands: elevated +12..+42 */
		} else {
			/* High mountain sharpening: soaring up to 1,854 (Mount Everest) */
			int32_t m = elev - 25;
			int32_t peak = 43 + (m * 18) / 10 + (m * m * 105) / 100;
			if (peak > 1854) peak = 1854;
			return (int16_t)peak;
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

static inline uint16_t nd_field_get_sun_azimuth(void) { return g_nd_field_sun_azimuth; }
static inline void nd_field_set_sun_azimuth(uint16_t az) { g_nd_field_sun_azimuth = az; }

static inline uint16_t
nd_field_sun_azimuth_from_day_tick(uint64_t day_tick_val)
{
	return (uint16_t)((day_tick_val & ((1ULL << ND_FIELD_DAYTICK_BITS) - 1ULL))
	                  << (16 - ND_FIELD_DAYTICK_BITS));
}

/* Toroidal solar incidence in Q15 [0..32768]: max(0, cos(phi) * cos(theta - lambda)) */
static inline int32_t
nd_field_torus_solar_incidence_q15(int16_t pos_x, int16_t pos_y, uint16_t sun_azimuth)
{
	int32_t cos_phi = nd_field_cos_q15((uint16_t)pos_y);
	int32_t cos_theta_rel = nd_field_cos_q15((uint16_t)((uint16_t)pos_x - sun_azimuth));
	int32_t dot_q15 = (cos_phi * cos_theta_rel) >> 15;
	return (dot_q15 > 0) ? dot_q15 : 0;
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
	return nd_field_torus_solar_cooling(0, pos_y, nd_field_get_sun_azimuth());
}

static inline int16_t
nd_field_temp_at_azimuth(uint16_t obits, uint32_t he, uint32_t tm,
                         int16_t pos_x, int16_t pos_y, uint16_t sun_azimuth)
{
	(void)obits; (void)he;
	int16_t solar_cool = nd_field_torus_solar_cooling(pos_x, pos_y, sun_azimuth);
	int32_t tm_var = (int32_t)(tm >> 16) - 128;
	return (int16_t)(ND_FIELD_SOLAR_TEMP_MAX - solar_cool + (tm_var * 30 / 128));
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

	uint8_t primary = (uint8_t)(2 + rn_bit + (tmp_bit << 2));

	uint32_t rn_rem = rn - (rn_bit * rn_step);
	uint32_t rn_frac = (rn_rem * 256) / rn_step;

	int32_t tmp_rem = (tmp - ND_FIELD_TEMP_MIN) - (tmp_bit * tmp_step);
	uint32_t tmp_frac = (uint32_t)((tmp_rem * 256) / tmp_step);

	int32_t d_rn = (rn_frac < 128) ? (int32_t)rn_frac : (int32_t)(256 - rn_frac);
	int32_t d_tmp = (tmp_frac < 128) ? (int32_t)tmp_frac : (int32_t)(256 - tmp_frac);

	uint8_t secondary = primary;
	int32_t min_d = 128;

	/* Smooth transitional blend to PERMANENT_ICE near cold threshold */
	if (tmp_bit == 0 && tmp_frac < 48) {
		secondary = BIOME_PERMANENT_ICE;
		min_d = (int32_t)tmp_frac;
	} else if (tmp_bit == 3 && tmp_frac > 208) {
		secondary = BIOME_VOLCANIC;
		min_d = (int32_t)(256 - tmp_frac);
	} else if (d_rn < d_tmp) {
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
		uint32_t t = (uint32_t)(48 - min_d);
		/* Smooth C1 quadratic ease-in reaching exactly 128 (50% blend) at the boundary */
		blend = (uint8_t)((t * t * 128u) / (48u * 48u));
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
	int64_t ox = (int64_t)(wx - 80) * 65536LL;
	int64_t oy = (int64_t)(64 - wy) * 65536LL;
	if (rx_q16) *rx_q16 = (int32_t)nd_field_fdiv64(ox, 160);
	if (ry_q16) *ry_q16 = (int32_t)nd_field_fdiv64(oy, 128);
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

	uint32_t v[4];
	noise_get_v(v, s, x, w, seed, 2);

	uint32_t b0 = noise_wstep(v[0], v[2], fade_y, x + 16);
	uint32_t b1 = noise_wstep(v[1], v[3], fade_y, x + 16);
	return noise_wstep(b0, b1, fade_x, x + 16);
}

typedef struct {
	int16_t elevation;        /* Total elevation H = coarse + fine */
	int16_t elevation_q12_4;  /* Sub-pixel elevation in Q12.4 fixed-point (16 sub-units/pixel) */
	int16_t coarse_elev;      /* Coarse elevation */
	int16_t fine_elev;        /* Fine detail elevation */
	int16_t water_level;      /* W */
	int16_t depth;            /* max(0, W - H) */
	uint8_t is_water;         /* H < W */
	int16_t temp;             /* Temperature */
	uint32_t rain;            /* Rain */
	uint8_t bio;              /* Primary biome */
	uint8_t bio2;             /* Secondary biome */
	uint8_t blend;            /* Biome blend factor (0..128) */
} nd_field_sample_t;

/* Whole-block physics compatibility: converts Q12.4 fixed-point elevation to integer block coordinates */
static inline int16_t
nd_field_elevation_to_block(int16_t elev_q12_4)
{
	return (int16_t)(elev_q12_4 >> 4);
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
nd_field_eval_coarse_unwrapped_q12_4(uint32_t seed, int32_t rx, int32_t ry)
{
	int32_t cur_rx = rx, cur_ry = ry, coarse_q12_4 = 0;

	for (uint8_t s = ND_FIELD_SHIFT_PLANET_MAX; s >= ND_FIELD_SHIFT_CONTINENT_MIN; s--) {
		int16_t amp = chaff_octave_amplitude(s);
		uint32_t oct_seed = seed ^ (uint32_t)(s * 0x85EBCA77u + 1u);
		uint32_t raw = chaff_wpoint_periodic_2d(cur_rx, cur_ry, s, ND_FIELD_PLANET_BITS, 8, oct_seed);
		int32_t val = (int32_t)(raw >> 16) - 128;
		coarse_q12_4 += (int32_t)((((int64_t)val * amp) << 4) / 128);
		chaff_rot2d_i32_q15(&cur_rx, &cur_ry, CHAFF_ROT_ATAN_COS_Q15, CHAFF_ROT_ATAN_SIN_Q15);
	}

	for (uint8_t s = 8; s >= ND_FIELD_SHIFT_REGIONAL_MIN; s--) {
		int16_t amp = chaff_octave_amplitude(s);
		uint32_t oct_seed = seed ^ (uint32_t)(s * 0x85EBCA77u + 1u);
		int16_t p[2] = { (int16_t)cur_ry, (int16_t)cur_rx };
		uint32_t raw = noise_wpoint_n(p, s, 8, oct_seed, 2, NOISE_FADE5);
		int32_t val = (int32_t)(raw >> 16) - 128;
		coarse_q12_4 += (int32_t)((((int64_t)val * amp) << 4) / 128);
		chaff_rot2d_i32_q15(&cur_rx, &cur_ry, CHAFF_ROT_ATAN_COS_Q15, CHAFF_ROT_ATAN_SIN_Q15);
	}

	return coarse_q12_4;
}

static inline int32_t
nd_field_eval_coarse_torus_x_q12_4(uint32_t seed, int16_t rx, int32_t ry_unwrapped)
{
	uint16_t ux = (uint16_t)((uint16_t)rx - 32512u);
	if (ux < 512u) {
		int32_t rx_hi = 32512 + (int32_t)ux;
		int32_t rx_lo = rx_hi - ND_FIELD_PLANET_ROOM_SPAN;
		int32_t c_hi = nd_field_eval_coarse_unwrapped_q12_4(seed, rx_hi, ry_unwrapped);
		int32_t c_lo = nd_field_eval_coarse_unwrapped_q12_4(seed, rx_lo, ry_unwrapped);
		return c_hi + (int32_t)(((int64_t)(c_lo - c_hi) * (int32_t)ux) >> 9);
	}
	return nd_field_eval_coarse_unwrapped_q12_4(seed, (int32_t)rx, ry_unwrapped);
}

/* Continuous regional ruggedness envelope R(wx, wy) in Q8 fixed-point [0..256].
 * Combines a macro tectonic province octave (shift 10 = 1,024 world units, ~6.4x8 rooms)
 * and a rotated meso ridge-belt octave (shift 8 = 256 world units, ~1.6x2 rooms),
 * followed by a smooth threshold ramp so ~45% of land forms open clear plains (R = 0),
 * ~30% forms rolling hills, and ~25% forms craggy rugged ridges and highlands (R -> 256). */
static inline uint16_t
nd_field_ruggedness_q8(uint16_t world, int32_t wx, int32_t wy)
{
	uint32_t rug_seed = 0x52554747u ^ ((uint32_t)world * 0x9E3779B1u);
	int16_t p0[2] = { (int16_t)wy, (int16_t)wx };
	uint32_t raw_macro = noise_wpoint_n(p0, 10, 8, rug_seed ^ 0xA531u, 2, NOISE_FADE5) >> 16;

	int32_t rx = wx;
	int32_t ry = wy;
	chaff_rot2d_i32_q15(&rx, &ry, CHAFF_ROT_ATAN_COS_Q15, CHAFF_ROT_ATAN_SIN_Q15);
	int16_t p1[2] = { (int16_t)ry, (int16_t)rx };
	uint32_t raw_meso = noise_wpoint_n(p1, 8, 8, rug_seed ^ 0x5CA9u, 2, NOISE_FADE5) >> 16;

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

static inline int32_t
nd_field_elevation_q12_4(uint16_t world, uint8_t lod, int32_t wx, int32_t wy,
                         int32_t *out_coarse_q12_4, int32_t *out_fine_q12_4)
{
	uint32_t seed = 0x48454947u ^ ((uint32_t)world * 0x9E3779B1u);

	int16_t rx = (int16_t)noise_fdiv(wx - (ND_FIELD_ROOM_W / 2), ND_FIELD_ROOM_W);
	int16_t ry = (int16_t)noise_fdiv((ND_FIELD_ROOM_H / 2) - wy, ND_FIELD_ROOM_H);

	int32_t coarse_q12_4 = 0;
	int32_t fine_q12_4 = 0;

	if (out_coarse_q12_4 != NULL || out_fine_q12_4 == NULL) {
		uint16_t uy = (uint16_t)((uint16_t)ry - 32512u);
		if (uy < 512u) {
			int32_t ry_hi = 32512 + (int32_t)uy;
			int32_t ry_lo = ry_hi - ND_FIELD_PLANET_ROOM_SPAN;
			int32_t c_hi = nd_field_eval_coarse_torus_x_q12_4(seed, rx, ry_hi);
			int32_t c_lo = nd_field_eval_coarse_torus_x_q12_4(seed, rx, ry_lo);
			coarse_q12_4 = c_hi + (int32_t)(((int64_t)(c_lo - c_hi) * (int32_t)uy) >> 9);
		} else {
			coarse_q12_4 = nd_field_eval_coarse_torus_x_q12_4(seed, rx, (int32_t)ry);
		}

		static uint16_t s_cached_world = 0xFFFFu;
		static int32_t s_cached_origin_bias = 0;
		if (world != s_cached_world) {
			s_cached_origin_bias = nd_field_eval_coarse_unwrapped_q12_4(seed, 0, 0) >> 2;
			s_cached_world = world;
		}
		uint32_t w = (((world >> 2) + 2) & 3) * 65536u;
		int32_t sea_offset_q12_4 = (int32_t)((((int64_t)w * 200 / 262144 - 100)) << 4) +
		                           ND_FIELD_ORIGIN_SEA_OFFSET_Q12_4 + s_cached_origin_bias;
		coarse_q12_4 -= sea_offset_q12_4;
	}

	if (lod > 0) {
		uint8_t min_shift;
		if (lod == 1)      min_shift = 5;
		else if (lod == 2) min_shift = 4;
		else if (lod == 3) min_shift = ND_FIELD_SHIFT_FINE_MIN;
		else               min_shift = (uint8_t)(ND_FIELD_SHIFT_FINE_MIN + (lod - 3));
		if (min_shift > ND_FIELD_SHIFT_FINE_MAX) min_shift = ND_FIELD_SHIFT_FINE_MAX;

		int32_t base_fine_q12_4 = chaff_fbm_rotated_2d_q12_4(
			wx, wy,
			min_shift, ND_FIELD_SHIFT_FINE_MAX,
			7, ND_FIELD_FINE_MIN_SHIFT, 1,
			seed, 0xBEEFu,
			CHAFF_ROT_ATAN_COS_Q15, CHAFF_ROT_ATAN_SIN_Q15);

		uint16_t rug_q8 = nd_field_ruggedness_q8(world, wx, wy);
		if (rug_q8 > 0) {
			uint8_t meso_min = (min_shift > 5) ? min_shift : 5;
			int32_t meso_ridge_q12_4 = chaff_fbm_rotated_2d_q12_4(
				wx, wy,
				meso_min, ND_FIELD_SHIFT_FINE_MAX,
				6, 5, 10,
				seed ^ 0x7F4A7C15u, 0xCAFEu,
				CHAFF_ROT_ATAN_COS_Q15, CHAFF_ROT_ATAN_SIN_Q15);
			int32_t base_scale_q8 = 128 + ((int32_t)rug_q8 * 3 >> 1);
			int32_t scaled_base = (int32_t)(((int64_t)base_fine_q12_4 * base_scale_q8) >> 8);
			fine_q12_4 = scaled_base + (int32_t)(((int64_t)meso_ridge_q12_4 * (int32_t)rug_q8) >> 8);
		} else {
			fine_q12_4 = base_fine_q12_4 >> 1;
		}
	}

	if (out_coarse_q12_4) *out_coarse_q12_4 = coarse_q12_4;
	if (out_fine_q12_4) *out_fine_q12_4 = fine_q12_4;
	return coarse_q12_4 + fine_q12_4;
}

static inline int32_t
chaff_elevation_q12_4(uint16_t world, uint8_t lod, int32_t wx, int32_t wy,
                      int32_t *out_coarse_q12_4, int32_t *out_fine_q12_4)
{
	return nd_field_elevation_q12_4(world, lod, wx, wy, out_coarse_q12_4, out_fine_q12_4);
}

typedef struct {
	uint16_t world;
	int16_t rx;
	int16_t ry;
	int16_t hyp;
	uint32_t cl_macro;
	uint32_t tm_macro;
	uint8_t hyp_valid;
	uint8_t macro_valid;
} nd_field_room_memo_t;

static inline nd_field_room_memo_t *
nd_field_room_memo_slot(uint16_t world, int16_t rx, int16_t ry)
{
	static nd_field_room_memo_t s_memo[4096];
	uint32_t idx = (((uint32_t)(uint16_t)rx * 0x9E3779B1u) ^
	                ((uint32_t)(uint16_t)ry * 0x85EBCA77u) ^
	                (uint32_t)world) & 4095u;
	nd_field_room_memo_t *m = &s_memo[idx];
	if (m->world != world || m->rx != rx || m->ry != ry) {
		m->world = world;
		m->rx = rx;
		m->ry = ry;
		m->hyp_valid = 0;
		m->macro_valid = 0;
	}
	return m;
}

static inline int16_t
nd_field_room_hyp_coarse(uint16_t world, int16_t rx, int16_t ry)
{
	nd_field_room_memo_t *m = nd_field_room_memo_slot(world, rx, ry);
	if (!m->hyp_valid) {
		int32_t r_wx = (int32_t)rx * ND_FIELD_ROOM_W + (ND_FIELD_ROOM_W / 2);
		int32_t r_wy = -(int32_t)ry * ND_FIELD_ROOM_H + (ND_FIELD_ROOM_H / 2);
		int32_t c = 0;
		nd_field_elevation_q12_4(world, 0, r_wx, r_wy, &c, NULL);
		m->hyp = nd_field_bimodal_hypsometry((int16_t)(c >> 4));
		m->hyp_valid = 1;
	}
	return m->hyp;
}

static inline int32_t
nd_field_interp_coarse_q12_4(uint16_t world, int32_t rx_q16, int32_t ry_q16)
{
	int16_t rx = (int16_t)(rx_q16 >> 16);
	int16_t ry = (int16_t)(ry_q16 >> 16);
	uint32_t fx = (uint32_t)(rx_q16 & 0xFFFF);
	uint32_t fy = (uint32_t)(ry_q16 & 0xFFFF);

	int32_t q00 = (int32_t)nd_field_room_hyp_coarse(world, rx, ry) << 4;
	if (fx == 0 && fy == 0) {
		return q00;
	}

	/* C2 quintic Hermite fade: 6t^5 - 15t^4 + 10t^3 (zero 1st & 2nd derivative at room grid lines) */
	uint32_t tx = noise_wfade5(fx, 16);
	uint32_t ty = noise_wfade5(fy, 16);

	int16_t rx_next = (int16_t)(rx + 1);
	int16_t ry_next = (int16_t)(ry + 1);
	int32_t q10 = (int32_t)nd_field_room_hyp_coarse(world, rx_next, ry) << 4;
	int32_t q01 = (int32_t)nd_field_room_hyp_coarse(world, rx, ry_next) << 4;
	int32_t q11 = (int32_t)nd_field_room_hyp_coarse(world, rx_next, ry_next) << 4;

	int32_t q0 = q00 + (int32_t)(((int64_t)(q10 - q00) * tx) >> 16);
	int32_t q1 = q01 + (int32_t)(((int64_t)(q11 - q01) * tx) >> 16);
	return q0 + (int32_t)(((int64_t)(q1 - q0) * ty) >> 16);
}

/* Deterministic Highland & Alpine Lake Basin Evaluator:
 * Uses libchaff spatial hashing and rotated fBm on a 1280x1024 (8x8-room) macro lattice
 * to carve self-contained highland lake basins with a strictly flat horizontal water table
 * (lake_water_lvl) and a continuous 360-degree dry containment rim. */
static inline void
nd_field_eval_mountain_lake(uint16_t world, int32_t wx, int32_t wy,
                            int32_t *inout_coarse_q12_4, int16_t *inout_water_level)
{
	int16_t sea_lvl = *inout_water_level;
	if (*inout_coarse_q12_4 < (((int32_t)sea_lvl + 28) << 4))
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
	if (dx < -360 || dx > 360 || dy < -288 || dy > 288)
		return;

	int16_t crx = (int16_t)noise_fdiv(cx - (ND_FIELD_ROOM_W / 2), ND_FIELD_ROOM_W);
	int16_t cry = (int16_t)noise_fdiv((ND_FIELD_ROOM_H / 2) - cy, ND_FIELD_ROOM_H);

	/* Aridity gating: Highland lakes require positive moisture balance.
	 * In subtropical desert and savannah belts, evaporation exceeds precipitation,
	 * so perched lakes are suppressed (rare 1-in-32 seasonal watering hole only). */
	int32_t abs_cry = abs((int32_t)cry);
	if (abs_cry >= 3200 && abs_cry <= 9200 && abs(abs_cry - 6200) < 2400) {
		if ((h_cell & 31u) != 0)
			return;
	}

	int16_t center_hyp = nd_field_room_hyp_coarse(world, crx, cry);
	if (center_hyp < sea_lvl + 36 || center_hyp > sea_lvl + 480)
		return;

	int16_t n_e = nd_field_room_hyp_coarse(world, (int16_t)(crx + 1), cry);
	int16_t n_w = nd_field_room_hyp_coarse(world, (int16_t)(crx - 1), cry);
	int16_t n_n = nd_field_room_hyp_coarse(world, crx, (int16_t)(cry - 1));
	int16_t n_s = nd_field_room_hyp_coarse(world, crx, (int16_t)(cry + 1));
	int16_t min_neigh = center_hyp;
	if (n_e < min_neigh) min_neigh = n_e;
	if (n_w < min_neigh) min_neigh = n_w;
	if (n_n < min_neigh) min_neigh = n_n;
	if (n_s < min_neigh) min_neigh = n_s;
	if (min_neigh < sea_lvl + 30)
		return;

	int16_t lake_water_lvl = (int16_t)(min_neigh - 2);

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
	int32_t warp_q12_4 = chaff_fbm_rotated_2d_q12_4(
		wx, wy, 6, 7, 2, 6, 3, seed, 0x5752u,
		CHAFF_ROT_ATAN_COS_Q15, CHAFF_ROT_ATAN_SIN_Q15);
	int32_t eff_r2_q16 = (int32_t)r2_q16 + (warp_q12_4 * 140);
	if (eff_r2_q16 >= 65536)
		return;
	if (eff_r2_q16 < 0)
		eff_r2_q16 = 0;

	uint32_t t_q16 = (uint32_t)(65536 - eff_r2_q16);
	uint32_t bowl_q16 = noise_wfade5(t_q16, 16);

	int32_t rim_elev_q12_4 = ((int32_t)lake_water_lvl + 6) << 4;
	int32_t bed_elev_q12_4 = ((int32_t)lake_water_lvl - 14) << 4;
	int32_t orig_c = *inout_coarse_q12_4;
	int32_t target_rim = (orig_c > rim_elev_q12_4) ? orig_c : rim_elev_q12_4;

	if (bowl_q16 <= 16384u) {
		uint32_t u_q16 = noise_wfade5(bowl_q16 << 2, 16);
		*inout_coarse_q12_4 = orig_c + (int32_t)(((int64_t)(target_rim - orig_c) * u_q16) >> 16);
	} else {
		uint32_t inner_t_q16 = ((bowl_q16 - 16384u) * 65536u) / 49152u;
		uint32_t u_q16 = noise_wfade5(inner_t_q16, 16);
		*inout_coarse_q12_4 = target_rim + (int32_t)(((int64_t)(bed_elev_q12_4 - target_rim) * u_q16) >> 16);
		*inout_water_level = lake_water_lvl;
	}
}

static inline nd_field_sample_t
nd_field_sample_at_azimuth(uint16_t world, uint8_t lod, int32_t wx, int32_t wy, uint16_t sun_azimuth)
{
	nd_field_sample_t s;
	memset(&s, 0, sizeof(s));

	int32_t rx_q16, ry_q16;
	nd_field_world_to_room_q16(wx, wy, &rx_q16, &ry_q16);

	int16_t room_x = (int16_t)(rx_q16 >> 16);
	int16_t room_y = (int16_t)(ry_q16 >> 16);

	int32_t coarse_q12_4 = nd_field_interp_coarse_q12_4(world, rx_q16, ry_q16);
	s.water_level = nd_field_water_level(world);
	nd_field_eval_mountain_lake(world, wx, wy, &coarse_q12_4, &s.water_level);
	int32_t fine_q12_4 = 0;
	if (lod > 0) {
		nd_field_elevation_q12_4(world, lod, wx, wy, NULL, &fine_q12_4);

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
		}

		/* Fine elevation budget bound: |fine_elev| <= ND_FIELD_FINE_AMP_BUDGET */
		int32_t max_fine_q12_4 = (int32_t)ND_FIELD_FINE_AMP_BUDGET << 4;
		if (fine_q12_4 > max_fine_q12_4) fine_q12_4 = max_fine_q12_4;
		if (fine_q12_4 < -max_fine_q12_4) fine_q12_4 = -max_fine_q12_4;
	}

	s.coarse_elev = (int16_t)(coarse_q12_4 >> 4);
	s.fine_elev = (int16_t)(fine_q12_4 >> 4);

	int32_t total_q12_4 = coarse_q12_4 + fine_q12_4;
	if (total_q12_4 > 32000) total_q12_4 = 32000;
	if (total_q12_4 < -32000) total_q12_4 = -32000;
	s.elevation = (int16_t)(total_q12_4 >> 4);
	s.elevation_q12_4 = (lod == 0) ? (int16_t)((int32_t)s.elevation << 4) : (int16_t)total_q12_4;

	if (s.elevation < s.water_level) {
		s.is_water = 1;
		s.depth = (int16_t)(s.water_level - s.elevation);
	} else {
		s.is_water = 0;
		s.depth = 0;
	}

	nd_field_room_memo_t *rm = nd_field_room_memo_slot(world, room_x, room_y);
	if (!rm->macro_valid) {
		rm->cl_macro = chaff_sample_torus_macro((int32_t)room_x, (int32_t)room_y, 14, 1 + world);
		rm->tm_macro = chaff_sample_torus_macro((int32_t)room_x, (int32_t)room_y, 14, 53 + world);
		rm->macro_valid = 1;
	}
	uint32_t cl_macro = rm->cl_macro;
	uint32_t tm_macro = rm->tm_macro;
	uint32_t cl_prov  = nd_field_eval_coarse_oct_q16(ry_q16, rx_q16, 10, 8, 101 + world);
	uint32_t tm_prov  = nd_field_eval_coarse_oct_q16(ry_q16, rx_q16, 10, 8, 153 + world);
	uint32_t cl_reg   = nd_field_eval_coarse_oct_q16(ry_q16, rx_q16, 7, 8, 1 + world);
	uint32_t tm_reg   = nd_field_eval_coarse_oct_q16(ry_q16, rx_q16, 7, 8, 53 + world);
	uint32_t cl_loc   = nd_field_eval_coarse_oct_q16(ry_q16, rx_q16, 4, 8, 201 + world);
	uint32_t tm_loc   = nd_field_eval_coarse_oct_q16(ry_q16, rx_q16, 4, 8, 253 + world);

	/* 4-octave continuous climate hierarchy across scales:
	 *   40% continental macro (shift 14: 16k rooms = 2.6M world units)
	 *   30% continental provinces (shift 10: 1k rooms = 164k world units)
	 *   20% regional corridors (shift 7: 128 rooms = 20.5k world units)
	 *   10% local eco-regions (shift 4: 16 rooms = 2.5k world units)
	 * Biomes form broad, natural geographical realms (like Earth's Sahara, Savannah, Taiga)
	 * that maintain continuous multi-octave level of detail when zooming out! */
	uint32_t cl_raw = (cl_macro * 4u + cl_prov * 3u + cl_reg * 2u + cl_loc * 1u) / 10u;
	uint32_t tm_raw = (tm_macro * 4u + tm_prov * 3u + tm_reg * 2u + tm_loc * 1u) / 10u;

	/* Contrast-stretch rainfall to utilize full dry-to-wet spectrum */
	int32_t rn_centered = (int32_t)(cl_raw >> 15) - 256;
	int32_t rn_val = 256 + (rn_centered * 15) / 10;

	int32_t abs_ry = abs((int32_t)room_y);
	/* Intertropical Convergence Zone (ITCZ) equatorial rainfall enhancement (latitudes < 12° == room_y < 2400) creates lush tropical rainforests */
	if (abs_ry < 2400) {
		int32_t wet_weight = (2400 - abs_ry) * 85 / 2400;
		rn_val += wet_weight;
	}
	/* Subtropical Hadley subsidence drying belt (latitudes ~15°..42° == room_y ~2400..10500) creates vast Earth-like desert belts */
	if (abs_ry >= 2200 && abs_ry <= 11000) {
		int32_t sub_dist = abs(abs_ry - 6200);
		if (sub_dist < 4400) {
			int32_t dry_weight = (4400 - sub_dist) * 150 / 4400;
			rn_val -= dry_weight;
		}
	}
	if (rn_val < 0) rn_val = 0;
	if (rn_val > (int32_t)ND_FIELD_RAIN_MAX) rn_val = ND_FIELD_RAIN_MAX;
	s.rain = (uint32_t)rn_val;

	s.temp = nd_field_temp_at_azimuth(world, (uint32_t)s.elevation, tm_raw, room_x, room_y, sun_azimuth);
	int32_t elev_above_base = (int32_t)s.elevation - 30;
	if (elev_above_base > 0) {
		/* Physical environmental lapse rate: high summits cool into subalpine tundra and permanent snowy peaks */
		int32_t lapse = (elev_above_base * 10) / 100;
		s.temp -= (int16_t)lapse;
	}
	nd_field_bio_blend(s.rain, s.temp, &s.bio, &s.bio2, &s.blend);

	/* Volcanic Hotspots: Rare localized tectonic rifts/calderas on dry land (~0.2% of terrain),
	 * rather than covering equatorial climate bands */
	if (!s.is_water && s.elevation > s.water_level + 20) {
		uint32_t v_caldera = chaff_sample_torus_macro((int32_t)room_x, (int32_t)room_y, 6, 0x564F4C43u);
		if ((v_caldera & 0x1FFu) <= 1u) {
			s.bio = BIOME_VOLCANIC;
			s.blend = 0;
		}
	}

	return s;
}

static inline nd_field_sample_t
nd_field_sample(uint16_t world, uint8_t lod, int32_t wx, int32_t wy)
{
	return nd_field_sample_at_azimuth(world, lod, wx, wy, nd_field_get_sun_azimuth());
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
		norm_he = (int64_t)w + ((int64_t)fs->elevation * (int64_t)(NOISE_MAX - w)) / 1854;
		norm_he ^= (int64_t)(rhash & 0xFFFu);
		if (norm_he <= (int64_t)w) norm_he = (int64_t)w + 1 + (int64_t)(rhash & 0xFFu);
		if (norm_he > NOISE_MAX) norm_he = NOISE_MAX;
	}
	uint32_t tm = (uint32_t)(((int64_t)(fs->temp - ND_FIELD_TEMP_MIN) * NOISE_MAX) /
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
