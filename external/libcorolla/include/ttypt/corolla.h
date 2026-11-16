#ifndef TTYPT_COROLLA_H
#define TTYPT_COROLLA_H

/*
 * ttypt/corolla.h — Generic Parametric 2-Manifold Surface Embedding,
 * Analytical Ray–Surface Self-Occlusion, and Orbital Irradiance Library.
 *
 * LICENSE: BSD-2-Clause
 * Copyright (c) 2026, tty-pt
 *
 * Pure C (C99/C11), domain-agnostic mathematical library.
 * Zero knowledge of game worlds, rooms, databases, or UI substrates.
 * Compatible with native Linux/OpenBSD (.so) and 32-bit WebAssembly (WASM).
 *
 * Core abstractions:
 *   1. Parametric 2-manifold differential frames (corolla_frame):
 *      Maps normalized surface parameters (u, v) in [0, 1)^2 and normal offset h
 *      to 3D position p, unit normal n, unit tangents (t_u, t_v), and metric
 *      arc-length scale factors (ds_u, ds_v) for Torus, Sphere/Ellipsoid,
 *      Cylinder, and Flat Plane surfaces.
 *   2. Exact analytical ray-surface intersection & self-occlusion
 *      (corolla_ray_hit, corolla_occluded):
 *      Closed-form quadratic and Ferrari/companion-polished quartic root solvers
 *      for ray-torus, ray-sphere, ray-cylinder, and ray-plane intersection.
 *   3. Axial-tilt spin & Keplerian orbital irradiance
 *      (corolla_sun_dir, corolla_insolation, corolla_annual_insolation,
 *       corolla_orbit_pos):
 *      Computes directional light vectors under axial obliquity, instantaneous
 *      self-shadowed irradiance, seasonal/annual integrated insolation, and
 *      3D Keplerian orbital positions.
 */

#include <stdint.h>
#include <stddef.h>
#if defined(__has_include) && __has_include("../../../libchaff/include/ttypt/chaff.h")
#include "../../../libchaff/include/ttypt/chaff.h"
#else
#include <ttypt/chaff.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifndef COROLLA_API
#if !defined(COROLLA_C_COMPILING) && defined(__has_include) && __has_include("../../src/corolla.c")
#define COROLLA_API static inline
#define COROLLA_HEADER_ONLY_ACTIVE 1
#else
#define COROLLA_API
#endif
#endif

#define COROLLA_PI     3.14159265358979323846
#define COROLLA_TWO_PI 6.28318530717958647692
#define COROLLA_HALF_PI 1.57079632679489661923

/* Supported parametric 2-manifold families */
typedef enum corolla_kind {
	COROLLA_NONE     = 0,
	COROLLA_TORUS    = 1, /* Ring torus: major radius R (r_major), tube radius r0 (r_minor) */
	COROLLA_SPHERE   = 2, /* Sphere / oblate spheroid: equatorial radius r_major, polar radius r_minor */
	COROLLA_CYLINDER = 3, /* Cylinder / ring habitat: radius r_major, height r_minor */
	COROLLA_PLANE    = 4  /* Euclidean sheet: width r_major (X), height r_minor (Y) */
} corolla_kind_t;

/* Parametric surface descriptor (zero magic constants; caller supplies dimensions) */
typedef struct corolla_surface {
	corolla_kind_t kind;
	double r_major; /* Torus R, Sphere R_eq, Cylinder R, Plane width */
	double r_minor; /* Torus r0, Sphere R_polar (0 -> r_major), Cylinder H, Plane height */
} corolla_surface_t;

/* Local differential frame at surface coordinates (u, v, h) */
typedef struct corolla_frame {
	double pos[3];  /* 3D Cartesian position (X, Y, Z) at height h along normal */
	double norm[3]; /* Outward unit surface normal n */
	double tan_u[3];/* Unit tangent vector along +u (East / azimuthal) */
	double tan_v[3];/* Unit tangent vector along +v (North / poloidal) */
	double ds_u;    /* Metric scale factor |dp/du| (arc length per unit u at h=0) */
	double ds_v;    /* Metric scale factor |dp/dv| (arc length per unit v at h=0) */
} corolla_frame_t;

/* Ray-manifold intersection result */
typedef struct corolla_hit {
	int    hit;     /* 1 if ray intersects surface at t >= t_min, 0 otherwise */
	double t;       /* Ray parameter t of nearest valid intersection */
	double pos[3];  /* 3D hit point o + t * d */
	double norm[3]; /* Outward unit surface normal at hit point */
	double u;       /* Normalized surface coordinate u in [0, 1) */
	double v;       /* Normalized surface coordinate v in [0, 1) */
} corolla_hit_t;

/* Keplerian orbital elements for multi-body systems */
typedef struct corolla_orbit {
	double semi_major_axis; /* a: orbital semi-major axis */
	double eccentricity;    /* e: orbital eccentricity in [0, 1) */
	double inclination_rad; /* i: orbital inclination (radians) */
	double lon_asc_node_rad;/* Omega: longitude of ascending node (radians) */
	double arg_periapsis_rad;/* omega: argument of periapsis (radians) */
	double mean_anomaly_0;  /* M0: mean anomaly at epoch t = 0 (radians) */
	double period;          /* Orbital period T (in caller time units; > 0) */
} corolla_orbit_t;

/* Rigid celestial body descriptor: parametric surface + Keplerian orbit + spin/obliquity */
typedef struct corolla_body {
	corolla_surface_t surf;            /* Parametric 2-manifold surface geometry */
	corolla_orbit_t   orbit;           /* Keplerian orbit around parent body (semi_major_axis <= 0 -> fixed at parent) */
	double            axial_tilt_rad;  /* Obliquity epsilon: tilt of spin axis from parent +Z (radians) */
	double            axis_azimuth_rad;/* Azimuth of spin axis tilt in parent XY plane (radians) */
	double            spin_period;     /* Sidereal/diurnal spin period (>0 prograde, <0 retrograde, 0 non-rotating) */
	double            spin_phase_0;    /* Initial spin phase angle at t = 0 (radians) */
} corolla_body_t;

/* Instantaneous 3D rigid-body pose (translation + orthonormal orientation basis) */
typedef struct corolla_pose {
	double center[3];  /* 3D Cartesian center of body in inertial/system frame */
	double rot[9];     /* Row-major 3x3 body-to-inertial rotation matrix (columns are body X, Y, Z axes in inertial frame) */
	double inv_rot[9]; /* Row-major 3x3 inertial-to-body rotation matrix (transpose of rot) */
	double spin_angle; /* Instantaneous spin angle phi(t) in [0, 2*pi) */
	double orbit_angle;/* Instantaneous true orbital longitude in [0, 2*pi) */
} corolla_pose_t;

/* Convenience constructors */
static inline corolla_surface_t
corolla_torus(double r_major, double r_minor)
{
	corolla_surface_t s;
	s.kind = COROLLA_TORUS;
	s.r_major = r_major;
	s.r_minor = r_minor;
	return s;
}

static inline corolla_surface_t
corolla_sphere(double radius)
{
	corolla_surface_t s;
	s.kind = COROLLA_SPHERE;
	s.r_major = radius;
	s.r_minor = radius;
	return s;
}

static inline corolla_surface_t
corolla_cylinder(double radius, double height)
{
	corolla_surface_t s;
	s.kind = COROLLA_CYLINDER;
	s.r_major = radius;
	s.r_minor = height;
	return s;
}

static inline corolla_surface_t
corolla_plane(double width, double height)
{
	corolla_surface_t s;
	s.kind = COROLLA_PLANE;
	s.r_major = width;
	s.r_minor = height;
	return s;
}

/*
 * corolla_eval_frame — Evaluate 3D position, unit normal, unit tangents, and
 * metric scale factors at normalized surface coordinates (u, v) in [0, 1)^2
 * and normal altitude offset h.
 *
 * Parameterization conventions:
 *   - COROLLA_TORUS:
 *       phi   = 2*pi * u   (toroidal / East-West angle)
 *       theta = 2*pi * (v - 0.5) in [-pi, pi)
 *         -> v = 0.5 is the outer equator (theta = 0, radius R + r0)
 *         -> v = 0.75 is the North crown pole (theta = +pi/2, Z = +r0)
 *         -> v = 0.25 is the South crown pole (theta = -pi/2, Z = -r0)
 *         -> v = 0.0 (or 1.0) is the inner hub equator (theta = -pi, radius R - r0)
 *   - COROLLA_SPHERE:
 *       lon = 2*pi * u
 *       lat = pi * (v - 0.5) in [-pi/2, +pi/2]
 *         -> v = 0.5 is the equator, v = 0.0 is South pole, v = 1.0 is North pole.
 *   - COROLLA_CYLINDER:
 *       lon = 2*pi * u, Z = (v - 0.5) * H
 *   - COROLLA_PLANE:
 *       X = (u - 0.5) * W, Y = (v - 0.5) * H, Z = h, normal = (0, 0, 1)
 */
COROLLA_API int corolla_eval_frame(const corolla_surface_t *surf,
                       double u, double v, double h,
                       corolla_frame_t *out);

/*
 * corolla_xyz_to_uvh — Inverse mapping from 3D body-frame Cartesian coordinates
 * (x, y, z) to normalized surface coordinates (u, v) in [0, 1)^2 and signed
 * normal altitude h.
 */
COROLLA_API int corolla_xyz_to_uvh(const corolla_surface_t *surf,
                       double x, double y, double z,
                       double *out_u, double *out_v, double *out_h);

/*
 * corolla_ray_hit — Exact analytical intersection of ray r(t) = orig + t * dir
 * (where dir need not be unit length, though unit length makes t Euclidean distance)
 * against the surface over t in [t_min, t_max].
 * Returns 1 on hit (filling *out), 0 on miss.
 */
COROLLA_API int corolla_ray_hit(const corolla_surface_t *surf,
                    const double orig[3], const double dir[3],
                    double t_min, double t_max,
                    corolla_hit_t *out);

/*
 * corolla_occluded — Test whether a point at (u, v, h) on the surface is
 * occluded (shadowed) by the surface itself along unit direction `dir_to_light`.
 * Returns 1 if shadowed (either because n . dir_to_light <= 0 or because the ray
 * intersects another part of the manifold such as the torus outer lip), 0 if lit.
 */
COROLLA_API int corolla_occluded(const corolla_surface_t *surf,
                     double u, double v, double h,
                     const double dir_to_light[3]);

/*
 * corolla_sun_dir — Compute the unit 3D directional vector toward the parent star
 * in the body's local frame given:
 *   - axial_tilt_rad: obliquity epsilon (angle between spin axis +Z and orbital normal)
 *   - orbit_angle_rad: seasonal orbital angle tau in [0, 2*pi) (0 = vernal equinox,
 *                      pi/2 = northern summer solstice, 3*pi/2 = southern summer solstice)
 *   - spin_angle_rad: diurnal rotation phase phi in [0, 2*pi)
 */
COROLLA_API void corolla_sun_dir(double axial_tilt_rad,
                     double orbit_angle_rad,
                     double spin_angle_rad,
                     double out_sun_dir[3]);

/*
 * corolla_insolation — Compute instantaneous direct irradiance I in [0, 1] at
 * surface point (u, v) for a given unit sun vector `sun_dir` in body frame.
 * Accounts for both Lambertian cosine law max(0, n . sun_dir) and exact
 * analytical self-shadowing (e.g., torus outer ring shadowing the inner hub).
 */
COROLLA_API double corolla_insolation(const corolla_surface_t *surf,
                          double u, double v,
                          const double sun_dir[3]);

/*
 * corolla_annual_insolation — Integrate mean annual insolation at surface
 * coordinates (u, v) over a full orbital year and diurnal spin cycle for a
 * given axial tilt `axial_tilt_rad`.
 * Uses `season_samples` orbital steps and `day_samples` diurnal steps
 * (if <= 0, defaults to 16 seasonal x 16 diurnal = 256 samples).
 */
COROLLA_API double corolla_annual_insolation(const corolla_surface_t *surf,
                                 double u, double v,
                                 double axial_tilt_rad,
                                 int season_samples,
                                 int day_samples);

/*
 * corolla_orbit_pos — Evaluate 3D Cartesian position (X, Y, Z) of an orbiting
 * body in its parent frame at time `t` by solving Kepler's equation
 * M = E - e * sin(E).
 */
COROLLA_API int corolla_orbit_pos(const corolla_orbit_t *orb,
                      double t,
                      double out_pos[3]);

/*
 * corolla_eval_body_pose — Evaluate the instantaneous 3D rigid-body pose
 * (position `center[3]` and orthonormal orientation matrices `rot[9]` / `inv_rot[9]`)
 * of `body` at time `t` relative to `parent_pos` (if NULL, parent is at origin).
 *
 * Orientation convention:
 *   R = R_z(axis_azimuth_rad) * R_x(-axial_tilt_rad) * R_z(spin_angle)
 * so that at axis_azimuth_rad = 0, a body orbiting in the XY plane with obliquity
 * `axial_tilt_rad` has its spin axis tilted by `axial_tilt_rad` and its body-frame
 * directional vector to a central star at orbital longitude `tau` matches
 * `corolla_sun_dir(axial_tilt_rad, tau, spin_angle)`.
 */
COROLLA_API int corolla_eval_body_pose(const corolla_body_t *body,
                           const double parent_pos[3],
                           double t,
                           corolla_pose_t *out);

/* Transform a 3D directional vector between body-local and inertial frames */
COROLLA_API void corolla_pose_vec_to_inertial(const corolla_pose_t *pose,
                                  const double v_body[3],
                                  double out_inertial[3]);

COROLLA_API void corolla_pose_vec_to_body(const corolla_pose_t *pose,
                              const double v_inertial[3],
                              double out_body[3]);

/* Transform a 3D position point between body-local and inertial frames */
COROLLA_API void corolla_pose_point_to_inertial(const corolla_pose_t *pose,
                                    const double p_body[3],
                                    double out_inertial[3]);

COROLLA_API void corolla_pose_point_to_body(const corolla_pose_t *pose,
                                const double p_inertial[3],
                                double out_body[3]);

/*
 * corolla_pose_relative — Compute the relative rigid-body pose of `target`
 * expressed in `observer`'s local body coordinate frame (and optionally offset
 * by `observer_local_origin` in observer body space).
 *   - out_rel->rot = observer->inv_rot * target->rot
 *   - out_rel->center = observer->inv_rot * (target->center - observer->center) - observer_local_origin
 */
COROLLA_API int corolla_pose_relative(const corolla_pose_t *observer,
                          const double observer_local_origin[3],
                          const corolla_pose_t *target,
                          corolla_pose_t *out_rel);

/*
 * corolla_patch_aabb — Compute a tight conservative 3D axis-aligned bounding box
 * `[out_min[3], out_max[3]]` for a parametric surface patch spanning
 * `u in [u0, u0 + du]`, `v in [v0, v0 + dv]`, and normal height `h in [h_min, h_max]`.
 *
 *   - If `pose` is non-NULL, the patch is transformed by `pose->rot` and translated by `pose->center`.
 *   - If `ref_origin` is non-NULL, `ref_origin` is subtracted in double precision before storing
 *     to `out_min` / `out_max` (eliminating float32 cancellation for camera-relative culling).
 *   - Evaluates all 8 patch corners plus analytical interior trigonometric extrema
 *     (phi = k*pi/2, theta = k*pi/2) that fall inside `[u0, u0+du] x [v0, v0+dv]`.
 */
COROLLA_API int corolla_patch_aabb(const corolla_surface_t *surf,
                       const corolla_pose_t *pose,
                       const double ref_origin[3],
                       double u0, double v0,
                       double du, double dv,
                       double h_min, double h_max,
                       double out_min[3],
                       double out_max[3]);

/* ============================================================================
 * 4. 1D Annual Insolation LUT, Planetary Wind, Cloud Advection & Whittaker
 *    Climate Transfer Layer (coupled with libchaff)
 * ============================================================================ */

#define COROLLA_LUT_DEFAULT_LEN 64

/* Canonical Whittaker biome taxonomy (0..18), matching 4x4 (moisture x temperature)
 * ecological classification plus water (0), permanent ice (1), and volcanic (18). */
typedef enum corolla_biome {
	COROLLA_BIOME_WATER                     = 0,
	COROLLA_BIOME_PERMANENT_ICE             = 1,
	/* Cold band (temp_bin = 0, moisture_bin = 0..3) */
	COROLLA_BIOME_TUNDRA                    = 2,
	COROLLA_BIOME_ALPINE_MEADOW             = 3,
	COROLLA_BIOME_MEDITERRANEAN             = 4,
	COROLLA_BIOME_WETLAND                   = 5,
	/* Cool-temperate band (temp_bin = 1, moisture_bin = 0..3) */
	COROLLA_BIOME_COLD_DESERT               = 6,
	COROLLA_BIOME_SHRUBLAND                 = 7,
	COROLLA_BIOME_CONIFEROUS_FOREST         = 8,
	COROLLA_BIOME_BOREAL_FOREST             = 9,
	/* Warm-temperate band (temp_bin = 2, moisture_bin = 0..3) */
	COROLLA_BIOME_TEMPERATE_GRASSLAND       = 10,
	COROLLA_BIOME_WOODLAND                  = 11,
	COROLLA_BIOME_TEMPERATE_SEASONAL_FOREST = 12,
	COROLLA_BIOME_TEMPERATE_RAINFOREST      = 13,
	/* Tropical band (temp_bin = 3, moisture_bin = 0..3) */
	COROLLA_BIOME_DESERT                    = 14,
	COROLLA_BIOME_SAVANNAH                  = 15,
	COROLLA_BIOME_TROPICAL_SEASONAL_FOREST  = 16,
	COROLLA_BIOME_TROPICAL_RAINFOREST       = 17,
	COROLLA_BIOME_VOLCANIC                  = 18,
	COROLLA_BIOME_MAX                       = 19
} corolla_biome_t;

/*
 * corolla_bake_annual_insolation_lut — Precompute a 1D lookup table of mean
 * annual self-shadowed insolation over poloidal/latitudinal parameter v in [0, 1).
 * Because a planet rotates uniformly around u, annual mean insolation depends
 * strictly on v. Baking `lut_len` samples (e.g. 64) once per world eliminates
 * per-room quartic raycasts while preserving exact self-shadowing & obliquity physics.
 */
COROLLA_API int corolla_bake_annual_insolation_lut(const corolla_surface_t *surf,
                                       double axial_tilt_rad,
                                       int season_samples,
                                       int day_samples,
                                       double *out_lut,
                                       size_t lut_len);

/*
 * corolla_sample_annual_insolation_lut — O(1) periodic linear interpolation
 * of pre-baked annual insolation at normalized coordinate v in [0, 1).
 */
COROLLA_API double corolla_sample_annual_insolation_lut(const double *lut,
                                            size_t lut_len,
                                            double v);

/* Planetary climate & atmospheric circulation model */
typedef struct corolla_climate_model {
	corolla_surface_t surf;        /* Parametric manifold (Torus, Sphere, Cylinder, Plane) */
	double axial_tilt_rad;         /* Obliquity epsilon (radians, e.g. 28 deg = 0.48869) */
	double spin_rate;              /* +1.0 = prograde rotation, -1.0 = retrograde */
	double temp_min;               /* Freezing / polar baseline temperature (e.g. -36.0) */
	double temp_max;               /* Peak equatorial solar temperature (e.g. +102.0) */
	double lapse_rate;             /* Cooling per unit elevation above sea_level (e.g. 0.165) */
	double sea_level;              /* Planetary sea level in elevation units */
	double rain_max;               /* Maximum precipitation scale (e.g. 512.0) */
	uint32_t seed;                 /* Planetary deterministic seed for fallback noise */

	/* Optional libchaff noise fields (if NULL, deterministic multi-scale chaff
	 * octaves seeded from `seed` are evaluated automatically): */
	const chaff_field_t *elev_field;  /* 2D elevation field for orographic gradients */
	const chaff_field_t *temp_field;  /* 2D temperature variance field */
	const chaff_field_t *rain_field;  /* 2D moisture / rainfall field */
	const chaff_field_t *psi_field;   /* 3D streamfunction field for curl-noise wind eddies */
	const chaff_field_t *cloud_field; /* 4D spatio-temporal cloud density field */

	double annual_insol_lut[COROLLA_LUT_DEFAULT_LEN]; /* Pre-baked 64-entry LUT */
	int    lut_valid;              /* 1 once annual_insol_lut is baked */
} corolla_climate_model_t;

/* Instantaneous planetary wind & vertical uplift vector at (u, v, t) */
typedef struct corolla_wind {
	double wind_u;    /* Zonal (+East / +u) tangent wind velocity */
	double wind_v;    /* Meridional (+North / +v) tangent wind velocity */
	double uplift_w;  /* Vertical convective + orographic uplift (>0 rising, <0 subsiding) */
	double gust;      /* Local turbulent eddy magnitude in [0, 1] */
} corolla_wind_t;

/* Complete planetary climate, weather & Whittaker biome sample at (u, v) */
typedef struct corolla_climate {
	double annual_insol;     /* Mean annual self-shadowed insolation in [0, 1] */
	double instant_insol;    /* Instantaneous self-shadowed insolation in [0, 1] */
	double signed_solar;     /* Signed diurnal solar factor in [-1, +1] (with arch-eclipse) */
	double mean_temp;        /* Permanent mean annual temperature (with altitude lapse) */
	double current_temp;     /* Instantaneous temperature (mean_temp + diurnal swing) */
	double mean_moisture;    /* Permanent annual moisture / rainfall in [0, rain_max] */
	double cloud_cover;      /* Advected instantaneous cloud fraction in [0, 1] */
	double precipitation;    /* Instantaneous active precipitation rate in [0, 1] */
	corolla_wind_t wind;     /* Local surface wind & vertical uplift */
	uint8_t primary_biome;   /* Primary Whittaker biome (corolla_biome_t) */
	uint8_t secondary_biome; /* Secondary Whittaker biome for smooth border transition */
	uint8_t biome_blend;     /* Blend weight in [0..128] (0 = 100% primary, 128 = 50/50 border) */
} corolla_climate_t;

/*
 * corolla_climate_model_init — Initialize a planetary climate model for `surf`
 * with given `axial_tilt_rad`, `spin_rate`, temperature bounds, and `seed`, and
 * automatically bake its 64-entry `annual_insol_lut`.
 */
COROLLA_API int corolla_climate_model_init(corolla_climate_model_t *model,
                               const corolla_surface_t *surf,
                               double axial_tilt_rad,
                               double spin_rate,
                               double temp_min,
                               double temp_max,
                               double lapse_rate,
                               double sea_level,
                               double rain_max,
                               uint32_t seed);

/*
 * corolla_whittaker_classify — Classify (mean_temp, mean_moisture) in
 * [temp_min, temp_max] x [0, rain_max] into primary biome, secondary neighbor
 * biome, and C1 quadratic border blend factor [0..128].
 */
COROLLA_API void corolla_whittaker_classify(double mean_temp,
                                double mean_moisture,
                                double temp_min,
                                double temp_max,
                                double rain_max,
                                uint8_t *out_primary,
                                uint8_t *out_secondary,
                                uint8_t *out_blend);

/*
 * corolla_eval_wind — Evaluate deterministic planetary surface wind (wind_u, wind_v)
 * and vertical uplift (uplift_w) at normalized manifold coordinates (u, v),
 * local elevation gradient (grad_u, grad_v), sun direction `sun_dir`, and time `time_ticks`.
 * Combines:
 *   1. Coriolis 3-cell zonal bands (Trade Winds, Westerlies, Polar Easterlies) from n_z & spin_rate,
 *   2. Subsolar thermal draft toward the lit hemisphere,
 *   3. Divergence-free curl-noise eddies via libchaff (`chaff_sample_flow_2d`),
 *   4. Windward orographic uplift & leeward Foehn descent (w . grad_H).
 */
COROLLA_API int corolla_eval_wind(const corolla_climate_model_t *model,
                      double u, double v,
                      double grad_u, double grad_v,
                      const double sun_dir[3],
                      double time_ticks,
                      corolla_wind_t *out_wind);

/*
 * corolla_eval_climate — Evaluate full planetary climate, wind, semi-Lagrangian
 * advected 4D cloud cover (`chaff_sample_grad_4d`), and Whittaker biome classification
 * at normalized coordinates (u, v), elevation `elev`, and local slope `(grad_u, grad_v)`.
 */
COROLLA_API int corolla_eval_climate(const corolla_climate_model_t *model,
                         double u, double v,
                         double elev,
                         double grad_u, double grad_v,
                         const double sun_dir[3],
                         double time_ticks,
                         corolla_climate_t *out);

#ifdef __cplusplus
}
#endif

#if defined(COROLLA_HEADER_ONLY_ACTIVE)
#include "../../src/corolla.c"
#endif

#endif /* TTYPT_COROLLA_H */
