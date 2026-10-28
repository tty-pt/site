#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include "nd/bcp-types.h"
#include "nd_lamina.h"
#include "nd_seven.h"
#include <nd/seven-world.h>

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
	/* Trees stand where Seven puts them: somewhere in their third, not at its
	 * centre, so each third's box is widened by the crown radius. */
	for (int c = 0; c < 3; c++)
		assert(count_in(TX(1) + c * CELL - 3, TY(1) - 16, CELL + 6, TILE + 16, C_BRIGHT_GRN) > 0);
	assert(count_in(TX(1) - 3, TY(1) - 16, TILE + 6, TILE + 16, C_BRIGHT_GRN) > 12);
	printf("PASS: left, centre and right thirds each draw a tree in the species colour\n");

	/* 6. The colour comes from the species, not from the glyph. */
	init_sample_map(&m);
	wild(&m, 1, 1, 2, 'x', 3, 0, 0, 0, 0); /* yellow birch-like */
	render(&m, "lamina", 0);
	assert(count_in(TX(1) - 3, TY(1) - 16, CELL + 6, TILE + 16, C_YELLOW) > 4);
	assert(count_in(TX(1) - 3, TY(1) - 16, CELL + 6, TILE + 16, C_BRIGHT_GRN) == 0);
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

	/* 12. Minerals sit where Seven puts them: not at a cell centre, different
	 * per absolute position, identical for the same position. Stones are small
	 * grey dots, rocks bigger and tan or grey. */
	{
		const uint32_t c_stone_lo = 0xFF808080, c_stone_hi = 0xFF8E8E8E;
		uint32_t a[W * H], b[W * H];
		int stone_px, rock_px;
		nd_map_data_t ma;

		init_sample_map(&ma);
		wild(&ma, 1, 1, 2, 0, 0, 0, 0, 0, 0);
		wild(&ma, 4, 1, 2, 0, 0, 0, 0, 0, 0);
		ma.tiles[1 * 7 + 1].symbol = '*';
		ma.tiles[1 * 7 + 4].symbol = '*';
		ma.pos[0] = 10; ma.pos[1] = 20;
		render(&ma, "lamina", 0);
		memcpy(a, fb, sizeof(a));
		render(&ma, "lamina", 0);
		assert(memcmp(a, fb, sizeof(a)) == 0);

		/* same tile glyphs, different absolute position: stones move */
		ma.pos[0] = 11;
		render(&ma, "lamina", 0);
		memcpy(b, fb, sizeof(b));
		assert(memcmp(a, b, sizeof(a)) != 0);

		/* a stone is a small grey dot (1..6 px), nowhere near a glyph "*" */
		memcpy(fb, a, sizeof(fb));
		stone_px = 0;
		for (int dy = -16; dy < TILE; dy++)
			for (int dx = 0; dx < TILE; dx++) {
				uint32_t px = fb[(TY(1) + dy) * W + TX(1) + dx];
				if (px >= c_stone_lo - 0x00182018 && px <= c_stone_hi &&
				    ((px >> 16) & 0xFF) == ((px >> 8) & 0xFF) && ((px >> 8) & 0xFF) == (px & 0xFF))
					stone_px++;
			}
		assert(stone_px >= 1 && stone_px <= 6);

		/* two tiles with the same glyphs at different positions put the dot
		 * at different offsets inside their tiles (not one fixed spot) */
		{
			int off[2] = { -1, -1 };
			for (int t = 0; t < 2; t++)
				for (int dy = -16; dy < TILE && off[t] < 0; dy++)
					for (int dx = 0; dx < TILE; dx++) {
						uint32_t px = a[(TY(1) + dy) * W + (t ? TX(4) : TX(1)) + dx];
						if (px >= c_stone_lo - 0x00182018 && px <= c_stone_hi &&
						    ((px >> 16) & 0xFF) == ((px >> 8) & 0xFF) &&
						    ((px >> 8) & 0xFF) == (px & 0xFF)) {
							off[t] = (dy + 16) * TILE + dx;
							break;
						}
					}
			assert(off[0] >= 0 && off[1] >= 0 && off[0] != off[1]);
		}

		/* a rock covers more pixels than a stone; a boulder more than a rock */
		{
			nd_map_data_t mr;
			int cnt[2];
			for (int kind = 0; kind < 2; kind++) {
				init_sample_map(&mr);
				wild(&mr, 1, 1, 2, 0, 0, kind ? 'O' : 'o', 7, 0, 0);
				mr.pos[0] = 10; mr.pos[1] = 20;
				render(&mr, "lamina", 0);
				cnt[kind] = 0;
				for (int dy = -16; dy < TILE; dy++)
					for (int dx = -4; dx < TILE + 4; dx++) {
						uint32_t px = fb[(TY(1) + dy) * W + TX(1) + dx];
						if (px != C_GREEN_BG && px != 0 && px != 0xFF08090C && (px & 0xFF000000))
							cnt[kind]++;
					}
			}
			rock_px = cnt[0];
			assert(cnt[1] > cnt[0] && rock_px > stone_px);
		}
		printf("PASS: minerals are placed like Seven, stones are small grey dots, rocks bigger\n");
	}

	/* 13. Live player & circular viewport:
	 * With Seven running (live == 1), the map is rounded/circular and dynamically
	 * centered on the player so the player marker core stays exactly at the canvas
	 * center (W/2, H/2), tiles translate oppositely, and pixels outside the circular
	 * radius are masked out. Idle / non-live it sits at the tile centre. */
	{
		uint32_t c0[W * H], c1[W * H], c2[W * H];
		nd_map_data_t ml;
		const int pcx = TX(3) + TILE / 2, pcy = TY(3) + TILE / 2;
		const int center_x = W / 2, center_y = H / 2;

		init_sample_map(&ml);

		/* Non-live mode: rectangular map, player at tile center */
		memset(fb, 0, sizeof(fb));
		nd_lamina_render_map_live(fb, W, H, &ml, "lamina", 43200, 0, 0, 0, 0);
		memcpy(c0, fb, sizeof(c0));
		assert(fb[pcy * W + pcx] == 0xFFFFFFFF);           /* white core at the tile centre */

		/* Live centered mode: lamb at tile center (NDW_CENTER_X, NDW_CENTER_Y).
		 * Player marker core must be pinned at canvas center (center_x, center_y). */
		memset(fb, 0, sizeof(fb));
		nd_lamina_render_map_live(fb, W, H, &ml, "lamina", 43200, 0, 1, NDW_CENTER_X, NDW_CENTER_Y);
		memcpy(c1, fb, sizeof(c1));
		assert(fb[center_y * W + center_x] == 0xFFFFFFFF); /* white core pinned at canvas center */

		/* Corners and points outside the reduced circle radius (R = 112 - 30 - 2 = 80)
		 * are fully transparent (alpha 0) */
		assert(fb[2 * W + 2] == 0x00000000);
		assert(fb[2 * W + (W - 3)] == 0x00000000);
		assert(fb[(H - 3) * W + 2] == 0x00000000);
		assert(fb[(H - 3) * W + (W - 3)] == 0x00000000);
		/* At y = 20, center_x = 112 (distance 92 from center > 80): transparent */
		assert(fb[20 * W + center_x] == 0x00000000);
		/* At y = center_y, x = 20 (distance 92 from center > 80): transparent */
		assert(fb[center_y * W + 20] == 0x00000000);

		/* As lamb moves right (+40), the player core stays at canvas center,
		 * but the rendered tiles translate left, so the pixel buffer changes. */
		memset(fb, 0, sizeof(fb));
		nd_lamina_render_map_live(fb, W, H, &ml, "lamina", 43200, 0, 1, NDW_CENTER_X + 40, NDW_CENTER_Y + 20);
		memcpy(c2, fb, sizeof(c2));
		assert(fb[center_y * W + center_x] == 0xFFFFFFFF); /* still centered */
		assert(memcmp(c1, c2, sizeof(c1)) != 0);           /* tiles shifted */

		/* As lamb moves down (+30), player core stays at canvas center */
		memset(fb, 0, sizeof(fb));
		nd_lamina_render_map_live(fb, W, H, &ml, "lamina", 43200, 0, 1, NDW_CENTER_X, NDW_CENTER_Y + 30);
		assert(fb[center_y * W + center_x] == 0xFFFFFFFF); /* still centered */

		/* Seams: every pixel inside the circle is opaque (no gaps between
		 * rooms), for a sweep of sub-tile lamb positions. */
		for (int off = 0; off < NDW_TILE_W; off += 7) {
			memset(fb, 0, sizeof(fb));
			nd_lamina_render_map_live(fb, W, H, &ml, "lamina", 43200, 0, 1, NDW_CENTER_X + off - 40, NDW_CENTER_Y + off / 2 - 20);
			for (int y = 0; y < H; y++)
				for (int x = 0; x < W; x++) {
					int dx = x - center_x, dy = y - center_y;
					if (dx * dx + dy * dy < 76 * 76)
						assert((fb[y * W + x] >> 24) == 0xFF);
				}
		}

		printf("PASS: live player marker stays centered in circular map and tiles translate smoothly\n");
	}

	/* 13. Organic biome borders: where two wilderness biomes meet, the border is
	 * the shared noise warp (ndw_ground_owner), not the straight tile edge, and
	 * every band pixel shows the biome the owner function gives it. Rooms stay
	 * square. West of tile column 3 is green (ANSI 2), east is yellow (ANSI 3). */
	{
		nd_map_data_t ms;
		uint32_t a[W * H];
		int lo = 9999, hi = -9999, west_yellow = 0, east_green = 0, mismatch = 0, checked = 0;
		const int edge = TX(3);

		init_sample_map(&ms);
		for (int y = 0; y < 7; y++)
			for (int x = 0; x < 7; x++) {
				nd_map_tile_t *t = &ms.tiles[y * 7 + x];
				t->flags = 0;
				t->exits = 0;
				t->bg = (uint8_t)(x >= 3 ? 3 : 2);
			}
		ms.tiles[3 * 7 + 3].flags = ND_MAP_FLAG_PLAYER;
		ms.pos[0] = 20; ms.pos[1] = -5; ms.pos[2] = 2; ms.pos[3] = 1;
		render(&ms, "lamina", 0);
		memcpy(a, fb, sizeof(a));
		render(&ms, "lamina", 0);
		assert(memcmp(a, fb, sizeof(a)) == 0);              /* deterministic */

		for (int ty = 0; ty < 7; ty++) {
			if (ty == 3)
				continue;                                   /* the player's glow sits here */
			for (int py = TY(ty); py < TY(ty) + TILE; py++) {
				int yellow = 0;

				for (int px = edge - 6; px < edge + 6; px++) {
					uint32_t c = fb[py * W + px];
					yellow += (c == C_YELLOW);
				}
				if (yellow < lo) lo = yellow;
				if (yellow > hi) hi = yellow;
				for (int px = edge - 4; px < edge; px++) {
					uint32_t c = fb[py * W + px];
					west_yellow += (c == C_YELLOW);
				}
				for (int px = edge; px < edge + 4; px++) {
					uint32_t c = fb[py * W + px];
					east_green += (c == C_GREEN_BG);
				}
				/* far from the border each biome keeps its own colour */
				assert(fb[py * W + edge - 8] == C_GREEN_BG);
				assert(fb[py * W + edge + 8] == C_YELLOW);
				/* band pixels follow the shared weights and blend function exactly */
				for (int tx_col = 2; tx_col <= 3; tx_col++) {
					int tx = TX(tx_col);
					int16_t pos[4] = { (int16_t)(ms.pos[0] + tx_col - 3), (int16_t)(ms.pos[1] - (ty - 3)),
					                   ms.pos[2], ms.pos[3] };
					const ndw_warp_grid_t *grid = nd_seven_warp_grid(pos);

					for (int px = tx; px < tx + TILE; px++) {
						int qx = ((px - tx) * 2 + 1) * (NDW_TILE_W * NDW_SUB) / (2 * TILE);
						int qy = ((py - TY(ty)) * 2 + 1) * (NDW_TILE_H * NDW_SUB) / (2 * TILE);
						uint16_t wt[9];
						uint32_t want, cols_c[9];
						uint8_t grp[9];
						unsigned mask = 0;

						if (ty == 0) mask |= NDW_OWN_BIT(-1, -1) | NDW_OWN_BIT(0, -1) | NDW_OWN_BIT(1, -1);
						if (ty == 6) mask |= NDW_OWN_BIT(-1, 1) | NDW_OWN_BIT(0, 1) | NDW_OWN_BIT(1, 1);
						for (int dj = -1; dj <= 1; dj++)
							for (int di = -1; di <= 1; di++) {
								int k = (dj + 1) * 3 + (di + 1);

								grp[k] = (tx_col + di >= 3) ? 3 : 2;
								cols_c[k] = (tx_col + di >= 3) ? C_YELLOW : C_GREEN_BG;
							}
						want = (tx_col >= 3) ? C_YELLOW : C_GREEN_BG;
						if (ndw_ground_weights(grid, qx, qy, grp, mask, wt)) {
							int own_k = 4;

							for (int k = 0; k < 9; k++)
								if (wt[k] == 256)
									own_k = k;
							assert(wt[own_k] == 256);          /* crisp: one owner, never a mix */
							want = cols_c[own_k];
						}
						checked++;
						mismatch += (fb[py * W + px] != want);
					}
				}
			}
		}
		assert(checked > 3000);
		assert(mismatch == 0);
		assert(hi - lo >= 2);                               /* wavy, not a straight line */
		assert(west_yellow > 0 && east_green > 0);          /* both reach across the old edge */
		printf("PASS: wilderness biome borders follow the shared noise warp\n");

		/* another level, another border */
		ms.pos[2] = 3;
		render(&ms, "lamina", 0);
		assert(memcmp(a, fb, sizeof(a)) != 0);
		printf("PASS: the minimap border depends on the world position\n");

		/* a room on the yellow side keeps the border square against the green tile */
		ms.pos[2] = 2;
		ms.tiles[1 * 7 + 3].flags = ND_MAP_FLAG_ROOM;
		ms.tiles[1 * 7 + 3].exits = 0x0F;
		render(&ms, "lamina", 0);
		for (int py = TY(1); py < TY(1) + TILE; py++)
			for (int px = edge - 6; px < edge; px++)
				assert(fb[py * W + px] != C_YELLOW);
		printf("PASS: a room's edge stays square on the minimap\n");
	}

	printf("ALL LAMINA MAP RENDER TESTS PASSED\n");
	return 0;
}
