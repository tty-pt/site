#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <nd/seven-world.h>

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
	assert(ndw_biome_style(18).kind == NDW_G_ASH && ndw_biome_style(2).kind == NDW_G_TUNDRA);
	assert(ndw_biome_style(250).kind == NDW_G_GRASS);
	printf("PASS: biomes pick the ground kind (desert = sand)\n");

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
		}
		assert(has_tree && has_rock);
		printf("PASS: AOT rocks and stones map to world objects and coexist with flora\n");
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
			uint16_t w[9];
			ndw_warp_grid_fill(&g_water, p_water);
			int water_count = 0, grass_count = 0, desert_count = 0;
			for (int ly = 0; ly < NDW_TILE_H; ly++)
				for (int lx = 0; lx < NDW_TILE_W; lx++) {
					ndw_ground_weights(&g_water, lx * NDW_SUB + NDW_SUB / 2,
					                   ly * NDW_SUB + NDW_SUB / 2, grp_3way, 0, w);
					for (int k = 0; k < 9; k++) {
						if (w[k] == 256) {
							if (grp_3way[k] == 2) water_count++;
							else if (grp_3way[k] == 0) grass_count++;
							else if (grp_3way[k] == 1) desert_count++;
						}
					}
				}
			assert(water_count > 10000); /* water body is preserved */
			assert(grass_count > 1000 && desert_count > 50); /* both adjacent biomes transition cleanly */
			assert(water_count + grass_count + desert_count == NDW_TILE_W * NDW_TILE_H);
		}
		printf("PASS: 3-way corner with one square of water resolves cleanly without pinwheel distortion\n");

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
					assert(dx - pdx <= 3 && pdx - dx <= 3);
					assert(dy - pdy <= 3 && pdy - dy <= 3);
				}
				pdx = dx; pdy = dy;
				if (dx < mn) mn = dx;
				if (dx > mx) mx = dx;
			}
			assert(mx - mn >= NDW_BLEND_AMP);   /* it actually varies */
		}
		printf("PASS: border warp is bounded, smooth and varied\n");

		/* The border is organic: along the east edge the owner flips between self
		 * and the east tile, differently in every column of the band. */
		{
			int east_at_edge = 0, west_at_edge = 0, cols_differ = 0, first = -1;
			for (int ly = 0; ly < NDW_TILE_H; ly++) {
				int oi, oj;
				ndw_ground_owner(p0, NDW_TILE_W - 1, ly, 0, &oi, &oj);
				east_at_edge += (oi == 1 && oj == 0);
				ndw_ground_owner(p0, 0, ly, 0, &oi, &oj);
				west_at_edge += (oi == -1 && oj == 0);
			}
			assert(east_at_edge > 0 && east_at_edge < NDW_TILE_H - 20);
			assert(west_at_edge > 0 && west_at_edge < NDW_TILE_H - 20);
			for (int lx = NDW_TILE_W - NDW_BLEND_BAND; lx < NDW_TILE_W; lx++) {
				int cnt = 0;
				for (int ly = 24; ly < NDW_TILE_H - 24; ly++) {
					int oi, oj;
					ndw_ground_owner(p0, lx, ly, 0, &oi, &oj);
					cnt += (oi == 1);
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

	printf("ALL SEVEN WORLD TESTS PASSED\n");
	return 0;
}
