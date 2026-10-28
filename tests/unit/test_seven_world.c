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

	printf("ALL SEVEN WORLD TESTS PASSED\n");
	return 0;
}
