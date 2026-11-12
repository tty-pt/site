#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#include <nd/seven-world.h>
#include <nd/field.h>
#include <ttypt/areole.h>
#include "nd_seven_ground.h"
#include "nd_seven.h"
#include "nd_lamina.h"

int main(void)
{
	printf("=== Testing 3D Seven & 2D Minimap Multi-View LOD Responsiveness & Isolation ===\n");

	nd_seven_ground_cache_forget();
	ar_system_t *cs = nd_seven_get_chunk_sys();
	assert(cs != NULL);

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
	map.tiles[3 * 7 + 3].flags = ND_MAP_FLAG_PLAYER;

	nd_client_state_t client_st;
	memset(&client_st, 0, sizeof(client_st));
	client_st.map_data = map;

	/* -------------------------------------------------------------------------
	 * Phase 1: Pure-3D Dynamic Zoom Sweep (2.0x -> 1.0x -> 0.25x -> 0.01x)
	 * ------------------------------------------------------------------------- */
	printf("--- Phase 1: Pure-3D Dynamic Zoom Sweep (2.0x -> 1.0x -> 0.25x -> 0.01x) ---\n");
	const float zoom_steps[] = { 2.0f, 1.0f, 0.35f, 0.25f, 0.10f, 0.02f, 0.005f };
	for (size_t s = 0; s < sizeof(zoom_steps) / sizeof(zoom_steps[0]); s++) {
		nd_seven_set_zoom(zoom_steps[s]);
		nd_seven_render(1280, 720, &client_st);

		ar_stats_t st_stats;
		ar_system_get_stats(cs, &st_stats);
		assert(st_stats.active_chunks_last_frame > 0);
		assert(st_stats.pool_in_use <= st_stats.pool_capacity);

		ar_sample_t probe0;
		assert(ar_probe_view(cs, 0, 0, 80, 64, &probe0));
		assert(probe0.rgba != 0);
		printf("  [PASS] 3D Zoom %6.4fx -> Active Leaves: %zu | Pool In Use: %zu/%zu\n",
		       zoom_steps[s], st_stats.active_chunks_last_frame,
		       st_stats.pool_in_use, st_stats.pool_capacity);
	}

	/* -------------------------------------------------------------------------
	 * Phase 2: Simultaneous High-Altitude Seven 3D (View 0) + Lamina Minimap (View 1)
	 * ------------------------------------------------------------------------- */
	printf("--- Phase 2: Simultaneous High-Altitude 3D View & 2D Minimap Isolation ---\n");
	{
		nd_seven_set_zoom(0.01f); /* High altitude macro LOD */
		nd_seven_render(1280, 720, &client_st);

		static uint32_t minimap_fb[224 * 224];
		memset(minimap_fb, 0, sizeof(minimap_fb));
		nd_lamina_render_map_live(minimap_fb, 224, 224, &map, "lamina", 43200, 0, 80, 64, 0);

		int non_zero = 0;
		for (int p = 0; p < 224 * 224; p++) {
			if (minimap_fb[p] != 0)
				non_zero++;
		}
		assert(non_zero > 18000); /* Circular live radar mask populated */

		/* Verify both View 0 (Seven 3D) and View 1 (Minimap 2D) probe cleanly */
		ar_sample_t s_v0, s_v1;
		assert(ar_probe_view(cs, 0, 0, 80, 64, &s_v0));
		assert(ar_probe_view(cs, 1, 0, 80, 64, &s_v1));
		assert(s_v0.rgba != 0 && s_v1.rgba != 0);
		printf("  [PASS] High-altitude 3D view (0.01x) and 2D minimap render simultaneously without LOD conflict\n");
	}

	nd_seven_ground_cache_forget();
	ar_stats_t final_stats;
	ar_system_get_stats(cs, &final_stats);
	assert(final_stats.pool_in_use == 0);

	printf("ALL MULTI-VIEW ZOOM LOD TESTS PASSED SUCCESSFULLY\n");
	return 0;
}
