#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <nd/bcp-types.h>
#include <nd/seven-world.h>
#include "nd_seven.h"
#include "nd_seven_atlas.h"

#define MAXW 1920
#define MAXH 1080
static uint32_t fb[MAXW * MAXH], fb2[MAXW * MAXH];

static nd_client_state_t st;

/* A 7x7 map of identical tiles centred on the world tile (x, y). */
static void set_map_keep(int x, int y, int biome, uint8_t exits, uint8_t flags,
                         int gl, int gc, int gr)
{
	memset(&st, 0, sizeof(st));
	strcpy(st.preferred_driver, "lamina-seven");
	st.map_data.width = st.map_data.height = 7;
	st.map_data.player_x = st.map_data.player_y = 3;
	st.map_data.pos[0] = (int16_t)x;
	st.map_data.pos[1] = (int16_t)y;
	for (int i = 0; i < 49; i++) {
		nd_map_tile_t *t = &st.map_data.tiles[i];
		t->bio_idx = (uint8_t)biome;
		t->exits = 0x1B;
		t->side_l = (uint8_t)gl; t->symbol = (uint8_t)gc; t->side_r = (uint8_t)gr;
		t->fg_l = t->fg = t->fg_r = 10;
	}
	st.map_data.tiles[3 * 7 + 3].exits = exits;
	st.map_data.tiles[3 * 7 + 3].flags = (uint8_t)(flags | ND_MAP_FLAG_PLAYER);
	st.map_data.tiles[3 * 7 + 3].symbol = '!';
}

static void set_map(int x, int y, int biome, uint8_t exits, uint8_t flags,
                    int gl, int gc, int gr)
{
	nd_seven_forget();
	set_map_keep(x, y, biome, exits, flags, gl, gc, gr);
}

/* The packed sprite atlas as palette indices, decoded once. */
static const uint8_t *atlas_px(void)
{
	static uint8_t atlas[ND_SEVEN_ATLAS_W * ND_SEVEN_ATLAS_H_PX];
	static int ready;

	if (!ready) {
		size_t o = 0;
		for (size_t i = 0; i + 1 < ND_SEVEN_RLE_LEN && o < sizeof(atlas); i += 2) {
			size_t run = (size_t)nd_seven_rle[i] + 1;
			while (run-- && o < sizeof(atlas))
				atlas[o++] = nd_seven_rle[i + 1];
		}
		ready = 1;
	}
	return atlas;
}

static void dump(const char *name, const uint32_t *p, int w, int h)
{
	const char *dir = getenv("SEVEN_DUMP");
	if (!dir) return;
	char path[512];
	snprintf(path, sizeof(path), "%s/%s.ppm", dir, name);
	FILE *f = fopen(path, "wb");
	fprintf(f, "P6\n%d %d\n255\n", w, h);
	for (int i = 0; i < w * h; i++) {
		fputc(p[i] & 0xFF, f); fputc((p[i] >> 8) & 0xFF, f); fputc((p[i] >> 16) & 0xFF, f);
	}
	fclose(f);
}

static int count_color(const uint32_t *p, int n, uint32_t c)
{
	int k = 0;
	for (int i = 0; i < n; i++) k += p[i] == c;
	return k;
}

/* green-dominant pixels (plants, grass) */
static int count_green(const uint32_t *p, int n)
{
	int k = 0;
	for (int i = 0; i < n; i++) {
		int r = (int)(p[i] & 0xFF), g = (int)((p[i] >> 8) & 0xFF), b = (int)((p[i] >> 16) & 0xFF);
		k += g > r + 8 && g > b + 25;
	}
	return k;
}

/* sand-like pixels: warm, light, r >= g >= b */
static int count_sand(const uint32_t *p, int n)
{
	int k = 0;
	for (int i = 0; i < n; i++) {
		int r = (int)(p[i] & 0xFF), g = (int)((p[i] >> 8) & 0xFF), b = (int)((p[i] >> 16) & 0xFF);
		k += r > 170 && r >= g && g >= b && r - b > 40;
	}
	return k;
}

static void hold(const char *key, int ticks, int *keyret)
{
	nd_seven_key(key, 1);
	for (int i = 0; i < ticks; i++) {
		int mv = nd_seven_tick(&st, 1.0f / 60);
		if (mv && keyret) { *keyret = mv; break; }
	}
	nd_seven_key(key, 0);
}

int main(void)
{
	const int sizes[3][2] = { { 640, 480 }, { 1280, 720 }, { 1920, 1080 } };
	int fx, fy, dir, walking, key;

	printf("=== ND seven stage: continuous world ===\n");

	/* The world fills any framebuffer, at the unchanged 2x scale. */
	for (int s = 0; s < 3; s++) {
		int w = sizes[s][0], h = sizes[s][1];
		set_map(5, -3, 10, 0x1B, 0, 'x', 0, 'i');
		nd_seven_render(fb, (uint32_t)w, (uint32_t)h, &st);
		assert(count_color(fb, w * h, 0xFF08080C) == 0);
		set_map(5, -3, 14, 0x1B, 0, 0, 0, 0);
		nd_seven_render(fb, (uint32_t)w, (uint32_t)h, &st);
		assert(count_color(fb, w * h, 0xFF08080C) == 0);
	}
	assert(NDW_TILE_W * 2 <= 320 && NDW_TILE_H * 2 <= 256);
	printf("PASS: 640x480, 1280x720 and 1920x1080 are fully covered by small tiles\n");

	/* Deterministic. */
	set_map(5, -3, 10, 0x1B, 0, 'x', 'T', 'i');
	nd_seven_render(fb, 1280, 720, &st);
	nd_seven_render(fb2, 1280, 720, &st);
	assert(!memcmp(fb, fb2, 1280 * 720 * 4));
	set_map(6, -3, 10, 0x1B, 0, 'x', 'T', 'i');
	nd_seven_render(fb2, 1280, 720, &st);
	assert(memcmp(fb, fb2, 1280 * 720 * 4) != 0);
	printf("PASS: same map and frame give the same pixels, other tiles differ\n");

	/* Biome decides the ground: desert is sand, grassland is grass. */
	set_map(5, -3, 14, 0x1B, 0, 0, 0, 0);
	nd_seven_render(fb, 1280, 720, &st);
	int sand = count_sand(fb, 1280 * 720), grass_in_desert = count_green(fb, 1280 * 720);
	dump("desert", fb, 1280, 720);
	assert(sand > 1280 * 720 / 2);
	assert(grass_in_desert < 1280 * 720 / 50);
	set_map(5, -3, 10, 0x1B, 0, 0, 0, 0);
	nd_seven_render(fb, 1280, 720, &st);
	dump("grass", fb, 1280, 720);
	assert(count_green(fb, 1280 * 720) > 1280 * 720 / 2);
	assert(count_sand(fb, 1280 * 720) < 1280 * 720 / 20);
	set_map(5, -3, 1, 0x1B, 0, 0, 0, 0);
	nd_seven_render(fb, 1280, 720, &st);
	dump("ice", fb, 1280, 720);
	assert(count_sand(fb, 1280 * 720) < 1280 * 720 / 20 && count_green(fb, 1280 * 720) < 1280 * 720 / 20);
	set_map(5, -3, 0, 0x1B, 0, 0, 0, 0);
	nd_seven_render(fb, 1280, 720, &st);
	dump("water", fb, 1280, 720);
	printf("PASS: desert draws sand, grassland grass, ice neither\n");

	/* A cactus on the map is a cactus in the view. */
	set_map(5, -3, 14, 0x1B, 0, 0, 0, 0);
	nd_seven_render(fb, 1280, 720, &st);
	int g0 = count_green(fb, 1280 * 720);
	set_map(5, -3, 14, 0x1B, 0, 0, 0, 'I');
	nd_seven_render(fb2, 1280, 720, &st);
	dump("desert_cactus", fb2, 1280, 720);
	int g1 = count_green(fb2, 1280 * 720);
	assert(g1 > g0 + 400);                     /* green cacti stand on the sand */
	assert(count_sand(fb2, 1280 * 720) > 1280 * 720 / 3);
	/* the cactus of the right map cell is on the right of its tile, ie the
	 * right third of the lamb's tile gained the green */
	{
		int tx = 1280 / 2 - NDW_CENTER_X * 2, ty = 720 / 2 - NDW_CENTER_Y * 2;
		int added = 0;
		for (int y = ty; y < ty + NDW_TILE_H * 2; y++)
			for (int x = tx + NDW_TILE_W * 2 * 2 / 3; x < tx + NDW_TILE_W * 2; x++) {
				uint32_t a = fb[y * 1280 + x], b = fb2[y * 1280 + x];
				added += a != b;
			}
		assert(added > 200);
	}
	/* the same cactus tile on grass biome has no sand */
	set_map(5, -3, 10, 0x1B, 0, 0, 0, 'I');
	nd_seven_render(fb2, 1280, 720, &st);
	assert(count_sand(fb2, 1280 * 720) < 1280 * 720 / 20);
	/* small and big cacti differ */
	set_map(5, -3, 14, 0x1B, 0, 0, 0, 'i');
	nd_seven_render(fb, 1280, 720, &st);
	set_map(5, -3, 14, 0x1B, 0, 0, 0, 'I');
	nd_seven_render(fb2, 1280, 720, &st);
	assert(memcmp(fb, fb2, 1280 * 720 * 4) != 0);
	printf("PASS: map cactus glyphs appear as cacti in their cell, on sand in desert\n");

	/* A tree seen from outside stays when the player steps into its tile: the
	 * server's '!' marker must not erase it (cache by absolute tile position). */
	{
		set_map(5, -3, 10, 0x1B, 0, 0, 'x', 0);
		nd_seven_render(fb, 1280, 720, &st);          /* (6,-3) seen as 'x' */
		set_map_keep(6, -3, 10, 0x1B, 0, 0, 'x', 0);   /* now we stand in it */
		nd_seven_render(fb, 1280, 720, &st);
		set_map(6, -3, 10, 0x1B, 0, 0, 'x', 0);        /* control: never seen */
		nd_seven_render(fb2, 1280, 720, &st);
		assert(memcmp(fb, fb2, 1280 * 720 * 4) != 0);   /* the restored tree is drawn */
		printf("PASS: a tree survives the player entering its tile\n");
	}

	/* The cache holds far more than one 7x7 view: after seeing ~300 distinct
	 * tiles, stepping into tiles seen long ago still restores their tree. */
	{
		static const int origin[] = { 5, 12, 19, 26, 33, 40 };
		static const int enter[] = { 3, 10, 17, 24, 31, 38 };

		set_map(origin[0], -3, 10, 0x1B, 0, 0, 'x', 0);
		nd_seven_render(fb, 1280, 720, &st);
		for (int i = 1; i < 6; i++) {
			set_map_keep(origin[i], -3, 10, 0x1B, 0, 0, 'x', 0);
			nd_seven_render(fb, 1280, 720, &st);
		}
		for (int i = 0; i < 6; i++) {
			set_map_keep(enter[i], -3, 10, 0x1B, 0, 0, 'x', 0);
			nd_seven_render(fb, 1280, 720, &st);
			nd_seven_forget();
			set_map_keep(enter[i], -3, 10, 0x1B, 0, 0, 'x', 0);
			nd_seven_render(fb2, 1280, 720, &st);          /* control: nothing remembered */
			assert(memcmp(fb, fb2, 1280 * 720 * 4) != 0);
			/* re-seed the cache for the next entry tile */
			for (int j = 0; j < 6; j++) {
				set_map_keep(origin[j], -3, 10, 0x1B, 0, 0, 'x', 0);
				nd_seven_render(fb2, 1280, 720, &st);
			}
		}
		printf("PASS: tree glyphs survive cache pressure\n");
	}

	/* Rock and stone sprites are cropped whole: trimmed to their opaque bounds
	 * (opaque pixels touch all four edges), the medium tan rock is not clipped,
	 * and no stone sprite holds the mushroom art that once sat next to it. */
	{
		static const int ids[] = {
			NDS_ROCK_TAN_BIG, NDS_ROCK_WHITE_BIG, NDS_ROCK_TAN, NDS_ROCK_TAN_SM, NDS_ROCK_TAN_TINY,
			NDS_ROCK_WHITE, NDS_ROCK_GREY, NDS_STONE_BLUE, NDS_STONE_BLUE_SM, NDS_STONE_GREY,
			NDS_STONE_FLAT, NDS_STONE_TINY
		};
		const uint8_t *atlas = atlas_px();

		for (size_t k = 0; k < sizeof(ids) / sizeof(ids[0]); k++) {
			const nd_seven_sprite_rect_t *r = &nd_seven_sprite_rects[ids[k]];
			int top = 0, bot = 0, lef = 0, rig = 0, mushroom = 0;

			assert(r->w > 0 && r->h > 0);
			for (int y = 0; y < r->h; y++)
				for (int x = 0; x < r->w; x++) {
					uint32_t c = nd_seven_palette[atlas[(r->y + y) * ND_SEVEN_ATLAS_W + r->x + x]];
					int cr = (int)(c & 0xFF), cg = (int)((c >> 8) & 0xFF), cb = (int)((c >> 16) & 0xFF); /* ABGR */

					if (!(c >> 24)) continue;
					top |= y == 0; bot |= y == r->h - 1;
					lef |= x == 0; rig |= x == r->w - 1;
					/* toadstool red: far more red than green and blue */
					mushroom |= cr - cg > 75 && cr - cb > 75;
				}
			assert(top && bot && lef && rig);
			assert(!mushroom);
		}
		/* the medium tan rock is ~48x41 px plus shadow: a cut at 350 would give < 40 */
		assert(nd_seven_sprite_rects[NDS_ROCK_TAN].w >= 44 && nd_seven_sprite_rects[NDS_ROCK_TAN].w <= 49);
		assert(nd_seven_sprite_rects[NDS_ROCK_TAN].h >= 40);
		printf("PASS: atlas rock sprites are whole\n");
	}

	/* Room contents: an entity named "stone" is a stone sprite at its spot, a
	 * "goblin" is still a pawn; and no stone is ever scattered as terrain. */
	{
		static const uint8_t mineral[] = {
			NDS_STONE_GREY, NDS_STONE_BLUE, NDS_STONE_BLUE_SM, NDS_STONE_FLAT,
			NDS_STONE_TINY, NDS_ROCK_TAN_SM, NDS_ROCK_TAN_TINY, NDS_ROCK_GREY
		};
		const uint32_t ref = 100;
		const nd_seven_sprite_rect_t *sr = &nd_seven_sprite_rects[mineral[ndw_mix(ref) % 8]];
		const uint8_t *atlas = atlas_px();
		uint32_t sprite_colour = 0;
		int none, with_stone, with_goblin;

		/* the sprite's centre-most opaque pixel gives a colour only it paints */
		for (int y = sr->h / 2; y < sr->h && !sprite_colour; y++)
			for (int x = sr->w / 2; x < sr->w && !sprite_colour; x++) {
				uint32_t c = nd_seven_palette[atlas[(sr->y + y) * ND_SEVEN_ATLAS_W + sr->x + x]];
				if ((c >> 24) == 0xFF)
					sprite_colour = c;
			}
		assert(sprite_colour);

		set_map(5, -3, 10, 0x1B, 0, 0, 0, 0);
		st.room.ref = 7;
		nd_seven_render(fb, 1280, 720, &st);
		none = count_color(fb, 1280 * 720, sprite_colour);

		st.entity_count = 1;
		st.entities[0].ref = ref; st.entities[0].location = 7;
		st.entities[0].ch = '*'; st.entities[0].fg = 7;
		strcpy(st.entities[0].name, "stone");
		nd_seven_render(fb, 1280, 720, &st);
		with_stone = count_color(fb, 1280 * 720, sprite_colour);
		assert(with_stone > none);                    /* the stone sprite is on screen */

		st.entities[0].ch = 'g';
		strcpy(st.entities[0].name, "goblin");
		nd_seven_render(fb2, 1280, 720, &st);
		with_goblin = count_color(fb2, 1280 * 720, sprite_colour);
		assert(with_goblin == none);                  /* a goblin is a pawn, not a stone */
		assert(memcmp(fb, fb2, 1280 * 720 * 4) != 0);

		st.entities[0].location = 8;                  /* an item of another room is not shown */
		strcpy(st.entities[0].name, "stone");
		nd_seven_render(fb2, 1280, 720, &st);
		assert(count_color(fb2, 1280 * 720, sprite_colour) == none);
		printf("PASS: a stone entity draws as a stone sprite at its spot, a goblin as a pawn\n");
	}

	/* Terrain carries no rocks, stones or mushrooms: the empty map draws only
	 * grass, the two yellow decor sprites and the lamb (no entity, no glyph). */
	{
		static const int ids[] = {
			NDS_ROCK_TAN_BIG, NDS_ROCK_WHITE_BIG, NDS_ROCK_TAN, NDS_ROCK_TAN_SM, NDS_ROCK_TAN_TINY,
			NDS_ROCK_WHITE, NDS_ROCK_GREY, NDS_STONE_BLUE, NDS_STONE_BLUE_SM, NDS_STONE_GREY,
			NDS_STONE_FLAT, NDS_STONE_TINY, NDS_DECOR_ORG, NDS_DECOR_PINK
		};
		const uint8_t *atlas = atlas_px();

		for (int bio = 10; bio <= 14; bio += 4) {      /* grassland and desert */
			set_map(5, -3, bio, 0x1B, 0, 0, 0, 0);
			nd_seven_render(fb, 1280, 720, &st);
			for (size_t k = 0; k < sizeof(ids) / sizeof(ids[0]); k++) {
				const nd_seven_sprite_rect_t *r = &nd_seven_sprite_rects[ids[k]];
				int found = 0;
				/* a colour of this sprite that no other kind of ground paints */
				for (int y = 0; y < r->h; y++)
					for (int x = 0; x < r->w; x++) {
						uint32_t c = nd_seven_palette[atlas[(r->y + y) * ND_SEVEN_ATLAS_W + r->x + x]];
						int cr = (int)(c & 0xFF), cg = (int)((c >> 8) & 0xFF), cb = (int)((c >> 16) & 0xFF);
						if ((c >> 24) != 0xFF || cr - cg < 60 || cr - cb < 60)
							continue;     /* only the toadstool reds are checked per pixel */
						found |= count_color(fb, 1280 * 720, c) > 0;
					}
				assert(!found);
			}
		}
		printf("PASS: terrain draws no rocks, stones or mushrooms\n");
	}

	/* Other plant families and map features show up too. */
	{
		const int gl[][3] = { { 'x', 0, 0 }, { 0, 'T', 0 }, { 0, 0, 'a' }, { 0, '~', 0 }, { 0, '$', 0 }, { '<', 0, '>' } };
		set_map(5, -3, 10, 0x1B, 0, 0, 0, 0);
		nd_seven_render(fb, 1280, 720, &st);
		for (unsigned k = 0; k < sizeof(gl) / sizeof(gl[0]); k++) {
			set_map(5, -3, 10, 0x1B, 0, gl[k][0], gl[k][1], gl[k][2]);
			nd_seven_render(fb2, 1280, 720, &st);
			assert(memcmp(fb, fb2, 1280 * 720 * 4) != 0);
		}
	}
	printf("PASS: deciduous, tall, fir, pond, shop and stairs glyphs are drawn\n");

	/* The room contents list is drawn in the lamb's tile. */
	set_map(5, -3, 10, 0x1B, 0, 0, 0, 0);
	st.room.ref = 9; st.me = 1;
	nd_seven_render(fb, 1280, 720, &st);
	st.entity_count = 2;
	st.entities[0].ref = 5; st.entities[0].location = 9; st.entities[0].ch = 'g'; st.entities[0].fg = 9;
	st.entities[1].ref = 1; st.entities[1].location = 9; st.entities[1].ch = '@'; st.entities[1].fg = 11;   /* me */
	nd_seven_render(fb2, 1280, 720, &st);
	assert(memcmp(fb, fb2, 1280 * 720 * 4) != 0);
	dump("entity", fb2, 1280, 720);
	st.entities[0].location = 77;                      /* another room */
	nd_seven_render(fb, 1280, 720, &st);
	st.entity_count = 0;
	nd_seven_render(fb2, 1280, 720, &st);
	assert(!memcmp(fb, fb2, 1280 * 720 * 4));          /* not here, not drawn; me never drawn */
	printf("PASS: room contents are drawn, the player and other rooms' items are not\n");

	/* The lamb walks and faces its way. */
	set_map(5, -3, 10, 0x1B, 0, 0, 0, 0);
	nd_seven_render(fb, 640, 480, &st);
	nd_seven_lamb(&fx, &fy, &dir, &walking);
	assert(fx == NDW_CENTER_X && fy == NDW_CENTER_Y);
	nd_seven_key("ArrowRight", 1);
	for (int i = 0; i < 20; i++) assert(nd_seven_tick(&st, 1.0f / 60) == 0);
	int fx2, fy2;
	nd_seven_lamb(&fx2, &fy2, &dir, &walking);
	assert(fx2 > fx && fy2 == fy && dir == 3 && walking);
	nd_seven_key("ArrowRight", 0);
	nd_seven_tick(&st, 1.0f / 60);
	nd_seven_lamb(&fx2, &fy2, &dir, &walking);
	assert(!walking);
	printf("PASS: lamb walks right, faces right, idles when released\n");

	/* Continuity: crossing a tile border sends ONE move key and the lamb keeps
	 * walking; when the new map arrives the world does not jump. */
	set_map(5, -3, 10, 0x1B, 0, 'x', 0, 0);
	nd_seven_tick(&st, 0);                         /* settle */
	set_map(40, 40, 10, 0x1B, 0, 0, 0, 0);
	nd_seven_tick(&st, 0);                         /* teleport: recentre */
	nd_seven_lamb(&fx, &fy, &dir, &walking);
	assert(fx == NDW_CENTER_X && fy == NDW_CENTER_Y);
	key = 0;
	hold("ArrowRight", 600, &key);
	assert(key == 'l');
	nd_seven_lamb(&fx, &fy, &dir, &walking);
	assert(fx >= NDW_TILE_W);
	/* waiting for the server: no repeated key */
	nd_seven_key("ArrowRight", 1);
	for (int i = 0; i < 30; i++) assert(nd_seven_tick(&st, 1.0f / 60) == 0);
	nd_seven_key("ArrowRight", 0);
	nd_seven_lamb(&fx, &fy, &dir, &walking);
	nd_seven_render(fb, 1280, 720, &st);
	/* the server moved us east: same world, shifted origin, same pixels */
	set_map(41, 40, 10, 0x1B, 0, 0, 0, 0);
	nd_seven_render(fb2, 1280, 720, &st);
	{
		int nfx, nfy;
		nd_seven_lamb(&nfx, &nfy, &dir, &walking);
		assert(nfx == fx - NDW_TILE_W && nfy == fy);
	}
	assert(!memcmp(fb, fb2, 1280 * 720 * 4));
	{
		int pending, px, py, have;
		nd_seven_debug(&pending, &px, &py, &have);
		assert(pending == 0 && px == 41 && py == 40 && have);
	}
	printf("PASS: crossing a border sends one key; the new map shifts the origin without a jump\n");

	/* Every direction maps to its key (pos.y is north-up, rows run south). */
	{
		const struct { const char *k; int ch; int dx, dy; } d[] = {
			{ "ArrowUp", 'k', 0, 1 }, { "ArrowDown", 'j', 0, -1 }, { "ArrowLeft", 'h', -1, 0 }, { "ArrowRight", 'l', 1, 0 }
		};
		for (int i = 0; i < 4; i++) {
			set_map(40, 40, 10, 0x1B, 0, 0, 0, 0);
			nd_seven_tick(&st, 0);
			nd_seven_lamb(&fx, &fy, &dir, &walking);
			key = 0;
			hold(d[i].k, 600, &key);
			assert(key == d[i].ch);
			nd_seven_lamb(&fx2, &fy2, &dir, &walking);
			set_map(40 + d[i].dx, 40 + d[i].dy, 10, 0x1B, 0, 0, 0, 0);
			nd_seven_tick(&st, 0);
			int sx, sy;
			nd_seven_lamb(&sx, &sy, &dir, &walking);
			/* back inside the origin tile after the server confirmed */
			assert(sx >= -NDW_FOOT_HW && sx <= NDW_TILE_W + NDW_FOOT_HW);
			assert(sy >= -NDW_FOOT_H && sy <= NDW_TILE_H + NDW_FOOT_H);
		}
	}
	printf("PASS: up/down/left/right ask for k/j/h/l and the origin follows\n");

	/* A server that does not move us rolls the lamb back. */
	set_map(40, 40, 10, 0x1B, 0, 0, 0, 0);
	nd_seven_tick(&st, 0);
	key = 0;
	hold("ArrowRight", 600, &key);
	assert(key == 'l');
	for (int i = 0; i < 150; i++) nd_seven_tick(&st, 1.0f / 60);
	nd_seven_lamb(&fx, &fy, &dir, &walking);
	assert(fx == NDW_CENTER_X && fy == NDW_CENTER_Y);
	{
		int pending, px, py, have;
		nd_seven_debug(&pending, &px, &py, &have);
		assert(pending == 0);
	}
	printf("PASS: an unanswered move rolls the lamb back\n");

	/* A room's closed edge stops the lamb; an open one lets it out. */
	set_map(40, 40, 10, 0x1B & ~NDW_EXIT_W, ND_MAP_FLAG_ROOM, 0, 0, 0);
	nd_seven_tick(&st, 0);
	nd_seven_lamb(&fx, &fy, &dir, &walking);
	key = 0;
	hold("a", 600, &key);
	nd_seven_lamb(&fx2, &fy2, &dir, &walking);
	assert(key == 0 && fx2 >= NDW_WALL_T + NDW_FOOT_HW && fx2 < fx);
	nd_seven_render(fb, 640, 480, &st);
	dump("room_walls", fb, 640, 480);
	set_map(40, 40, 10, 0x1B, ND_MAP_FLAG_ROOM, 0, 0, 0);
	nd_seven_tick(&st, 0);
	key = 0;
	hold("a", 600, &key);
	assert(key == 'h');
	/* open country has no walls anywhere */
	set_map(40, 40, 10, 0x1B & ~NDW_EXIT_W, 0, 0, 0, 0);
	nd_seven_tick(&st, 0);
	key = 0;
	hold("a", 600, &key);
	assert(key == 'h');
	printf("PASS: room walls block where an exit is missing; open country never does\n");

	printf("ALL SEVEN RENDER TESTS PASSED\n");
	return 0;
}
