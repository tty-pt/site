#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include "nd/bcp-types.h"
#include "nd_lamina.h"

static void init_sample_map(nd_map_data_t *map)
{
	memset(map, 0, sizeof(*map));
	map->width = 7;
	map->height = 7;
	map->player_x = 3;
	map->player_y = 3;
	map->room_ref = 100;

	for (int y = 0; y < 7; y++) {
		for (int x = 0; x < 7; x++) {
			int idx = y * 7 + x;
			nd_map_tile_t *t = &map->tiles[idx];
			t->bio_idx = (uint8_t)((x + y) % 6);
			t->exits = 0x0F; /* N, S, E, W */
			t->flags = ND_MAP_FLAG_ROOM;
			t->symbol = '.';
			t->fg = 7;
			t->bg = t->bio_idx;
		}
	}
	/* Mark player tile at center (3,3) */
	map->tiles[3 * 7 + 3].flags |= ND_MAP_FLAG_PLAYER;
	map->tiles[3 * 7 + 3].symbol = '!';
	map->tiles[3 * 7 + 3].fg = 14;

	/* Mark an entity at (4,3) */
	map->tiles[3 * 7 + 4].flags |= ND_MAP_FLAG_ENTITY;
	map->tiles[3 * 7 + 4].symbol = 'A';

	/* Mark unexplored tile at (0,0) */
	map->tiles[0].flags = 0;
	map->tiles[0].symbol = ' ';
}

int main(void)
{
	printf("=== Testing NeverDark Pretty Room Map Renderer ===\n");

	nd_map_data_t map;
	init_sample_map(&map);

	uint32_t fb1[160 * 160] = {0};
	uint32_t fb2[160 * 160] = {0};

	/* 1. Basic render succeeds and outputs non-zero pixels */
	nd_lamina_render_map_themed(fb1, 160, 160, &map, "lamina", 43200, 0);
	int non_zero = 0;
	for (int i = 0; i < 160 * 160; i++) {
		if (fb1[i] != 0) non_zero++;
	}
	assert(non_zero > 1000);
	printf("PASS: basic map render yields %d non-zero pixels\n", non_zero);

	/* 2. Determinism: identical input yields byte-identical output */
	nd_lamina_render_map_themed(fb2, 160, 160, &map, "lamina", 43200, 0);
	assert(memcmp(fb1, fb2, sizeof(fb1)) == 0);
	printf("PASS: map render is deterministic\n");

	/* 3. Theme customization: different theme names produce distinct visual output */
	uint32_t fb_classic[160 * 160] = {0};
	nd_lamina_render_map_themed(fb_classic, 160, 160, &map, "classic", 43200, 0);
	assert(memcmp(fb1, fb_classic, sizeof(fb1)) != 0);
	printf("PASS: 'classic' theme produces distinct output from 'lamina'\n");

	/* 4. Unknown theme cleanly falls back to default 'lamina' */
	uint32_t fb_unknown[160 * 160] = {0};
	nd_lamina_render_map_themed(fb_unknown, 160, 160, &map, "non-existent-theme-xyz", 43200, 0);
	assert(memcmp(fb1, fb_unknown, sizeof(fb1)) == 0);
	printf("PASS: unknown theme gracefully falls back to default theme\n");

	/* 5. Biome tint impact: altering tile bio_idx changes rendered pixel colors */
	nd_map_data_t map_desert;
	init_sample_map(&map_desert);
	for (int i = 0; i < 49; i++) {
		map_desert.tiles[i].bio_idx = 9; /* desert/sand */
	}
	uint32_t fb_desert[160 * 160] = {0};
	nd_lamina_render_map_themed(fb_desert, 160, 160, &map_desert, "lamina", 43200, 0);
	assert(memcmp(fb1, fb_desert, sizeof(fb1)) != 0);
	printf("PASS: biome index influences rendered palette\n");

	/* 6. Explored vs unexplored contrast: unexplored tile is darker than explored */
	/* (0,0) is unexplored, (1,1) is explored room */
	uint32_t px_unexplored = fb1[10 * 160 + 10]; /* inside (0,0) tile area */
	uint32_t px_explored = fb1[35 * 160 + 35];   /* inside (1,1) tile area */
	/* Compare luminance (simple R+G+B sum) */
	unsigned lum_unexp = (px_unexplored & 0xFF) + ((px_unexplored >> 8) & 0xFF) + ((px_unexplored >> 16) & 0xFF);
	unsigned lum_exp = (px_explored & 0xFF) + ((px_explored >> 8) & 0xFF) + ((px_explored >> 16) & 0xFF);
	assert(lum_unexp < lum_exp);
	printf("PASS: unexplored tile area is darker than explored tile area (lum %u < %u)\n", lum_unexp, lum_exp);

	/* 7. Player pulsing: different frame values modulate player marker/glow */
	uint32_t fb_pulse[160 * 160] = {0};
	nd_lamina_render_map_themed(fb_pulse, 160, 160, &map, "lamina", 43200, 30);
	assert(memcmp(fb1, fb_pulse, sizeof(fb1)) != 0);
	printf("PASS: animation frame modulates player marker pulse\n");

	/* 8. Backward compatibility wrapper nd_lamina_render_map still works */
	uint32_t fb_compat[160 * 160] = {0};
	nd_lamina_render_map(fb_compat, 160, 160, &map);
	assert(memcmp(fb1, fb_compat, sizeof(fb1)) == 0);
	printf("PASS: backward-compatible nd_lamina_render_map matches default themed render\n");

	printf("ALL PRETTY MAP RENDER TESTS PASSED\n");
	return 0;
}
