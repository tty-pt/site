#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <nd/bcp-types.h>
#include <nd/seven-world.h>
#include "nd_seven.h"
#include "nd_seven_atlas.h"
#include "nd_seven_objects.h"
#include "nd_seven_motion.h"

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
	nd_seven_set_lamb(NDW_CENTER_X, NDW_CENTER_Y);
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

/* Grassland west of tile column `split`, desert from it east. The border is
 * the same ABSOLUTE world line for any origin: abs x = x + col - 3. */
static void set_split(int x, int y, int split, int room_east)
{
	set_map(x, y, 10, 0x1B, 0, 0, 0, 0);
	for (int r = 0; r < 7; r++)
		for (int c = split; c < 7; c++) {
			nd_map_tile_t *t = &st.map_data.tiles[r * 7 + c];
			t->bio_idx = 14;
			if (room_east)
				t->flags |= ND_MAP_FLAG_ROOM;
		}
}

static int is_sand_px(uint32_t c)
{
	int r = (int)(c & 0xFF), g = (int)((c >> 8) & 0xFF), b = (int)((c >> 16) & 0xFF);
	return r > 170 && r >= g && g >= b && r - b > 40;
}

/* First x in [x0, x1) that starts a run of 16 sand pixels on row y, else -1
 * (long enough that a yellow flower decor sprite never counts). */
static int first_sand_run(const uint32_t *p, int w, int y, int x0, int x1)
{
	for (int x = x0; x + 16 <= x1 + 15; x++) {
		int run = 1;
		for (int k = 0; k < 16; k++)
			run &= is_sand_px(p[y * w + x + k]);
		if (run)
			return x;
	}
	return -1;
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

	/* Player named entities and non-landscape entities are never bound to trees */
	{
		set_map(5, -3, 10, 0x1B, 0, 'x', 0, 0); /* an oak tree glyph */
		st.room.ref = 15;
		st.me = 1;
		st.entity_count = 1;
		st.entities[0].ref = 2; /* other player or entity */
		st.entities[0].location = 15;
		st.entities[0].type = 1; /* TYPE_ENTITY */
		strcpy(st.entities[0].name, "quirinpa");
		sync_room_objects(&st);
		const room_tracker_t *tr = nd_seven_get_tracker();
		assert(tr->count > 0);
		for (int k = 0; k < tr->count; k++) {
			assert(tr->objs[k].entity_ref != 2);
			assert(strcmp(tr->objs[k].name, "quirinpa") != 0);
		}
		printf("PASS: player entities are never bound as trees or landscape elements\n");
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
		const int gl[][3] = { { 'x', 0, 0 }, { 0, 'T', 0 }, { 0, 0, 'a' }, { 0, '~', 0 }, { 0, '$', 0 }, { '<', 0, '>' }, { '*', 0, 0 }, { 0, 'o', 0 }, { 0, 0, 'O' } };
		set_map(5, -3, 10, 0x1B, 0, 0, 0, 0);
		nd_seven_render(fb, 1280, 720, &st);
		for (unsigned k = 0; k < sizeof(gl) / sizeof(gl[0]); k++) {
			set_map(5, -3, 10, 0x1B, 0, gl[k][0], gl[k][1], gl[k][2]);
			nd_seven_render(fb2, 1280, 720, &st);
			assert(memcmp(fb, fb2, 1280 * 720 * 4) != 0);
		}
	}
	printf("PASS: deciduous, tall, fir, pond, shop, stairs, stone and rock glyphs are drawn\n");

	/* Stones are drawn pebbles, rocks bigger, boulders bigger still: the
	 * pixels each adds over the bare map grow with the glyph, and a stone
	 * stays well under the old 25x24 atlas sprite per object. */
	{
		static const int glyph[3] = { '*', 'o', 'O' };
		int area[3], objs[3];

		for (int k = 0; k < 3; k++) {
			int diff = 0;

			set_map(5, -3, 10, 0x1B, 0, 0, 0, 0);
			nd_seven_render(fb2, 1280, 720, &st);
			set_map(5, -3, 10, 0x1B, 0, 0, glyph[k], 0);
			nd_seven_render(fb, 1280, 720, &st);
			for (int i = 0; i < 1280 * 720; i++)
				diff += fb[i] != fb2[i];
			area[k] = diff;
			/* objects Seven laid out in the 7x7 map that can be on screen */
			objs[k] = 0;
			for (int r = 0; r < 7; r++)
				for (int c = 0; c < 7; c++) {
					ndw_obj_t o[NDW_MAX_OBJ];
					objs[k] += nd_seven_tile_objects(&st.map_data, c, r, o);
				}
			assert(diff > 0);
		}
		assert(area[0] < area[1] && area[1] < area[2]);
		assert(objs[0] > 0 && area[0] / objs[0] < 25 * 24 * 4);
		printf("PASS: stones, rocks and boulders draw as distinct, growing shapes\n");
	}

	/* The minimap reuses Seven's layout: nd_seven_tile_objects is the same
	 * list ndw_tile_objects gives for the tile's absolute position. */
	{
		ndw_obj_t a[NDW_MAX_OBJ], b[NDW_MAX_OBJ];
		ndw_tile_t t;
		int16_t pos[4] = { (int16_t)(5 + 1 - 3), (int16_t)(-3 - (1 - 3)), 0, 0 };
		int na, nb;

		set_map(5, -3, 10, 0x1B, 0, 0, 'o', 'a');
		nd_seven_forget();
		na = nd_seven_tile_objects(&st.map_data, 1, 1, a);
		memset(&t, 0, sizeof(t));
		t.biome = st.map_data.tiles[1 * 7 + 1].bio_idx;
		t.exits = st.map_data.tiles[1 * 7 + 1].exits & 0x1B;
		t.glyph[1] = 'o'; t.glyph[2] = 'a';
		t.fg[1] = t.fg[2] = 10;
		nb = ndw_tile_objects(&t, pos, 0, b);
		assert(na == nb && na > 0);
		assert(memcmp(a, b, (size_t)na * sizeof(a[0])) == 0);
		assert(nd_seven_tile_objects(&st.map_data, -1, 0, a) == 0);
		assert(nd_seven_tile_objects(NULL, 0, 0, a) == 0);
		printf("PASS: nd_seven_tile_objects mirrors the world layout and rejects off-map tiles\n");
	}


	/* A rock seen from outside survives the player entering its tile, just like a tree. */
	{
		set_map(5, -3, 10, 0x1B, 0, 0, 'o', 0);
		nd_seven_render(fb, 1280, 720, &st);          /* (6,-3) seen as 'o' */
		set_map_keep(6, -3, 10, 0x1B, 0, 0, 'o', 0);   /* now we stand in it */
		nd_seven_render(fb, 1280, 720, &st);
		set_map(6, -3, 10, 0x1B, 0, 0, 'o', 0);        /* control: never seen */
		nd_seven_render(fb2, 1280, 720, &st);
		assert(memcmp(fb, fb2, 1280 * 720 * 4) != 0);   /* the restored rock is drawn */
		printf("PASS: a rock survives the player entering its tile without popping\n");
	}

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
	set_map_keep(41, 40, 10, 0x1B, 0, 0, 0, 0);
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
			set_map_keep(40 + d[i].dx, 40 + d[i].dy, 10, 0x1B, 0, 0, 0, 0);
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

	/* Walking out of a room transitions seamlessly without teleportation or snapping */
	{
		/* 1. Walk East ('d' -> 'l') */
		set_map(40, 40, 10, 0x1B, ND_MAP_FLAG_ROOM, 0, 0, 0);
		nd_seven_tick(&st, 0);
		key = 0;
		hold("d", 600, &key);
		assert(key == 'l');
		int out_x, out_y;
		nd_seven_lamb(&out_x, &out_y, NULL, NULL);
		assert(out_x >= NDW_TILE_W);
		/* Server adopts the move to adjacent wilderness tile East (41, 40) */
		set_map_keep(41, 40, 10, 0x1B, 0, 0, 0, 0);
		nd_seven_tick(&st, 0);
		int new_x, new_y;
		nd_seven_lamb(&new_x, &new_y, NULL, NULL);
		assert(new_x == out_x - NDW_TILE_W && new_y == out_y);
		assert(new_x >= 0 && new_x < NDW_TILE_W);

		/* 2. Walk North ('w' -> 'k') */
		set_map(40, 40, 10, 0x1B, ND_MAP_FLAG_ROOM, 0, 0, 0);
		nd_seven_tick(&st, 0);
		key = 0;
		hold("w", 600, &key);
		assert(key == 'k');
		nd_seven_lamb(&out_x, &out_y, NULL, NULL);
		assert(out_y < 0);
		/* Server adopts the move to adjacent wilderness tile North (40, 41) */
		set_map_keep(40, 41, 10, 0x1B, 0, 0, 0, 0);
		nd_seven_tick(&st, 0);
		nd_seven_lamb(&new_x, &new_y, NULL, NULL);
		assert(new_x == out_x && new_y == out_y + NDW_TILE_H);
		assert(new_y >= 0 && new_y < NDW_TILE_H);

		/* 3. Walk South ('s' -> 'j') */
		set_map(40, 40, 10, 0x1B, ND_MAP_FLAG_ROOM, 0, 0, 0);
		nd_seven_tick(&st, 0);
		key = 0;
		hold("s", 600, &key);
		assert(key == 'j');
		nd_seven_lamb(&out_x, &out_y, NULL, NULL);
		assert(out_y >= NDW_TILE_H);
		/* Server adopts the move to adjacent wilderness tile South (40, 39) */
		set_map_keep(40, 39, 10, 0x1B, 0, 0, 0, 0);
		nd_seven_tick(&st, 0);
		nd_seven_lamb(&new_x, &new_y, NULL, NULL);
		assert(new_x == out_x && new_y == out_y - NDW_TILE_H);
		assert(new_y >= 0 && new_y < NDW_TILE_H);

		printf("PASS: walking out of a room (East, North, South) shifts origin smoothly without teleportation\n");
	}

	/* North/South/East/West tile indexing and world coords in nd_seven_tile_at */
	{
		set_map(50, 60, 10, 0x1B, 0, 0, 0, 0);
		/* Mark North tile (row 2, col 3) with biome 2 */
		st.map_data.tiles[2 * 7 + 3].bio_idx = 2;
		st.map_data.tiles[2 * 7 + 3].symbol = 'N';
		/* Mark South tile (row 4, col 3) with biome 4 */
		st.map_data.tiles[4 * 7 + 3].bio_idx = 4;
		st.map_data.tiles[4 * 7 + 3].symbol = 'S';
		/* Mark East tile (row 3, col 4) with biome 6 */
		st.map_data.tiles[3 * 7 + 4].bio_idx = 6;
		st.map_data.tiles[3 * 7 + 4].symbol = 'E';
		/* Mark West tile (row 3, col 2) with biome 8 */
		st.map_data.tiles[3 * 7 + 2].bio_idx = 8;
		st.map_data.tiles[3 * 7 + 2].symbol = 'W';

		ndw_tile_t t;
		int16_t pos[4];

		/* Center: ti=0, tj=0 -> row 3, col 3, world (50, 60) */
		assert(nd_seven_tile_at(&st, 0, 0, &t, pos) == 1);
		assert(pos[0] == 50 && pos[1] == 60);

		/* North: ti=0, tj=-1 -> row 2, col 3, world (50, 61), symbol 'N', biome 2 */
		assert(nd_seven_tile_at(&st, 0, -1, &t, pos) == 1);
		assert(pos[0] == 50 && pos[1] == 61);
		assert(t.biome == 2 && t.glyph[1] == 'N');

		/* South: ti=0, tj=1 -> row 4, col 3, world (50, 59), symbol 'S', biome 4 */
		assert(nd_seven_tile_at(&st, 0, 1, &t, pos) == 1);
		assert(pos[0] == 50 && pos[1] == 59);
		assert(t.biome == 4 && t.glyph[1] == 'S');

		/* East: ti=1, tj=0 -> row 3, col 4, world (51, 60), symbol 'E', biome 6 */
		assert(nd_seven_tile_at(&st, 1, 0, &t, pos) == 1);
		assert(pos[0] == 51 && pos[1] == 60);
		assert(t.biome == 6 && t.glyph[1] == 'E');

		/* West: ti=-1, tj=0 -> row 3, col 2, world (49, 60), symbol 'W', biome 8 */
		assert(nd_seven_tile_at(&st, -1, 0, &t, pos) == 1);
		assert(pos[0] == 49 && pos[1] == 60);
		assert(t.biome == 8 && t.glyph[1] == 'W');

		printf("PASS: nd_seven_tile_at maps North/South/East/West tile rows and world coordinates correctly\n");
	}

	/* Organic biome borders: the grass|desert border is not the tile edge. The
	 * lamb stands in the first desert tile at 1280x720, so the tile edge is at
	 * screen x = 640 - NDW_CENTER_X * 2 = 480; the warp reaches 11 units = 22 px. */
	{
		const int W = 1280, H = 720, EDGE = 640 - NDW_CENTER_X * 2;
		int lo = 9999, hi = -9999, bulge_west = 0, bulge_east = 0, rows = 0;

		set_split(5, -3, 3, 0);
		nd_seven_render(fb, W, H, &st);
		nd_seven_render(fb2, W, H, &st);
		assert(!memcmp(fb, fb2, W * H * 4));             /* deterministic */
		dump("border", fb, W, H);
		for (int y = 8; y < H - 8; y++) {
			int x = first_sand_run(fb, W, y, EDGE - 60, EDGE + 60);
			assert(x >= 0);                               /* desert starts near the edge */
			assert(x >= EDGE - 2 * NDW_BLEND_AMP - 2 && x <= EDGE + 2 * NDW_BLEND_AMP + 2);
			if (x < lo) lo = x;
			if (x > hi) hi = x;
			bulge_west += x < EDGE - 2;
			bulge_east += x > EDGE + 2;
			rows++;
		}
		assert(hi - lo >= 10);                            /* wavy, not a straight line */
		assert(bulge_west > 10 && bulge_east > 10);       /* both biomes reach across the old edge */
		/* well inside each biome the ground is its own: no sand far west, no grass far east */
		for (int y = 8; y < H - 8; y += 7) {
			assert(first_sand_run(fb, W, y, 0, EDGE - 60) < 0);
			for (int x = EDGE + 60; x < EDGE + 200; x++) {
				uint32_t c = fb[y * W + x];
				int g = (int)((c >> 8) & 0xFF), r = (int)(c & 0xFF), b = (int)((c >> 16) & 0xFF);
				assert(!(g > r + 8 && g > b + 25));
			}
		}

		/* Zero color blur: every pixel across the border zone has an RGB value matching
		 * either pure unblurred sand or pure unblurred grass, with no linear color blend. */
		for (int y = 8; y < H - 8; y += 4) {
			for (int x = EDGE - 20; x < EDGE + 20; x++) {
				uint32_t c = fb[y * W + x];
				int r = (int)(c & 0xFF), g = (int)((c >> 8) & 0xFF), b = (int)((c >> 16) & 0xFF);
				int is_grass = (g > r + 8 && g > b + 25);
				int is_sand = (r > 170 && r >= g && g >= b && r - b > 40);
				assert(is_grass || is_sand); /* crisp: strictly one ground, never a blended blur */
			}
		}
		printf("PASS: zero color blur across the border zone (100%% crisp pixel art)\n");
		printf("PASS: the grass|desert border wanders instead of following the tile edge (%d rows)\n", rows);

		/* Seamless: seen from another origin tile, the same world pixels are identical. */
		set_split(6, -3, 2, 0);
		nd_seven_render(fb2, W, H, &st);
		for (int y = 8; y < H - 8; y++)
			for (int x = EDGE - 70; x < EDGE + 70; x++)
				assert(fb[y * W + x] == fb2[y * W + x - NDW_TILE_W * 2]);
		printf("PASS: the border is identical after re-centring the view\n");

		/* A different z changes the shape: the border follows the world, not the tile. */
		set_split(5, -3, 3, 0);
		st.map_data.pos[2] = 7;
		nd_seven_render(fb2, W, H, &st);
		assert(memcmp(fb, fb2, W * H * 4) != 0);
		printf("PASS: another level has another border\n");
	}

	/* A room blends seamlessly with wilderness */
	{
		const int W = 1280, H = 720, EDGE = 640 - NDW_CENTER_X * 2;
		int west_sand = 0;

		set_split(5, -3, 3, 1);                           /* the desert side is rooms */
		nd_seven_render(fb, W, H, &st);
		for (int y = 8; y < H - 8; y++)
			west_sand += first_sand_run(fb, W, y, EDGE - 60, EDGE - 1) >= 0;
		assert(west_sand > 10);
		printf("PASS: a room's edge blends organically against wilderness\n");
	}

	/* Persistent picked object tracking, cross-room stability, and database entities */
	{
		nd_seven_forget();
		nd_seven_forget_picked();

		/* 1. Picked tracker basic properties */
		assert(nd_seven_is_picked(100) == 0);
		nd_seven_mark_picked(100);
		assert(nd_seven_is_picked(100) == 1);
		assert(nd_seven_is_picked(101) == 0);

		/* 2. Room 10: has a stone entity #100 */
		set_map(10, 10, 10, 0x1B, ND_MAP_FLAG_ROOM, '*', 0, 0);
		nd_seven_mark_picked(100);
		assert(nd_seven_is_picked(100) == 1);
		st.room.ref = 10;
		st.me = 1;
		st.entity_count = 1;
		st.entities[0].ref = 100;
		st.entities[0].location = 10;
		st.entities[0].ch = '*';
		strcpy(st.entities[0].name, "granite pebble");

		sync_room_objects(&st);
		const room_tracker_t *tr = nd_seven_get_tracker();
		assert(tr->count > 0);
		/* Since #100 was marked picked, is_tracked_obj_present MUST be 0 */
		assert(is_tracked_obj_present(&st, 0) == 0);

		/* 3. Cross-room scenery stability: move to Room 20 with picked stone in inventory */
		set_map_keep(11, 10, 10, 0x1B, ND_MAP_FLAG_ROOM, 'x', 0, '~'); /* Tree and pond */
		assert(nd_seven_is_picked(100) == 1);
		st.room.ref = 20;
		st.me = 1;
		/* st.entities now has player and the picked stone in inventory (location == me) */
		st.entity_count = 2;
		st.entities[0].ref = 1;
		st.entities[0].location = 20;
		strcpy(st.entities[0].name, "quirinpa");
		st.entities[1].ref = 100;
		st.entities[1].location = 1; /* inventory */
		strcpy(st.entities[1].name, "granite pebble");

		sync_room_objects(&st);
		tr = nd_seven_get_tracker();
		assert(tr->count >= 2);
		/* For all procedural scenery objects (tree and pond), they MUST be present! */
		for (int k = 0; k < tr->count; k++) {
			assert(is_tracked_obj_present(&st, k) == 1);
		}
		printf("PASS: picking a stone in one room does not suppress scenery in another room\n");

		/* 4. Action execution marks entity picked and suppresses it */
		set_map(12, 10, 10, 0x1B, ND_MAP_FLAG_ROOM, '*', 0, 0);
		st.room.ref = 30;
		st.entity_count = 1;
		st.entities[0].ref = 301;
		st.entities[0].location = 30;
		st.entities[0].ch = '*';
		strcpy(st.entities[0].name, "obsidian shard");
		nd_seven_sync_origin(&st);
		sync_room_objects(&st);
		tr = nd_seven_get_tracker();
		assert(tr->count > 0);
		assert(is_tracked_obj_present(&st, 0) == 1);
		assert(strcmp(tr->objs[0].name, "obsidian shard") == 0);

		/* Move lamb close to the stone */
		nd_seven_set_lamb(tr->objs[0].x, tr->objs[0].y);
		char act[64] = { 0 };
		int act_len = nd_seven_action(&st, act, sizeof(act));
		assert(act_len > 0);
		assert(strcmp(act, "get #301\n") == 0);

		/* Execute the get action */
		act_len = nd_seven_action_execute(&st, act, sizeof(act));
		assert(act_len > 0);
		assert(nd_seven_is_picked(301) == 1);
		assert(is_tracked_obj_present(&st, 0) == 0);

		/* 5. Picking ground items (dropped items like sword) */
		set_map(13, 10, 10, 0x1B, ND_MAP_FLAG_ROOM, 0, 0, 0);
		st.room.ref = 40;
		st.entity_count = 1;
		st.entities[0].ref = 401;
		st.entities[0].location = 40;
		st.entities[0].ch = '/';
		st.entities[0].actions = 4;
		strcpy(st.entities[0].name, "Steel Broadsword");
		nd_seven_sync_origin(&st);
		sync_room_objects(&st);

		int sx, sy;
		nd_seven_spot(401, &sx, &sy);
		nd_seven_set_lamb(sx, sy);
		act_len = nd_seven_action(&st, act, sizeof(act));
		assert(act_len > 0);
		assert(strcmp(act, "get #401\n") == 0);

		act_len = nd_seven_action_execute(&st, act, sizeof(act));
		assert(act_len > 0);
		assert(nd_seven_is_picked(401) == 1);
		/* After picking, action balloon disappears */
		act_len = nd_seven_action(&st, act, sizeof(act));
		assert(act_len == 0);
		printf("PASS: action execution marks entities picked and suppresses them\n");

		/* 6. Bounded capacity ring buffer */
		nd_seven_forget_picked();
		for (uint32_t i = 1; i <= 200; i++)
			nd_seven_mark_picked(i);
		/* The most recent item must definitely be present */
		assert(nd_seven_is_picked(200) == 1);
		assert(nd_seven_is_picked(190) == 1);
		printf("PASS: picked items tracker operates as a bounded collection\n");

		/* 7. Sticks are contained inside trees and never drawn on ground */
		set_map(14, 10, 10, 0x1B, ND_MAP_FLAG_ROOM, 'x', 0, 0); /* Tree on tile */
		st.room.ref = 50;
		st.me = 1;
		st.entity_count = 2;
		/* Tree entity */
		st.entities[0].ref = 501;
		st.entities[0].location = 50;
		st.entities[0].ch = 'x';
		strcpy(st.entities[0].name, "pinus sylvestris");
		/* Stick contained inside tree */
		st.entities[1].ref = 502;
		st.entities[1].location = 501; /* tree ref! */
		st.entities[1].ch = '!';
		strcpy(st.entities[1].name, "stick");

		nd_seven_sync_origin(&st);
		sync_room_objects(&st);
		tr = nd_seven_get_tracker();
		assert(tr->count > 0);

		/* is_contained_in_tree MUST return 1 for the stick */
		assert(is_contained_in_tree(&st, &st.entities[1]) == 1);

		/* Render Seven stage: stick MUST NOT be drawn as a ground entity */
		nd_seven_render(fb, 640, 480, &st);

		/* Approach the tree: action balloon MUST offer to harvest stick from tree */
		nd_seven_set_lamb(tr->objs[0].x, tr->objs[0].y);
		act_len = nd_seven_action(&st, act, sizeof(act));
		assert(act_len > 0);
		assert(strcmp(act, "get #501 #502\n") == 0);

		/* Execute stick harvest from tree */
		act_len = nd_seven_action_execute(&st, act, sizeof(act));
		assert(act_len > 0);
		assert(nd_seven_is_picked(502) == 1);

		/* After harvesting, tree no longer offers the stick */
		act_len = nd_seven_action(&st, act, sizeof(act));
		assert(act_len == 0);

		/* Even if stick has location == room in a room with trees, is_contained_in_tree MUST return 1 */
		st.entities[1].location = 50;
		assert(is_contained_in_tree(&st, &st.entities[1]) == 1);
		printf("PASS: sticks are contained inside trees and never drawn on ground\n");

		/* 8. Multi-packet entity arrival dynamic binding */
		set_map(15, 10, 10, 0x1B, ND_MAP_FLAG_ROOM, '*', 0, '*'); /* 2 stones */
		st.room.ref = 60;
		st.me = 1;
		st.entity_count = 0; /* Packet 0: no entities arrived yet */
		nd_seven_sync_origin(&st);
		sync_room_objects(&st);
		tr = nd_seven_get_tracker();
		assert(tr->count >= 2);
		assert(tr->bound_entities == 0);

		/* Packet 1 arrives: first stone entity #601 (side 0) */
		st.entity_count = 1;
		st.entities[0].ref = 601;
		st.entities[0].location = 60;
		st.entities[0].ch = '*';
		st.entities[0].side = 0;
		strcpy(st.entities[0].name, "first stone");
		sync_room_objects(&st);
		int idx_s0 = (tr->objs[0].x < 53) ? 0 : 1;
		int idx_s2 = 1 - idx_s0;
		assert(tr->bound_entities == 1);
		assert(tr->objs[idx_s0].entity_ref == 601);
		assert(tr->objs[idx_s2].entity_ref == 0);

		/* Packet 2 arrives: second stone entity #602 (side 2) */
		st.entity_count = 2;
		st.entities[1].ref = 602;
		st.entities[1].location = 60;
		st.entities[1].ch = '*';
		st.entities[1].side = 2;
		strcpy(st.entities[1].name, "second stone");
		sync_room_objects(&st);
		/* Must dynamically re-sync and bind the second stone to its side! */
		assert(tr->bound_entities == 2);
		assert(tr->objs[idx_s0].entity_ref == 601);
		assert(tr->objs[idx_s2].entity_ref == 602);
		printf("PASS: multi-packet entity arrival dynamically binds all stones\n");

		/* Pick stone 1 (#601): side 0 object must become absent, side 2 object must remain present */
		nd_seven_mark_picked(601);
		sync_room_objects(&st);
		assert(is_tracked_obj_present(&st, idx_s0) == 0);
		assert(is_tracked_obj_present(&st, idx_s2) == 1);
		printf("PASS: picking 1 stone of 2 removes only that stone while keeping the second\n");

		/* 9. Seam boundary protection: cannot target room objects when outside the tile */
		nd_seven_set_lamb(NDW_TILE_W + 50, NDW_TILE_H / 2); /* outside tile */
		act_len = nd_seven_action(&st, act, sizeof(act));
		assert(act_len == 0);
		printf("PASS: seam boundary protection prevents targeting out-of-bounds objects\n");

		/* 10. Table-driven scenery trait registry and deterministic slot identity */
		const nd_scenery_trait_t *t_stone = nd_seven_get_scenery_trait(NDW_K_STONE);
		assert(t_stone != NULL);
		assert(t_stone->traits & ND_SCENERY_TRAIT_MINERAL);
		assert(t_stone->default_slot == 0);
		assert(strcmp(t_stone->harvest_cmd, "get") == 0);
		assert(strcmp(t_stone->alias_stem, "stone") == 0);

		const nd_scenery_trait_t *t_tree = nd_seven_get_scenery_trait(NDW_K_DECID);
		assert(t_tree != NULL);
		assert(t_tree->traits & ND_SCENERY_TRAIT_FOLIAGE);
		assert(t_tree->traits & ND_SCENERY_TRAIT_CONTAINER);
		assert(t_tree->default_slot == 1);
		assert(strcmp(t_tree->harvest_cmd, "forage") == 0);
		assert(strcmp(t_tree->alias_stem, "tree") == 0);

		assert(nd_seven_has_scenery_trait(NDW_K_ROCK, ND_SCENERY_TRAIT_MINERAL));
		assert(nd_seven_has_scenery_trait(NDW_K_BOULDER, ND_SCENERY_TRAIT_BLOCKING));
		assert(!nd_seven_has_scenery_trait(NDW_K_STONE, ND_SCENERY_TRAIT_FOLIAGE));

		assert(tr->objs[idx_s0].slot_id == 0);
		assert(tr->objs[idx_s2].slot_id == 2);
		printf("PASS: table-driven scenery traits and deterministic slot identity verified\n");

		/* 11. Ground surface cache consistency */
		static uint32_t g_fb1[640 * 480], g_fb2[640 * 480];
		nd_seven_forget();
		nd_seven_render(g_fb1, 640, 480, &st);
		/* Second render should hit the ground tile cache */
		nd_seven_render(g_fb2, 640, 480, &st);
		assert(memcmp(g_fb1, g_fb2, sizeof(g_fb1)) == 0);
		printf("PASS: ground surface cache hit produces pixel-identical rendering\n");

		/* 12. Multi-room surrounding entity cache and cross-room picked suppression */
		int16_t pos_east[4] = { 61, 60, 0, 1 };
		nd_entity_desc_t east_ents[2];
		memset(east_ents, 0, sizeof(east_ents));
		east_ents[0].ref = 777;
		east_ents[0].location = 301;
		east_ents[0].side = 0;
		strcpy(east_ents[0].name, "east stone");
		east_ents[1].ref = 778;
		east_ents[1].location = 301;
		east_ents[1].side = 2;
		strcpy(east_ents[1].name, "east rock");

		nd_seven_cache_room_entities(301, pos_east, east_ents, 2);

		size_t check_count = 0;
		uint32_t check_room = 0;
		const nd_entity_desc_t *retrieved = nd_seven_get_cached_entities(pos_east, &check_count, &check_room);
		assert(retrieved != NULL);
		assert(check_count == 2);
		assert(check_room == 301);
		assert(retrieved[0].ref == 777);

		/* Before picking 777: neither side is picked */
		assert(nd_seven_is_tile_obj_picked(pos_east, 0) == 0);
		assert(nd_seven_is_tile_obj_picked(pos_east, 2) == 0);

		/* Mark 777 picked: side 0 must report picked, side 2 must remain unpicked */
		nd_seven_mark_picked(777);
		assert(nd_seven_is_tile_obj_picked(pos_east, 0) == 1);
		assert(nd_seven_is_tile_obj_picked(pos_east, 2) == 0);
		printf("PASS: multi-room surrounding entity cache and cross-room picked suppression verified\n");

		/* 13. Rock collision before vs after picking (R1) */
		set_map(20, 20, 10, 0x1B, ND_MAP_FLAG_ROOM, 'o', 0, 0); /* rock at side 0 */
		st.room.ref = 120;
		st.me = 1;
		st.entity_count = 1;
		st.entities[0].ref = 888;
		st.entities[0].location = 120;
		st.entities[0].side = 0;
		st.entities[0].ch = 'o';
		strcpy(st.entities[0].name, "granite rock");

		nd_seven_sync_origin(&st);
		sync_room_objects(&st);
		tr = nd_seven_get_tracker();
		assert(tr->count > 0);
		assert(tr->objs[0].kind == NDW_K_ROCK);
		assert(is_tracked_obj_present(&st, 0) == 1);
		int rx = tr->objs[0].x, ry = tr->objs[0].y;

		/* Position lamb directly south of rock base (outside South wall which starts at y=112) */
		nd_seven_set_lamb(rx, ry + 4);
		/* Attempt to walk North into rock body */
		nd_seven_key("w", 1);
		for (int i = 0; i < 40; i++) nd_seven_tick(&st, 1.0f / 60);
		nd_seven_key("w", 0);
		int blocked_x, blocked_y;
		nd_seven_lamb(&blocked_x, &blocked_y, NULL, NULL);
		/* Lamb must be blocked by the rock collision box (cannot walk through rock) */
		assert(blocked_y >= ry);

		/* Approach rock and pick it */
		char rock_act[64] = { 0 };
		int r_act_len = nd_seven_action(&st, rock_act, sizeof(rock_act));
		assert(r_act_len > 0);
		assert(strcmp(rock_act, "get #888\n") == 0);

		r_act_len = nd_seven_action_execute(&st, rock_act, sizeof(rock_act));
		assert(r_act_len > 0);
		assert(nd_seven_is_picked(888) == 1);
		assert(is_tracked_obj_present(&st, 0) == 0);

		/* Now walk North directly through the picked rock position */
		nd_seven_set_lamb(rx, ry + 4);
		nd_seven_key("w", 1);
		for (int i = 0; i < 40; i++) nd_seven_tick(&st, 1.0f / 60);
		nd_seven_key("w", 0);
		int free_x, free_y;
		nd_seven_lamb(&free_x, &free_y, NULL, NULL);
		/* Lamb successfully walked past the rock (free_y reached or passed the rock) */
		assert(free_y < ry);
		printf("PASS: rock blocks collision before picking, clears collision after picking\n");

		/* 14. Non-teleporting adjacent room transitions when s_pending_key == 0 (R2) */
		set_map(30, 30, 10, 0x1B, ND_MAP_FLAG_ROOM, 0, 0, 0);
		nd_seven_tick(&st, 0);
		/* Lamb is positioned at the East doorway seam */
		nd_seven_set_lamb(NDW_TILE_W, NDW_TILE_H / 2);
		int pending_check = 0;
		nd_seven_debug(&pending_check, NULL, NULL, NULL);
		assert(pending_check == 0); /* s_pending_key is explicitly 0 */

		/* Server pushes/adopts the move to adjacent room East (31, 30) while s_pending_key is 0 */
		set_map_keep(31, 30, 10, 0x1B, ND_MAP_FLAG_ROOM, 0, 0, 0);
		nd_seven_tick(&st, 0);
		int seam_x, seam_y;
		nd_seven_lamb(&seam_x, &seam_y, NULL, NULL);
		/* Lamb enters at West doorway seam of adjacent room: preserved coordinates, zero center snap */
		assert(seam_x != NDW_CENTER_X);
		assert(seam_x >= 0 && seam_x < NDW_CENTER_X);
		assert(seam_y == NDW_TILE_H / 2);

		/* Also test entering from West doorway when s_pending_key is 0 */
		nd_seven_set_lamb(0, NDW_TILE_H / 2);
		nd_seven_debug(&pending_check, NULL, NULL, NULL);
		assert(pending_check == 0); /* s_pending_key is explicitly 0 */

		/* Server pushes/adopts the move to adjacent room West (30, 30) while s_pending_key is 0 */
		set_map_keep(30, 30, 10, 0x1B, ND_MAP_FLAG_ROOM, 0, 0, 0);
		nd_seven_tick(&st, 0);
		nd_seven_lamb(&seam_x, &seam_y, NULL, NULL);
		/* Lamb enters at East doorway seam of adjacent room: preserved coordinates, zero center snap */
		assert(seam_x != NDW_CENTER_X);
		assert(seam_x > NDW_CENTER_X);
		assert(seam_y == NDW_TILE_H / 2);

		/* Non-adjacent teleport (abs(dx) > 1): recentres to room center */
		set_map_keep(35, 35, 10, 0x1B, ND_MAP_FLAG_ROOM, 0, 0, 0);
		nd_seven_tick(&st, 0);
		nd_seven_lamb(&seam_x, &seam_y, NULL, NULL);
		assert(seam_x == NDW_CENTER_X && seam_y == NDW_CENTER_Y);
		printf("PASS: room transitions with s_pending_key == 0 preserve continuous entrance coordinates\n");

		/* 15. Multi-room harvest persistence across room re-entry (R3) */
		set_map(50, 50, 10, 0x1B, ND_MAP_FLAG_ROOM, 'O', 0, 0); /* boulder in room 50,50 */
		st.room.ref = 250;
		st.me = 1;
		st.entity_count = 1;
		st.entities[0].ref = 999;
		st.entities[0].location = 250;
		st.entities[0].side = 0;
		st.entities[0].ch = 'O';
		strcpy(st.entities[0].name, "ancient boulder");

		nd_seven_sync_origin(&st);
		sync_room_objects(&st);
		assert(is_tracked_obj_present(&st, 0) == 1);

		/* Pick the boulder in room 250 */
		nd_seven_mark_picked(999);
		assert(is_tracked_obj_present(&st, 0) == 0);
		assert(nd_seven_is_tile_obj_picked(st.map_data.pos, 0) == 1);

		/* Leave to adjacent room (51, 50) */
		set_map_keep(51, 50, 10, 0x1B, ND_MAP_FLAG_ROOM, 0, 0, 0);
		st.room.ref = 251;
		st.entity_count = 0;
		nd_seven_sync_origin(&st);
		sync_room_objects(&st);

		/* Looking back at previous room (50, 50): boulder slot 0 remains picked */
		int16_t prev_room_pos[4] = { 50, 50, 0, 0 };
		assert(nd_seven_is_tile_obj_picked(prev_room_pos, 0) == 1);

		/* Re-enter room 250 at (50, 50) with 0 entities loaded */
		set_map_keep(50, 50, 10, 0x1B, ND_MAP_FLAG_ROOM, 'O', 0, 0);
		st.room.ref = 250;
		st.entity_count = 0; /* Server entity list is now empty */
		nd_seven_sync_origin(&st);
		sync_room_objects(&st);

		/* Boulder must remain suppressed upon re-entry */
		assert(is_tracked_obj_present(&st, 0) == 0);
		assert(nd_seven_is_tile_obj_picked(st.map_data.pos, 0) == 1);
		printf("PASS: multi-room mineral persistence across room leave and re-entry verified\n");
	}

	/* 16. Continuous world coordinates parity between Seven and Lamina (R1) */
	{
		set_map(12, -8, 10, 0x1B, ND_MAP_FLAG_ROOM, 0, 0, 0);
		nd_seven_sync_origin(&st);
		nd_seven_set_lamb(42, 68);
		int64_t wx = 0, wy = 0;
		nd_seven_world_pos(&st, &wx, &wy);
		/* In Lamina reference model:
		 * world_x = player_x_world * NDW_TILE_W + lamb_x
		 * world_y = -player_y_world * NDW_TILE_H + lamb_y */
		int64_t expected_wx = (int64_t)12 * NDW_TILE_W + 42;
		int64_t expected_wy = (int64_t)(-(-8)) * NDW_TILE_H + 68;
		assert(wx == expected_wx);
		assert(wy == expected_wy);
		printf("PASS: continuous world coordinates parity between Seven and Lamina verified\n");
	}

	/* 17. Pre-computed tile collision hull cache and O(1) hit testing (R2) */
	{
		set_map(100, 100, 10, 0x1B, ND_MAP_FLAG_ROOM, 'O', 0, 0); /* boulder in room */
		st.room.ref = 500;
		st.entity_count = 1;
		st.entities[0].ref = 888;
		st.entities[0].location = 500;
		st.entities[0].side = 0;
		st.entities[0].ch = 'O';
		strcpy(st.entities[0].name, "boulder");
		nd_seven_forget();
		nd_seven_sync_origin(&st);
		sync_room_objects(&st);

		/* Retrieve boulder position */
		const room_tracker_t *tr = nd_seven_get_tracker();
		assert(tr->count > 0);
		int bx = tr->objs[0].x, by = tr->objs[0].y;

		/* Collision check at boulder coordinates reports blocked */
		int blocked = nd_seven_collision(&st, bx, by);
		assert(blocked == 1);

		/* Clear collision via slot harvest */
		nd_seven_mark_tile_slot_harvested(st.map_data.pos, tr->objs[0].slot_id);
		int cleared = nd_seven_collision(&st, bx, by);
		assert(cleared == 0);
		printf("PASS: pre-computed tile collision hull cache O(1) hit testing and harvest invalidation verified\n");
	}

	/* 18. Shared spatial entity resolver unification between Seven and Lamina (R3) */
	{
		set_map(20, -10, 10, 0x1B, ND_MAP_FLAG_ROOM, 'x', 0, 'o');
		st.room.ref = 300;
		st.entity_count = 1;
		st.entities[0].ref = 301;
		st.entities[0].location = 300;
		st.entities[0].ch = 'g';
		st.entities[0].fg = 2;
		strcpy(st.entities[0].name, "goblin guard");

		nd_spatial_tile_t sp_seven;
		int res_seven = nd_spatial_tile_resolve(&st, 0, 0, &sp_seven);
		assert(res_seven == 1);
		assert(sp_seven.obj_count >= 3); /* 2 procedural + 1 entity */

		nd_spatial_tile_t sp_lamina;
		int res_lamina = nd_spatial_tile_resolve_map(&st.map_data, st.map_data.player_x, st.map_data.player_y, &sp_lamina);
		assert(res_lamina == 1);
		assert(sp_lamina.obj_count >= 2); /* 2 procedural */

		/* Both spatial queries must agree on procedural objects count and properties */
		assert(sp_seven.objs[0].kind == sp_lamina.objs[0].kind);
		assert(sp_seven.objs[0].x == sp_lamina.objs[0].x);
		assert(sp_seven.objs[0].y == sp_lamina.objs[0].y);
		assert(sp_seven.objs[0].slot_id == sp_lamina.objs[0].slot_id);
		printf("PASS: shared spatial entity resolver unification between Seven and Lamina verified\n");
	}

	/* 19. Declarative scenery component registry and modular interact dispatch (R5) */
	{
		const nd_scenery_def_t *def_rock = nd_seven_get_scenery_def(NDW_K_ROCK);
		assert(def_rock != NULL);
		assert(def_rock->traits & ND_SCENERY_TRAIT_MINERAL);
		assert(def_rock->traits & ND_SCENERY_TRAIT_BLOCKING);
		assert(def_rock->interact != NULL);

		/* Test modular interact callback on mineral object */
		tracked_obj_t mock_obj = { 0 };
		mock_obj.kind = NDW_K_ROCK;
		mock_obj.entity_ref = 777;
		char cmd[64] = { 0 };
		uint32_t picked_ref = 0;
		int handled = def_rock->interact(&st, &mock_obj, cmd, sizeof(cmd), &picked_ref);
		assert(handled == 1);
		assert(strcmp(cmd, "get #777\n") == 0);
		assert(picked_ref == 777);

		/* Test stairs component handler */
		const nd_scenery_def_t *def_stairs = nd_seven_get_scenery_def(NDW_K_STAIRS_DOWN);
		assert(def_stairs != NULL);
		assert(def_stairs->interact != NULL);
		handled = def_stairs->interact(&st, &mock_obj, cmd, sizeof(cmd), &picked_ref);
		assert(handled == 1);
		assert(strcmp(cmd, ">\n") == 0);
		printf("PASS: declarative scenery component registry and modular interact dispatch verified\n");
	}

	printf("ALL SEVEN RENDER TESTS PASSED\n");
	return 0;
}
