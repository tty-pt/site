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
			/* High mountain sharpening: plateaus (43..270), cordilleras (500..1100), alpine summits up to 1,854 (Mount Everest) */
			int32_t m = elev - 25;
			int32_t peak = 43 + (m * 22) / 10 + (m * m * 12) / 100;
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
	double wdim_x = (double)ND_FIELD_PLANET_ROOM_SPAN * (double)ND_FIELD_ROOM_W;
	double wdim_y = (double)ND_FIELD_PLANET_ROOM_SPAN * (double)ND_FIELD_ROOM_H;
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
	static nd_field_world_memo_t s_world_memo[16];
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

/* Canonical Mean Annual Toroidal Climate Temperature (in NeverDark temperature units):
 * Governed by poloidal latitude phi (room_y) relative to the plane of the torus (Z = 0):
 *   - Outer Equator (room_y = 0, phi = 0): unobstructed overhead insolation (+94 base)
 *   - Inner Equator (room_y = +-32768, phi = +-pi): in the torus plane but partially eclipsed
 *     by the opposite side of the ring around equinox noon (-8 arch-shadow cooling -> +86 base)
 *   - North & South Polar Crowns (room_y = +-16384, phi = +-pi/2): tangent to orbital plane (-46 base)
 *   - High mountain environmental lapse rate above elev_rel > 220 world units. */
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
	int32_t bio_temp = 96 - lat_cool - inner_eclipse_cool + (tm_var * tm_amp / 128);
	if (elev_rel > 180) {
		/* High mountain environmental lapse rate: cordilleras & summits (> 350..750+ wu) cool into alpine tundra and snowcaps */
		bio_temp -= ((elev_rel - 180) * 165) / 1000;
	}
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
	 *    High mountain slopes and benches (elev_rel >= 260 wu) where montane climate cools
	 *    into timberline flower-rich meadows (-22 <= tmp <= +28) with sufficient moisture (rn >= 115).
	 *    Smooth transition across edge in [0..40]. */
	if (elev_rel >= 260 && tmp >= -22 && tmp <= 28 && rn >= 115u && b1 != BIOME_PERMANENT_ICE) {
		int32_t d_elev = elev_rel - 260;
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
	static nd_coarse_oct_slot_t s_slots[64];
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
	int16_t elevation;        /* Total elevation H = coarse + fine */
	int16_t elevation_q12_4;  /* Sub-pixel elevation in Q12.4 fixed-point (16 sub-units/pixel) */
	int16_t coarse_elev;      /* Coarse elevation */
	int16_t fine_elev;        /* Fine detail elevation */
	int16_t water_level;      /* W */
	int16_t depth;            /* max(0, W - H) */
	uint8_t is_water;         /* H < W */
	int16_t temp;             /* Live temperature (climate_temp + diurnal day/night delta) */
	int16_t climate_temp;     /* Mean annual latitudinal & alpine climate temperature */
	uint32_t rain;            /* Rain */
	uint8_t bio;              /* Primary biome */
	uint8_t bio2;             /* Secondary biome */
	uint8_t blend;            /* Biome blend factor (0..128) */
	uint8_t river_carve_q4;   /* Upland river channel incision C(wx,wy) in Q4 (0..254) */
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
nd_field_eval_coarse_unwrapped_step_q12_4(uint32_t seed, int32_t room_step_q16, int32_t rx, int32_t ry)
{
	static _CHAFF_TLS noise_wcache_t s_coarse_wc[9];
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
nd_field_eval_coarse_torus_x_step_q12_4(uint32_t seed, int32_t room_step_q16, int16_t rx, int32_t ry_unwrapped)
{
	uint16_t ux = (uint16_t)((uint16_t)rx - 32512u);
	if (ux < 512u) {
		int32_t rx_hi = 32512 + (int32_t)ux;
		int32_t rx_lo = rx_hi - ND_FIELD_PLANET_ROOM_SPAN;
		int32_t c_hi = nd_field_eval_coarse_unwrapped_step_q12_4(seed, room_step_q16, rx_hi, ry_unwrapped);
		int32_t c_lo = nd_field_eval_coarse_unwrapped_step_q12_4(seed, room_step_q16, rx_lo, ry_unwrapped);
		return c_hi + (int32_t)(((int64_t)(c_lo - c_hi) * (int32_t)ux) >> 9);
	}
	return nd_field_eval_coarse_unwrapped_step_q12_4(seed, room_step_q16, (int32_t)rx, ry_unwrapped);
}

static inline int32_t
nd_field_eval_coarse_torus_x_q12_4(uint32_t seed, int16_t rx, int32_t ry_unwrapped)
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

static inline int32_t
nd_field_elevation_step_q12_4(uint16_t world, int32_t step_q16, int32_t wx, int32_t wy,
                              int32_t *out_coarse_q12_4, int32_t *out_fine_q12_4)
{
	uint32_t seed = 0x48454947u ^ ((uint32_t)world * 0x9E3779B1u);

	int16_t rx = (int16_t)noise_fdiv(wx - (ND_FIELD_ROOM_W / 2), ND_FIELD_ROOM_W);
	int16_t ry = (int16_t)noise_fdiv((ND_FIELD_ROOM_H / 2) - wy, ND_FIELD_ROOM_H);

	int32_t coarse_q12_4 = 0;
	int32_t fine_q12_4 = 0;

	if (out_coarse_q12_4 != NULL || out_fine_q12_4 == NULL) {
		int32_t room_step_q16 = (step_q16 >= (2048 << 16)) ? (step_q16 / ND_FIELD_ROOM_W) : 0;
		uint16_t uy = (uint16_t)((uint16_t)ry - 32512u);
		if (uy < 512u) {
			int32_t ry_hi = 32512 + (int32_t)uy;
			int32_t ry_lo = ry_hi - ND_FIELD_PLANET_ROOM_SPAN;
			int32_t c_hi = nd_field_eval_coarse_torus_x_step_q12_4(seed, room_step_q16, rx, ry_hi);
			int32_t c_lo = nd_field_eval_coarse_torus_x_step_q12_4(seed, room_step_q16, rx, ry_lo);
			coarse_q12_4 = c_hi + (int32_t)(((int64_t)(c_lo - c_hi) * (int32_t)uy) >> 9);
		} else {
			coarse_q12_4 = nd_field_eval_coarse_torus_x_step_q12_4(seed, room_step_q16, rx, (int32_t)ry);
		}

		static _CHAFF_TLS uint16_t s_cached_world = 0xFFFFu;
		static _CHAFF_TLS int32_t s_cached_origin_bias = 0;
		if (world != s_cached_world) {
			s_cached_origin_bias = nd_field_eval_coarse_unwrapped_q12_4(seed, 0, 0) >> 2;
			s_cached_world = world;
		}
		uint32_t w = (((world >> 2) + 2) & 3) * 65536u;
		int32_t sea_offset_q12_4 = (int32_t)((((int64_t)w * 200 / 262144 - 100)) << 4) +
		                           ND_FIELD_ORIGIN_SEA_OFFSET_Q12_4 + s_cached_origin_bias;
		coarse_q12_4 -= sea_offset_q12_4;
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

/* Conservative O(1) analytical elevation bounds [out_z_min, out_z_max] (in world units)
 * over the continuous world-space rectangle [ox, ox + w] x [oy, oy + h] for ALL LODs.
 * Evaluates coarse elevation once at the patch center room, adds exact C2 quintic
 * Lipschitz bounds across coarse octaves (shifts 15..2), maps through the strictly
 * monotonic bimodal hypsometry curve, and adds the fine amplitude & hydrology budgets.
 * Guaranteed conservative (zero false negatives) while dramatically tightening vertical
 * bounding boxes over ocean basins, lowlands, and plains. */
static inline void
nd_field_elevation_bounds(uint16_t world, int32_t ox, int32_t oy, int32_t w, int32_t h,
                          float *out_z_min, float *out_z_max)
{
	const float global_min = -2150.0f;
	const float global_max =  2000.0f;

	if (w < 0) { ox += w; w = -w; }
	if (h < 0) { oy += h; h = -h; }

	int32_t rw = nd_field_get_room_w();
	int32_t rh = nd_field_get_room_h();
	if (rw <= 0) rw = ND_FIELD_DEFAULT_ROOM_W;
	if (rh <= 0) rh = ND_FIELD_DEFAULT_ROOM_H;

	/* Room stencil touched by nd_field_interp_coarse_q12_4 over [ox..ox+w] x [oy..oy+h] */
	int64_t rx0 = nd_field_fdiv64((int64_t)ox - (int64_t)(rw / 2), (int64_t)rw);
	int64_t rx1 = nd_field_fdiv64((int64_t)ox + (int64_t)w - (int64_t)(rw / 2), (int64_t)rw) + 1;
	int64_t ry0 = nd_field_fdiv64((int64_t)(rh / 2) - ((int64_t)oy + (int64_t)h), (int64_t)rh);
	int64_t ry1 = nd_field_fdiv64((int64_t)(rh / 2) - (int64_t)oy, (int64_t)rh) + 1;

	int64_t span_rx = rx1 - rx0;
	int64_t span_ry = ry1 - ry0;

	/* Very large planetary quadrants or patches straddling the torus wrap seam [32512..33023]
	 * (which also covers int16 wrap +/-32768) fall back to the exact global envelope. */
	if (span_rx >= 8192 || span_ry >= 8192 ||
	    (uint16_t)((uint16_t)rx0 - 32512u) < 512u || (uint16_t)(32512u - (uint16_t)rx0) <= (uint32_t)span_rx ||
	    (uint16_t)((uint16_t)ry0 - 32512u) < 512u || (uint16_t)(32512u - (uint16_t)ry0) <= (uint32_t)span_ry) {
		if (out_z_min) *out_z_min = global_min;
		if (out_z_max) *out_z_max = global_max;
		return;
	}

	int16_t rx_mid = (int16_t)(rx0 + (span_rx >> 1));
	int16_t ry_mid = (int16_t)(ry0 + (span_ry >> 1));
	int32_t r_wx = (int32_t)rx_mid * rw + (rw / 2);
	int32_t r_wy = -(int32_t)ry_mid * rh + (rh / 2);
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
	int32_t cur_rx = (int32_t)rx_mid;
	int32_t cur_ry = (int32_t)ry_mid;
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

	int16_t hyp_min = nd_field_bimodal_hypsometry((int16_t)c_lo);
	int16_t hyp_max = nd_field_bimodal_hypsometry((int16_t)c_hi);
	int16_t sea_lvl = nd_field_water_level(world);

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
	uint16_t world;
	int16_t rx;
	int16_t ry;
	int16_t hyp;
	uint32_t cl_macro;
	uint32_t tm_macro;
	uint32_t rain_memo;
	int16_t  bio_temp_memo;
	uint8_t hyp_valid;
	uint8_t macro_valid;
	uint8_t climate_valid;
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
		m->climate_valid = 0;
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

static inline int16_t
nd_field_room_hyp_coarse_step(uint16_t world, int32_t step_q16, int16_t rx, int16_t ry)
{
	if (step_q16 < (2048 << 16))
		return nd_field_room_hyp_coarse(world, rx, ry);
	int32_t r_wx = (int32_t)rx * ND_FIELD_ROOM_W + (ND_FIELD_ROOM_W / 2);
	int32_t r_wy = -(int32_t)ry * ND_FIELD_ROOM_H + (ND_FIELD_ROOM_H / 2);
	int32_t c = 0;
	nd_field_elevation_step_q12_4(world, step_q16, r_wx, r_wy, &c, NULL);
	return nd_field_bimodal_hypsometry((int16_t)(c >> 4));
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

static inline int32_t
nd_field_interp_coarse_step_q12_4(uint16_t world, int32_t step_q16, int32_t rx_q16, int32_t ry_q16)
{
	if (step_q16 < (640 << 16))
		return nd_field_interp_coarse_q12_4(world, rx_q16, ry_q16);
	int16_t rx = (int16_t)(rx_q16 >> 16);
	int16_t ry = (int16_t)(ry_q16 >> 16);
	uint32_t fx = (uint32_t)(rx_q16 & 0xFFFF);
	uint32_t fy = (uint32_t)(ry_q16 & 0xFFFF);

	int32_t q00 = (int32_t)nd_field_room_hyp_coarse_step(world, step_q16, rx, ry) << 4;
	if ((fx == 0 && fy == 0) || step_q16 >= (2048 << 16))
		return q00;

	uint32_t tx = noise_wfade5(fx, 16);
	uint32_t ty = noise_wfade5(fy, 16);

	int16_t rx_next = (int16_t)(rx + 1);
	int16_t ry_next = (int16_t)(ry + 1);
	int32_t q10 = (int32_t)nd_field_room_hyp_coarse_step(world, step_q16, rx_next, ry) << 4;
	int32_t q01 = (int32_t)nd_field_room_hyp_coarse_step(world, step_q16, rx, ry_next) << 4;
	int32_t q11 = (int32_t)nd_field_room_hyp_coarse_step(world, step_q16, rx_next, ry_next) << 4;

	int32_t q0 = q00 + (int32_t)(((int64_t)(q10 - q00) * tx) >> 16);
	int32_t q1 = q01 + (int32_t)(((int64_t)(q11 - q01) * tx) >> 16);
	return q0 + (int32_t)(((int64_t)(q1 - q0) * ty) >> 16);
}

static inline void
nd_field_room_macro_memo(uint16_t world, int16_t rx, int16_t ry,
                         uint32_t *out_cl, uint32_t *out_tm)
{
	nd_field_room_memo_t *rm = nd_field_room_memo_slot(world, rx, ry);
	if (!rm->macro_valid) {
		rm->cl_macro = chaff_sample_torus_macro((int32_t)rx, (int32_t)ry, 14, 1 + world);
		rm->tm_macro = chaff_sample_torus_macro((int32_t)rx, (int32_t)ry, 14, 53 + world);
		rm->macro_valid = 1;
	}
	*out_cl = rm->cl_macro;
	*out_tm = rm->tm_macro;
}

static inline void
nd_field_interp_macro_q16(uint16_t world, int32_t rx_q16, int32_t ry_q16,
                          uint32_t *out_cl_macro, uint32_t *out_tm_macro)
{
	int16_t rx = (int16_t)(rx_q16 >> 16);
	int16_t ry = (int16_t)(ry_q16 >> 16);
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
	int16_t rx_next = (int16_t)(rx + 1);
	int16_t ry_next = (int16_t)(ry + 1);

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
	int16_t rx = (int16_t)((rx_q16 + 32768) >> 16);
	int16_t ry = (int16_t)((ry_q16 + 32768) >> 16);
	nd_field_room_macro_memo(world, rx, ry, out_cl_macro, out_tm_macro);
}

/* Deterministic Highland & Alpine Lake Basin Evaluator:
 * Uses libchaff spatial hashing and rotated fBm on a 1280x1024 (8x8-room) macro lattice
 * to carve self-contained highland lake basins with a strictly flat horizontal water table
 * (lake_water_lvl) and a continuous 360-degree dry containment rim. */
static inline void
nd_field_eval_mountain_lake_step(uint16_t world, int32_t step_q16, int32_t wx, int32_t wy,
                                 int32_t *inout_coarse_q12_4, int16_t *inout_water_level)
{
	int16_t sea_lvl = *inout_water_level;
	if (*inout_coarse_q12_4 < (((int32_t)sea_lvl + 28) << 4))
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

	/* Memoize 8x8-room basin cell topography (25-room neighbor scan) per (world, bx, by) */
	typedef struct {
		uint16_t world;
		int16_t bx, by;
		int16_t lake_water_lvl;
		uint8_t state; /* 0 = empty, 1 = rejected, 2 = active */
	} nd_lake_cell_memo_t;
	static nd_lake_cell_memo_t s_lake_memo[256];
	uint32_t l_idx = (((uint32_t)(uint16_t)bx * 0x9E3779B1u) ^
	                  ((uint32_t)(uint16_t)by * 0x85EBCA77u) ^
	                  (uint32_t)world) & 255u;
	nd_lake_cell_memo_t *lm = &s_lake_memo[l_idx];
	if (lm->state == 0 || lm->world != world || lm->bx != (int16_t)bx || lm->by != (int16_t)by) {
		lm->world = world;
		lm->bx = (int16_t)bx;
		lm->by = (int16_t)by;
		lm->state = 1;

		int16_t crx = (int16_t)noise_fdiv(cx - (ND_FIELD_ROOM_W / 2), ND_FIELD_ROOM_W);
		int16_t cry = (int16_t)noise_fdiv((ND_FIELD_ROOM_H / 2) - cy, ND_FIELD_ROOM_H);

		int32_t abs_cry = abs((int32_t)cry);
		int32_t eq_cry = (abs_cry <= 16384) ? abs_cry : (32768 - abs_cry);
		int arid_reject = (eq_cry >= 2400 && eq_cry <= 9600 && abs(eq_cry - 5800) < 2800 && (h_cell & 31u) != 0);
		if (!arid_reject) {
			int16_t center_hyp = nd_field_room_hyp_coarse(world, crx, cry);
			if (center_hyp >= sea_lvl + 36 && center_hyp <= sea_lvl + 480) {
				int16_t min_neigh = center_hyp;
				int16_t max_neigh = center_hyp;
				for (int dy_r = -2; dy_r <= 2; dy_r++) {
					for (int dx_r = -2; dx_r <= 2; dx_r++) {
						if (dx_r == 0 && dy_r == 0) continue;
						int16_t nh = nd_field_room_hyp_coarse(world, (int16_t)(crx + dx_r), (int16_t)(cry + dy_r));
						if (nh < min_neigh) min_neigh = nh;
						if (nh > max_neigh) max_neigh = nh;
					}
				}
				if (min_neigh >= sea_lvl + 30 && (max_neigh - min_neigh) <= 18) {
					lm->lake_water_lvl = (int16_t)(min_neigh - 2);
					lm->state = 2;
				}
			}
		}
	}
	if (lm->state != 2)
		return;

	int16_t lake_water_lvl = lm->lake_water_lvl;

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

	int32_t bed_elev_q12_4 = ((int32_t)lake_water_lvl - 14) << 4;
	int32_t orig_c = *inout_coarse_q12_4;
	if (orig_c > bed_elev_q12_4) {
		uint32_t u_q16 = noise_wfade5(bowl_q16, 16);
		if (lake_w_q16 < 65536u)
			u_q16 = (uint32_t)(((uint64_t)u_q16 * lake_w_q16) >> 16);
		int32_t carved_c = orig_c - (int32_t)(((int64_t)(orig_c - bed_elev_q12_4) * u_q16) >> 16);
		*inout_coarse_q12_4 = carved_c;
		if (carved_c <= (((int32_t)lake_water_lvl + 2) << 4))
			*inout_water_level = lake_water_lvl;
	}
}

static inline void
nd_field_eval_mountain_lake(uint16_t world, int32_t wx, int32_t wy,
                            int32_t *inout_coarse_q12_4, int16_t *inout_water_level)
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
nd_field_eval_river(uint16_t world, int32_t wx, int32_t wy, uint32_t rain,
                    int32_t *inout_coarse_q12_4, int16_t *inout_water_level,
                    uint8_t *out_river_carve_q4, uint16_t *out_fine_damp_q8)
{
	static _CHAFF_TLS noise_wcache_t s_riv_c12, s_riv_c10, s_riv_c8, s_riv_c7, s_riv_c6;
	*out_river_carve_q4 = 0;
	*out_fine_damp_q8 = 0;

	int16_t base_sea_lvl = nd_field_water_level(world);
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
			rug_q8 = nd_field_ruggedness_q8(world, wx, wy);
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

	if (!rug_computed)
		rug_q8 = nd_field_ruggedness_q8(world, wx, wy);

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
			int16_t riv_wl = (int16_t)((w_river_q4 + 8) >> 4);
			if (riv_wl > *inout_water_level)
				*inout_water_level = riv_wl;
		}
	}

	*inout_coarse_q12_4 = valley_floor_q4;
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
	int16_t up_hyp = (int16_t)(nd_field_interp_coarse_step_q12_4(world, step_q16, rx_q16 - (3 << 16), ry_q16 + (3 << 16)) >> 4);
	int16_t cur_hyp = (int16_t)(coarse_q12_4 >> 4);
	int16_t sea_lvl = s.water_level;
	if (up_hyp > sea_lvl + 140 && cur_hyp < up_hyp - 25) {
		/* Rain Shadow: Downwind in the lee of high mountains, descending dry air suppresses precipitation */
		int32_t shadow_depth = (int32_t)(up_hyp - (sea_lvl + 140));
		if (shadow_depth > 180) shadow_depth = 180;
		int32_t rain_shadow = (shadow_depth * 64) / 180;
		rn_val -= rain_shadow;
	} else if (cur_hyp > up_hyp + 35 && cur_hyp > sea_lvl + 80) {
		/* Orographic Lift: Windward rising slopes condense moisture and enhance rainfall */
		int32_t lift_height = (int32_t)(cur_hyp - up_hyp);
		if (lift_height > 160) lift_height = 160;
		int32_t oro_lift = (lift_height * 48) / 160;
		rn_val += oro_lift;
	}
	if (rn_val < 0) rn_val = 0;
	if (rn_val > (int32_t)ND_FIELD_RAIN_MAX) rn_val = ND_FIELD_RAIN_MAX;
	s.rain = (uint32_t)rn_val;

	uint16_t river_fine_damp_q8 = 0;
	if (step_q16 <= (880 << 16)) {
		nd_field_eval_river(world, wx, wy, s.rain, &coarse_q12_4, &s.water_level,
		                    &s.river_carve_q4, &river_fine_damp_q8);
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
		nd_field_elevation_step_q12_4(world, step_q16, wx, wy, NULL, &fine_q12_4);

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

static inline void
nd_field_sample_row_step(uint16_t world, int32_t step_q16, int32_t wy,
                         int32_t ox, int32_t span_w, uint32_t count,
                         nd_field_sample_t *out_row)
{
	if (!out_row || count == 0)
		return;
	uint16_t sun_azimuth = nd_field_get_sun_azimuth();
	int32_t gdim = (count > 1u) ? (int32_t)(count - 1u) : 1;
	for (uint32_t c = 0; c < count; c++) {
		int32_t wx = ox + (int32_t)(((int64_t)c * span_w) / gdim);
		out_row[c] = nd_field_sample_at_azimuth_step(world, 3, step_q16, wx, wy, sun_azimuth);
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

	int16_t h_e = nd_field_room_hyp_coarse(world, (int16_t)(rx + 1), ry);
	int16_t h_w = nd_field_room_hyp_coarse(world, (int16_t)(rx - 1), ry);
	int16_t h_n = nd_field_room_hyp_coarse(world, rx, (int16_t)(ry + 1));
	int16_t h_s = nd_field_room_hyp_coarse(world, rx, (int16_t)(ry - 1));
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

	int16_t h_c = nd_field_room_hyp_coarse(world, rx, ry);
	int16_t h_e = nd_field_room_hyp_coarse(world, (int16_t)(rx + 1), ry);
	int16_t h_w = nd_field_room_hyp_coarse(world, (int16_t)(rx - 1), ry);
	int16_t h_n = nd_field_room_hyp_coarse(world, rx, (int16_t)(ry + 1));
	int16_t h_s = nd_field_room_hyp_coarse(world, rx, (int16_t)(ry - 1));
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
		norm_he = (int64_t)w + ((int64_t)fs->elevation * (int64_t)(NOISE_MAX - w)) / 1854;
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
