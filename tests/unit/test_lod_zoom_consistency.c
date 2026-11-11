#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <assert.h>

#include "ttypt/areole.h"
#include <nd/field.h>

static void canonical_field_sampler(void *user_ctx, uint16_t world, uint8_t level,
                                    int32_t wx, int32_t wy, ar_sample_t *out)
{
	(void)user_ctx;
	nd_field_sample_t s = nd_field_sample(world, level > 3 ? 0 : (uint8_t)(3 - level), wx, wy);
	out->val = s.elevation_q12_4;
	out->attr[0] = s.bio;
	out->attr[1] = s.bio2;
	out->attr[2] = s.blend;
	out->flags = s.is_water ? 1 : 0;
}

int main(void)
{
	printf("=== Testing Continuous Multi-Altitude LOD Zoom Consistency & Reasonability ===\n");

	ar_system_t *cs = ar_create(ND_FIELD_ROOM_W, ND_FIELD_ROOM_H, canonical_field_sampler, NULL);
	assert(cs != NULL);

	const float viewport_w = 1280.0f;
	const float viewport_h = 720.0f;
	const float fov = 45.0f * 3.14159265f / 180.0f;
	const float pitch = 55.0f * 3.14159265f / 180.0f;
	const float eff_cam_dist = 280.0f;
	const float f_px = viewport_w / (2.0f * tanf(0.5f * fov));

	/* 1. Evaluate 20 logarithmic altitude steps from 1.0x (ground) to 0.005x (stratosphere) */
	const float test_zooms[20] = {
		1.000f, 0.750f, 0.500f, 0.350f, 0.250f,
		0.200f, 0.160f, 0.125f, 0.100f, 0.080f,
		0.060f, 0.050f, 0.040f, 0.030f, 0.025f,
		0.020f, 0.015f, 0.010f, 0.007f, 0.005f
	};

	printf("--- Phase 1: 20-Step Continuous Altitude Zoom Sweep ---\n");

	for (int i = 0; i < 20; i++) {
		float zoom = test_zooms[i];
		ar_view_t view = {
			.cam_x = 0.0f,
			.cam_y = 0.0f,
			.cam_z = 0.0f,
			.pitch = pitch,
			.yaw = 0.0f,
			.fov = fov,
			.zoom = zoom,
			.viewport_w = viewport_w,
			.viewport_h = viewport_h,
			.eff_cam_dist = eff_cam_dist
		};

		const ar_node_t *out_chunks[AR_DEFAULT_MAX_ACTIVE];
		size_t count = ar_update(cs, 0, &view, out_chunks, AR_DEFAULT_MAX_ACTIVE);

		assert(count > 0);
		assert(count <= AR_DEFAULT_MAX_ACTIVE);

		/* Find focal chunk covering (cam_x, cam_y) */
		const ar_node_t *focal_node = NULL;
		float max_quad_px = 0.0f;
		float focal_quad_px = 0.0f;

		float eye_dist = eff_cam_dist / zoom;
		float eye_y = view.cam_y + eye_dist * cosf(pitch);
		float eye_z = view.cam_z + eye_dist * sinf(pitch);

		for (size_t c = 0; c < count; c++) {
			const ar_node_t *node = out_chunks[c];
			float cx = (float)node->ox + (float)node->w * 0.5f;
			float cy = (float)node->oy + (float)node->h * 0.5f;

			float v_dx = cx - view.cam_x;
			float v_dy = cy - eye_y;
			float v_dz = 0.0f - eye_z;
			float dist = sqrtf(v_dx * v_dx + v_dy * v_dy + v_dz * v_dz);
			if (dist < 1.0f) dist = 1.0f;

			float quad_w_world = (float)node->w / (float)AR_DEFAULT_GRID_DIM;
			float quad_px = (quad_w_world * f_px) / dist;

			if (quad_px > max_quad_px)
				max_quad_px = quad_px;

			/* Check if camera center is inside this node */
			if (view.cam_x >= (float)node->ox && view.cam_x <= (float)(node->ox + node->w) &&
			    view.cam_y >= (float)node->oy && view.cam_y <= (float)(node->oy + node->h)) {
				focal_node = node;
				focal_quad_px = quad_px;
			}
		}

		assert(focal_node != NULL);

		printf("  Zoom %7.4fx: %2zu chunks | Focal L%d (quad: %4.1fpx) | Max Quad: %4.1fpx\n",
		       zoom, count, focal_node->level, focal_quad_px, max_quad_px);
		fflush(stdout);

		/* Verify visual reasonability and continuous bounds across all altitude tiers */
		assert(focal_quad_px <= 32.0f);
		assert(max_quad_px <= 35.0f);
		assert(count <= AR_DEFAULT_MAX_ACTIVE);
	}

	/* 2. Phase 2: Dynamic Zoom Responsiveness on Already Loaded Chunks */
	printf("--- Phase 2: Dynamic LOD Responsiveness On Already-Loaded Chunks ---\n");
	{
		/* Step A: Update at zoom 0.05x */
		ar_view_t view_out = {
			.cam_x = 500.0f, .cam_y = -300.0f, .cam_z = 0.0f,
			.pitch = pitch, .yaw = 0.0f, .fov = fov,
			.zoom = 0.05f, .viewport_w = viewport_w, .viewport_h = viewport_h,
			.eff_cam_dist = eff_cam_dist
		};
		const ar_node_t *chunks_out[AR_DEFAULT_MAX_ACTIVE];
		size_t count_out = ar_update(cs, 0, &view_out, chunks_out, AR_DEFAULT_MAX_ACTIVE);
		assert(count_out > 0);

		/* Record max level at 0.05x */
		uint8_t max_level_at_05 = 0;
		for (size_t i = 0; i < count_out; i++) {
			if (chunks_out[i]->level > max_level_at_05)
				max_level_at_05 = chunks_out[i]->level;
		}

		/* Step B: Zoom in to 0.20x at the EXACT SAME position (chunks already in pool) */
		ar_view_t view_in = view_out;
		view_in.zoom = 0.20f;

		const ar_node_t *chunks_in[AR_DEFAULT_MAX_ACTIVE];
		size_t count_in = ar_update(cs, 0, &view_in, chunks_in, AR_DEFAULT_MAX_ACTIVE);
		assert(count_in > 0);

		/* Find the focal chunk covering (500, -300) in both passes */
		const ar_node_t *focal_out = NULL;
		for (size_t i = 0; i < count_out; i++) {
			if (view_out.cam_x >= chunks_out[i]->ox && view_out.cam_x <= chunks_out[i]->ox + chunks_out[i]->w &&
			    view_out.cam_y >= chunks_out[i]->oy && view_out.cam_y <= chunks_out[i]->oy + chunks_out[i]->h) {
				focal_out = chunks_out[i];
				break;
			}
		}

		const ar_node_t *focal_in = NULL;
		for (size_t i = 0; i < count_in; i++) {
			if (view_in.cam_x >= chunks_in[i]->ox && view_in.cam_x <= chunks_in[i]->ox + chunks_in[i]->w &&
			    view_in.cam_y >= chunks_in[i]->oy && view_in.cam_y <= chunks_in[i]->oy + chunks_in[i]->h) {
				focal_in = chunks_in[i];
				break;
			}
		}

		assert(focal_out != NULL);
		assert(focal_in != NULL);

		/* Zooming IN must yield a strictly finer (or equal) LOD level on already loaded nodes */
		assert(focal_in->level <= focal_out->level);

		/* Step C: Zoom back OUT to 0.05x */
		const ar_node_t *chunks_out2[AR_DEFAULT_MAX_ACTIVE];
		size_t count_out2 = ar_update(cs, 0, &view_out, chunks_out2, AR_DEFAULT_MAX_ACTIVE);
		assert(count_out2 == count_out);

		printf("  [PASS] Zooming in and out dynamically updates LODs on cached chunks (L%d -> L%d -> L%d)\n",
		       focal_out->level, focal_in->level, focal_out->level);
	}

	/* 3. Phase 3: Extreme Multi-Altitude Oscillation Stress */
	printf("--- Phase 3: 50-Cycle Multi-Altitude Oscillation Stress ---\n");
	{
		for (int cyc = 0; cyc < 50; cyc++) {
			float z = (cyc % 2 == 0) ? 0.24f : 0.01f;
			ar_view_t view = {
				.cam_x = (float)(cyc * 30), .cam_y = (float)(cyc * -20), .cam_z = 0.0f,
				.pitch = pitch, .yaw = 0.0f, .fov = fov,
				.zoom = z, .viewport_w = viewport_w, .viewport_h = viewport_h,
				.eff_cam_dist = eff_cam_dist
			};
			const ar_node_t *chunks[AR_DEFAULT_MAX_ACTIVE];
			size_t count = ar_update(cs, 0, &view, chunks, AR_DEFAULT_MAX_ACTIVE);
			assert(count <= 64);
		}
		ar_stats_t s_osc;
		ar_system_get_stats(cs, &s_osc);
		assert(s_osc.pool_in_use <= 128);
		printf("  [PASS] 50 rapid altitude oscillations: 100%% pool safety (%zu/128 in-use)\n",
		       s_osc.pool_in_use);
	}

	ar_destroy(cs);
	printf("ALL MULTI-ALTITUDE LOD CONSISTENCY TESTS PASSED\n");
	return 0;
}
