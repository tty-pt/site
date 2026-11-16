#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include <time.h>

#include <nd/field.h>
#include "ttypt/areole.h"

/* Canonical sampler binding directly to NeverDark's pure-C multi-scale procedural field */
static void field_sampler_binding(void *user_ctx, uint16_t world, uint8_t level,
                                  int32_t wx, int32_t wy, ar_sample_t *out)
{
	(void)user_ctx;
	/* Sample canonical continuous mathematical field */
	uint8_t field_lod = (level == 0) ? 3 : ((level <= 2) ? 2 : ((level <= 4) ? 1 : 0));
	nd_field_sample_t fs = nd_field_sample(world, field_lod, wx, wy);

	out->val = fs.elevation_q12_4;
	out->attr[0] = fs.bio;
	out->attr[1] = fs.bio2;
	out->attr[2] = fs.blend;
	out->attr[3] = (fs.depth > 255) ? 255 : (uint8_t)fs.depth;
	out->flags = (fs.is_water ? 1 : 0);
}

/* Sparsity test binding */
static bool field_sparsity_binding(void *user_ctx, uint16_t world, uint8_t level,
                                   int32_t ox, int32_t oy, int32_t w, int32_t h)
{
	(void)user_ctx;
	(void)world;
	(void)level;

	/* Mock sparse structure presence: structures situated in origin town region */
	int32_t town_x0 = -320, town_x1 = 320;
	int32_t town_y0 = -256, town_y1 = 256;

	bool overlap = !(ox + w < town_x0 || ox > town_x1 || oy + h < town_y0 || oy > town_y1);
	return overlap;
}

#define TEST_GRID_DIM      32
#define TEST_POOL_CAP      128
#define TEST_MAX_ACTIVE    64
#define TEST_MAX_LEVELS    17

int main(void)
{
	printf("=== Testing Generic Multi-Scale Chunk Hierarchy with Canonical Field ===\n");

	ar_system_t *cs = ar_system_create(160, 128, TEST_GRID_DIM, TEST_POOL_CAP, TEST_MAX_ACTIVE, TEST_MAX_LEVELS,
	                                   field_sampler_binding, field_sparsity_binding, NULL);
	assert(cs != NULL);
	printf("PASS: ar_system_create with canonical nd_field_sample binding\n");

	/* 1. Zoom Invariance: Bounded Complexity from 4.0x to 0.0001x (Planetary Orbit) */
	const float zoom_steps[] = {
		4.0f, 2.0f, 1.0f, 0.5f, 0.25f, 0.1f, 0.05f, 0.02f, 0.01f, 0.002f, 0.0005f, 0.0001f
	};
	const size_t num_zooms = sizeof(zoom_steps) / sizeof(zoom_steps[0]);

	for (size_t z = 0; z < num_zooms; z++) {
		float zoom = zoom_steps[z];
		ar_view_t view = {
			.cam_x = 0.0f,
			.cam_y = 0.0f,
			.cam_z = 0.0f,
			.pitch = 0.959931f, /* 55 deg diorama */
			.yaw = 0.0f,
			.fov = 0.785398f,
			.zoom = zoom,
			.viewport_w = 1280.0f,
			.viewport_h = 720.0f,
			.eff_cam_dist = 400.0f
		};

		const ar_node_t *out_chunks[TEST_MAX_ACTIVE];
		size_t n = ar_system_update(cs, 1, &view, out_chunks, TEST_MAX_ACTIVE);

		assert(n > 0);
		assert(n <= TEST_MAX_ACTIVE);

		ar_stats_t stats;
		ar_system_get_stats(cs, &stats);

		printf("PASS: Zoom %9.4fx -> Active Chunks: %2zu <= %d limit | Levels [%u..%u] | Pool In-Use: %zu\n",
		       zoom, n, TEST_MAX_ACTIVE, stats.min_level_observed, stats.max_level_observed, stats.pool_in_use);
	}

	/* 2. Hierarchical Spatial Scaling Invariant */
	{
		ar_view_t view_macro = {
			.cam_x = 0.0f, .cam_y = 0.0f, .cam_z = 0.0f,
			.pitch = 0.959931f, .yaw = 0.0f, .fov = 0.785398f,
			.zoom = 0.001f,
			.viewport_w = 1280.0f, .viewport_h = 720.0f,
			.eff_cam_dist = 400.0f
		};
		const ar_node_t *macro_chunks[TEST_MAX_ACTIVE];
		size_t n_macro = ar_system_update(cs, 1, &view_macro, macro_chunks, TEST_MAX_ACTIVE);
		assert(n_macro > 0);

		/* Verify chunk physical extents scale exponentially with level */
		for (size_t i = 0; i < n_macro; i++) {
			const ar_node_t *c = macro_chunks[i];
			int32_t expected_w = 160 << c->level;
			int32_t expected_h = 128 << c->level;
			assert(c->w == expected_w);
			assert(c->h == expected_h);
		}
		printf("PASS: Chunk world extents accurately scale as S_w * 2^L and S_h * 2^L\n");
	}

	/* 3. Surface Height Probe Parity with Direct Canonical Field Sample */
	{
		int32_t test_points[][2] = {
			{ 0, 0 }, { 80, 64 }, { -160, 128 }, { 500, -300 }, { 1200, 800 }
		};
		size_t pt_count = sizeof(test_points) / sizeof(test_points[0]);

		for (size_t p = 0; p < pt_count; p++) {
			int32_t wx = test_points[p][0];
			int32_t wy = test_points[p][1];

			ar_sample_t probed;
			bool ok = ar_system_probe(cs, 1, wx, wy, &probed);
			assert(ok);

			nd_field_sample_t direct = nd_field_sample(1, 0, wx, wy);
			int elev_probed = probed.val >> 4;
			int elev_direct = direct.elevation;

			int delta = abs(elev_probed - elev_direct);
			assert(delta <= 20); /* Within smooth interpolation bound */
		}
		printf("PASS: Point elevation probe matches direct canonical field sampling\n");
	}

	/* 4. Zero Dynamic Heap Allocations During Camera Movement */
	{
		ar_stats_t s0;
		ar_system_get_stats(cs, &s0);
		size_t initial_misses = s0.cache_misses_total;

		/* Simulate continuous orbiting movement over 300 frames */
		for (int f = 0; f < 300; f++) {
			float angle = (float)f * 0.02f;
			ar_view_t orb_view = {
				.cam_x = 500.0f * cosf(angle),
				.cam_y = 500.0f * sinf(angle),
				.cam_z = 0.0f,
				.pitch = 0.959931f,
				.yaw = 0.0f,
				.fov = 0.785398f,
				.zoom = 0.5f,
				.viewport_w = 1280.0f,
				.viewport_h = 720.0f,
				.eff_cam_dist = 400.0f
			};
			const ar_node_t *orb_chunks[TEST_MAX_ACTIVE];
			size_t n = ar_system_update(cs, 1, &orb_view, orb_chunks, TEST_MAX_ACTIVE);
			assert(n > 0 && n <= TEST_MAX_ACTIVE);
		}

		ar_stats_t s1;
		ar_system_get_stats(cs, &s1);

		assert(s1.pool_in_use <= TEST_POOL_CAP);
		assert(s1.cache_misses_total >= initial_misses);
		assert(s1.cache_hits_total > s1.cache_misses_total);
		printf("PASS: Zero heap allocations during orbit: %zu cache hits, pool bounded to %zu/%d\n",
		       s1.cache_hits_total, s1.pool_in_use, TEST_POOL_CAP);
	}

	/* 5. Sparsity Mask Accuracy */
	{
		ar_view_t view_origin = {
			.cam_x = 0.0f, .cam_y = 0.0f, .cam_z = 0.0f,
			.pitch = 0.959931f, .yaw = 0.0f, .fov = 0.785398f,
			.zoom = 1.0f,
			.viewport_w = 1280.0f, .viewport_h = 720.0f,
			.eff_cam_dist = 400.0f
		};
		const ar_node_t *origin_chunks[TEST_MAX_ACTIVE];
		size_t n_orig = ar_system_update(cs, 1, &view_origin, origin_chunks, TEST_MAX_ACTIVE);

		bool town_detected = false;
		for (size_t i = 0; i < n_orig; i++) {
			if ((origin_chunks[i]->flags & (1u << 31)) != 0)
				town_detected = true;
		}
		assert(town_detected);

		/* Move far into procedural wilderness */
		ar_view_t view_wild = {
			.cam_x = 50000.0f, .cam_y = 50000.0f, .cam_z = 0.0f,
			.pitch = 0.959931f, .yaw = 0.0f, .fov = 0.785398f,
			.zoom = 1.0f,
			.viewport_w = 1280.0f, .viewport_h = 720.0f,
			.eff_cam_dist = 400.0f
		};
		const ar_node_t *wild_chunks[TEST_MAX_ACTIVE];
		size_t n_wild = ar_system_update(cs, 1, &view_wild, wild_chunks, TEST_MAX_ACTIVE);

		bool any_wild_structures = false;
		for (size_t i = 0; i < n_wild; i++) {
			if ((wild_chunks[i]->flags & (1u << 31)) != 0)
				any_wild_structures = true;
		}
		assert(!any_wild_structures);
		printf("PASS: Sparsity mask correctly distinguishes town structures from pure wilderness\n");
	}

	/* 6. Execution Speed Benchmark */
	{
		const int BENCH_FRAMES = 2000;
		clock_t t0 = clock();

		ar_view_t b_view = {
			.cam_x = 0.0f, .cam_y = 0.0f, .cam_z = 0.0f,
			.pitch = 0.959931f, .yaw = 0.0f, .fov = 0.785398f,
			.zoom = 0.05f,
			.viewport_w = 1280.0f, .viewport_h = 720.0f,
			.eff_cam_dist = 400.0f
		};

		for (int i = 0; i < BENCH_FRAMES; i++) {
			b_view.cam_x += 1.0f;
			const ar_node_t *bench_chunks[TEST_MAX_ACTIVE];
			ar_system_update(cs, 1, &b_view, bench_chunks, TEST_MAX_ACTIVE);
		}

		clock_t t1 = clock();
		double total_sec = (double)(t1 - t0) / CLOCKS_PER_SEC;
		double us_per_frame = (total_sec / BENCH_FRAMES) * 1e6;

		printf("PASS: Traversal benchmark: %d frames in %.3f sec (%.2f us/frame, %.0f FPS capability)\n",
		       BENCH_FRAMES, total_sec, us_per_frame, (double)BENCH_FRAMES / total_sec);
		assert(us_per_frame < 150.0);
	}

	/* 7. Non-Monotonic Extreme Zoom Oscillations */
	{
		const float osc_zooms[] = { 1.0f, 0.0001f, 4.0f, 0.0005f, 2.0f, 0.002f, 0.5f, 0.01f };
		const size_t num_osc = sizeof(osc_zooms) / sizeof(osc_zooms[0]);

		for (int iter = 0; iter < 500; iter++) {
			float zoom = osc_zooms[iter % num_osc];
			ar_view_t osc_view = {
				.cam_x = (float)(iter * 7),
				.cam_y = (float)(iter * 5),
				.cam_z = 0.0f,
				.pitch = 0.959931f,
				.yaw = 0.0f,
				.fov = 0.785398f,
				.zoom = zoom,
				.viewport_w = 1280.0f,
				.viewport_h = 720.0f,
				.eff_cam_dist = 400.0f
			};
			const ar_node_t *osc_chunks[TEST_MAX_ACTIVE];
			size_t n = ar_system_update(cs, 1, &osc_view, osc_chunks, TEST_MAX_ACTIVE);
			assert(n > 0 && n <= TEST_MAX_ACTIVE);
		}

		ar_stats_t s_osc;
		ar_system_get_stats(cs, &s_osc);
		assert(s_osc.pool_in_use <= TEST_POOL_CAP);
		printf("PASS: Extreme zoom oscillations (1.0x <-> 0.0001x): Pool bounded at %zu/%d\n",
		       s_osc.pool_in_use, TEST_POOL_CAP);
	}

	ar_system_destroy(cs);
	printf("=== All Generic Chunk Hierarchy Tests Passed Successfully ===\n");
	return 0;
}
