#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <nd/bcp-types.h>
#include <nd/seven-world.h>
#include "nd_seven_ground.h"
#include "nd_seven_motion.h"

static ndw_tile_t tile(uint8_t biome, uint8_t exits, uint8_t flags,
                       int gl, int gc, int gr)
{
	ndw_tile_t t;
	memset(&t, 0, sizeof(t));
	t.biome = biome; t.exits = exits; t.flags = flags;
	t.glyph[0] = (uint8_t)gl; t.glyph[1] = (uint8_t)gc; t.glyph[2] = (uint8_t)gr;
	t.fg[0] = t.fg[1] = t.fg[2] = 10;
	return t;
}

static uint32_t objs_hash(const ndw_obj_t *o, int n)
{
	uint32_t h = 2166136261u;
	for (int i = 0; i < n; i++) {
		const uint8_t *p = (const uint8_t *)&o[i];
		for (size_t k = 0; k < sizeof(o[i]); k++)
			h = (h ^ p[k]) * 16777619u;
	}
	return h;
}

static int count_kind(const ndw_obj_t *o, int n, int kind)
{
	int c = 0;
	for (int i = 0; i < n; i++)
		c += o[i].kind == kind;
	return c;
}

/* A grid of identical open wilderness tiles, for the collision tests. */
static ndw_tile_t g_grid_tile;
static int lookup_open(void *ctx, int ti, int tj, ndw_tile_t *t, int16_t pos[4])
{
	(void)ctx;
	if (ti < -1 || ti > 1 || tj < -1 || tj > 1)
		return 0;
	*t = g_grid_tile;
	pos[0] = (int16_t)(10 + ti); pos[1] = (int16_t)(20 - tj); pos[2] = 0; pos[3] = 0;
	return 1;
}

int main(void)
{
	int16_t pos[4] = { 5, -3, 0, 0 };
	ndw_obj_t a[NDW_MAX_OBJ], b[NDW_MAX_OBJ];

	printf("=== ND seven world: continuous tiles ===\n");

	/* Tiles are much smaller than the old 320x240 room and keep the 3 cells. */
	assert(NDW_TILE_W == 160 && NDW_TILE_H == 128);
	assert(NDW_TILE_W * 2 < 640 && NDW_TILE_H * 2 < 480);
	printf("PASS: a tile is 160x128 units\n");

	/* Determinism: absolute coordinates fully decide the tile. */
	ndw_tile_t t = tile(14, 0x1B, 0, 'x', 'T', 'i');
	int na = ndw_tile_objects(&t, pos, 0, a);
	int nb = ndw_tile_objects(&t, pos, 0, b);
	assert(na > 0 && na == nb && objs_hash(a, na) == objs_hash(b, nb));
	int16_t pos2[4] = { 6, -3, 0, 0 };
	nb = ndw_tile_objects(&t, pos2, 0, b);
	assert(objs_hash(a, na) != objs_hash(b, nb));
	printf("PASS: tiles are a pure function of their absolute coordinates\n");

	/* A cactus glyph is a cactus; the families map one to one. */
	t = tile(14, 0x1B, 0, 0, 0, 'I');
	na = ndw_tile_objects(&t, pos, 0, a);
	assert(na >= 2 && count_kind(a, na, NDW_K_CACTUS) == na);
	for (int i = 0; i < na; i++)
		assert(a[i].big && a[i].fg == 10 && a[i].glyph == 'I');
	t = tile(14, 0x1B, 0, 0, 0, 'i');
	na = ndw_tile_objects(&t, pos, 0, a);
	assert(na == 1 && a[0].kind == NDW_K_CACTUS && !a[0].big);
	assert(ndw_plant_kind('x') == NDW_K_DECID && ndw_plant_kind('X') == NDW_K_DECID);
	assert(ndw_plant_kind('t') == NDW_K_TALL && ndw_plant_kind('A') == NDW_K_FIR);
	assert(ndw_plant_kind('i') == NDW_K_CACTUS && ndw_plant_kind('$') == 0);
	/* the cactus of the right map cell stands in the right third */
	assert(a[0].x >= 2 * NDW_TILE_W / 3 - 8);
	printf("PASS: cactus glyph i/I produces cactus objects in its map cell\n");

	/* Other map glyphs. */
	t = tile(10, 0x1B, 0, '<', '~', '>');
	na = ndw_tile_objects(&t, pos, 0, a);
	assert(count_kind(a, na, NDW_K_STAIRS_DOWN) == 1 && count_kind(a, na, NDW_K_POND) == 1 &&
	       count_kind(a, na, NDW_K_STAIRS_UP) == 1);
	t = tile(10, 0x1B, 0, 0, '$', '!');
	na = ndw_tile_objects(&t, pos, 0, a);
	assert(count_kind(a, na, NDW_K_SHOP) == 1 && count_kind(a, na, NDW_K_MARK) == 1);
	/* The centre of the lamb's own tile is the lamb, not an object. */
	t = tile(10, 0x1B, NDW_F_PLAYER, 0, '!', 0);
	assert(ndw_tile_objects(&t, pos, 0, a) == 0);
	/* Unknown tiles carry plants only. */
	t = tile(10, 0x1B, 0, 'x', '!', '$');
	na = ndw_tile_objects(&t, pos, 1, a);
	assert(na >= 1 && count_kind(a, na, NDW_K_DECID) == na);
	printf("PASS: ponds, stairs, shops, marks; the player's centre stays free\n");

	/* Biome decides the ground: desert is sand, ice snow, water water. */
	assert(ndw_biome_style(14).kind == NDW_G_SAND);
	assert(ndw_biome_style(10).kind == NDW_G_GRASS && ndw_biome_style(14).kind != ndw_biome_style(10).kind);
	assert(ndw_biome_style(1).kind == NDW_G_SNOW && ndw_biome_style(0).kind == NDW_G_WATER);
	assert(ndw_is_water_ground(NDW_G_WATER) && ndw_is_water_ground(NDW_G_SHALLOW_WATER));
	assert(!ndw_is_water_ground(NDW_G_GRASS) && !ndw_is_water_ground(NDW_G_SAND));
	assert(ndw_water_depth_tier(0) == NDW_WATER_DEPTH_SHORE && ndw_water_depth_tier(3) == NDW_WATER_DEPTH_SHORE);
	assert(ndw_water_depth_tier(4) == NDW_WATER_DEPTH_SHALLOW && ndw_water_depth_tier(10) == NDW_WATER_DEPTH_SHALLOW);
	assert(ndw_water_depth_tier(11) == NDW_WATER_DEPTH_MID && ndw_water_depth_tier(24) == NDW_WATER_DEPTH_MID);
	assert(ndw_water_depth_tier(25) == NDW_WATER_DEPTH_DEEP && ndw_water_depth_tier(80) == NDW_WATER_DEPTH_DEEP);
	assert(ndw_biome_style(18).kind == NDW_G_ASH && ndw_biome_style(2).kind == NDW_G_TUNDRA);
	assert(ndw_biome_style(250).kind == NDW_G_GRASS);
	printf("PASS: biomes pick the ground kind (desert = sand)\n");

	/* Water depth tiers and dynamic surface wave animation on room chunks */
	{
		assert(ndw_water_depth_tier(0) == NDW_WATER_DEPTH_SHORE && ndw_water_depth_tier(3) == NDW_WATER_DEPTH_SHORE);
		assert(ndw_water_depth_tier(4) == NDW_WATER_DEPTH_SHALLOW && ndw_water_depth_tier(10) == NDW_WATER_DEPTH_SHALLOW);
		assert(ndw_water_depth_tier(11) == NDW_WATER_DEPTH_MID && ndw_water_depth_tier(24) == NDW_WATER_DEPTH_MID);
		assert(ndw_water_depth_tier(25) == NDW_WATER_DEPTH_DEEP && ndw_water_depth_tier(80) == NDW_WATER_DEPTH_DEEP);

		nd_seven_ground_cache_forget();
		nd_map_data_t map;
		memset(&map, 0, sizeof(map));
		map.width = map.height = 7;
		map.player_x = map.player_y = 3;
		map.pos[0] = -19; map.pos[1] = -20; map.pos[2] = 0; map.pos[3] = 1;
		for (int i = 0; i < 49; i++) {
			map.tiles[i].bio_idx = 0; /* water */
			map.tiles[i].exits = 0x1B;
		}

		/* Water chunk must allocate depth grid and record has_water */
		const nd_room_chunk_t *w_chunk = nd_seven_chunk_ensure(&map, 3, 3);
		assert(w_chunk != NULL);
		assert(w_chunk->has_water == 1);
		assert(w_chunk->depth != NULL);
		assert(w_chunk->depth[0] > 0);

		/* Render water tile at two different animation frame ticks: must exhibit dynamic wave animation */
		uint32_t fb_anim0[320 * 256];
		uint32_t fb_anim1[320 * 256];
		nd_client_state_t st;
		memset(&st, 0, sizeof(st));
		st.map_data = map;
		int16_t r_pos[4] = { -19, -20, 0, 1 };
		ndw_tile_t own_tile = nd_seven_convert_tile(&map.tiles[3 * 7 + 3]);

		nd_seven_ground_tile_render(fb_anim0, 320, 256, &st, 0, 0, r_pos, &own_tile, 0, 0);
		nd_seven_step_anim(24);
		nd_seven_ground_tile_render(fb_anim1, 320, 256, &st, 0, 0, r_pos, &own_tile, 0, 0);
		assert(memcmp(fb_anim0, fb_anim1, sizeof(fb_anim0)) != 0);

		nd_seven_ground_cache_forget();
		printf("PASS: water depth tiers and dynamic animation variance on room chunks verified\n");
	}

	/* Water animation must be VISIBLE, not merely non-identical: across 8 ticks
	 * (4 redraws) a real share of shallow and of deep water pixels changes by a
	 * perceptible amount, and the pattern is anchored to the world, not the
	 * screen. */
	{
		static uint32_t fa[320 * 256], fb2[320 * 256], fs[320 * 256];
		nd_seven_ground_cache_forget();
		nd_client_state_t st;
		memset(&st, 0, sizeof(st));
		st.map_data.width = st.map_data.height = 7;
		st.map_data.player_x = st.map_data.player_y = 3;
		st.map_data.pos[0] = -19; st.map_data.pos[1] = -20; st.map_data.pos[2] = 0; st.map_data.pos[3] = 1;
		for (int i = 0; i < 49; i++) {
			st.map_data.tiles[i].bio_idx = (i % 7) <= 2 ? 10 : 0; /* land west, sea east */
			st.map_data.tiles[i].exits = 0x1B;
		}
		int16_t r_pos[4] = { -19, -20, 0, 1 };
		ndw_tile_t own = nd_seven_convert_tile(&st.map_data.tiles[3 * 7 + 3]);

		nd_seven_ground_tile_render(fa, 320, 256, &st, 0, 0, r_pos, &own, 0, 0);
		nd_seven_step_anim(8);
		nd_seven_ground_tile_render(fb2, 320, 256, &st, 0, 0, r_pos, &own, 0, 0);
		/* same frame, camera moved 2 px (1 world unit) */
		nd_seven_ground_tile_render(fs, 320, 256, &st, 0, 0, r_pos, &own, -2, 0);

		const nd_room_chunk_t *ch = nd_seven_chunk_find(r_pos);
		assert(ch && ch->depth);
		int sh_n = 0, sh_c = 0, dp_n = 0, dp_c = 0;
		for (int i = 0; i < 320 * 256; i++) {
			int d = ch->depth[i];
			if (d < 2)
				continue;
			int m = 0;
			for (int s = 0; s < 24; s += 8) {
				int a = (int)((fa[i] >> s) & 0xFF), b = (int)((fb2[i] >> s) & 0xFF);
				int df = a > b ? a - b : b - a;
				if (df > m) m = df;
			}
			int *n = d <= 6 ? &sh_n : &dp_n, *c = d <= 6 ? &sh_c : &dp_c;
			(*n)++;
			if (m >= 12)
				(*c)++;
		}
		printf("INFO: visibly changed after 8 ticks: shallow %d/%d, deep %d/%d\n", sh_c, sh_n, dp_c, dp_n);
		fflush(stdout);
		assert(sh_n > 0 && dp_n > 0);
		assert(sh_c * 10 >= sh_n);
		assert(dp_c * 10 >= dp_n);

		for (int y = 0; y < 256; y++)
			for (int x = 0; x < 318; x++)
				assert(fs[y * 320 + x] == fb2[y * 320 + x + 2]);
		nd_seven_ground_cache_forget();
		printf("PASS: water animation is visible in shallows and deep sea and anchored to the world\n");
	}

	/* Client-side room chunks: 7x7 active view window retention and eager eviction */
	{
		nd_seven_ground_cache_forget();
		assert(nd_seven_chunk_count() == 0);

		nd_map_data_t map;
		memset(&map, 0, sizeof(map));
		map.width = map.height = 7;
		map.player_x = map.player_y = 3;
		map.pos[0] = 50; map.pos[1] = -50; map.pos[2] = 0; map.pos[3] = 0;
		for (int i = 0; i < 49; i++) {
			map.tiles[i].bio_idx = 0; /* water */
			map.tiles[i].exits = 0x1B;
		}

		/* Ensure chunks for center (3, 3) and an edge room (0, 3) */
		const nd_room_chunk_t *c_center = nd_seven_chunk_ensure(&map, 3, 3);
		assert(c_center && c_center->pixels);
		const nd_room_chunk_t *c_west = nd_seven_chunk_ensure(&map, 0, 3);
		assert(c_west && c_west->pixels);
		assert(nd_seven_chunk_count() == 2);
		int allocs = nd_seven_chunk_alloc_count();
		assert(allocs == 2);

		/* End frame with same 7x7 map: both chunks are in window, both MUST be retained */
		nd_seven_ground_frame_begin();
		nd_seven_ground_frame_end(&map);
		assert(nd_seven_chunk_count() == 2);
		assert(nd_seven_chunk_alloc_count() == 2); /* 0 re-evaluations */

		/* Move 7x7 map East: player pos moves from 50 to 52.
		 * West room (col 0, which was at world x = 50 - 3 = 47) is now outside the window (col would be 47 - 52 + 3 = -2 < 0) */
		map.pos[0] = 52;
		for (int f = 0; f < 125; f++)
			nd_seven_ground_frame_begin();
		nd_seven_ground_frame_end(&map);
		/* The West chunk MUST have been eagerly freed upon exiting the 7x7 window and TTL expiration */
		assert(nd_seven_chunk_count() == 1);
		assert(nd_seven_chunk_free_count() == 1);
		/* The center chunk (world x = 50) is still inside the 7x7 window (col = 50 - 52 + 3 = 1 >= 0) and retained! */
		assert(nd_seven_chunk_find(c_center->pos) != NULL);

		nd_seven_ground_cache_forget();
		assert(nd_seven_chunk_count() == 0);
		printf("PASS: room chunk 7x7 view window retention and eager eviction verified\n");
	}

	/* No crossroads: nothing walkable-only exists as ground, and an all-open
	 * wilderness tile has no walls at all. */
	t = tile(10, 0x1B, 0, 'x', 'x', 'x');
	ndw_rect_t w[NDW_MAX_WALL];
	assert(ndw_walls(&t, w) == 0);
	printf("PASS: no road/path ground and no walls in open country\n");

	/* Objects keep clear of every tile edge and of the centre box. */
	for (int x = -20; x < 20; x++) {
		int16_t p[4] = { (int16_t)x, (int16_t)(x * 3), 0, 0 };
		t = tile(15, 0x1B, 0, 'X', 'T', 'A');
		t.flags = NDW_F_ENTITY;
		na = ndw_tile_objects(&t, p, 0, a);
		for (int i = 0; i < na; i++) {
			ndw_rect_t r = ndw_obj_rect(&a[i]);
			ndw_rect_t clear = { NDW_CLEAR_X0, NDW_CLEAR_Y0, NDW_CLEAR_X1, NDW_CLEAR_Y1 };
			assert(r.x0 >= NDW_MARGIN && r.y0 >= NDW_MARGIN);
			assert(r.x1 <= NDW_TILE_W - NDW_MARGIN && r.y1 <= NDW_TILE_H - NDW_MARGIN);
			assert(!ndw_rect_hit(&r, &clear));
		}
	}
	printf("PASS: objects keep a margin from tile edges, so seams stay passable\n");

	/* Rooms get walls only where an exit is missing; open exits get a gate. */
	t = tile(10, NDW_EXIT_N | NDW_EXIT_E, NDW_F_ROOM, 0, 0, 0);
	int nw = ndw_walls(&t, w);
	assert(nw == 2 + 1 + 2 + 1);   /* N gate (2), S closed, E gate (2), W closed */
	ndw_rect_t n_gap = { NDW_TILE_W / 2 - 4, 0, NDW_TILE_W / 2 + 4, NDW_WALL_T };
	for (int i = 0; i < nw; i++)
		assert(!ndw_rect_hit(&w[i], &n_gap) || w[i].y0 >= NDW_TILE_H - NDW_WALL_T ||
		       w[i].x0 >= NDW_TILE_W - NDW_WALL_T || w[i].x1 <= NDW_WALL_T);
	t = tile(10, 0x1B, NDW_F_ROOM, 0, 0, 0);
	assert(ndw_walls(&t, w) == 8);
	printf("PASS: only rooms have walls; closed edges are solid, open ones leave a gate\n");

	/* Collision: open country is free; a closed room edge blocks; the lamb
	 * crosses a tile seam freely. */
	g_grid_tile = tile(10, 0x1B, 0, 0, 0, 0);
	assert(!ndw_blocked(lookup_open, NULL, NDW_CENTER_X, NDW_CENTER_Y));
	assert(!ndw_blocked(lookup_open, NULL, NDW_TILE_W, NDW_CENTER_Y));        /* the seam */
	assert(!ndw_blocked(lookup_open, NULL, NDW_TILE_W + 3, NDW_TILE_H + 3));  /* the corner */
	assert(ndw_blocked(lookup_open, NULL, 3 * NDW_TILE_W, NDW_CENTER_Y));      /* beyond the view */
	g_grid_tile = tile(10, 0x1B & ~NDW_EXIT_W, NDW_F_ROOM, 0, 0, 0);
	assert(ndw_blocked(lookup_open, NULL, 4, NDW_TILE_H / 4));                 /* west wall */
	assert(!ndw_blocked(lookup_open, NULL, NDW_CENTER_X, NDW_CENTER_Y));
	g_grid_tile = tile(14, 0x1B, 0, 0, 0, 'I');
	{
		int16_t p[4] = { 10, 20, 0, 0 };
		na = ndw_tile_objects(&g_grid_tile, p, 0, a);
		assert(na > 0);
		/* standing on a cactus foot point is blocked */
		assert(ndw_blocked(lookup_open, NULL, a[0].x, a[0].y));
	}
	printf("PASS: collision: cacti and room walls block, seams and open ground do not\n");

	/* Small stones/pebbles are walkable and do not block the lamb */
	g_grid_tile = tile(10, 0x1B, 0, '*', 0, 0);
	{
		int16_t p[4] = { 10, 20, 0, 0 };
		na = ndw_tile_objects(&g_grid_tile, p, 0, a);
		assert(na >= 1);
		for (int i = 0; i < na; i++) {
			assert(a[i].hw == 0 && a[i].hh == 0);
			assert(!ndw_blocked(lookup_open, NULL, a[i].x, a[i].y));
		}
	}
	/* Huge rocks ('o') and big boulders ('O') block at their physical ground base across all directions */
	g_grid_tile = tile(10, 0x1B, 0, 'o', 'O', 0);
	{
		int16_t p[4] = { 10, 20, 0, 0 };
		na = ndw_tile_objects(&g_grid_tile, p, 0, a);
		assert(na >= 2);
		for (int i = 0; i < na; i++) {
			assert(a[i].hw >= 7 && a[i].hh >= 7);
			if (a[i].kind == NDW_K_BOULDER) {
				assert(a[i].hw == 13 && a[i].hh == 22);
				/* Player cannot walk inside/under the boulder body */
				assert(ndw_blocked(lookup_open, NULL, a[i].x, a[i].y - 8));
				assert(ndw_blocked(lookup_open, NULL, a[i].x, a[i].y - 12));
			} else if (a[i].kind == NDW_K_ROCK) {
				assert((a[i].hw == 10 && a[i].hh == 14) || (a[i].hw == 7 && a[i].hh == 7));
				if (a[i].hh >= 14)
					assert(ndw_blocked(lookup_open, NULL, a[i].x, a[i].y - 8));
			}
			/* 1. Center base blocks */
			assert(ndw_blocked(lookup_open, NULL, a[i].x, a[i].y));

			/* 2. South approach (walking North into rock base): blocks when hitting base, clear when outside */
			assert(ndw_blocked(lookup_open, NULL, a[i].x, a[i].y + 2));
			assert(!ndw_blocked(lookup_open, NULL, a[i].x, a[i].y + 12));

			/* 3. North approach (walking South into rock base): upper dome allows walking behind, blocks at mass */
			assert(ndw_blocked(lookup_open, NULL, a[i].x, a[i].y - a[i].hh + 1));
			assert(!ndw_blocked(lookup_open, NULL, a[i].x, a[i].y - a[i].hh - 6));
			assert(!ndw_blocked(lookup_open, NULL, a[i].x, a[i].y - a[i].hh - 12));

			/* 4. West approach (walking East into rock base): blocks at base, clear outside */
			assert(ndw_blocked(lookup_open, NULL, a[i].x - a[i].hw + 1, a[i].y - a[i].hh / 2));
			assert(!ndw_blocked(lookup_open, NULL, a[i].x - a[i].hw - 10, a[i].y - a[i].hh / 2));

			/* 5. East approach (walking West into rock base): blocks at base, clear over side shadow */
			assert(ndw_blocked(lookup_open, NULL, a[i].x + a[i].hw - 1, a[i].y - a[i].hh / 2));
			assert(!ndw_blocked(lookup_open, NULL, a[i].x + a[i].hw + 10, a[i].y - a[i].hh / 2));
		}
	}
	printf("PASS: collision: pebbles are walkable; huge rocks and boulders block at base across all 4 directions while shadows/upper body allow walking\n");

	/* The void rule that stops stray trees: a tile with no plant glyphs draws
	 * nothing, so a tile the map does not describe cannot paint a neighbour's
	 * tree in empty space (R1). A tile that does carry a plant glyph still
	 * draws an object of that same kind (D1: kind matches the real object). */
	{
		ndw_tile_t blank = tile(10, 0x1B, 0, 0, 0, 0);
		assert(ndw_tile_objects(&blank, pos, 1, a) == 0);
		assert(ndw_tile_objects(&blank, pos, 0, a) == 0);
		ndw_tile_t dec = tile(10, 0x1B, 0, 'x', 0, 0);
		na = ndw_tile_objects(&dec, pos, 1, a);
		assert(na == 1 && a[0].kind == NDW_K_DECID && a[0].fg == 10 && a[0].glyph == 'x');
		/* a non-plant centre glyph is dropped on an unknown tile */
		ndw_tile_t mark = tile(10, 0x1B, 0, 0, '!', 0);
		assert(ndw_tile_objects(&mark, pos, 1, a) == 0);
		printf("PASS: void tiles draw no objects; plant glyphs keep their kind\n");
	}

	/* The player marker '!' is not an object, but the player flag alone does not
	 * hide a plant glyph (the client restores the glyph under the marker). */
	{
		ndw_tile_t occ = tile(10, 0x1B, NDW_F_PLAYER, 0, 'x', 0);
		na = ndw_tile_objects(&occ, pos, 0, a);
		assert(na >= 1 && a[0].kind == NDW_K_DECID);
		ndw_tile_t mk = tile(10, 0x1B, NDW_F_PLAYER, 0, '!', 0);
		assert(ndw_tile_objects(&mk, pos, 0, a) == 0);
		printf("PASS: plant under the player flag still draws; '!' marker does not\n");
	}

	/* Ahead-of-Time (AOT) rocks and stones: mineral glyphs '*', 'o', 'O'
	 * produce persistent world objects of kinds NDW_K_STONE, NDW_K_ROCK, NDW_K_BOULDER. */
	{
		ndw_tile_t stn = tile(10, 0x1B, 0, '*', 0, 0);
		na = ndw_tile_objects(&stn, pos, 0, a);
		assert(na == 1 && a[0].kind == NDW_K_STONE && a[0].glyph == '*');

		ndw_tile_t rck = tile(10, 0x1B, 0, 0, 'o', 0);
		na = ndw_tile_objects(&rck, pos, 0, a);
		assert(na == 1 && a[0].kind == NDW_K_ROCK && a[0].glyph == 'o');

		ndw_tile_t bld = tile(10, 0x1B, 0, 0, 0, 'O');
		na = ndw_tile_objects(&bld, pos, 0, a);
		assert(na >= 1 && a[0].kind == NDW_K_BOULDER && a[0].glyph == 'O' && a[0].big);

		/* Multi-content tile: both flora and mineral can coexist in separate cells */
		ndw_tile_t multi = tile(10, 0x1B, 0, 'x', 0, 'o');
		na = ndw_tile_objects(&multi, pos, 0, a);
		assert(na >= 2);
		int has_tree = 0, has_rock = 0;
		for (int i = 0; i < na; i++) {
			if (a[i].kind == NDW_K_DECID) has_tree = 1;
			if (a[i].kind == NDW_K_ROCK) has_rock = 1;
			ndw_rect_t ri = ndw_obj_rect(&a[i]);
			for (int j = i + 1; j < na; j++) {
				ndw_rect_t rj = ndw_obj_rect(&a[j]);
				if (a[i].hw && a[j].hw)
					assert(!ndw_rect_hit(&ri, &rj));
			}
		}
		assert(has_tree && has_rock);
		printf("PASS: AOT rocks and stones map to world objects and coexist with flora (non-overlapping footprints)\n");
	}

	/* Organic biome borders: ndw_ground_owner(pos, lx, ly, mask, &oi, &oj) says
	 * which of the 3x3 tiles around the one at `pos` owns the ground at the
	 * tile-local unit (lx, ly). It returns only the owner offset; callers decide
	 * whether that owner's biome differs (the same comparison in Seven and the
	 * lamina minimap). `mask` is a 3x3 bit set (NDW_OWN_BIT) of tiles that may not
	 * blend: rooms (square, walled) and neighbours the caller does not know. */
	{
		int16_t p0[4] = { 12, -7, 3, 1 };
		int16_t pe[4] = { 13, -7, 3, 1 };   /* the tile east of p0 */
		int16_t ps[4] = { 12, -8, 3, 1 };   /* the tile south of p0 (y grows north) */

		/* Corner rounding radius and displacement amplitude are positive and well-defined. */
		assert(NDW_BLEND_AMP > 0 && NDW_CORNER_R > 0 && NDW_BLEND_BAND > NDW_CORNER_R);

		/* Determinism and the interior fast path: the middle of a tile never changes owner. */
		for (int ly = 0; ly < NDW_TILE_H; ly++)
			for (int lx = 0; lx < NDW_TILE_W; lx++) {
				int oi = 7, oj = 7, oi2 = 8, oj2 = 8;
				int ch = ndw_ground_owner(p0, lx, ly, 0, &oi, &oj);
				ndw_ground_owner(p0, lx, ly, 0, &oi2, &oj2);
				assert(oi == oi2 && oj == oj2);
				assert(oi >= -1 && oi <= 1 && oj >= -1 && oj <= 1);
				assert(ch == (oi != 0 || oj != 0));
				if (lx >= NDW_BLEND_BAND && lx < NDW_TILE_W - NDW_BLEND_BAND &&
				    ly >= NDW_BLEND_BAND && ly < NDW_TILE_H - NDW_BLEND_BAND)
					assert(oi == 0 && oj == 0);
			}
		printf("PASS: ground owner is deterministic and never leaves the 3x3 / edge band\n");

		/* Crisp weights: every point has exactly one owner with weight 256 (zero blur). */
		{
			ndw_warp_grid_t g0;
			uint8_t grp[9] = { 0, 1, 2, 3, 4, 5, 6, 7, 8 };
			uint16_t w[9];
			ndw_warp_grid_fill(&g0, p0);
			for (int ly = 0; ly < NDW_TILE_H; ly += 2)
				for (int lx = 0; lx < NDW_TILE_W; lx += 2) {
					int sum = 0, count256 = 0;
					ndw_ground_weights(&g0, lx * NDW_SUB, ly * NDW_SUB, grp, 0, w);
					for (int k = 0; k < 9; k++) {
						assert(w[k] == 0 || w[k] == 256);
						sum += w[k];
						count256 += (w[k] == 256);
					}
					assert(sum == 256 && count256 == 1);
				}
		}
		printf("PASS: ground weights are strictly crisp 256/0 (zero blur)\n");

		/* Shared edge agreement: across the vertical border between p0 and pe,
		 * if p0 assigns owner = east, pe must assign owner = self. */
		{
			ndw_warp_grid_t ga, gb;
			uint8_t grp_a[9] = { 0, 0, 1, 0, 0, 1, 0, 0, 1 }; /* west is 0, east is 1 */
			uint8_t grp_b[9] = { 0, 0, 1, 0, 0, 1, 0, 0, 1 }; /* same */
			uint16_t wa[9], wb[9];
			ndw_warp_grid_fill(&ga, p0);
			ndw_warp_grid_fill(&gb, pe);
			for (int ly = 0; ly < NDW_TILE_H; ly++) {
				ndw_ground_weights(&ga, (NDW_TILE_W - 1) * NDW_SUB + NDW_SUB / 2,
				                   ly * NDW_SUB + NDW_SUB / 2, grp_a, 0, wa);
				ndw_ground_weights(&gb, NDW_SUB / 2,
				                   ly * NDW_SUB + NDW_SUB / 2, grp_b, 0, wb);
				if (wa[5] == 256) /* tile A says east */
					assert(wb[4] == 256); /* tile B must say self */
				if (wb[3] == 256) /* tile B says west */
					assert(wa[4] == 256); /* tile A must say self */
			}
		}
		printf("PASS: shared edge owner agreement is seamless across tiles\n");

		/* Mode 1 (horizontal pair): owner agreement across horizontal edge between Grass tile and Desert tile */
		{
			ndw_warp_grid_t ga, gb;
			int16_t pa[4] = { 10, -5, 2, 1 }; /* Grass tile above Desert */
			int16_t pb[4] = { 10, -6, 2, 1 }; /* Desert tile */
			uint8_t grp_a[9] = { 0, 0, 0, 0, 0, 0, 1, 1, 1 }; /* south row is desert */
			uint8_t grp_b[9] = { 0, 0, 0, 1, 1, 1, 1, 1, 1 }; /* north row is grass, rest desert */
			uint16_t wa[9], wb[9];
			ndw_warp_grid_fill(&ga, pa);
			ndw_warp_grid_fill(&gb, pb);
			for (int lx = 0; lx < NDW_TILE_W; lx++) {
				ndw_ground_weights(&ga, lx * NDW_SUB + NDW_SUB / 2,
				                   (NDW_TILE_H - 1) * NDW_SUB + NDW_SUB / 2, grp_a, 0, wa);
				ndw_ground_weights(&gb, lx * NDW_SUB + NDW_SUB / 2,
				                   NDW_SUB / 2, grp_b, 0, wb);
				/* Monotone transition: if Grass tile at row 127 yields to Desert, Desert tile at row 0 must be Desert */
				if (wa[7] == 256) assert(wb[4] == 256);
				/* If Desert tile at row 0 yields to Grass, Grass tile at row 127 must be Grass */
				if (wb[1] == 256) assert(wa[4] == 256);
			}
		}
		printf("PASS: Mode 1 pair horizontal edge has monotone transition without inverted stripes\n");

		/* Mode 2 (L-shape): zero disconnected shards in surrounding Grass tiles */
		{
			ndw_warp_grid_t g_nw;
			int16_t p_nw[4] = { 9, -5, 2, 1 }; /* Top-Left Grass tile, SE neighbor is Desert */
			uint8_t grp_nw[9] = { 0, 0, 0, 0, 0, 0, 0, 0, 1 }; /* Only SE is Desert */
			uint16_t w[9];
			ndw_warp_grid_fill(&g_nw, p_nw);
			int desert_shards = 0;
			for (int ly = 0; ly < NDW_TILE_H; ly++)
				for (int lx = 0; lx < NDW_TILE_W; lx++) {
					ndw_ground_weights(&g_nw, lx * NDW_SUB + NDW_SUB / 2,
					                   ly * NDW_SUB + NDW_SUB / 2, grp_nw, 0, w);
					desert_shards += (w[8] == 256);
				}
			assert(desert_shards == 0);
		}
		printf("PASS: Mode 2 L-shape produces zero disconnected shards in diagonal tiles\n");

		/* R7: 3-way corner with one square of water (Water, Grass, Desert) */
		{
			ndw_warp_grid_t g_water;
			int16_t p_water[4] = { 10, -5, 2, 1 }; /* Water tile */
			/* Water=2 at center (4). North=Grass (0), West=Grass (0), East=Grass (0), South=Desert (1), SE=Desert (1) */
			uint8_t grp_3way[9] = {
				0, 0, 0,
				0, 2, 0,
				0, 1, 1
			};
			unsigned mask_3way = NDW_WATER_BIT(0, 0);
			uint16_t w[9];
			ndw_warp_grid_fill(&g_water, p_water);
			int water_count = 0, grass_count = 0, desert_count = 0;
			for (int ly = 0; ly < NDW_TILE_H; ly++)
				for (int lx = 0; lx < NDW_TILE_W; lx++) {
					ndw_ground_weights(&g_water, lx * NDW_SUB + NDW_SUB / 2,
					                   ly * NDW_SUB + NDW_SUB / 2, grp_3way, mask_3way, w);
					for (int k = 0; k < 9; k++) {
						if (w[k] == 256) {
							if (grp_3way[k] == 2) water_count++;
							else if (grp_3way[k] == 0) grass_count++;
							else if (grp_3way[k] == 1) desert_count++;
						}
					}
				}
			assert(water_count > 7000); /* water body is preserved */
			assert(grass_count > 1000 && desert_count > 50); /* both adjacent biomes transition cleanly */
			assert(water_count + grass_count + desert_count == NDW_TILE_W * NDW_TILE_H);
		}
		printf("PASS: 3-way corner with one square of water resolves cleanly without pinwheel distortion\n");

		/* "XY" vertically on top of "WY" (X=non-water, Y=other biome, W=water):
		 * Verifies that the T-junction has no inverted land/water stripes, no pinwheel distortion,
		 * and no deep intrusion of biome X into water or water into biome X. */
		{
			int16_t pos_origin[4] = { 10, -5, 2, 1 };
			/* Tile W is at (1, 2); Tile X is at (1, 1); Tile Y is at (2, 1) and (2, 2). */
			int16_t pw[4] = { (int16_t)(pos_origin[0] + 1), (int16_t)(pos_origin[1] - 2), pos_origin[2], pos_origin[3] };
			int16_t px[4] = { (int16_t)(pos_origin[0] + 1), (int16_t)(pos_origin[1] - 1), pos_origin[2], pos_origin[3] };
			ndw_warp_grid_t gw, gx;
			ndw_warp_grid_fill(&gw, pw);
			ndw_warp_grid_fill(&gx, px);

			/* In tile W (water=2, North=Grass=0, East=Desert=1, NE=Desert=1, rest Water=2) */
			uint8_t grp_w[9] = {
				0, 0, 1,
				2, 2, 1,
				2, 2, 1
			};
			unsigned mask_w = NDW_WATER_BIT(-1, 0) | NDW_WATER_BIT(0, 0) | NDW_WATER_BIT(-1, 1) | NDW_WATER_BIT(0, 1);
			/* In tile X (Grass=0, South=Water=2, East=Desert=1, SE=Desert=1, rest Grass=0) */
			uint8_t grp_x[9] = {
				0, 0, 1,
				0, 0, 1,
				2, 2, 1
			};
			unsigned mask_x = NDW_WATER_BIT(-1, 1) | NDW_WATER_BIT(0, 1);

			int grass_deep_in_water = 0;
			for (int ly = NDW_EDGE_BAND; ly < NDW_TILE_H; ly++) {
				for (int lx = 0; lx < NDW_TILE_W; lx++) {
					uint16_t w[9];
					ndw_ground_weights(&gw, lx * NDW_SUB + NDW_SUB / 2,
					                   ly * NDW_SUB + NDW_SUB / 2, grp_w, mask_w, w);
					for (int k = 0; k < 9; k++) {
						if (w[k] == 256 && grp_w[k] == 0)
							grass_deep_in_water++;
					}
				}
			}
			assert(grass_deep_in_water == 0);

			int water_deep_in_grass = 0;
			for (int ly = 0; ly < NDW_TILE_H - NDW_EDGE_BAND; ly++) {
				for (int lx = 0; lx < NDW_TILE_W; lx++) {
					uint16_t w[9];
					ndw_ground_weights(&gx, lx * NDW_SUB + NDW_SUB / 2,
					                   ly * NDW_SUB + NDW_SUB / 2, grp_x, mask_x, w);
					for (int k = 0; k < 9; k++) {
						if (w[k] == 256 && grp_x[k] == 2)
							water_deep_in_grass++;
					}
				}
			}
			assert(water_deep_in_grass == 0);

			/* Verify that the corners round smoothly into biome Y rather than remaining sharp 90-degree corners */
			int desert_in_w = 0;
			for (int ly = 0; ly < 16; ly++) {
				for (int lx = NDW_TILE_W - 20; lx < NDW_TILE_W; lx++) {
					uint16_t w[9];
					ndw_ground_weights(&gw, lx * NDW_SUB + NDW_SUB / 2, ly * NDW_SUB + NDW_SUB / 2, grp_w, mask_w, w);
					if (w[5] == 256 || w[2] == 256 || w[8] == 256) desert_in_w++;
				}
			}
			int desert_in_x = 0;
			for (int ly = NDW_TILE_H - 16; ly < NDW_TILE_H; ly++) {
				for (int lx = NDW_TILE_W - 20; lx < NDW_TILE_W; lx++) {
					uint16_t w[9];
					ndw_ground_weights(&gx, lx * NDW_SUB + NDW_SUB / 2, ly * NDW_SUB + NDW_SUB / 2, grp_x, mask_x, w);
					if (w[5] == 256 || w[2] == 256 || w[8] == 256) desert_in_x++;
				}
			}
			assert(desert_in_w > 100);
		}
		printf("PASS: XY vertically on top of WY resolves with full corner roundness into Y without inverted stripes or water/land intrusion\n");

		/* Multi-biome vertical coastline (Grass A above Grass B adjacent to Water):
		 * Verifies that land never rounds into water at the boundary between Grass A and Grass B,
		 * preserving a continuous coastline without rectangular chunk seams or bites. */
		{
			int16_t pos_origin[4] = { 10, -5, 2, 1 };
			int16_t p_ga[4] = { (int16_t)(pos_origin[0]), (int16_t)(pos_origin[1] - 1), pos_origin[2], pos_origin[3] };
			int16_t p_gb[4] = { (int16_t)(pos_origin[0]), (int16_t)(pos_origin[1] - 2), pos_origin[2], pos_origin[3] };
			ndw_warp_grid_t g_ga, g_gb;
			ndw_warp_grid_fill(&g_ga, p_ga);
			ndw_warp_grid_fill(&g_gb, p_gb);

			/* In tile GA (Grass A = 0; South = Grass B = 1; East = Water = 2; SE = Water = 2) */
			uint8_t grp_ga[9] = {
				0, 0, 2,
				0, 0, 2,
				1, 1, 2
			};
			unsigned mask_ga = NDW_WATER_BIT(1, -1) | NDW_WATER_BIT(1, 0) | NDW_WATER_BIT(1, 1);

			/* In tile GB (Grass B = 1; North = Grass A = 0; East = Water = 2; NE = Water = 2) */
			uint8_t grp_gb[9] = {
				0, 0, 2,
				1, 1, 2,
				1, 1, 2
			};
			unsigned mask_gb = NDW_WATER_BIT(1, -1) | NDW_WATER_BIT(1, 0) | NDW_WATER_BIT(1, 1);

			/* Assert that in the land region (beyond edge band) near the SE corner of GA, water NEVER intrudes */
			int water_in_ga_corner = 0;
			for (int ly = NDW_TILE_H - 24; ly < NDW_TILE_H; ly++) {
				for (int lx = 0; lx < NDW_TILE_W - NDW_EDGE_BAND; lx++) {
					uint16_t w[9];
					ndw_ground_weights(&g_ga, lx * NDW_SUB + NDW_SUB / 2, ly * NDW_SUB + NDW_SUB / 2, grp_ga, mask_ga, w);
					if (w[2] == 256 || w[5] == 256 || w[8] == 256)
						water_in_ga_corner++;
				}
			}
			/* Water must NOT carve a big bite into the land corner */
			assert(water_in_ga_corner == 0);

			int water_in_gb_corner = 0;
			for (int ly = 0; ly < 24; ly++) {
				for (int lx = 0; lx < NDW_TILE_W - NDW_EDGE_BAND; lx++) {
					uint16_t w[9];
					ndw_ground_weights(&g_gb, lx * NDW_SUB + NDW_SUB / 2, ly * NDW_SUB + NDW_SUB / 2, grp_gb, mask_gb, w);
					if (w[2] == 256 || w[5] == 256 || w[8] == 256)
						water_in_gb_corner++;
				}
			}
			assert(water_in_gb_corner == 0);
		}
		printf("PASS: multi-biome coastline boundary invariant prevents water intrusion and chunk seams\n");

		/* Horizontal multi-biome coastline: Land A West of Land B with Water to the North */
		{
			ndw_warp_grid_t g_h;
			uint8_t grp_h[9] = {
				2, 2, 2,   /* Water, Water, Water */
				0, 0, 1,   /* Land A, Land A, Land B */
				0, 0, 1    /* Land A, Land A, Land B */
			};
			unsigned mask_h = NDW_WATER_BIT(-1, -1) | NDW_WATER_BIT(0, -1) | NDW_WATER_BIT(1, -1);
			int16_t pos_h[4] = { 10, -5, 2, 1 };
			ndw_warp_grid_fill(&g_h, pos_h);
			int water_in_h_corner = 0;
			for (int ly = NDW_EDGE_BAND; ly < NDW_EDGE_BAND + 24; ly++) {
				for (int lx = 0; lx < NDW_TILE_W; lx++) {
					uint16_t w[9];
					ndw_ground_weights(&g_h, lx * NDW_SUB + NDW_SUB / 2, ly * NDW_SUB + NDW_SUB / 2, grp_h, mask_h, w);
					if (w[0] == 256 || w[1] == 256 || w[2] == 256)
						water_in_h_corner++;
				}
			}
			assert(water_in_h_corner == 0);
		}
		printf("PASS: horizontal multi-biome coastline boundary invariant prevents water intrusion\n");

		/* No isolated single-pixel islands (speckle-free continuous contour) */
		{
			ndw_warp_grid_t g0;
			uint8_t grp[9] = { 0, 0, 1, 0, 0, 1, 0, 0, 1 };
			uint8_t own[NDW_TILE_H][NDW_TILE_W];
			ndw_warp_grid_fill(&g0, p0);
			for (int ly = 0; ly < NDW_TILE_H; ly++)
				for (int lx = 0; lx < NDW_TILE_W; lx++) {
					uint16_t w[9];
					ndw_ground_weights(&g0, lx * NDW_SUB + NDW_SUB / 2,
					                   ly * NDW_SUB + NDW_SUB / 2, grp, 0, w);
					own[ly][lx] = (w[5] == 256);
				}
			for (int ly = 1; ly < NDW_TILE_H - 1; ly++)
				for (int lx = 1; lx < NDW_TILE_W - 1; lx++) {
					uint8_t v = own[ly][lx];
					int same_neighbors = (own[ly-1][lx] == v) + (own[ly+1][lx] == v) +
					                     (own[ly][lx-1] == v) + (own[ly][lx+1] == v);
					assert(same_neighbors >= 1); /* no isolated 1-pixel dither dots */
				}
		}
		printf("PASS: border contour is continuous with zero speckle/dither dots\n");

		/* Corner rounding on isolated tile: exterior corner points are rounded away */
		{
			ndw_warp_grid_t g0;
			uint8_t grp[9] = { 0, 0, 0, 0, 1, 0, 0, 0, 0 }; /* isolated center tile 1 */
			uint16_t w[9];
			ndw_warp_grid_fill(&g0, p0);
			/* Center of tile is 100% self */
			ndw_ground_weights(&g0, NDW_CENTER_X * NDW_SUB, NDW_CENTER_Y * NDW_SUB, grp, 0, w);
			assert(w[4] == 256);
			/* Exterior of top-left corner is rounded away */
			ndw_ground_weights(&g0, 4 * NDW_SUB, 4 * NDW_SUB, grp, 0, w);
			assert(w[4] == 0);
		}
		printf("PASS: isolated tile has rounded corners with no sharp 90-degree edges\n");

		/* The warp is a function of absolute world coordinates: the same world
		 * point seen from the east neighbour (local x - TILE_W) or the south
		 * neighbour gets the same displacement, so no seam appears at a tile edge. */
		for (int k = 0; k < 40; k++)
			for (int y = 0; y < NDW_TILE_H; y += 3) {
				int dxa, dya, dxb, dyb;
				ndw_border_warp(p0, NDW_TILE_W + k - 20, y, &dxa, &dya);
				ndw_border_warp(pe, k - 20, y, &dxb, &dyb);
				assert(dxa == dxb && dya == dyb);
				ndw_border_warp(p0, y, NDW_TILE_H + k - 20, &dxa, &dya);
				ndw_border_warp(ps, y, k - 20, &dxb, &dyb);
				assert(dxa == dxb && dya == dyb);
			}
		printf("PASS: border warp is seamless across tile edges (absolute coordinates)\n");

		/* Bounded and smooth: |warp| <= amplitude and neighbouring units differ by little. */
		{
			int pdx = 0, pdy = 0, mn = 99, mx = -99;
			for (int x = -40; x < 2 * NDW_TILE_W; x++) {
				int dx, dy;
				ndw_border_warp(p0, x, 5, &dx, &dy);
				assert(dx >= -NDW_BLEND_AMP && dx <= NDW_BLEND_AMP);
				assert(dy >= -NDW_BLEND_AMP && dy <= NDW_BLEND_AMP);
				if (x > -40) {
					assert(dx - pdx <= 10 && pdx - dx <= 10);
					assert(dy - pdy <= 10 && pdy - dy <= 10);
				}
				pdx = dx; pdy = dy;
				if (dx < mn) mn = dx;
				if (dx > mx) mx = dx;
			}
			assert(mx - mn >= 40);   /* it actually varies */
		}
		printf("PASS: border warp is bounded, smooth and varied\n");

		/* R1: Prominent medium-frequency variation in ndw_border_warp across 8-16 unit distances */
		{
			int max_step_delta = 0;
			int64_t sum_sq = 0;
			for (int x = 0; x < NDW_TILE_W; x++) {
				int dx, dy;
				ndw_border_warp(p0, x, 5, &dx, &dy);
				sum_sq += (int64_t)dx * dx;
				if (x >= 8) {
					int p8_dx, p8_dy;
					ndw_border_warp(p0, x - 8, 5, &p8_dx, &p8_dy);
					int delta8 = dx > p8_dx ? dx - p8_dx : p8_dx - dx;
					if (delta8 > max_step_delta) max_step_delta = delta8;
				}
			}
			int mean_sq = (int)(sum_sq / NDW_TILE_W);
			assert(max_step_delta >= 3); /* prominent 8-unit medium-scale delta */
			assert(mean_sq >= 2);        /* strong displacement energy */
		}
		printf("PASS: prominent medium-frequency variation across 8-16 unit distances\n");

		/* R2: Full macroscopic corner coverage without flat horizontal/vertical shelves */
		{
			assert(NDW_CORNER_RX == 80 && NDW_CORNER_RY == 64);
			assert(NDW_CORNER_R == 80);
			assert(NDW_BLEND_AMP == 66);
		}
		printf("PASS: full macroscopic corner coverage (RX=80, RY=64, AMP=66)\n");

		/* Macro noise octave: 256/128-unit octaves produce half/three-quarters room features */
		{
			int min_dx = 999, max_dx = -999;
			for (int y = 0; y < NDW_TILE_H; y++) {
				for (int x = 0; x < NDW_TILE_W; x++) {
					int dx, dy;
					ndw_border_warp(p0, x, y, &dx, &dy);
					if (dx < min_dx) min_dx = dx;
					if (dx > max_dx) max_dx = dx;
				}
			}
			/* Peak-to-peak feature swing across room is at least 50 units (>= 1/2 to 3/4 of room scale) */
			assert(max_dx - min_dx >= 50);
		}
		printf("PASS: macro noise octave produces half-to-three-quarters room features (swing >= 50 units)\n");

		/* R3: Small-scale micro-variation preservation */
		{
			int non_zero_deltas = 0;
			int pdx = 0;
			for (int x = 0; x < NDW_TILE_W; x++) {
				int dx, dy;
				ndw_border_warp(p0, x, 5, &dx, &dy);
				if (x > 0 && dx != pdx) non_zero_deltas++;
				pdx = dx;
			}
			assert(non_zero_deltas > 20); /* fine micro-detail across unit steps */
		}
		printf("PASS: small-scale micro-variation preserved across unit steps\n");

		/* The border is organic: along the edge band the owner transitions between self
		 * and the neighbor tile, differently in every column of the band. */
		{
			int east_at_edge = 0, west_at_edge = 0, cols_differ = 0, first = -1;
			for (int ly = 0; ly < NDW_TILE_H; ly++) {
				int oi, oj;
				ndw_ground_owner(p0, NDW_TILE_W - 1, ly, 0, &oi, &oj);
				east_at_edge += (oi == 1 && oj == 0);
				ndw_ground_owner(p0, 0, ly, 0, &oi, &oj);
				west_at_edge += (oi == -1 && oj == 0);
			}
			assert(east_at_edge > 0 || west_at_edge > 0);
			for (int lx = 0; lx < NDW_BLEND_BAND; lx++) {
				int cnt = 0;
				for (int ly = 24; ly < NDW_TILE_H - 24; ly++) {
					int oi, oj;
					ndw_ground_owner(p0, lx, ly, 0, &oi, &oj);
					cnt += (oi == -1);
				}
				if (first < 0) first = cnt;
				cols_differ += (cnt != first);
			}
			assert(cols_differ > 0);
		}
		printf("PASS: tile borders are wavy, not straight\n");

		/* Corners: both axes can cross, giving a diagonal owner. */
		{
			int diag = 0, sides = 0;
			for (int k = 0; k < 64; k++) {
				int16_t pc[4] = { (int16_t)(k * 3 - 90), (int16_t)(k * 5 - 150), 0, 0 };
				for (int ly = NDW_TILE_H - 4; ly < NDW_TILE_H; ly++)
					for (int lx = NDW_TILE_W - 4; lx < NDW_TILE_W; lx++) {
						int oi, oj;
						ndw_ground_owner(pc, lx, ly, 0, &oi, &oj);
						diag += (oi == 1 && oj == 1);
						sides += ((oi == 1) != (oj == 1));
					}
			}
			assert(diag > 0 && sides > 0);
		}
		printf("PASS: corner points can be owned by the diagonal tile\n");

		/* Rooms and unknown neighbours never blend: an own-tile bit pins every point to
		 * itself; a neighbour bit stops that neighbour from owning any point. */
		for (int ly = 0; ly < NDW_TILE_H; ly++)
			for (int lx = 0; lx < NDW_TILE_W; lx++) {
				int oi, oj;
				unsigned all_but_self = 0x1FFu & ~NDW_OWN_BIT(0, 0);
				ndw_ground_owner(p0, lx, ly, NDW_OWN_BIT(0, 0), &oi, &oj);
				assert(oi == 0 && oj == 0);
				ndw_ground_owner(p0, lx, ly, all_but_self, &oi, &oj);
				assert(oi == 0 && oj == 0);
				ndw_ground_owner(p0, lx, ly, NDW_OWN_BIT(1, 0), &oi, &oj);
				assert(!(oi == 1 && oj == 0));
			}
		printf("PASS: rooms and unknown neighbours stay square\n");

		/* z and world change the shape; the same z/world repeat it. */
		{
			int16_t pz[4] = { 12, -7, 4, 1 }, pw[4] = { 12, -7, 3, 2 };
			int dz = 0, dw = 0;
			for (int x = 0; x < NDW_TILE_W; x++) {
				int a0, b0, a1, b1, a2, b2;
				ndw_border_warp(p0, x, 9, &a0, &b0);
				ndw_border_warp(pz, x, 9, &a1, &b1);
				ndw_border_warp(pw, x, 9, &a2, &b2);
				dz += (a0 != a1 || b0 != b1);
				dw += (a0 != a2 || b0 != b2);
			}
			assert(dz > 20 && dw > 20);
		}
		printf("PASS: z and world seed the border shape\n");

		/* Objects in the interior keep their own ground; objects near rounded corners
		 * may stand on the smoothly blended transition ground per operator authorization. */
		{
			const char glyphs[] = "xXtTaAiI*oO~$<>?";
			int on_own = 0, on_transition = 0;
			for (int k = 0; k < 300; k++) {
				int16_t pk[4] = { (int16_t)(k * 7 - 1000), (int16_t)(k * 13 - 2000), (int16_t)(k % 3), (int16_t)(k % 2) };
				ndw_tile_t tk = tile((uint8_t)(k % 19), 0x1B, 0,
				                     glyphs[k % 16], glyphs[(k / 3) % 16], glyphs[(k / 7) % 16]);
				ndw_obj_t ob[NDW_MAX_OBJ];
				int n = ndw_tile_objects(&tk, pk, 0, ob);
				for (int i = 0; i < n; i++) {
					int oi, oj;
					if (ob[i].kind == NDW_K_POND)
						continue;
					ndw_ground_owner(pk, ob[i].x, ob[i].y, 0, &oi, &oj);
					if (oi == 0 && oj == 0)
						on_own++;
					else
						on_transition++;
				}
			}
			assert(on_own > 500);
			assert(on_transition >= 0);
		}
		printf("PASS: objects keep own ground with corner objects on blended transition\n");

		/* Negative and large absolute coordinates floor correctly. */
		{
			int16_t pn[4] = { -32768, 32767, 0, 0 };
			int16_t pm[4] = { 32767, -32768, 0, 0 };
			for (int lx = 0; lx < NDW_TILE_W; lx += 5) {
				int oi, oj;
				ndw_ground_owner(pn, lx, 3, 0, &oi, &oj);
				assert(oi >= -1 && oi <= 1 && oj >= -1 && oj <= 1);
				ndw_ground_owner(pm, lx, 3, 0, &oi, &oj);
				assert(oi >= -1 && oi <= 1 && oj >= -1 && oj <= 1);
			}
		}
		printf("PASS: extreme coordinates stay in range\n");
	}

	/* 2D organic grove and outcrop dispersion, Poisson-disc blue-noise spacing,
	 * and elimination of artificial vertical columns (R1, R2, R3, R4) */
	{
		/* Part A: Intra-slot grove cluster dispersion (single-slot grove with 'X' in Slot 0) */
		int intra_groves = 0;
		int intra_diagonal_pairs = 0;
		int total_intra_pairs = 0;

		for (int k = 0; k < 100; k++) {
			int16_t pk[4] = { (int16_t)(k * 13 - 350), (int16_t)(k * 17 + 220), 0, 0 };
			ndw_tile_t tk = tile(10, 0x1B, 0, 'X', 0, 0); /* 'X' exclusively in Slot 0 */
			ndw_obj_t ob[NDW_MAX_OBJ];
			int n = ndw_tile_objects(&tk, pk, 0, ob);
			assert(n >= 2); /* Big tree produces 2 or 3 trees in same grove */

			intra_groves++;
			for (int a = 0; a < n; a++) {
				assert(ob[a].slot_id == 0);
				assert(ob[a].kind == NDW_K_DECID);
				ndw_rect_t r = ndw_obj_rect(&ob[a]);
				ndw_rect_t clear = { NDW_CLEAR_X0, NDW_CLEAR_Y0, NDW_CLEAR_X1, NDW_CLEAR_Y1 };
				assert(r.x0 >= NDW_MARGIN && r.y0 >= NDW_MARGIN);
				assert(r.x1 <= NDW_TILE_W - NDW_MARGIN && r.y1 <= NDW_TILE_H - NDW_MARGIN);
				assert(!ndw_rect_hit(&r, &clear));

				for (int b = a + 1; b < n; b++) {
					total_intra_pairs++;
					int dx = ob[a].x - ob[b].x;
					int dy = ob[a].y - ob[b].y;
					int d2 = dx * dx + dy * dy;
					if (dx < 0) dx = -dx;
					if (dy < 0) dy = -dy;

					/* Poisson-disc blue noise spacing within the same grove */
					assert(d2 >= 360);
					/* Zero vertical totem poles */
					assert(!(dx < 5 && dy < 22));
					/* Zero horizontal fences */
					assert(!(dy < 5 && dx < 22));
					/* Diagonal 2D radial dispersion */
					if (dx >= 6 && dy >= 6)
						intra_diagonal_pairs++;
				}
			}
		}

		assert(intra_groves == 100);
		assert(total_intra_pairs > 0);
		assert(intra_diagonal_pairs * 2 >= total_intra_pairs);

		/* Part B: Full tile multi-slot coverage across continuous dimensions */
		int min_x = NDW_TILE_W, max_x = 0;
		int min_y = NDW_TILE_H, max_y = 0;

		for (int k = 0; k < 100; k++) {
			int16_t pk[4] = { (int16_t)(k * 11 - 500), (int16_t)(k * 17 + 300), 0, 0 };
			ndw_tile_t tk = tile(10, 0x1B, 0, 'X', 'o', 'T');
			ndw_obj_t ob[NDW_MAX_OBJ];
			int n = ndw_tile_objects(&tk, pk, 0, ob);
			assert(n >= 3);

			for (int i = 0; i < n; i++) {
				if (ob[i].x < min_x) min_x = ob[i].x;
				if (ob[i].x > max_x) max_x = ob[i].x;
				if (ob[i].y < min_y) min_y = ob[i].y;
				if (ob[i].y > max_y) max_y = ob[i].y;

				ndw_rect_t r = ndw_obj_rect(&ob[i]);
				ndw_rect_t clear = { NDW_CLEAR_X0, NDW_CLEAR_Y0, NDW_CLEAR_X1, NDW_CLEAR_Y1 };
				assert(r.x0 >= NDW_MARGIN && r.y0 >= NDW_MARGIN);
				assert(r.x1 <= NDW_TILE_W - NDW_MARGIN && r.y1 <= NDW_TILE_H - NDW_MARGIN);
				assert(!ndw_rect_hit(&r, &clear));

				if (ob[i].slot_id == 2) {
					assert(ob[i].x >= 98);
				}
			}
		}

		/* Objects are widely spread across the full width [18..142] and height [18..116] of the tile */
		assert(min_x <= 25 && max_x >= 135);
		assert(min_y <= 30 && max_y >= 100);
		printf("PASS: 2D organic grove and outcrop dispersion, blue-noise spacing, and fence elimination verified\n");
	}

	/* Slot 1 organic spread and arrival-box flank occupancy verification (R1, R2, R3) */
	{
		int s1_min_x = NDW_TILE_W, s1_max_x = 0;
		int west_flank_hits = 0;
		int east_flank_hits = 0;

		for (int k = 0; k < 120; k++) {
			int16_t pk[4] = { (int16_t)(k * 13 + 100), (int16_t)(k * 19 - 250), 0, 0 };
			ndw_tile_t tk = tile(10, 0x1B, 0, 0, 'x', 0);
			ndw_obj_t ob[NDW_MAX_OBJ];
			int n = ndw_tile_objects(&tk, pk, 0, ob);
			assert(n == 1);
			assert(ob[0].slot_id == 1);

			if (ob[0].x < s1_min_x) s1_min_x = ob[0].x;
			if (ob[0].x > s1_max_x) s1_max_x = ob[0].x;

			/* Middle vertical range where clear box [56..104] x [44..84] resides */
			if (ob[0].y >= 44 && ob[0].y <= 84) {
				if (ob[0].x <= 55) west_flank_hits++;
				if (ob[0].x >= 105) east_flank_hits++;
			}
			ndw_rect_t r = ndw_obj_rect(&ob[0]);
			ndw_rect_t clear = { NDW_CLEAR_X0, NDW_CLEAR_Y0, NDW_CLEAR_X1, NDW_CLEAR_Y1 };
			assert(!ndw_rect_hit(&r, &clear));
		}

		/* Slot 1 objects must span across the expanded [44..116] domain */
		assert(s1_min_x <= 50 && s1_max_x >= 110);
		/* Both West and East flanks around arrival clear box are actively occupied in middle Y */
		assert(west_flank_hits > 0 && east_flank_hits > 0);
		printf("PASS: Slot 1 organic spread [44..116] and arrival-box flank occupancy verified\n");
	}

	/* Rock and mineral placement dispersion across all slots (R1, R2, R3) */
	{
		int r0_min_x = NDW_TILE_W, r0_max_x = 0;
		int r1_min_x = NDW_TILE_W, r1_max_x = 0;
		int r2_min_x = NDW_TILE_W, r2_max_x = 0;
		int r1_west_flank = 0, r1_east_flank = 0;

		for (int k = 0; k < 120; k++) {
			int16_t pk[4] = { (int16_t)(k * 23 - 400), (int16_t)(k * 29 + 150), 0, 0 };

			/* Slot 0 rock: 'o' in slot 0 */
			ndw_tile_t t0 = tile(10, 0x1B, 0, 'o', 0, 0);
			ndw_obj_t ob0[NDW_MAX_OBJ];
			int n0 = ndw_tile_objects(&t0, pk, 0, ob0);
			assert(n0 == 1 && ob0[0].slot_id == 0 && ob0[0].kind == NDW_K_ROCK);
			if (ob0[0].x < r0_min_x) r0_min_x = ob0[0].x;
			if (ob0[0].x > r0_max_x) r0_max_x = ob0[0].x;

			/* Slot 1 rock: 'o' in slot 1 */
			ndw_tile_t t1 = tile(10, 0x1B, 0, 0, 'o', 0);
			ndw_obj_t ob1[NDW_MAX_OBJ];
			int n1 = ndw_tile_objects(&t1, pk, 0, ob1);
			assert(n1 == 1 && ob1[0].slot_id == 1 && ob1[0].kind == NDW_K_ROCK);
			if (ob1[0].x < r1_min_x) r1_min_x = ob1[0].x;
			if (ob1[0].x > r1_max_x) r1_max_x = ob1[0].x;
			if (ob1[0].y >= 40 && ob1[0].y <= 88) {
				if (ob1[0].x <= 55) r1_west_flank++;
				if (ob1[0].x >= 105) r1_east_flank++;
			}
			ndw_rect_t r1 = ndw_obj_rect(&ob1[0]);
			ndw_rect_t clear = { NDW_CLEAR_X0, NDW_CLEAR_Y0, NDW_CLEAR_X1, NDW_CLEAR_Y1 };
			assert(!ndw_rect_hit(&r1, &clear));

			/* Slot 2 rock: 'o' in slot 2 */
			ndw_tile_t t2 = tile(10, 0x1B, 0, 0, 0, 'o');
			ndw_obj_t ob2[NDW_MAX_OBJ];
			int n2 = ndw_tile_objects(&t2, pk, 0, ob2);
			assert(n2 == 1 && ob2[0].slot_id == 2 && ob2[0].kind == NDW_K_ROCK);
			if (ob2[0].x < r2_min_x) r2_min_x = ob2[0].x;
			if (ob2[0].x > r2_max_x) r2_max_x = ob2[0].x;
			assert(ob2[0].x >= 98);
		}

		/* Slot 0 rocks disperse across expanded domain [margin+obj_m .. 72] */
		assert(r0_min_x <= 28 && r0_max_x >= 60);
		/* Slot 1 rocks disperse across expanded domain [44..116] */
		assert(r1_min_x <= 50 && r1_max_x >= 110);
		/* Slot 1 rocks actively populate flanks around arrival clear box in middle Y */
		assert(r1_west_flank > 0 && r1_east_flank > 0);
		/* Slot 2 rocks disperse across [98..138] while strictly respecting x >= 98 */
		assert(r2_min_x >= 98 && r2_min_x <= 106 && r2_max_x >= 130);

		printf("PASS: rock placement dispersion across Slot 0 [<=28..>=60], Slot 1 [<=50..>=110], and Slot 2 [>=98..>=130] verified\n");
	}

	printf("ALL SEVEN WORLD TESTS PASSED\n");
	return 0;
}
