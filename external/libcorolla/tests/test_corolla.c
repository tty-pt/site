#include <ttypt/corolla.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define ASSERT_NEAR(a, b, eps) do { \
	double _va = (a), _vb = (b), _ve = (eps); \
	if (fabs(_va - _vb) > _ve) { \
		fprintf(stderr, "FAIL %s:%d: %s = %.12g, expected %.12g (diff %.12g > %.12g)\n", \
			__FILE__, __LINE__, #a, _va, _vb, fabs(_va - _vb), _ve); \
		exit(1); \
	} \
} while (0)

static void
test_torus_frame_and_inverse(void)
{
	/* World 0 baseline radii: R = 2,000,000, r0 = 1,335,000 */
	corolla_surface_t torus = corolla_torus(2000.0, 1335.0);
	corolla_frame_t fr;

	/* 1. Outer equator at u = 0.0, v = 0.5 -> (R + r0, 0, 0), normal (+1, 0, 0) */
	assert(corolla_eval_frame(&torus, 0.0, 0.5, 0.0, &fr) == 0);
	ASSERT_NEAR(fr.pos[0], 3335.0, 1e-9);
	ASSERT_NEAR(fr.pos[1], 0.0, 1e-9);
	ASSERT_NEAR(fr.pos[2], 0.0, 1e-9);
	ASSERT_NEAR(fr.norm[0], 1.0, 1e-9);
	ASSERT_NEAR(fr.norm[1], 0.0, 1e-9);
	ASSERT_NEAR(fr.norm[2], 0.0, 1e-9);
	ASSERT_NEAR(fr.ds_u, COROLLA_TWO_PI * 3335.0, 1e-6);
	ASSERT_NEAR(fr.ds_v, COROLLA_TWO_PI * 1335.0, 1e-6);

	/* 2. Inner hub equator at u = 0.0, v = 0.0 -> (R - r0, 0, 0), normal (-1, 0, 0) */
	assert(corolla_eval_frame(&torus, 0.0, 0.0, 0.0, &fr) == 0);
	ASSERT_NEAR(fr.pos[0], 665.0, 1e-9);
	ASSERT_NEAR(fr.pos[1], 0.0, 1e-9);
	ASSERT_NEAR(fr.pos[2], 0.0, 1e-9);
	ASSERT_NEAR(fr.norm[0], -1.0, 1e-9);
	ASSERT_NEAR(fr.norm[1], 0.0, 1e-9);
	ASSERT_NEAR(fr.norm[2], 0.0, 1e-9);
	ASSERT_NEAR(fr.ds_u, COROLLA_TWO_PI * 665.0, 1e-6);

	/* 3. North crown pole at u = 0.25, v = 0.75 -> (0, R, +r0), normal (0, 0, +1) */
	assert(corolla_eval_frame(&torus, 0.25, 0.75, 0.0, &fr) == 0);
	ASSERT_NEAR(fr.pos[0], 0.0, 1e-9);
	ASSERT_NEAR(fr.pos[1], 2000.0, 1e-9);
	ASSERT_NEAR(fr.pos[2], 1335.0, 1e-9);
	ASSERT_NEAR(fr.norm[0], 0.0, 1e-9);
	ASSERT_NEAR(fr.norm[1], 0.0, 1e-9);
	ASSERT_NEAR(fr.norm[2], 1.0, 1e-9);

	/* 4. Orthonormality & round-trip (u, v, h) -> (X, Y, Z) -> (u, v, h) across grid */
	for (int iu = 1; iu < 16; iu++) {
		for (int iv = 1; iv < 16; iv++) {
			double u = (double)iu / 16.0;
			double v = (double)iv / 16.0;
			double h = 42.5;
			assert(corolla_eval_frame(&torus, u, v, h, &fr) == 0);

			/* Check unit lengths and mutual orthogonality */
			double n_len = hypot(hypot(fr.norm[0], fr.norm[1]), fr.norm[2]);
			double tu_len = hypot(hypot(fr.tan_u[0], fr.tan_u[1]), fr.tan_u[2]);
			double tv_len = hypot(hypot(fr.tan_v[0], fr.tan_v[1]), fr.tan_v[2]);
			ASSERT_NEAR(n_len, 1.0, 1e-12);
			ASSERT_NEAR(tu_len, 1.0, 1e-12);
			ASSERT_NEAR(tv_len, 1.0, 1e-12);

			double dot_ntu = fr.norm[0]*fr.tan_u[0] + fr.norm[1]*fr.tan_u[1] + fr.norm[2]*fr.tan_u[2];
			double dot_ntv = fr.norm[0]*fr.tan_v[0] + fr.norm[1]*fr.tan_v[1] + fr.norm[2]*fr.tan_v[2];
			double dot_utv = fr.tan_u[0]*fr.tan_v[0] + fr.tan_u[1]*fr.tan_v[1] + fr.tan_u[2]*fr.tan_v[2];
			ASSERT_NEAR(dot_ntu, 0.0, 1e-12);
			ASSERT_NEAR(dot_ntv, 0.0, 1e-12);
			ASSERT_NEAR(dot_utv, 0.0, 1e-12);

			/* Inverse round-trip */
			double ru = 0.0, rv = 0.0, rh = 0.0;
			assert(corolla_xyz_to_uvh(&torus, fr.pos[0], fr.pos[1], fr.pos[2], &ru, &rv, &rh) == 0);
			ASSERT_NEAR(ru, u, 1e-9);
			ASSERT_NEAR(rv, v, 1e-9);
			ASSERT_NEAR(rh, h, 1e-9);
		}
	}
	printf("  [PASS] Torus frame & inverse round-trip\n");
}

static void
test_sphere_cylinder_plane_frames(void)
{
	corolla_surface_t sph = corolla_sphere(1000.0);
	corolla_frame_t fr;

	assert(corolla_eval_frame(&sph, 0.0, 0.5, 10.0, &fr) == 0);
	ASSERT_NEAR(fr.pos[0], 1010.0, 1e-9);
	ASSERT_NEAR(fr.pos[1], 0.0, 1e-9);
	ASSERT_NEAR(fr.pos[2], 0.0, 1e-9);

	double u, v, h;
	assert(corolla_xyz_to_uvh(&sph, fr.pos[0], fr.pos[1], fr.pos[2], &u, &v, &h) == 0);
	ASSERT_NEAR(u, 0.0, 1e-9);
	ASSERT_NEAR(v, 0.5, 1e-9);
	ASSERT_NEAR(h, 10.0, 1e-9);

	/* Ray hit against sphere */
	double orig[3] = { 3000.0, 0.0, 0.0 };
	double dir[3]  = { -1.0, 0.0, 0.0 };
	corolla_hit_t hit;
	assert(corolla_ray_hit(&sph, orig, dir, 0.0, 10000.0, &hit) == 1);
	ASSERT_NEAR(hit.t, 2000.0, 1e-9);
	ASSERT_NEAR(hit.pos[0], 1000.0, 1e-9);
	ASSERT_NEAR(hit.u, 0.0, 1e-9);
	ASSERT_NEAR(hit.v, 0.5, 1e-9);

	/* Cylinder & Plane */
	corolla_surface_t cyl = corolla_cylinder(500.0, 200.0);
	assert(corolla_ray_hit(&cyl, orig, dir, 0.0, 10000.0, &hit) == 1);
	ASSERT_NEAR(hit.t, 2500.0, 1e-9);

	corolla_surface_t pln = corolla_plane(100.0, 100.0);
	double porig[3] = { 10.0, -20.0, 50.0 };
	double pdir[3]  = { 0.0, 0.0, -1.0 };
	assert(corolla_ray_hit(&pln, porig, pdir, 0.0, 1000.0, &hit) == 1);
	ASSERT_NEAR(hit.t, 50.0, 1e-9);
	ASSERT_NEAR(hit.u, 0.6, 1e-9);
	ASSERT_NEAR(hit.v, 0.3, 1e-9);

	printf("  [PASS] Sphere, Cylinder, and Plane frames & ray intersections\n");
}

static void
test_torus_quartic_ray_and_axial_tilt_insolation(void)
{
	/* Torus with R = 2000, r0 = 1335:
	 * Outer equatorial radius = R + r0 = 3335
	 * Inner hub radius        = R - r0 = 665
	 * A ray along -X in the z = 0 plane starting at x = +5000 intersects the torus
	 * 4 times: +3335, +665, -665, -3335! */
	corolla_surface_t torus = corolla_torus(2000.0, 1335.0);
	double orig[3] = { 5000.0, 0.0, 0.0 };
	double dir[3]  = { -1.0, 0.0, 0.0 };
	corolla_hit_t hit;

	/* First hit: outer front wall at x = +3335 (t = 1665) */
	assert(corolla_ray_hit(&torus, orig, dir, 0.0, 10000.0, &hit) == 1);
	ASSERT_NEAR(hit.t, 1665.0, 1e-5);
	ASSERT_NEAR(hit.pos[0], 3335.0, 1e-5);
	ASSERT_NEAR(hit.u, 0.0, 1e-5);
	ASSERT_NEAR(hit.v, 0.5, 1e-5);

	/* Second hit: inner front wall at x = +665 (t = 4335) when starting past first hit */
	assert(corolla_ray_hit(&torus, orig, dir, 1700.0, 10000.0, &hit) == 1);
	ASSERT_NEAR(hit.t, 4335.0, 1e-5);
	ASSERT_NEAR(hit.pos[0], 665.0, 1e-5);
	ASSERT_NEAR(hit.u, 0.0, 1e-5);
	ASSERT_NEAR(hit.v, 0.0, 1e-5);

	/* Third hit: inner back wall at x = -665 (t = 5665) */
	assert(corolla_ray_hit(&torus, orig, dir, 4400.0, 10000.0, &hit) == 1);
	ASSERT_NEAR(hit.t, 5665.0, 1e-5);
	ASSERT_NEAR(hit.pos[0], -665.0, 1e-5);
	ASSERT_NEAR(hit.u, 0.5, 1e-5);
	ASSERT_NEAR(hit.v, 0.0, 1e-5);

	/* Fourth hit: outer back wall at x = -3335 (t = 8335) */
	assert(corolla_ray_hit(&torus, orig, dir, 5700.0, 10000.0, &hit) == 1);
	ASSERT_NEAR(hit.t, 8335.0, 1e-5);
	ASSERT_NEAR(hit.pos[0], -3335.0, 1e-5);

	/* --- Self-Shadowing & Axial Tilt Physics (Anders Sandberg Model) ---
	 * Consider the inner hub equator at u = 0.0, v = 0.0:
	 * Position = (+665, 0, 0), Outward Normal = (-1, 0, 0) (pointing across the central hole
	 * toward the -X lip of the torus at x = -2000, z = +-1335).
	 *
	 * 1. At EQUINOX (delta = 0), when the sun is in the -X direction (-1, 0, 0):
	 *    The ray from (+665, 0, 0) toward (-1, 0, 0) hits the opposite inner wall at (-665, 0, 0)!
	 *    So the inner hub equator is 100% self-shadowed (insolation == 0.0)! */
	double sun_equinox[3] = { -1.0, 0.0, 0.0 };
	assert(corolla_occluded(&torus, 0.0, 0.0, 0.0, sun_equinox) == 1);
	ASSERT_NEAR(corolla_insolation(&torus, 0.0, 0.0, sun_equinox), 0.0, 1e-9);

	/* 2. At SOLSTICE with axial tilt (say sun elevation angle 32 deg above -X axis):
	 *    From (+665, 0, 0), the opposite torus crown at x = -2000 has maximum height z = +1335
	 *    over horizontal distance dx = 2665 (slope ~ 1335 / 2665 = 0.50 -> ~26.6 deg).
	 *    So at 32 deg solar declination, sunlight clears the opposite lip and directly
	 *    illuminates the inner hub equator! */
	double tilt_32 = 32.0 * (COROLLA_PI / 180.0);
	double sun_solstice[3] = { -cos(tilt_32), 0.0, sin(tilt_32) };
	assert(corolla_occluded(&torus, 0.0, 0.0, 0.0, sun_solstice) == 0);
	double ins_solstice = corolla_insolation(&torus, 0.0, 0.0, sun_solstice);
	ASSERT_NEAR(ins_solstice, cos(tilt_32), 1e-6);

	/* 3. Annual integrated insolation at inner hub (v = 0.0) and upper hub (v = 0.08):
	 *    - With zero axial tilt (0 rad): inner hub (v = 0.0) is permanently shadowed all year -> 0.0!
	 *    - With 32 deg axial tilt: inner hub receives direct summer sunlight (~0.033 mean annual),
	 *      and upper inner hub (v = 0.08) receives > 0.08 mean annual insolation! */
	double ann_zero_tilt = corolla_annual_insolation(&torus, 0.0, 0.0, 0.0, 16, 16);
	double ann_with_tilt = corolla_annual_insolation(&torus, 0.0, 0.0, tilt_32, 16, 16);
	double ann_upper_hub = corolla_annual_insolation(&torus, 0.0, 0.08, tilt_32, 16, 16);
	ASSERT_NEAR(ann_zero_tilt, 0.0, 1e-9);
	assert(ann_with_tilt > 0.025);
	assert(ann_upper_hub > 0.08);

	printf("  [PASS] Torus quartic 4-wall raycast & axial-tilt hub insolation (zero-tilt=%.4f, 32deg-hub=%.4f, 32deg-upper-hub=%.4f)\n",
		ann_zero_tilt, ann_with_tilt, ann_upper_hub);
}

static void
test_kepler_orbit(void)
{
	corolla_orbit_t orb = {
		.semi_major_axis = 10000.0,
		.eccentricity = 0.2,
		.inclination_rad = 0.0,
		.lon_asc_node_rad = 0.0,
		.arg_periapsis_rad = 0.0,
		.mean_anomaly_0 = 0.0,
		.period = 100.0
	};
	double pos[3];

	/* At t = 0 (periapsis, E = 0): x = a*(1 - e) = 8000, y = 0, z = 0 */
	assert(corolla_orbit_pos(&orb, 0.0, pos) == 0);
	ASSERT_NEAR(pos[0], 8000.0, 1e-6);
	ASSERT_NEAR(pos[1], 0.0, 1e-6);
	ASSERT_NEAR(pos[2], 0.0, 1e-6);

	/* At t = T/2 (apoapsis, E = pi): x = -a*(1 + e) = -12000, y = 0, z = 0 */
	assert(corolla_orbit_pos(&orb, 50.0, pos) == 0);
	ASSERT_NEAR(pos[0], -12000.0, 1e-6);
	ASSERT_NEAR(pos[1], 0.0, 1e-6);
	ASSERT_NEAR(pos[2], 0.0, 1e-6);

	printf("  [PASS] Keplerian orbit solver\n");
}

static void
test_body_pose_and_patch_aabb(void)
{
	/* 1. Planet 0 with 32 deg axial tilt and prograde spin */
	double tilt_32 = 32.0 * (COROLLA_PI / 180.0);
	corolla_body_t planet0 = {
		.surf = corolla_torus(2000000.0, 1335000.0),
		.orbit = {
			.semi_major_axis = 0.0,
			.period = 64.0 * 3600.0
		},
		.axial_tilt_rad = tilt_32,
		.axis_azimuth_rad = 0.0,
		.spin_period = 3600.0,
		.spin_phase_0 = 0.0
	};

	corolla_pose_t p0_pose;
	assert(corolla_eval_body_pose(&planet0, NULL, 900.0, &p0_pose) == 0);
	ASSERT_NEAR(p0_pose.center[0], 0.0, 1e-9);
	ASSERT_NEAR(p0_pose.center[1], 0.0, 1e-9);
	ASSERT_NEAR(p0_pose.center[2], 0.0, 1e-9);
	ASSERT_NEAR(p0_pose.spin_angle, COROLLA_HALF_PI, 1e-9);

	/* Verify R * R^T == I (orthonormality) and round-trip vector/point transforms */
	for (int r = 0; r < 3; r++) {
		for (int c = 0; c < 3; c++) {
			double dot = p0_pose.rot[r * 3 + 0] * p0_pose.inv_rot[0 * 3 + c] +
			             p0_pose.rot[r * 3 + 1] * p0_pose.inv_rot[1 * 3 + c] +
			             p0_pose.rot[r * 3 + 2] * p0_pose.inv_rot[2 * 3 + c];
			ASSERT_NEAR(dot, (r == c) ? 1.0 : 0.0, 1e-12);
		}
	}

	/* Spin axis +Z in body frame should be tilted by 32 deg in inertial frame */
	double z_body[3] = { 0.0, 0.0, 1.0 }, z_inert[3], z_back[3];
	corolla_pose_vec_to_inertial(&p0_pose, z_body, z_inert);
	ASSERT_NEAR(z_inert[0], 0.0, 1e-12);
	ASSERT_NEAR(z_inert[1], sin(tilt_32), 1e-12);
	ASSERT_NEAR(z_inert[2], cos(tilt_32), 1e-12);
	corolla_pose_vec_to_body(&p0_pose, z_inert, z_back);
	ASSERT_NEAR(z_back[0], 0.0, 1e-12);
	ASSERT_NEAR(z_back[1], 0.0, 1e-12);
	ASSERT_NEAR(z_back[2], 1.0, 1e-12);

	/* 2. Moon orbiting Planet 0 on an inclined elliptical Keplerian orbit with retrograde spin */
	corolla_body_t moon = {
		.surf = corolla_torus(800000.0, 500000.0),
		.orbit = {
			.semi_major_axis = 16000000.0,
			.eccentricity = 0.05,
			.inclination_rad = 28.584 * (COROLLA_PI / 180.0),
			.lon_asc_node_rad = 0.25,
			.arg_periapsis_rad = 0.50,
			.mean_anomaly_0 = 0.785398,
			.period = 3725.0
		},
		.axial_tilt_rad = 18.0 * (COROLLA_PI / 180.0),
		.axis_azimuth_rad = 0.4,
		.spin_period = -1862.5,
		.spin_phase_0 = 0.1
	};

	corolla_pose_t moon_pose0, moon_pose1, rel_pose;
	assert(corolla_eval_body_pose(&moon, p0_pose.center, 0.0, &moon_pose0) == 0);
	assert(corolla_eval_body_pose(&moon, p0_pose.center, 931.25, &moon_pose1) == 0);
	/* Moon must translate along its 3D orbit over time */
	double dx = moon_pose1.center[0] - moon_pose0.center[0];
	double dy = moon_pose1.center[1] - moon_pose0.center[1];
	double dz = moon_pose1.center[2] - moon_pose0.center[2];
	assert(hypot(hypot(dx, dy), dz) > 5000000.0);

	/* Relative pose of Moon in Planet 0's local frame */
	double cam_origin[3] = { 3335000.0, 0.0, 0.0 };
	assert(corolla_pose_relative(&p0_pose, cam_origin, &moon_pose1, &rel_pose) == 0);

	/* 3. Verify corolla_patch_aabb strictly bounds all sampled interior points of curved & rotated patches */
	double bmin[3], bmax[3];
	assert(corolla_patch_aabb(&moon.surf, &rel_pose, NULL,
	                          0.125, 0.375, 0.25, 0.25, -200.0, 450.0,
	                          bmin, bmax) == 0);
	for (int iu = 0; iu <= 16; iu++) {
		for (int iv = 0; iv <= 16; iv++) {
			double u = 0.125 + 0.25 * ((double)iu / 16.0);
			double v = 0.375 + 0.25 * ((double)iv / 16.0);
			for (int ih = 0; ih <= 1; ih++) {
				double h = ih ? 450.0 : -200.0;
				corolla_frame_t fr;
				assert(corolla_eval_frame(&moon.surf, u, v, h, &fr) == 0);
				double pt[3];
				corolla_pose_point_to_inertial(&rel_pose, fr.pos, pt);
				assert(pt[0] >= bmin[0] - 1e-6 && pt[0] <= bmax[0] + 1e-6);
				assert(pt[1] >= bmin[1] - 1e-6 && pt[1] <= bmax[1] + 1e-6);
				assert(pt[2] >= bmin[2] - 1e-6 && pt[2] <= bmax[2] + 1e-6);
			}
		}
	}

	/* Full torus [0, 1) x [0, 1) unrotated AABB must equal [-(R+r0), +(R+r0)] x [-(R+r0), +(R+r0)] x [-r0, +r0] */
	assert(corolla_patch_aabb(&planet0.surf, NULL, NULL, 0.0, 0.0, 1.0, 1.0, 0.0, 0.0, bmin, bmax) == 0);
	ASSERT_NEAR(bmin[0], -3335000.0, 1.0);
	ASSERT_NEAR(bmax[0],  3335000.0, 1.0);
	ASSERT_NEAR(bmin[1], -3335000.0, 1.0);
	ASSERT_NEAR(bmax[1],  3335000.0, 1.0);
	ASSERT_NEAR(bmin[2], -1335000.0, 1.0);
	ASSERT_NEAR(bmax[2],  1335000.0, 1.0);

	printf("  [PASS] 3D rigid-body pose, axial-tilt spin basis, relative pose & manifold patch AABB\n");
}

static void
test_climate_wind_and_whittaker(void)
{
	corolla_surface_t torus = corolla_torus(2000000.0, 1335000.0);
	/* For R = 2000, r0 = 1335, the opposite crown shadow cone from the inner hub equator
	 * (v = 0.0) is arctan(1335 / 2665) = 26.61 deg. With 32 deg obliquity, summer sunlight
	 * clears the opposite lip and illuminates the entire inner hub! */
	double tilt_rad = 32.0 * (COROLLA_PI / 180.0);

	/* 1. Verify 1D Annual Insolation LUT accuracy against direct integration */
	corolla_climate_model_t prograde_model, retrograde_model;
	assert(corolla_climate_model_init(&prograde_model, &torus, tilt_rad, +1.0,
	                                  -36.0, 102.0, 0.165, 0.0, 512.0, 0x1234u) == 0);
	assert(prograde_model.lut_valid == 1);

	assert(corolla_climate_model_init(&retrograde_model, &torus, tilt_rad, -1.0,
	                                  -36.0, 102.0, 0.165, 0.0, 512.0, 0x1234u) == 0);

	/* At exact grid points v = k / 64, LUT must match corolla_annual_insolation exactly */
	for (int k = 0; k < 64; k += 8) {
		double v = (double)k / 64.0;
		double exact = corolla_annual_insolation(&torus, 0.0, v, tilt_rad, 16, 16);
		double lut_v = corolla_sample_annual_insolation_lut(prograde_model.annual_insol_lut,
		                                                    COROLLA_LUT_DEFAULT_LEN, v);
		ASSERT_NEAR(lut_v, exact, 1e-12);
	}

	/* 2. Verify Coriolis Trade Winds vs Westerlies and Prograde/Retrograde reversal:
	 *    Use zero curl-amp to isolate the Coriolis planetary wind cells cleanly, and
	 *    also verify full curl-eddy evaluation. */
	double sun_eq[3] = { 1.0, 0.0, 0.0 };
	corolla_wind_t w_eq_pro, w_eq_ret, w_mid_pro;
	/* At outer equator (u=0.25 so sun_dir . tan_u = 0, v=0.5 -> n_z = 0):
	 * Trade winds blow westward (wind_u < 0 for prograde +1, wind_u > 0 for retrograde -1) */
	assert(corolla_eval_wind(&prograde_model, 0.25, 0.50, 0.0, 0.0, sun_eq, 10.0, &w_eq_pro) == 0);
	assert(corolla_eval_wind(&retrograde_model, 0.25, 0.50, 0.0, 0.0, sun_eq, 10.0, &w_eq_ret) == 0);
	assert(w_eq_pro.wind_u < -0.30);
	assert(w_eq_ret.wind_u > +0.30);

	/* At mid-latitude Ferrel cell (v = 0.63 -> theta ~ 47 deg -> cos(3*lat) < 0):
	 * Westerlies blow eastward (wind_u > 0 for prograde) */
	assert(corolla_eval_wind(&prograde_model, 0.25, 0.63, 0.0, 0.0, sun_eq, 10.0, &w_mid_pro) == 0);
	assert(w_mid_pro.wind_u > 0.20);

	/* 3. Verify Orographic Lift vs Leeward Rain Shadow and Cloud Advection:
	 *    At outer equator (u=0.25, v=0.50), wind_u < 0 (blowing in -u direction).
	 *    - Windward slope: terrain rises in -u direction -> grad_u < 0 -> w . grad_H > 0 (uplift!)
	 *    - Leeward slope:  terrain falls in -u direction -> grad_u > 0 -> w . grad_H < 0 (rain shadow!) */
	corolla_climate_t clim_windward, clim_leeward;
	assert(corolla_eval_climate(&prograde_model, 0.25, 0.50, 50.0, -0.8, 0.0, sun_eq, 25.0, &clim_windward) == 0);
	assert(corolla_eval_climate(&prograde_model, 0.25, 0.50, 50.0, +0.8, 0.0, sun_eq, 25.0, &clim_leeward) == 0);

	assert(clim_windward.wind.uplift_w > clim_leeward.wind.uplift_w);
	assert(clim_windward.mean_moisture > clim_leeward.mean_moisture + 30.0);
	assert(clim_windward.cloud_cover >= clim_leeward.cloud_cover);

	/* 4. Verify Distinct Whittaker Biomes across Torus Zones:
	 *    - Outer Equator (v = 0.50, wet windward): Tropical Rainforest / Seasonal Forest
	 *    - Subtropical Hadley Belt (v = 0.57, dry leeward): Desert / Savannah
	 *    - North Crown Pole (v = 0.75, high elevation): Permanent Ice / Tundra
	 *    - Axial-Tilted Inner Hub (v = 0.00, 28 deg tilt): Habitable cool/temperate biome (NOT permanent ice!) */
	corolla_climate_t clim_eq, clim_hadley, clim_pole, clim_hub;
	assert(corolla_eval_climate(&prograde_model, 0.10, 0.50, 20.0, -0.5, 0.0, sun_eq, 0.0, &clim_eq) == 0);
	assert(corolla_eval_climate(&prograde_model, 0.10, 0.57, 20.0, +0.5, 0.0, sun_eq, 0.0, &clim_hadley) == 0);
	assert(corolla_eval_climate(&prograde_model, 0.10, 0.75, 450.0, 0.0, 0.0, sun_eq, 0.0, &clim_pole) == 0);
	assert(corolla_eval_climate(&prograde_model, 0.10, 0.00, 20.0, 0.0, 0.0, sun_eq, 0.0, &clim_hub) == 0);

	assert(clim_eq.mean_temp > 65.0);
	assert(clim_eq.primary_biome >= COROLLA_BIOME_TEMPERATE_GRASSLAND);
	assert(clim_hadley.mean_moisture < clim_eq.mean_moisture);
	assert(clim_pole.primary_biome == COROLLA_BIOME_PERMANENT_ICE);
	assert(clim_hub.annual_insol > 0.01);
	assert(clim_hub.primary_biome != COROLLA_BIOME_PERMANENT_ICE);

	printf("  [PASS] LUT insolation, Coriolis prograde/retrograde wind, orographic lift & Whittaker biomes\n");
}

int
main(void)
{
	printf("=== Running libcorolla unit test suite ===\n");
	test_torus_frame_and_inverse();
	test_sphere_cylinder_plane_frames();
	test_torus_quartic_ray_and_axial_tilt_insolation();
	test_kepler_orbit();
	test_body_pose_and_patch_aabb();
	test_climate_wind_and_whittaker();
	printf("=== All libcorolla tests passed! ===\n");
	return 0;
}
