#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include "nd/bcp-types.h"
#include "nd_lamina.h"

/* Frame geometry for a 224x224 buffer and a 7x7 grid (as nd_lamina.c lays it
 * out): 4px pad, tile 30px, three 10px cells per tile, map centred. */
#define W 224
#define H 224
#define TILE 30
#define ORG 7
#define CELL 10
#define TX(x) (ORG + (x) * TILE)
#define TY(y) (ORG + (y) * TILE)

/* The ASCII map's palette (nd.css --c*), 0xAABBGGRR, for the colours the
 * assertions look for. */
#define C_GREEN_BG   0xFF465E3A /* ANSI 2  */
#define C_YELLOW     0xFF5A97A3 /* ANSI 3  */
#define C_BRIGHT_GRN 0xFF8AD353 /* ANSI 10 */
#define C_BRIGHT_YEL 0xFF2ECDB8 /* ANSI 11 */

static uint32_t fb[W * H];

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
			nd_map_tile_t *t = &map->tiles[y * 7 + x];
			t->bio_idx = (uint8_t)((x + y) % 6);
			t->exits = 0x0F; /* N, S, E, W */
			t->flags = ND_MAP_FLAG_ROOM;
			t->bg = 2; /* green biome background */
			t->wall_fg = 3; /* yellow wall paint */
		}
	}
	map->tiles[3 * 7 + 3].flags |= ND_MAP_FLAG_PLAYER;
	map->tiles[3 * 7 + 3].symbol = '!';
	map->tiles[3 * 7 + 3].fg = 11;
}

static void render(const nd_map_data_t *map, const char *theme, uint32_t frame)
{
	memset(fb, 0, sizeof(fb));
	nd_lamina_render_map_themed(fb, W, H, map, theme, 43200, frame);
}

/* Count pixels equal to `col` inside the box. */
static int count_in(int x0, int y0, int w, int h, uint32_t col)
{
	int n = 0;
	for (int y = y0; y < y0 + h; y++)
		for (int x = x0; x < x0 + w; x++)
			if (fb[y * W + x] == col)
				n++;
	return n;
}

/* A map whose tile (gx,gy) is a non-room wilderness tile with the given cells. */
static void wild(nd_map_data_t *m, int gx, int gy, uint8_t bg,
                 uint8_t l, uint8_t fl, uint8_t c, uint8_t fc, uint8_t r, uint8_t fr)
{
	nd_map_tile_t *t = &m->tiles[gy * 7 + gx];
	t->flags = 0;
	t->exits = 0;
	t->bg = bg;
	t->side_l = l; t->fg_l = fl;
	t->symbol = c; t->fg = fc;
	t->side_r = r; t->fg_r = fr;
}

int main(void)
{
	printf("=== Testing NeverDark Lamina Room Map Renderer ===\n");

	nd_map_data_t map;
	init_sample_map(&map);

	/* 1. Basic render yields a lot of pixels */
	render(&map, "lamina", 0);
	uint32_t base[W * H];
	memcpy(base, fb, sizeof(base));
	int non_zero = 0;
	for (int i = 0; i < W * H; i++)
		if (base[i]) non_zero++;
	assert(non_zero > 20000);
	printf("PASS: basic map render yields %d non-zero pixels\n", non_zero);

	/* 2. Determinism */
	render(&map, "lamina", 0);
	assert(memcmp(base, fb, sizeof(base)) == 0);
	printf("PASS: map render is deterministic\n");

	/* 3. Themes differ; unknown theme falls back to the default */
	render(&map, "classic", 0);
	assert(memcmp(base, fb, sizeof(base)) != 0);
	render(&map, "non-existent-theme-xyz", 0);
	assert(memcmp(base, fb, sizeof(base)) == 0);
	printf("PASS: themes are distinct and unknown themes fall back\n");

	/* 4. BIOME BACKGROUND on a wilderness (non-room) tile: it is the biome's
	 * colour, not dark void. This is what the ASCII map shows there. */
	nd_map_data_t m;
	init_sample_map(&m);
	wild(&m, 1, 1, 2, 0, 0, 0, 0, 0, 0);
	render(&m, "lamina", 0);
	assert(fb[(TY(1) + 1) * W + TX(1) + 1] == C_GREEN_BG);
	wild(&m, 1, 1, 4, 0, 0, 0, 0, 0, 0); /* ANSI 4 = blue */
	render(&m, "lamina", 0);
	assert(fb[(TY(1) + 1) * W + TX(1) + 1] == 0xFFBD8973);
	printf("PASS: wilderness tiles show their biome background colour\n");

	/* 5. TREES in their own colour on ALL THREE cells of a wilderness tile. */
	init_sample_map(&m);
	wild(&m, 1, 1, 2, 'T', 10, 'x', 10, 'a', 10);
	render(&m, "lamina", 0);
	for (int c = 0; c < 3; c++)
		assert(count_in(TX(1) + c * CELL, TY(1), CELL, TILE, C_BRIGHT_GRN) > 6);
	printf("PASS: left, centre and right cells each draw a tree in the species colour\n");

	/* 6. The colour comes from the species, not from the glyph. */
	init_sample_map(&m);
	wild(&m, 1, 1, 2, 'x', 3, 0, 0, 0, 0); /* yellow birch-like */
	render(&m, "lamina", 0);
	assert(count_in(TX(1), TY(1), CELL, TILE, C_YELLOW) > 4);
	assert(count_in(TX(1), TY(1), CELL, TILE, C_BRIGHT_GRN) == 0);
	printf("PASS: a yellow plant is drawn yellow, not green\n");

	/* 7. Plant families and growth sizes are visually distinct. */
	uint32_t shots[5][W * H];
	const char fam[5] = { 'x', 'X', 't', 'a', 'i' };
	for (int i = 0; i < 5; i++) {
		init_sample_map(&m);
		wild(&m, 1, 1, 2, 0, 0, fam[i], 10, 0, 0);
		render(&m, "lamina", 0);
		memcpy(shots[i], fb, sizeof(fb));
	}
	for (int i = 0; i < 5; i++)
		for (int j = i + 1; j < 5; j++)
			assert(memcmp(shots[i], shots[j], sizeof(fb)) != 0);
	printf("PASS: x X t a i plants all render differently (family and size)\n");

	/* 8. Non-plant glyphs show in their colour: entity '!' bold yellow,
	 * stairs '<' / '>' on the side cells. */
	init_sample_map(&m);
	wild(&m, 5, 5, 2, '<', 7, '!', 11, '>', 7);
	render(&m, "lamina", 0);
	assert(count_in(TX(5) + CELL, TY(5), CELL, TILE, C_BRIGHT_YEL) > 3);
	assert(count_in(TX(5), TY(5), CELL, TILE, 0xFFA5858B) > 3);
	assert(count_in(TX(5) + 2 * CELL, TY(5), CELL, TILE, 0xFFA5858B) > 3);
	printf("PASS: entity glyph and stair glyphs render in their ASCII colours\n");

	/* 9. Walls use the tile's wall paint colour: a room tile with no north
	 * exit has a wall line on its top edge. */
	init_sample_map(&m);
	m.tiles[1 * 7 + 1].exits = 0x0F & ~2;
	render(&m, "lamina", 0);
	assert(fb[TY(1) * W + TX(1) + TILE / 2] == C_YELLOW);
	m.tiles[1 * 7 + 1].wall_fg = 1; /* red wall paint (desert) */
	render(&m, "lamina", 0);
	assert(fb[TY(1) * W + TX(1) + TILE / 2] == 0xFF6A4ABD);
	printf("PASS: walls are drawn in the tile's wall paint colour\n");

	/* 10. Time of day dims the background but not the plants. */
	init_sample_map(&m);
	wild(&m, 1, 1, 2, 0, 0, 0, 0, 0, 0);
	memset(fb, 0, sizeof(fb));
	nd_lamina_render_map_themed(fb, W, H, &m, "lamina", 100, 0); /* night */
	assert(fb[(TY(1) + 1) * W + TX(1) + 1] != C_GREEN_BG);
	printf("PASS: night dims the biome background\n");

	/* 11. Player pulse and backward-compatible wrapper */
	render(&map, "lamina", 30);
	assert(memcmp(base, fb, sizeof(base)) != 0);
	memset(fb, 0, sizeof(fb));
	nd_lamina_render_map(fb, W, H, &map);
	render(&map, "lamina", 0);
	uint32_t again[W * H];
	memcpy(again, fb, sizeof(again));
	memset(fb, 0, sizeof(fb));
	nd_lamina_render_map(fb, W, H, &map);
	assert(memcmp(again, fb, sizeof(again)) == 0);
	printf("PASS: player pulse animates; compat wrapper matches the default render\n");

	printf("ALL LAMINA MAP RENDER TESTS PASSED\n");
	return 0;
}
