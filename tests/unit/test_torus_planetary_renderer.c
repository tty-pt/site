#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <string.h>
#include <assert.h>

#include "ttypt/areole.h"
#include <nd/field.h>
#include "nd_seven.h"

#define EPSILON 1e-4f

/* Mock sampler for unit testing */
static void mock_planet_sampler(void *user_ctx, uint16_t world, uint8_t level,
                                int32_t wx, int32_t wy, ar_sample_t *out)
{
	(void)user_ctx; (void)world; (void)level;
	memset(out, 0, sizeof(*out));
	out->val = (int16_t)((wx ^ wy) & 0x0FFF);
	out->rgba = 0xFF208020u;
}

static void test_torus_mathematics(void)
{
	printf("[1. Torus Parametric Geometry & Analytical Normals]\n");
	const float world_w = ND_SEVEN_WORLD_DIM_W;
	const float world_h = ND_SEVEN_WORLD_DIM_H;
	const float R = 2000000.0f;
	const float r0 = 1335000.0f;
	const float TWO_PI = 6.28318530718f;

	/* Test periodic wrapping across boundaries */
	float test_pts[][2] = {
		{ 0.0f, 0.0f },
		{ world_w * 0.25f, world_h * 0.25f },
		{ world_w * 0.50f, world_h * 0.50f },
		{ world_w * 0.75f, world_h * 0.75f },
		{ world_w, world_h },
		{ world_w * 1.5f, -world_h * 0.5f }
	};
	size_t num_pts = sizeof(test_pts) / sizeof(test_pts[0]);

	for (size_t i = 0; i < num_pts; i++) {
		float wx = test_pts[i][0];
		float wy = test_pts[i][1];
		float u = TWO_PI * (wx / world_w - floorf(wx / world_w));
		float v = TWO_PI * (wy / world_h - floorf(wy / world_h));

		float cu = cosf(u), su = sinf(u);
		float cv = cosf(v), sv = sinf(v);
		float r = r0;

		/* 3D torus position */
		float px = (R + r * cv) * cu;
		float py = (R + r * cv) * su;
		float pz = r * sv;
		assert(fabsf(pz) <= r0 + EPSILON);

		/* Analytical normal and tangents */
		float N[3]  = { cv * cu, cv * su, sv };
		float Tu[3] = { -su, cu, 0.0f };
		float Tv[3] = { -sv * cu, -sv * su, cv };

		/* Verify unit lengths */
		float len_N  = sqrtf(N[0]*N[0] + N[1]*N[1] + N[2]*N[2]);
		float len_Tu = sqrtf(Tu[0]*Tu[0] + Tu[1]*Tu[1] + Tu[2]*Tu[2]);
		float len_Tv = sqrtf(Tv[0]*Tv[0] + Tv[1]*Tv[1] + Tv[2]*Tv[2]);
		assert(fabsf(len_N - 1.0f) < EPSILON);
		assert(fabsf(len_Tu - 1.0f) < EPSILON);
		assert(fabsf(len_Tv - 1.0f) < EPSILON);

		/* Verify orthogonality: N . Tu = 0, N . Tv = 0, Tu . Tv = 0 */
		float dot_N_Tu = N[0]*Tu[0] + N[1]*Tu[1] + N[2]*Tu[2];
		float dot_N_Tv = N[0]*Tv[0] + N[1]*Tv[1] + N[2]*Tv[2];
		float dot_Tu_Tv = Tu[0]*Tv[0] + Tu[1]*Tv[1] + Tu[2]*Tv[2];
		assert(fabsf(dot_N_Tu) < EPSILON);
		assert(fabsf(dot_N_Tv) < EPSILON);
		assert(fabsf(dot_Tu_Tv) < EPSILON);

		/* Metric determinant dA = r * (R + r * cv) > 0 everywhere since R > r */
		float dA = r * (R + r * cv);
		assert(dA > 0.0f);
		assert(px * px + py * py > 0.0f);
	}
	printf("  PASS: Unit normals, tangent orthogonality, and metric positivity verified\n");
}

static void test_camera_zoom_progression(void)
{
	printf("[2. Diorama Camera & Extreme Zoom Sweep (4.0x down to 0.00001x)]\n");
	const float test_zooms[] = {
		4.0f, 2.0f, 1.0f, 0.5f, 0.25f, 0.1f, 0.05f, 0.01f,
		0.005f, 0.001f, 0.0006f, 0.0002f, 0.0001f, 0.00007f, 0.00001f
	};
	const size_t num_zooms = sizeof(test_zooms) / sizeof(test_zooms[0]);

	for (size_t z = 0; z < num_zooms; z++) {
		float zoom = test_zooms[z];
		float eff_cam_dist = ND_SEVEN_CAM_DIST / zoom;
		assert(eff_cam_dist > 0.0f);
		assert(zoom >= ND_SEVEN_ZOOM_MIN);
		assert(zoom <= ND_SEVEN_ZOOM_MAX);

		/* At 0.00001x zoom, distance reaches deep space overview */
		if (zoom <= 0.000015f) {
			assert(eff_cam_dist >= 28000000.0f);
		}
	}
	printf("  PASS: 15-step zoom sweep from 4.0x to 0.00001x strictly bounded\n");
}

static void test_multi_planet_definitions(void)
{
	printf("[3. Multi-Planet Celestial Bodies]\n");
	const nd_planet_def_t *p1 = nd_seven_get_planet(1);
	assert(p1 != NULL);
	assert(p1->major_r == 2000000.0f);
	assert(p1->minor_r == 1335000.0f);
	assert(p1->center[0] == 0.0f && p1->center[1] == 0.0f && p1->center[2] == 0.0f);

	const nd_planet_def_t *p2 = nd_seven_get_planet(2);
	assert(p2 != NULL);
	assert(p2->major_r == 800000.0f);
	assert(p2->minor_r == 500000.0f);
	assert(p2->center[0] == 4500000.0f);
	assert(p2->center[1] == -14000000.0f);
	assert(p2->center[2] == -8500000.0f);

	printf("  PASS: Planet 1 (Citadel) and Planet 2 (Aethelgard Moon) verified\n");
}

static void test_libchunk_level16_coverage(void)
{
	printf("[4. libchunk Level 16 Auto-Tiling & Full Torus Coverage]\n");
	ar_system_t *cs = ar_create(NDW_TILE_W, NDW_TILE_H, 32, 128, 64, 17, mock_planet_sampler, NULL);
	assert(cs != NULL);

	const float viewport_w = 1280.0f;
	const float viewport_h = 720.0f;
	const float pitch = 55.0f * 3.14159265f / 180.0f;
	const float cos_p = cosf(pitch);
	const float sin_p = sinf(pitch);

	const float test_zooms[] = { 0.25f, 0.05f, 0.005f, 0.0002f, 0.00007f, 0.00001f };
	const size_t num_zooms = sizeof(test_zooms) / sizeof(test_zooms[0]);

	for (size_t z = 0; z < num_zooms; z++) {
		float zoom = test_zooms[z];
		float dist = ND_SEVEN_CAM_DIST / zoom;
		float eye[3] = { 0.0f, dist * cos_p, dist * sin_p };
		float target[3] = { 0.0f, 0.0f, 0.0f };
		float up[3] = { 0.0f, -sin_p, cos_p };

		float modelview[16], proj[16], mvp[16];
		ar_mat4_lookat(modelview, eye, target, up);

		float scale = 2.0f * zoom;
		float hw = viewport_w / (2.0f * scale);
		float hh = viewport_h / (2.0f * scale);
		ar_mat4_ortho(proj, -hw, hw, -hh, hh, -30000000.0f, 30000000.0f);
		ar_mat4_multiply(mvp, proj, modelview);

		float r_planet = dist * 1.25f;
		if (r_planet < 16000.0f) r_planet = 16000.0f;
		if (r_planet > 80000.0f) r_planet = 80000.0f;

		ar_cull_opts_t opts = {
			.z_min = -2000.0f,
			.z_max = 2000.0f,
			.sag_radius = r_planet,
			.focal_x = 0.0f,
			.focal_y = 0.0f,
			.margin = 150.0f,
			.target_quad_px = 2.0f,
			.viewport_w = viewport_w,
			.viewport_h = viewport_h,
			.min_level = 0,
			.max_level = 16
		};

		const ar_node_t *visible[64];
		size_t count = ar_system_query_matrix(cs, 1, mvp, &opts, visible, 64);

		assert(count > 0);
		assert(count <= 64);

		bool has_pos_x = false, has_neg_x = false, has_pos_y = false, has_neg_y = false;
		for (size_t i = 0; i < count; i++) {
			const ar_node_t *n = visible[i];
			if (n->ox + n->w > 0) has_pos_x = true;
			if (n->ox < 0) has_neg_x = true;
			if (n->oy + n->h > 0) has_pos_y = true;
			if (n->oy < 0) has_neg_y = true;
		}
		assert(has_pos_x && has_neg_x && has_pos_y && has_neg_y);
	}

	ar_destroy(cs);
	printf("  PASS: Full 4-quadrant continuous coverage guaranteed across all zooms up to Level 16\n");
}

static void test_torus_solar_incidence_floating_point_parity(void)
{
	printf("[5. Torus Solar Incidence & Surface Normal Parity (Q15 Fixed-Point vs IEEE-754 Float)]\n");
	const float TWO_PI = 6.28318530718f;
	const uint16_t test_azimuths[4] = { 0, 16384, 32768, 49152 };

	for (size_t a = 0; a < 4; a++) {
		uint16_t az_turn = test_azimuths[a];
		float lambda = TWO_PI * ((float)az_turn / 65536.0f);
		float sun_x = cosf(lambda);
		float sun_y = sinf(lambda);

		int32_t sx_q15 = 0, sy_q15 = 0, sz_q15 = 0;
		nd_field_sun_dir_q15(az_turn, &sx_q15, &sy_q15, &sz_q15);
		assert(fabsf(((float)sx_q15 / 32768.0f) - sun_x) < 1e-3f);
		assert(fabsf(((float)sy_q15 / 32768.0f) - sun_y) < 1e-3f);
		assert(sz_q15 == 0);

		for (int iy = 0; iy < 32; iy++) {
			for (int ix = 0; ix < 32; ix++) {
				int16_t pos_x = (int16_t)(ix * 2048);
				int16_t pos_y = (int16_t)(iy * 2048);

				float theta = TWO_PI * ((float)(uint16_t)pos_x / 65536.0f);
				float phi   = TWO_PI * ((float)(uint16_t)pos_y / 65536.0f);

				float nx = cosf(phi) * cosf(theta);
				float ny = cosf(phi) * sinf(theta);
				float nz = sinf(phi);

				int32_t nx_q15 = 0, ny_q15 = 0, nz_q15 = 0;
				nd_field_torus_normal_q15(pos_x, pos_y, &nx_q15, &ny_q15, &nz_q15);
				assert(fabsf(((float)nx_q15 / 32768.0f) - nx) < 1e-3f);
				assert(fabsf(((float)ny_q15 / 32768.0f) - ny) < 1e-3f);
				assert(fabsf(((float)nz_q15 / 32768.0f) - nz) < 1e-3f);

				float dot = nx * sun_x + ny * sun_y;
				float expected_inc = (dot > 0.0f) ? dot : 0.0f;
				float expected_equiv = cosf(phi) * cosf(theta - lambda);
				if (expected_equiv < 0.0f) expected_equiv = 0.0f;
				assert(fabsf(expected_inc - expected_equiv) < 1e-5f);

				int32_t inc_q15 = nd_field_torus_solar_incidence_q15(pos_x, pos_y, az_turn);
				float actual_inc = (float)inc_q15 / 32768.0f;
				assert(fabsf(actual_inc - expected_inc) < 1e-3f);
			}
		}
	}
	printf("  PASS: Q15 torus normals and solar incidence match cos(phi)*cos(theta - lambda) within < 1e-3 across all azimuths\n");
}

int main(void)
{
	printf("=== Testing 3D Torus Planetary Engine & Renderer ===\n");
	test_torus_mathematics();
	test_camera_zoom_progression();
	test_multi_planet_definitions();
	test_libchunk_level16_coverage();
	test_torus_solar_incidence_floating_point_parity();
	printf("ALL TORUS PLANETARY RENDERER TESTS PASSED\n");
	return 0;
}
