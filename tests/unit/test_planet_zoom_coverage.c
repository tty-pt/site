#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <string.h>
#include <assert.h>

#include "ttypt/areole.h"

/* Canonical mock sampler returning procedural elevation */
static void mock_planet_sampler(void *user_ctx, uint16_t world, uint8_t level,
                                int32_t wx, int32_t wy, ar_sample_t *out)
{
	(void)user_ctx; (void)world; (void)level;
	memset(out, 0, sizeof(*out));
	/* Simple multi-scale gradient for testing */
	out->val = (int16_t)((wx ^ wy) & 0x0FFF);
	out->rgba = 0xFF208020u;
}

#define TEST_MAX_ACTIVE 64

int main(void)
{
	printf("=== Testing Minimized Zoom Full Planet Coverage (NeverDark Seven Orbit) ===\n");

	const int32_t base_w = 160;
	const int32_t base_h = 128;
	ar_system_t *cs = ar_create(base_w, base_h, 32, 128, TEST_MAX_ACTIVE, 17, mock_planet_sampler, NULL);
	assert(cs != NULL);

	const float viewport_w = 1280.0f;
	const float viewport_h = 720.0f;
	const float pitch = 55.0f * 3.14159265f / 180.0f;
	const float cos_p = cosf(pitch);
	const float sin_p = sinf(pitch);

	/* Test across the zoom out range where the planet view is minimized:
	 * 0.25x (boundary), 0.10x (high orbit), 0.03x (planetary view), 0.005x (extreme planetary horizon),
	 * down to 0.0006x, 0.0002x, 0.00007x, and 0.00001x (deep space planetary torus overview) */
	const float test_zooms[] = { 0.25f, 0.10f, 0.03f, 0.005f, 0.0006f, 0.0002f, 0.00007f, 0.00001f };
	const size_t num_zooms = sizeof(test_zooms) / sizeof(test_zooms[0]);

	for (size_t z = 0; z < num_zooms; z++) {
		float zoom = test_zooms[z];
		float eff_cam_dist = 280.0f / zoom;
		float lwx = 0.0f;
		float lwy = 0.0f;
		float lwz = 0.0f;
		float dist = eff_cam_dist;

		/* 1. Camera LookAt and Orthographic Projection matching Seven & Lamina WebGL */
		float eye[3] = {
			lwx,
			lwy + dist * cos_p,
			lwz + dist * sin_p
		};
		float target[3] = { lwx, lwy, lwz };
		float up[3] = { 0.0f, -sin_p, cos_p };

		float modelview[16], proj[16], mvp[16];
		ar_mat4_lookat(modelview, eye, target, up);

		float scale = 2.0f * zoom;
		float hw = viewport_w / (2.0f * scale);
		float hh = viewport_h / (2.0f * scale);
		ar_mat4_ortho(proj, -hw, hw, -hh, hh, -30000000.0f, 30000000.0f);
		ar_mat4_multiply(mvp, proj, modelview);

		/* 2. Planetary Curvature Sag Radius */
		float r_planet = dist * 1.25f;
		if (r_planet < 16000.0f) r_planet = 16000.0f;
		if (r_planet > 80000.0f) r_planet = 80000.0f;

		ar_cull_opts_t opts = {
			.z_min = -2000.0f,
			.z_max = 2000.0f,
			.sag_radius = r_planet,
			.focal_x = lwx,
			.focal_y = lwy,
			.margin = 150.0f,
			.target_quad_px = 2.0f,
			.viewport_w = viewport_w,
			.viewport_h = viewport_h,
			.min_level = 0,
			.max_level = 16
		};

		const ar_node_t *visible[TEST_MAX_ACTIVE];
		size_t count = ar_system_query_matrix(cs, 1, mvp, &opts, visible, TEST_MAX_ACTIVE);

		printf("--- Zoom %.4fx (dist=%.0f, req_span: W=%.0f, H=%.0f) ---\n",
		       zoom, dist, 2.0f * hw, 2.0f * hh / sin_p);
		printf("  Active chunks returned: %zu / %d\n", count, TEST_MAX_ACTIVE);

		assert(count > 0);
		assert(count <= TEST_MAX_ACTIVE);

		/* 3. Quadrant Coverage Check: All 4 quadrants around focal point must have chunks */
		bool has_pos_x = false, has_neg_x = false;
		bool has_pos_y = false, has_neg_y = false;
		float min_x = 1e9f, max_x = -1e9f;
		float min_y = 1e9f, max_y = -1e9f;

		for (size_t i = 0; i < count; i++) {
			const ar_node_t *n = visible[i];
			float c_min_x = (float)n->ox;
			float c_max_x = (float)(n->ox + n->w);
			float c_min_y = (float)n->oy;
			float c_max_y = (float)(n->oy + n->h);

			if (c_min_x < min_x) min_x = c_min_x;
			if (c_max_x > max_x) max_x = c_max_x;
			if (c_min_y < min_y) min_y = c_min_y;
			if (c_max_y > max_y) max_y = c_max_y;

			if (c_max_x > lwx) has_pos_x = true;
			if (c_min_x < lwx) has_neg_x = true;
			if (c_max_y > lwy) has_pos_y = true;
			if (c_min_y < lwy) has_neg_y = true;
		}

		printf("  Rendered World Span: X [%.0f .. %.0f] (w=%.0f), Y [%.0f .. %.0f] (h=%.0f)\n",
		       min_x, max_x, max_x - min_x, min_y, max_y, max_y - min_y);
		printf("  Quadrant Presence: +X:%s, -X:%s, +Y:%s, -Y:%s\n",
		       has_pos_x ? "YES" : "MISSING",
		       has_neg_x ? "YES" : "MISSING",
		       has_pos_y ? "YES" : "MISSING",
		       has_neg_y ? "YES" : "MISSING");

		/* In a fully rendered planet, chunks MUST NOT be missing in any visible quadrant */
		if (!has_pos_x || !has_neg_x || !has_pos_y || !has_neg_y) {
			fprintf(stderr, "FAIL: Zoom %.4fx: Incomplete planet render! Missing quadrants (+X:%d, -X:%d, +Y:%d, -Y:%d)\n",
			        zoom, has_pos_x, has_neg_x, has_pos_y, has_neg_y);
			assert(has_pos_x && has_neg_x && has_pos_y && has_neg_y);
		}

		/* Check that rendered span covers at least 80% of visible viewport or full planet span */
		float max_expected_w = (2.0f * hw < 10485760.0f) ? (2.0f * hw) : 10485760.0f;
		float actual_w = max_x - min_x;
		if (actual_w < max_expected_w * 0.80f) {
			fprintf(stderr, "FAIL: Zoom %.5fx: Rendered width %.0f is less than 80%% of expected span %.0f\n",
			        zoom, actual_w, max_expected_w);
			assert(actual_w >= max_expected_w * 0.80f);
		}
	}

	ar_destroy(cs);
	printf("ALL MINIMIZED ZOOM PLANET COVERAGE TESTS PASSED\n");
	return 0;
}
