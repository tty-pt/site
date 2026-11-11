#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#include <nd/seven-world.h>
#include <nd/field.h>
#include "nd_seven_ground.h"
#include "nd_seven.h"

int main(void)
{
	printf("=== Testing Ground-Level Chunk LOD Responsiveness & Zoom Calibration ===\n");

	nd_seven_ground_cache_forget();
	assert(nd_seven_chunk_count() == 0);

	/* Initialize test map (7x7 room window with player centered at 3, 3) */
	nd_map_data_t map;
	memset(&map, 0, sizeof(map));
	map.width = 7;
	map.height = 7;
	map.player_x = 3;
	map.player_y = 3;
	map.pos[0] = 0;
	map.pos[1] = 0;
	map.pos[2] = 0;
	map.pos[3] = 0; /* world 0 */

	for (int i = 0; i < 49; i++) {
		map.tiles[i].bio_idx = (i % 7 == 0) ? 0 : 5; /* water on left, grassland elsewhere */
		map.tiles[i].exits = 0x1B;
	}

	/* -------------------------------------------------------------------------
	 * Phase 1: 3D Mode Dynamic Zoom LOD Transitions & Buffer Preservation
	 * ------------------------------------------------------------------------- */
	printf("--- Phase 1: 3D Mode Dynamic Zoom Sweep (1.0x -> 0.25x -> 2.0x) ---\n");
	nd_seven_set_3d_mode(1);
	nd_seven_set_zoom(1.0f);

	/* 1. Initial population at spawn (zoom 1.0x) */
	const nd_room_chunk_t *c_center = nd_seven_chunk_ensure(&map, 3, 3);
	assert(c_center != NULL);
	assert(c_center->lod == 2); /* At default 1.0x zoom, rooms evaluate at clean balanced LOD 2 */

	const nd_room_chunk_t *c_edge = nd_seven_chunk_ensure(&map, 0, 0);
	assert(c_edge != NULL);
	assert(c_edge->lod == 2);

	const uint32_t *init_pixels = c_center->pixels;
	const uint32_t *init_substrate = c_center->substrate;
	const int16_t *init_height = c_center->height;
	uint32_t init_rev = c_center->rev;

	/* Populate all 49 rooms at 1.0x */
	for (int r = 0; r < 7; r++) {
		for (int c = 0; c < 7; c++) {
			const nd_room_chunk_t *chk = nd_seven_chunk_ensure(&map, c, r);
			assert(chk != NULL);
			assert(chk->lod == 2); /* All 49 rooms evaluate at clean balanced LOD 2 (2560 tris/room) */
		}
	}
	assert(nd_seven_chunk_count() == 49);
	printf("  [PASS] Initial spawn at 1.0x: 49 rooms populated at clean balanced LOD 2\n");

	/* 2. Zoom out to 0.35x: All rooms maintain at least LOD 2 */
	nd_seven_set_zoom(0.35f);
	for (int r = 0; r < 7; r++) {
		for (int c = 0; c < 7; c++) {
			const nd_room_chunk_t *chk = nd_seven_chunk_ensure(&map, c, r);
			assert(chk != NULL);
			assert(chk->pixels != NULL);
			assert(chk->substrate != NULL);
			assert(chk->height != NULL);
			assert(chk->lod >= 2);
		}
	}

	assert(c_center->lod == 2);
	assert(c_center->pixels != NULL);
	assert(c_center->substrate != NULL);
	assert(c_center->height != NULL);
	printf("  [PASS] Zoom out to 0.35x: Rooms preserve balanced LOD 2\n");

	/* 3. Zoom out to 0.25x: All 49 rooms adapt smoothly to step-4 LOD 2 / step-16 LOD 1 */
	nd_seven_set_zoom(0.25f);
	for (int r = 0; r < 7; r++) {
		for (int c = 0; c < 7; c++) {
			const nd_room_chunk_t *chk = nd_seven_chunk_ensure(&map, c, r);
			assert(chk != NULL);
			assert(chk->pixels != NULL);
			assert(chk->substrate != NULL);
			assert(chk->height != NULL);
			assert(chk->lod >= 1);
		}
	}
	assert(c_center->lod == 2);
	assert(c_center->pixels != NULL);
	assert(c_center->height != NULL);
	printf("  [PASS] Zoom out to 0.25x: Center preserves LOD 2, outer rooms adapt to LOD 1/2\n");

	/* Zoom out to 0.10x: Corner room adapts to LOD 1 (s_apparent = 0.091 < 0.15) */
	nd_seven_set_zoom(0.10f);
	for (int r = 0; r < 7; r++) {
		for (int c = 0; c < 7; c++) {
			const nd_room_chunk_t *chk = nd_seven_chunk_ensure(&map, c, r);
			assert(chk != NULL);
			assert(chk->lod >= 1);
		}
	}
	assert(c_edge->lod == 1);
	assert(c_edge->pixels != NULL);
	assert(c_edge->height != NULL);
	printf("  [PASS] Zoom out to 0.10x: Corner room adapts to LOD 1\n");

	/* Zoom out to 0.05x: Distant corner adapts to LOD 1 */
	nd_seven_set_zoom(0.05f);
	for (int r = 0; r < 7; r++) {
		for (int c = 0; c < 7; c++) {
			const nd_room_chunk_t *chk = nd_seven_chunk_ensure(&map, c, r);
			assert(chk != NULL);
			assert(chk->lod >= 1);
		}
	}
	assert(c_edge->lod == 1);
	assert(c_edge->pixels != NULL);
	assert(c_edge->height != NULL);
	printf("  [PASS] Zoom out to 0.05x: Distant corner room adapts to LOD 1\n");

	/* 4. Zoom in to inspection zoom 2.0x: Focal center refines to high-definition LOD 3 */
	nd_seven_set_zoom(2.0f);
	for (int r = 0; r < 7; r++) {
		for (int c = 0; c < 7; c++) {
			const nd_room_chunk_t *chk = nd_seven_chunk_ensure(&map, c, r);
			assert(chk != NULL);
			assert(chk->pixels != NULL);
			assert(chk->substrate != NULL);
			assert(chk->height != NULL);
			assert(chk->lod >= 2);
		}
	}
	assert(c_edge->lod >= 2);
	assert(c_center->lod == 3); /* Focal center room refines to LOD 3 at inspection zoom */
	assert(c_center->pixels != NULL);
	assert(c_center->substrate != NULL);
	assert(c_center->height != NULL);
	printf("  [PASS] Zoom in to 2.0x: Focal center refines to high-definition LOD 3\n");

	/* 5. Buffer Preservation Invariant: Verify zero heap reallocation */
	assert(c_center->pixels == init_pixels);
	assert(c_center->substrate == init_substrate);
	assert(c_center->height == init_height);
	assert(c_center->rev > init_rev); /* Revision incremented on in-place re-evaluation */
	printf("  [PASS] In-place buffer preservation: heap buffer pointers remained 100%% stable across transitions\n");

	/* -------------------------------------------------------------------------
	 * Phase 2: Water Depth Buffer Zero-Leak Lifecycle
	 * ------------------------------------------------------------------------- */
	printf("--- Phase 2: Water Depth Buffer Lifecycle & Zero-Leak Verification ---\n");
	{
		/* Room (0, 3) was set to biome 0 (water) */
		const nd_room_chunk_t *c_water = nd_seven_chunk_ensure(&map, 0, 3);
		assert(c_water != NULL);
		assert(c_water->has_water == 1);
		assert(c_water->depth != NULL);
		const uint8_t *orig_depth = c_water->depth;

		/* Re-evaluate water chunk at different zoom levels */
		nd_seven_set_zoom(0.5f);
		nd_seven_chunk_ensure(&map, 0, 3);
		assert(c_water->has_water == 1);
		assert(c_water->depth == orig_depth); /* Depth buffer preserved without reallocation */

		nd_seven_set_zoom(1.0f);
		nd_seven_chunk_ensure(&map, 0, 3);
		assert(c_water->depth == orig_depth);
		printf("  [PASS] Water depth buffer reused in-place across zoom transitions (zero leak)\n");
	}

	/* -------------------------------------------------------------------------
	 * Phase 3: 2D Mode Zoom Calibration & LOD 3 Preservation at 0.35x
	 * ------------------------------------------------------------------------- */
	printf("--- Phase 3: 2D Mode Zoom Calibration & LOD 3 Preservation ---\n");
	{
		nd_seven_set_3d_mode(0);

		/* 2D Mode at 2.0x: inspection zoom activates LOD 3 */
		nd_seven_set_zoom(2.0f);
		assert(nd_seven_calc_target_lod(0, 0, 2.0f, 0) == 3);

		/* 2D Mode at 1.0x: default initial zoom evaluates at balanced LOD 2 */
		nd_seven_set_zoom(1.0f);
		assert(nd_seven_calc_target_lod(0, 0, 1.0f, 0) == 2);

		/* 2D Mode at 0.35x: preserves LOD 2 */
		nd_seven_set_zoom(0.35f);
		assert(nd_seven_calc_target_lod(0, 0, 0.35f, 0) == 2);

		/* 2D Mode at 0.03x: adapts to LOD 0 */
		assert(nd_seven_calc_target_lod(0, 0, 0.03f, 0) == 0);

		printf("  [PASS] 2D mode evaluates at LOD 2 at default zoom, LOD 3 at inspection zoom\n");
	}

	/* -------------------------------------------------------------------------
	 * Phase 4: 7-Step Zoom-Out Left-Side & Water Preservation Verification
	 * ------------------------------------------------------------------------- */
	printf("--- Phase 4: 7-Step Zoom-Out Left-Side & Water Preservation ---\n");
	{
		nd_seven_set_3d_mode(1);
		float zoom_7x = 1.0f / powf(1.15f, 7.0f); /* ~0.3759x */
		nd_seven_set_zoom(zoom_7x);

		/* Render 3D frame into NULL fb (simulating WebGL client) */
		nd_client_state_t client_st;
		memset(&client_st, 0, sizeof(client_st));
		client_st.map_data = map;
		nd_seven_render(NULL, 1280, 720, &client_st);

		/* Verify that rooms on the left side (negative ti) exist and are populated */
		for (int ti = -5; ti <= -1; ti++) {
			int16_t left_pos[4] = { (int16_t)ti, 0, 0, 0 };
			const nd_room_chunk_t *chk = nd_seven_chunk_find(left_pos);
			assert(chk != NULL);
			assert(chk->pixels != NULL);
			assert(chk->height != NULL);
			assert(chk->lod >= 1); /* Rooms adapt smoothly according to distance */
		}

		/* Verify water room on the left (col 0, row 3 -> pos[-3, 0, 0, 0]) retains water flags and depth */
		int16_t water_pos[4] = { -3, 0, 0, 0 };
		const nd_room_chunk_t *chk_water = nd_seven_chunk_find(water_pos);
		assert(chk_water != NULL);
		assert(chk_water->has_water == 1);
		assert(chk_water->depth != NULL);

		printf("  [PASS] 7 zoom-outs: Left-side rooms preserved (LOD >= 1) and water active\n");
	}

	/* -------------------------------------------------------------------------
	 * Phase 5: Continuous Zoom Image Rasterization & Visual Continuity (R4)
	 * ------------------------------------------------------------------------- */
	printf("--- Phase 5: Continuous Zoom Image Rasterization & Visual Continuity ---\n");
	{
		const float zoom_steps[] = { 2.0f, 1.0f, 0.5f, 0.25f, 0.10f };
		const size_t num_steps = sizeof(zoom_steps) / sizeof(zoom_steps[0]);

		static uint32_t fb_prev[640 * 480];
		static uint32_t fb_curr[640 * 480];

		for (size_t s = 0; s < num_steps; s++) {
			float z = zoom_steps[s];
			nd_seven_set_3d_mode(0);
			nd_seven_set_zoom(z);

			nd_client_state_t client_st;
			memset(&client_st, 0, sizeof(client_st));
			client_st.map_data = map;

			memset(fb_curr, 0, sizeof(fb_curr));
			nd_seven_render(fb_curr, 640, 480, &client_st);

			/* Validate image rasterization:
			 * 1. Output must not be blank or unrendered placeholder
			 * 2. All 640x480 pixels must be populated with non-zero color
			 * 3. Non-trivial pixel variance indicates authentic terrain features (not solid blank) */
			int non_zero = 0;
			uint32_t first_pixel = fb_curr[0];
			int has_variance = 0;

			for (int p = 0; p < 640 * 480; p++) {
				if (fb_curr[p] != 0) non_zero++;
				if (fb_curr[p] != first_pixel) has_variance = 1;
			}

			assert(non_zero == 640 * 480); /* 100% canvas coverage across all zoom altitudes */
			assert(has_variance);          /* Rich multi-biome terrain detail present */

			if (s > 0) {
				/* Image must change meaningfully as zoom level changes */
				assert(memcmp(fb_prev, fb_curr, sizeof(fb_curr)) != 0);
			}
			memcpy(fb_prev, fb_curr, sizeof(fb_prev));
			printf("  [PASS] Zoom %4.2fx: 100%% canvas coverage (307,200 px), continuous terrain rasterized\n", z);
		}
	}

	nd_seven_ground_cache_forget();
	assert(nd_seven_chunk_count() == 0);

	printf("ALL GROUND-LEVEL ZOOM LOD TESTS PASSED SUCCESSFULLY\n");
	return 0;
}
