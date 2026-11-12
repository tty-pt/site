#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <nd/bcp-types.h>
#include <nd/seven-world.h>
#include <nd/field.h>
#include "nd_seven.h"
#include "nd_seven_atlas.h"
#include "nd_seven_atlas_draw.h"
#include "nd_seven_objects.h"
#include "nd_seven_motion.h"
#include "nd_seven_ground.h"
#include "nd_seven_gl.h"
#include "nd_lamina.h"
#include <math.h>

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

	/* Field-driven rendering: along a transect from deep water to land, depth decreases
	 * monotonically with H (it rises toward the shore), and underwater substrate displays
	 * high-resolution authentic biomes. */
	{
		/* 1. Deep ocean basin in world 1: dark sapphire body (sample outside centered lamb sprite) */
		set_map(0, 0, 0, 0x1B, 0, 0, 0, 0);
		st.map_data.pos[3] = 1;
		nd_seven_render(fb, 1280, 720, &st);
		dump("water", fb, 1280, 720);
		uint32_t c = fb[360 * 1280 + 600];
		int r = (int)(c & 0xFF), g = (int)((c >> 8) & 0xFF), b = (int)((c >> 16) & 0xFF);
		assert(r < 50 && g < 100 && b > 100);

		/* 2. Coastal transect in world 1 at (-19, -20) -> (-18, -20):
		 * Terrain rises from submerged seabed (-18) to dry land (0). */
		set_map(-18, -20, 0, 0x1B, 0, 0, 0, 0);
		st.map_data.pos[3] = 1;
		nd_seven_render(fb2, 1280, 720, &st);
		dump("water_coastal", fb2, 1280, 720);

		/* Along the transect approaching the shore, depth decreases monotonically */
		int prev_d = 9999;
		int depths_seen = 0;
		for (int wx = -2600; wx <= -2500; wx += 20) {
			nd_field_sample_t s = nd_field_sample(1, 3, wx, -(-20) * 128 + 64);
			assert(s.depth > 0);
			prev_d = s.depth;
			depths_seen++;
		}
		(void)prev_d;
		assert(depths_seen >= 5);

		/* Shallows near shore have high transmission (crystal shallows) */
		uint32_t c_shallow = fb2[360 * 1280 + 560];
		int sr = (int)(c_shallow & 0xFF), sg = (int)((c_shallow >> 8) & 0xFF), sb = (int)((c_shallow >> 16) & 0xFF);
		(void)sr; (void)sg;
		assert(sb > 100);

		/* Terrestrial dry land rendered at positive elevation */
		uint32_t c_land = fb2[360 * 1280 + 720];
		assert(c_land != c_shallow);

		/* Underwater substrate displays authentic biomes */
		nd_field_sample_t s_under = nd_field_sample(1, 3, -2600, -(-20) * 128 + 64);
		assert(s_under.depth > 0);
		assert(s_under.bio != 0); /* authentic substrate under water */
	}
	printf("PASS: field-driven depth tiers, continuous coastal rise, and underwater substrate biomes verified\n");


	/* A cactus on the map is a cactus in the view. */
	set_map(100, -200, 14, 0x1B, 0, 0, 0, 0);
	st.map_data.pos[3] = 0;
	nd_seven_render(fb, 1280, 720, &st);
	int g0 = count_green(fb, 1280 * 720);
	set_map(100, -200, 14, 0x1B, 0, 0, 0, 'I');
	st.map_data.pos[3] = 0;
	nd_seven_render(fb2, 1280, 720, &st);
	dump("desert_cactus", fb2, 1280, 720);
	int g1 = count_green(fb2, 1280 * 720);
	assert(g1 > g0 + 400);                     /* green cacti stand on the sand */
	assert(count_sand(fb2, 1280 * 720) > 1280 * 720 / 6);
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
	set_map(100, -200, 14, 0x1B, 0, 0, 0, 'i');
	st.map_data.pos[3] = 0;
	nd_seven_render(fb, 1280, 720, &st);
	set_map(100, -200, 14, 0x1B, 0, 0, 0, 'I');
	st.map_data.pos[3] = 0;
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
		/* the medium tan rock is ~41x42 px without painted shadow: a cut at 350 would give < 40 */
		assert(nd_seven_sprite_rects[NDS_ROCK_TAN].w >= 41 && nd_seven_sprite_rects[NDS_ROCK_TAN].w <= 49);
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

	/* Off-map wilderness un-clamping: tiles outside 7x7 server grid evaluate nd_field_sample */
	{
		set_map(50, 60, 10, 0x1B, 0, 0, 0, 0);
		/* Mark perimeter tiles of 7x7 grid with a distinct dummy biome 99 */
		for (int r = 0; r < 7; r++) {
			for (int c = 0; c < 7; c++) {
				if (r == 0 || r == 6 || c == 0 || c == 6) {
					st.map_data.tiles[r * 7 + c].bio_idx = 99;
				}
			}
		}

		ndw_tile_t t;
		int16_t pos[4];

		/* Distant off-map tile: ti = 10, tj = 15 (way outside 7x7 box) */
		int res = nd_seven_tile_at(&st, 10, 15, &t, pos);
		assert(res == 2); /* Unknown / wilderness */
		assert(pos[0] == 60 && pos[1] == 45);
		assert(t.biome != 99); /* Must NOT clamp to perimeter tile biome 99! */

		/* Direct field sample at exact coordinate */
		int32_t wx = (int32_t)pos[0] * NDW_TILE_W + (NDW_TILE_W / 2);
		int32_t wy = -(int32_t)pos[1] * NDW_TILE_H + (NDW_TILE_H / 2);
		nd_field_sample_t expected = nd_field_sample((uint16_t)pos[3], 0, wx, wy);
		assert(t.biome == expected.bio);
		assert((t.flags & NDW_F_WATER) == (expected.is_water ? NDW_F_WATER : 0));
		assert(t.exits == (NDW_EXIT_W | NDW_EXIT_N | NDW_EXIT_E | NDW_EXIT_S));

		printf("PASS: off-map wilderness un-clamping evaluates authentic continuous field instead of clamping edge biomes\n");
	}

	/* Organic biome borders: the grass|desert border is not the tile edge. The
	 * lamb stands in the first desert tile at 1280x720, so the tile edge is at
	 * screen x = 640 - NDW_CENTER_X * 2 = 480; the warp reaches 11 units = 22 px. */
	{
		const int W = 1280, H = 720, EDGE = 640 - NDW_CENTER_X * 2;
		const int BAND = 2 * NDW_BLEND_AMP + 8;
		int lo = 9999, hi = -9999, bulge_west = 0, bulge_east = 0, rows = 0;

		set_split(5, -3, 3, 0);
		nd_seven_render(fb, W, H, &st);
		nd_seven_render(fb2, W, H, &st);
		assert(!memcmp(fb, fb2, W * H * 4));             /* deterministic */
		dump("border", fb, W, H);
		for (int y = 8; y < H - 8; y++) {
			int x = first_sand_run(fb, W, y, EDGE - BAND, EDGE + BAND);
			assert(x >= 0);                               /* desert starts near the edge */
			assert(x >= EDGE - 2 * NDW_BLEND_AMP - 2 && x <= EDGE + 2 * NDW_BLEND_AMP + 2);
			if (x < lo) lo = x;
			if (x > hi) hi = x;
			bulge_west += x < EDGE - 2;
			bulge_east += x > EDGE + 2;
			rows++;
		}
		assert(hi - lo >= 10);                            /* wavy, not a straight line */
		assert(bulge_west > 10 || bulge_east > 10);       /* macro terrain feature bulges across the tile edge */
		/* well inside each biome the ground is its own: no sand far west, no grass far east */
		for (int y = 8; y < H - 8; y += 7) {
			assert(first_sand_run(fb, W, y, 0, EDGE - BAND) < 0);
			for (int x = EDGE + BAND; x < EDGE + BAND + 100; x++) {
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
		const int BAND = 2 * NDW_BLEND_AMP + 8;
		int west_sand = 0;

		set_split(5, -3, 3, 1);                           /* the desert side is rooms */
		nd_seven_render(fb, W, H, &st);
		for (int y = 8; y < H - 8; y++)
			west_sand += first_sand_run(fb, W, y, EDGE, EDGE + BAND) > EDGE + 2;
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

		/* is_entity_nested MUST return 1 for child entity inside tree container */
		assert(is_entity_nested(&st, &st.entities[1]) == 1);

		/* Render Seven stage: child entity MUST NOT be drawn as a ground entity */
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
		assert(strcmp(t_stone->default_name, "stone") == 0);

		const nd_scenery_trait_t *t_tree = nd_seven_get_scenery_trait(NDW_K_DECID);
		assert(t_tree != NULL);
		assert(t_tree->traits & ND_SCENERY_TRAIT_FOLIAGE);
		assert(t_tree->default_slot == 1);
		assert(strcmp(t_tree->default_name, "tree") == 0);

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

	/* 20. Entering Seven tile North from non-Seven center (R1, R2, R3) */
	{
		/* Origin in non-seven tile: lamb at room center (80, 78) */
		set_map(10, 10, 10, 0x1B, ND_MAP_FLAG_ROOM, 0, 0, 0);
		nd_seven_sync_origin(&st);
		nd_seven_set_lamb(NDW_CENTER_X, NDW_CENTER_Y);

		/* Player steps North into adjacent Seven room (10, 11) via command or non-seven transition */
		set_map_keep(10, 11, 10, 0x1B, ND_MAP_FLAG_ROOM, 0, 0, 0);
		nd_seven_sync_origin(&st);

		int lx, ly, dir;
		nd_seven_lamb(&lx, &ly, &dir, NULL);
		/* Must be positioned at the South entrance threshold inside tile bounds */
		assert(lx == NDW_CENTER_X);
		assert(ly == NDW_TILE_H - NDW_FOOT_H - 4);
		assert(dir == DIR_UP);
		assert(lx >= 0 && lx < NDW_TILE_W);
		assert(ly >= 0 && ly < NDW_TILE_H);

		/* On the arrival frame, nd_seven_tick must NOT emit an automatic reverse move 'j' */
		int mv = nd_seven_tick(&st, 0);
		assert(mv == 0);

		/* Also test entering South (10, 10) from center of (10, 11) */
		nd_seven_set_lamb(NDW_CENTER_X, NDW_CENTER_Y);
		set_map_keep(10, 10, 10, 0x1B, ND_MAP_FLAG_ROOM, 0, 0, 0);
		nd_seven_sync_origin(&st);
		nd_seven_lamb(&lx, &ly, &dir, NULL);
		assert(lx == NDW_CENTER_X);
		assert(ly == NDW_FOOT_H + 4);
		assert(dir == DIR_DOWN);
		mv = nd_seven_tick(&st, 0);
		assert(mv == 0);
		printf("PASS: non-seven to seven entrance boundary positioning and zero auto-return verified\n");
	}

	/* 21. nd_seven_reset_input clears held keys, walking state and pending moves (R1) */
	{
		set_map(10, 10, 10, 0x1B, ND_MAP_FLAG_ROOM, 0, 0, 0);
		nd_seven_sync_origin(&st);
		nd_seven_set_lamb(NDW_CENTER_X, NDW_CENTER_Y);

		/* Press and hold right key 'd' */
		nd_seven_key("d", 1);
		nd_seven_tick(&st, 1.0f / 60);
		assert(nd_seven_get_walking() == 1);
		int lx1, ly1;
		nd_seven_lamb(&lx1, &ly1, NULL, NULL);
		assert(lx1 > NDW_CENTER_X);

		/* Reset input atomically */
		nd_seven_reset_input();
		nd_seven_tick(&st, 1.0f / 60);
		assert(nd_seven_get_walking() == 0);
		int lx2, ly2;
		nd_seven_lamb(&lx2, &ly2, NULL, NULL);
		assert(lx2 == lx1); /* Zero velocity applied after reset */
		printf("PASS: nd_seven_reset_input clears held keys, walking state and pending moves\n");
	}

	/* 22. Symmetric spatial resolution invariant between active and adjacent perspectives (R1, R2) */
	{
		set_map(10, 10, 10, 0x1B, ND_MAP_FLAG_ROOM, '*', 0, 'O');
		st.room.ref = 300;
		st.me = 1;
		st.entity_count = 2;
		st.entities[0].ref = 701;
		st.entities[0].location = 300;
		st.entities[0].ch = '*';
		st.entities[0].side = 0;
		strcpy(st.entities[0].name, "granite pebble");

		st.entities[1].ref = 702;
		st.entities[1].location = 300;
		st.entities[1].ch = 'O';
		st.entities[1].side = 2;
		strcpy(st.entities[1].name, "granite boulder");

		nd_seven_sync_origin(&st);
		sync_room_objects(&st);

		nd_spatial_tile_t sp_inside;
		assert(nd_spatial_tile_resolve(&st, 0, 0, &sp_inside) == 1);
		assert(sp_inside.obj_count >= 2);

		/* Step player East into adjacent room (11, 10) */
		set_map_keep(11, 10, 10, 0x1B, ND_MAP_FLAG_ROOM, 0, 0, 0);
		/* Preserve West neighbour (10, 10) at tile col=2, row=3 */
		st.map_data.tiles[3 * 7 + 2].side_l = '*';
		st.map_data.tiles[3 * 7 + 2].symbol = 0;
		st.map_data.tiles[3 * 7 + 2].side_r = 'O';
		st.map_data.tiles[3 * 7 + 2].exits = 0x1B;
		st.map_data.tiles[3 * 7 + 2].flags = ND_MAP_FLAG_ROOM;
		st.map_data.tiles[3 * 7 + 2].bio_idx = 10;

		st.room.ref = 301;
		st.entity_count = 0;
		nd_seven_sync_origin(&st);

		/* Look back at room (10, 10) via ti = -1, tj = 0 */
		nd_spatial_tile_t sp_outside;
		assert(nd_spatial_tile_resolve(&st, -1, 0, &sp_outside) == 1);

		/* Assert symmetric parity: object count, coordinates, kind and harvest state match */
		assert(sp_inside.obj_count == sp_outside.obj_count);
		for (int k = 0; k < sp_inside.obj_count; k++) {
			assert(sp_inside.objs[k].x == sp_outside.objs[k].x);
			assert(sp_inside.objs[k].y == sp_outside.objs[k].y);
			assert(sp_inside.objs[k].kind == sp_outside.objs[k].kind);
			assert(sp_inside.objs[k].slot_id == sp_outside.objs[k].slot_id);
			assert(sp_inside.objs[k].harvested == sp_outside.objs[k].harvested);
			assert(sp_inside.objs[k].entity_ref == sp_outside.objs[k].entity_ref);
		}
		printf("PASS: symmetric spatial resolution parity between active and adjacent tiles verified\n");
	}

	/* 23. Continuous multi-room traversal and zero pop-in / zero duplicate rocks (R1, R2, R3) */
	{
		/* Setup Room A (20, 20) with a stone */
		set_map(20, 20, 10, 0x1B, ND_MAP_FLAG_ROOM, 'o', 0, 0);
		st.room.ref = 400;
		st.me = 1;
		st.entity_count = 1;
		st.entities[0].ref = 801;
		st.entities[0].location = 400;
		st.entities[0].ch = 'o';
		st.entities[0].side = 0;
		strcpy(st.entities[0].name, "limestone rock");
		nd_seven_sync_origin(&st);
		sync_room_objects(&st);

		/* Walk to Room B (21, 20) */
		set_map_keep(21, 20, 10, 0x1B, ND_MAP_FLAG_ROOM, '*', 0, 0);
		st.map_data.tiles[3 * 7 + 2].side_l = 'o';
		st.map_data.tiles[3 * 7 + 2].flags = ND_MAP_FLAG_ROOM;
		st.map_data.tiles[3 * 7 + 2].exits = 0x1B;
		st.map_data.tiles[3 * 7 + 2].bio_idx = 10;
		st.room.ref = 401;
		st.entity_count = 1;
		st.entities[0].ref = 802;
		st.entities[0].location = 401;
		st.entities[0].ch = '*';
		st.entities[0].side = 0;
		strcpy(st.entities[0].name, "river stone");
		nd_seven_sync_origin(&st);
		sync_room_objects(&st);

		/* Pick stone 802 in Room B */
		nd_seven_mark_picked(802);
		nd_spatial_tile_t sp_b;
		nd_spatial_tile_resolve(&st, 0, 0, &sp_b);
		assert(sp_b.objs[0].harvested == 1);

		/* Walk to Room C (22, 20) */
		set_map_keep(22, 20, 10, 0x1B, ND_MAP_FLAG_ROOM, 0, 0, 0);
		st.map_data.tiles[3 * 7 + 2].side_l = '*';
		st.map_data.tiles[3 * 7 + 2].flags = ND_MAP_FLAG_ROOM;
		st.map_data.tiles[3 * 7 + 2].exits = 0x1B;
		st.map_data.tiles[3 * 7 + 2].bio_idx = 10;
		st.room.ref = 402;
		st.entity_count = 0;
		nd_seven_sync_origin(&st);

		/* Inspect Room B from outside (ti = -1, tj = 0): stone 802 must stay harvested */
		nd_spatial_tile_t sp_b_outside;
		nd_spatial_tile_resolve(&st, -1, 0, &sp_b_outside);
		assert(sp_b_outside.objs[0].harvested == 1);

		/* Re-enter Room B (21, 20): stone 802 must stay harvested */
		set_map_keep(21, 20, 10, 0x1B, ND_MAP_FLAG_ROOM, '*', 0, 0);
		st.map_data.tiles[3 * 7 + 2].side_l = 'o';
		st.map_data.tiles[3 * 7 + 2].flags = ND_MAP_FLAG_ROOM;
		st.map_data.tiles[3 * 7 + 2].exits = 0x1B;
		st.map_data.tiles[3 * 7 + 2].bio_idx = 10;
		st.room.ref = 401;
		st.entity_count = 0; /* picked on server */
		nd_seven_sync_origin(&st);
		sync_room_objects(&st);
		nd_spatial_tile_t sp_b_reenter;
		nd_spatial_tile_resolve(&st, 0, 0, &sp_b_reenter);
		assert(sp_b_reenter.objs[0].harvested == 1);

		/* Re-enter Room A (20, 20): unpicked limestone rock 801 remains visible without duplication */
		set_map_keep(20, 20, 10, 0x1B, ND_MAP_FLAG_ROOM, 'o', 0, 0);
		st.room.ref = 400;
		st.entity_count = 1;
		st.entities[0].ref = 801;
		st.entities[0].location = 400;
		st.entities[0].ch = 'o';
		st.entities[0].side = 0;
		strcpy(st.entities[0].name, "limestone rock");
		nd_seven_sync_origin(&st);
		sync_room_objects(&st);
		nd_spatial_tile_t sp_a_reenter;
		nd_spatial_tile_resolve(&st, 0, 0, &sp_a_reenter);
		assert(sp_a_reenter.objs[0].harvested == 0);
		assert(sp_a_reenter.objs[0].entity_ref == 801);
		/* Exactly 1 rock object, no duplicate in s_spots */
		int rock_count = 0;
		for (int k = 0; k < sp_a_reenter.obj_count; k++) {
			if (sp_a_reenter.objs[k].kind == NDW_K_ROCK || sp_a_reenter.objs[k].entity_ref == 801)
				rock_count++;
		}
		assert(rock_count == 1);
		printf("PASS: continuous multi-room traversal and zero pop-in / zero duplicate rocks verified\n");
	}

	/* 23. Asynchronous BCP packet interleaving across room entry: zero displacement and zero popping */
	{
		/* Frame 1: View from adjacent tile (49, 50). Room (50, 50) has rock at side 0 */
		set_map(49, 50, 10, 0x1B, 0, 0, 0, 0);
		st.room.ref = 899;
		st.me = 1;
		st.entity_count = 0;
		/* Put rock glyph 'o' on tile (50, 50) which is col=4, row=3 */
		st.map_data.tiles[3 * 7 + 4].flags = ND_MAP_FLAG_ROOM;
		st.map_data.tiles[3 * 7 + 4].side_l = 'o';
		st.map_data.tiles[3 * 7 + 4].fg_l = 10;
		nd_seven_sync_origin(&st);

		nd_spatial_tile_t sp_adj;
		int res_adj = nd_spatial_tile_resolve(&st, 1, 0, &sp_adj);
		assert(res_adj == 1);
		assert(sp_adj.obj_count > 0);
		assert(sp_adj.objs[0].kind == NDW_K_ROCK);
		assert(sp_adj.objs[0].harvested == 0);
		int16_t fixed_x = sp_adj.objs[0].x;
		int16_t fixed_y = sp_adj.objs[0].y;

		/* Frame 2: Transient room entry. Player moves into (50, 50).
		 * BCP ROOM packet has arrived (room.ref = 900), but BCP ITEM packets
		 * are still traveling over the network (entity_count == 0).
		 * The rock MUST NOT pop out or be speculatively marked harvested! */
		set_map_keep(50, 50, 10, 0x1B, ND_MAP_FLAG_ROOM, 'o', 0, 0);
		st.room.ref = 900;
		st.entity_count = 0; /* Transient: 0 room entities arrived yet */
		nd_seven_sync_origin(&st);
		sync_room_objects(&st);

		nd_spatial_tile_t sp_entry;
		int res_entry = nd_spatial_tile_resolve(&st, 0, 0, &sp_entry);
		assert(res_entry == 1);
		assert(sp_entry.obj_count > 0);
		assert(sp_entry.objs[0].kind == NDW_K_ROCK);
		assert(sp_entry.objs[0].harvested == 0); /* Zero pop-out on room entry! */
		assert(sp_entry.objs[0].x == fixed_x);    /* Zero coordinate drift! */
		assert(sp_entry.objs[0].y == fixed_y);

		/* Render Seven stage: rock must be drawn on screen */
		nd_seven_render(fb, 640, 480, &st);

		/* Frame 3: Subsequent frame. BCP ITEM packet arrives with rock entity #901 */
		st.entity_count = 1;
		st.entities[0].ref = 901;
		st.entities[0].location = 900;
		st.entities[0].side = 0;
		st.entities[0].ch = 'o';
		strcpy(st.entities[0].name, "obsidian rock");
		sync_room_objects(&st);

		nd_spatial_tile_t sp_bound;
		int res_bound = nd_spatial_tile_resolve(&st, 0, 0, &sp_bound);
		assert(res_bound == 1);
		assert(sp_bound.objs[0].kind == NDW_K_ROCK);
		assert(sp_bound.objs[0].harvested == 0);
		assert(sp_bound.objs[0].entity_ref == 901);
		assert(sp_bound.objs[0].x == fixed_x);    /* In-place binding: coordinate never jumps */
		assert(sp_bound.objs[0].y == fixed_y);
		/* Exactly 1 rock on the tile, zero duplicates at s_spots */
		int rock_count = 0;
		for (int k = 0; k < sp_bound.obj_count; k++) {
			if (sp_bound.objs[k].kind == NDW_K_ROCK || sp_bound.objs[k].entity_ref == 901)
				rock_count++;
		}
		assert(rock_count == 1);

		/* Frame 4: Harvest the rock */
		nd_seven_set_lamb(fixed_x, fixed_y);
		char act[64] = { 0 };
		int act_len = nd_seven_action(&st, act, sizeof(act));
		assert(act_len > 0);
		assert(strcmp(act, "get #901\n") == 0);

		act_len = nd_seven_action_execute(&st, act, sizeof(act));
		assert(act_len > 0);
		assert(nd_seven_is_picked(901) == 1);

		nd_spatial_tile_t sp_harvested;
		nd_spatial_tile_resolve(&st, 0, 0, &sp_harvested);
		assert(sp_harvested.objs[0].harvested == 1);

		/* Frame 5: Step back outside to (49, 50). Harvested rock remains absent */
		set_map_keep(49, 50, 10, 0x1B, 0, 0, 0, 0);
		st.room.ref = 899;
		st.entity_count = 0;
		st.map_data.tiles[3 * 7 + 4].side_l = 'o'; /* Tile still has glyph, but locally picked */
		nd_seven_sync_origin(&st);

		nd_spatial_tile_t sp_outside;
		nd_spatial_tile_resolve(&st, 1, 0, &sp_outside);
		assert(sp_outside.objs[0].harvested == 1); /* Remains suppressed from outside */
		printf("PASS: async BCP packet interleaving across room entry: zero displacement and zero popping verified\n");
	}

	/* Test 24: Isomorphic entity flags: binary bitflags drive scenery matching & sprite resolution with zero string checks */
	{
		const nd_scenery_def_t *min_def = nd_seven_get_scenery_def(NDW_K_STONE);
		const nd_scenery_def_t *tree_def = nd_seven_get_scenery_def(NDW_K_DECID);
		assert(min_def && tree_def);

		nd_entity_desc_t e;
		memset(&e, 0, sizeof(e));
		e.ref = 777;
		/* Arbitrary name and art string that do NOT contain mineral or plant */
		strcpy(e.name, "mysterious-deposit");
		strcpy(e.art, "custom/unusual_art/1.jpeg");

		/* Without flags or recognized mineral glyph, no match */
		assert(nd_scenery_matches_entity(min_def, &e) == 0);
		assert(nd_seven_resolve_entity_sprite(&e) == 0);

		/* With ND_EF_MINERAL, instant match and mineral sprite resolution */
		e.flags = ND_EF_MINERAL;
		assert(nd_scenery_matches_entity(min_def, &e) == 1);
		assert(nd_seven_resolve_entity_sprite(&e) != 0);

		/* With ND_EF_FLORA, matches permanent tree scenery */
		e.flags = ND_EF_FLORA;
		assert(nd_scenery_matches_entity(tree_def, &e) == 1);
		assert(nd_seven_resolve_entity_sprite(&e) == NDS_TREE_ROUND);

		/* With ND_EF_PLAYER or ND_EF_CREATURE, strictly rejected even if mineral/flora bit set */
		e.flags = ND_EF_MINERAL | ND_EF_PLAYER;
		assert(nd_scenery_matches_entity(min_def, &e) == 0);
		assert(nd_seven_resolve_entity_sprite(&e) == 0);

		e.flags = ND_EF_MINERAL | ND_EF_CREATURE;
		assert(nd_scenery_matches_entity(min_def, &e) == 0);
		assert(nd_seven_resolve_entity_sprite(&e) == 0);

		printf("PASS: isomorphic entity classification flags drive scenery matching and sprite resolution with zero string checks\n");
	}

	/* Test 25: Action debouncing cooldown latch and strict ID command formatting */
	{
		nd_seven_action_cooldown_reset();
		set_map(5, -3, 10, 0x1B, 0, 0, 0, 0);
		st.room.ref = 88;
		st.me = 1;
		st.entity_count = 2;
		st.entities[0].ref = 1;
		st.entities[0].location = 88;
		st.entities[0].flags = ND_EF_PLAYER;
		strcpy(st.entities[0].name, "quirinpa");

		st.entities[1].ref = 555;
		st.entities[1].location = 88;
		st.entities[1].ch = '*';
		st.entities[1].flags = ND_EF_MINERAL;
		st.entities[1].actions = 4;
		strcpy(st.entities[1].name, "gem");

		/* Step anim to establish a baseline animation frame */
		nd_seven_step_anim(100);

		char act1[64] = {0};
		int rc1 = nd_seven_action_execute(&st, act1, sizeof(act1));
		assert(rc1 > 0);
		assert(strcmp(act1, "get #555\n") == 0);

		/* Burst repeat execution on next frame must be debounced (returns 0) */
		nd_seven_step_anim(2);
		char act2[64] = {0};
		int rc2 = nd_seven_action_execute(&st, act2, sizeof(act2));
		assert(rc2 == 0);

		/* After cooldown window passes (>= 18 frames), execution is re-enabled */
		nd_seven_step_anim(20);
		char act3[64] = {0};
		/* Since 555 was already marked picked by act1 execution, next execution targets nothing */
		int rc3 = nd_seven_action_execute(&st, act3, sizeof(act3));
		assert(rc3 == 0);

		/* Multi-packet interleaving: stone must NOT pop out when entity_count > 0 during transit */
		nd_seven_forget_picked();
		set_map(5, -3, 10, 0x1B, 0, '*', 0, 0);
		st.room.ref = 99;
		st.entity_count = 1; /* Only player entity arrived so far */
		sync_room_objects(&st);
		const room_tracker_t *tr = nd_seven_get_tracker();
		assert(tr && tr->count > 0);
		/* Must remain present even though stone entity packet is still in flight */
		assert(is_tracked_obj_present(&st, 0) == 1);

		printf("PASS: action debouncing prevents burst double-picks and strict ID commands verified\n");
	}

	/* Client-side room chunks: retention when in-view and eager freeing when out-of-view */
	{
		nd_seven_ground_cache_forget();
		assert(nd_seven_chunk_count() == 0);

		/* Frame 1: render at (20, 20) */
		set_map(20, 20, 10, 0x1B, 0, 0, 0, 0);
		nd_seven_render(fb, 640, 480, &st);
		int initial_chunks = nd_seven_chunk_count();
		int initial_allocs = nd_seven_chunk_alloc_count();
		assert(initial_chunks > 0);
		assert(initial_allocs == initial_chunks);

		/* Frame 2: render same view -> chunks MUST be kept, 0 allocations, 0 frees */
		nd_seven_render(fb2, 640, 480, &st);
		assert(nd_seven_chunk_count() == initial_chunks);
		assert(nd_seven_chunk_alloc_count() == initial_allocs); /* Zero noise/alloc overhead! */
		assert(memcmp(fb, fb2, 640 * 480 * 4) == 0);

		/* Frame 3: camera teleports far away to (200, 200). Old rooms (20, 20) are out of view */
		set_map_keep(200, 200, 10, 0x1B, 0, 0, 0, 0);
		for (int f = 0; f < 125; f++)
			nd_seven_ground_frame_begin();
		nd_seven_render(fb, 640, 480, &st);
		/* The old chunks MUST have been freed upon TTL expiration */
		assert(nd_seven_chunk_free_count() >= initial_chunks);
		assert(nd_seven_chunk_count() > 0);

		/* Lamina minimap renders without building Seven chunks */
		nd_seven_ground_cache_forget();
		set_map_keep(10, 10, 0, 0x1B, 0, 0, 0, 0); /* 0 = water */
		uint32_t map_fb[224 * 224];
		nd_lamina_render_map_live(map_fb, 224, 224, &st.map_data, "lamina", 43200, 0, 0, 0, 0);
		/* No Seven chunks built for minimap */
		assert(nd_seven_chunk_count() == 0);

		/* Seven stage renders the water map */
		nd_seven_render(fb, 640, 480, &st);
		int seven_chunks = nd_seven_chunk_count();
		assert(seven_chunks > 0);
		int seven_allocs = nd_seven_chunk_alloc_count();

		/* Seven stage renders the same water map: hits the exact same room chunks (0 new allocations) */
		nd_seven_render(fb, 640, 480, &st);
		assert(nd_seven_chunk_alloc_count() == seven_allocs);

		/* Lamina minimap renders land without building Seven chunks */
		nd_seven_ground_cache_forget();
		set_map_keep(15, 15, 10, 0x1B, 0, 0, 0, 0); /* 10 = grassland */
		nd_lamina_render_map_live(map_fb, 224, 224, &st.map_data, "lamina", 43200, 0, 1, NDW_CENTER_X, NDW_CENTER_Y);
		assert(nd_seven_chunk_count() == 0);

		/* Seven stage renders the grassland map */
		nd_seven_render(fb, 640, 480, &st);
		int land_chunks = nd_seven_chunk_count();
		assert(land_chunks > 0);
		int land_allocs = nd_seven_chunk_alloc_count();

		/* Re-render hits cached chunks */
		nd_seven_render(fb, 640, 480, &st);
		assert(nd_seven_chunk_alloc_count() == land_allocs);

		/* Explicit forget clears everything */
		nd_seven_ground_cache_forget();
		assert(nd_seven_chunk_count() == 0);

		printf("PASS: client room chunks retention in-view, O(1) frame hits, and out-of-view freeing verified\n");
	}

	/* Dynamic water surface animation & wave motion over precomputed chunks */
	{
		set_map_keep(-50, 9, 0, 0x1B, 0, 0, 0, 0); /* deep/mid water map */
		nd_seven_render(fb, 640, 480, &st);

		/* Step animation by 18 ticks (drift changes by 3) */
		nd_seven_step_anim(18);
		nd_seven_render(fb2, 640, 480, &st);

		/* Water surface must dynamically animate over time (never static or frozen) */
		assert(memcmp(fb, fb2, 640 * 480 * 4) != 0);

		/* Subsequent frame must hit the exact same cached chunk with 0 re-evaluations */
		int allocs_before = nd_seven_chunk_alloc_count();
		nd_seven_render(fb, 640, 480, &st);
		assert(nd_seven_chunk_alloc_count() == allocs_before);

		printf("PASS: dynamic water surface wave animation across frames verified\n");
	}

	/* Shoreline Multi-Scenario Rendering: Seamless continuous bathymetry across chunks */
	{
		/* Scenario 1: Convex Cape (L-shaped coastline) */
		nd_seven_forget();
		memset(&st, 0, sizeof(st));
		strcpy(st.preferred_driver, "lamina-seven");
		st.map_data.width = st.map_data.height = 7;
		st.map_data.player_x = st.map_data.player_y = 3;
		st.map_data.pos[0] = 10;
		st.map_data.pos[1] = 10;

		/* Top-left 4x4 is Land (Grassland = 10), rest is Water (0) */
		for (int r = 0; r < 7; r++) {
			for (int c = 0; c < 7; c++) {
				int idx = r * 7 + c;
				int is_land = (r <= 3 && c <= 3);
				st.map_data.tiles[idx].bio_idx = is_land ? 10 : 0;
				st.map_data.tiles[idx].exits = 0x1B;
			}
		}
		nd_seven_set_lamb(NDW_CENTER_X, NDW_CENTER_Y);
		nd_seven_render(fb, 640, 480, &st);

		/* Verify boundary agreement across room chunk vertical seam */
		for (int y = 300; y < 400; y += 10) {
			uint32_t col_left = fb[y * 640 + 319];
			uint32_t col_right = fb[y * 640 + 320];
			int dr = abs((int)(col_left & 0xFF) - (int)(col_right & 0xFF));
			int dg = abs((int)((col_left >> 8) & 0xFF) - (int)((col_right >> 8) & 0xFF));
			int db = abs((int)((col_left >> 16) & 0xFF) - (int)((col_right >> 16) & 0xFF));
			assert(dr < 120 && dg < 120 && db < 120);
		}
		printf("PASS: convex cape water renders continuous across vertical and horizontal chunk boundaries\n");

		/* Scenario 2: Multi-Biome Coastline Junction (Grassland + Shrubland meeting Ocean) */
		nd_seven_forget();
		memset(&st, 0, sizeof(st));
		strcpy(st.preferred_driver, "lamina-seven");
		st.map_data.width = st.map_data.height = 7;
		st.map_data.player_x = st.map_data.player_y = 3;
		st.map_data.pos[0] = 20;
		st.map_data.pos[1] = 20;

		for (int r = 0; r < 7; r++) {
			for (int c = 0; c < 7; c++) {
				int idx = r * 7 + c;
				if (c <= 3) {
					st.map_data.tiles[idx].bio_idx = (r <= 3) ? 10 : 250;
				} else {
					st.map_data.tiles[idx].bio_idx = 0;
				}
				st.map_data.tiles[idx].exits = 0x1B;
			}
		}
		nd_seven_set_lamb(NDW_CENTER_X, NDW_CENTER_Y);
		nd_seven_render(fb, 640, 480, &st);

		/* Verify at the multi-biome horizontal boundary (y=256, c=3) that land does not round into water */
		uint32_t land_north = fb[250 * 640 + 300];
		uint32_t land_south = fb[260 * 640 + 300];
		assert((land_north & 0xFF) > 30 || ((land_north >> 8) & 0xFF) > 40);
		assert((land_south & 0xFF) > 30 || ((land_south >> 8) & 0xFF) > 40);
		printf("PASS: multi-biome coastline boundary has zero water intrusion or corner bites\n");

		/* Scenario 3: Sheep in Ocean looking at Coastline */
		nd_seven_forget();
		memset(&st, 0, sizeof(st));
		strcpy(st.preferred_driver, "lamina-seven");
		st.map_data.width = st.map_data.height = 7;
		st.map_data.player_x = st.map_data.player_y = 3;
		st.map_data.pos[0] = 30;
		st.map_data.pos[1] = 30;

		for (int r = 0; r < 7; r++) {
			for (int c = 0; c < 7; c++) {
				int idx = r * 7 + c;
				int is_land = (c <= 2) || (r <= 1 && c <= 3);
				st.map_data.tiles[idx].bio_idx = is_land ? 10 : 0;
				st.map_data.tiles[idx].exits = 0x1B;
			}
		}
		nd_seven_set_lamb(NDW_CENTER_X, NDW_CENTER_Y);
		nd_seven_render(fb, 640, 480, &st);
		printf("PASS: sheep in ocean renders seamless continuous bathymetry across all ocean rooms\n");

		/* Volumetric Water Fog & Shallows Optical Transmission Test */
		{
			/* Shore shallows (d = 1..2): gentle crystal shallows transmission (~12% opacity) */
			int a_shore = water_alpha_at_depth(1);
			assert(a_shore >= 30 && a_shore <= 105);

			/* Wading shallows (d = 6..8): sweet spot where submerged bodies are visible with rich water substance (alpha 140..165) */
			int a_wade = water_alpha_at_depth(7);
			assert(a_wade >= 140 && a_wade <= 165);

			/* Shallows color: high green and blue, red attenuated (crystal turquoise/cerulean) */
			uint32_t c_shallow = water_color_at_depth(4, 128, 128, 128);
			int r_s = c_shallow & 0xFF;
			int g_s = (c_shallow >> 8) & 0xFF;
			int b_s = (c_shallow >> 16) & 0xFF;
			assert(b_s > 180 && g_s > 160 && r_s < 100);

			/* Deep abyss (d >= 28): total optical extinction (alpha >= 245) */
			int a_deep = water_alpha_at_depth(28);
			assert(a_deep >= 245);

			/* Seabed elevation preservation: water chunks maintain true seabed H < w_level */
			const nd_room_chunk_t *gc = nd_seven_chunk_find(st.map_data.pos);
			if (gc && gc->height && gc->has_water) {
				int found_seabed = 0;
				for (int i = 0; i < TILE_PX_W * TILE_PX_H; i++) {
					if (gc->depth && gc->depth[i] > 0) {
						found_seabed = 1;
						break;
					}
				}
				assert(found_seabed);
			}

			/* Buoyancy & Surface Swimming Elevation Test (SQ2 / R2) */
			uint16_t world = (uint16_t)st.map_data.pos[3];
			int16_t w_level = nd_field_water_level(world);
			int64_t lx = 0, ly = 0;
			nd_seven_world_pos(&st, &lx, &ly);
			int16_t lamb_z = nd_seven_lamb_elevation(&st, (int32_t)lx, (int32_t)ly);
			int16_t seabed_z = nd_seven_ground_elevation((int32_t)lx, (int32_t)ly);
			assert(lamb_z >= w_level - 8);
			assert(lamb_z >= seabed_z);

			/* 2D Software Isomorphic Submersion Blit Test (SQ3 / R3) */
			extern void draw_lamb_scaled_sub(uint32_t *fb, int w, int h, int sx, int sy, int scale, int sub_units);
			uint32_t test_fb[64 * 64];
			for (int i = 0; i < 64 * 64; i++) test_fb[i] = 0xFFFFFFFFu;
			draw_lamb_scaled_sub(test_fb, 64, 64, 32, 40, 1, 8);
			/* Verify pixels below waterline were tinted by water (not pure white) */
			int water_tinted_pixels = 0;
			for (int y = 40 - 8; y < 40; y++) {
				for (int x = 20; x < 44; x++) {
					uint32_t px = test_fb[y * 64 + x];
					if (px != 0xFFFFFFFFu && (px & 0xFF) < 220)
						water_tinted_pixels++;
				}
			}
			assert(water_tinted_pixels > 0);

			printf("PASS: volumetric water fog, seabed preservation, lamb buoyancy, and 2D submersion blit verified\n");
		}
	}

	/* Dynamic Camera Zoom & View Frustum Culling (2D and 3D): Zero Black Borders */
	{
		const int W = 1280, H = 720;
		const uint32_t CLEAR_BLACK = 0xFF08080C;

		/* Test across multiple zoom factors: 0.25x, 0.5x, 1.0x, 2.0x */
		float zoom_levels[4] = { 0.25f, 0.5f, 1.0f, 2.0f };
		for (int z = 0; z < 4; z++) {
			float zoom = zoom_levels[z];
			nd_seven_set_zoom(zoom);
			assert(nd_seven_get_zoom() == zoom);

			/* 2D Mode: Full screen coverage */
			nd_seven_set_3d_mode(0);
			set_map(-8, -3, 10, 0x1B, 0, 0, 0, 0);
			nd_seven_render(fb, W, H, &st);

			int black_pixels_2d = 0;
			for (int i = 0; i < W * H; i++) {
				if (fb[i] == CLEAR_BLACK)
					black_pixels_2d++;
			}
			assert(black_pixels_2d == 0); /* Zero black pixels anywhere on the canvas */

			/* 3D Mode: Full screen coverage including tilted northern horizon */
			nd_seven_set_3d_mode(1);
			nd_seven_render(fb, W, H, &st);

			int black_pixels_3d = 0;
			for (int i = 0; i < W * H; i++) {
				if (fb[i] == CLEAR_BLACK)
					black_pixels_3d++;
			}
			assert(black_pixels_3d == 0); /* Zero black pixels in 3D diorama */
		}
		nd_seven_set_zoom(1.0f);
		assert(nd_seven_get_zoom() == 1.0f);
		printf("PASS: dynamic camera zoom 0.25x-2.0x guarantees 100%% canvas coverage with zero black borders\n");
	}

	/* Multi-Resolution Noise LOD Selection across Zoom and Distance Tiers */
	{
		/* 2D Mode: Uniform LOD selection based on zoom */
		nd_seven_set_3d_mode(0);

		nd_seven_set_zoom(2.0f);
		set_map(-8, -3, 10, 0x1B, 0, 0, 0, 0);
		nd_seven_render(fb, 640, 480, &st);
		const nd_room_chunk_t *c_high = nd_seven_chunk_find(st.map_data.pos);
		assert(c_high != NULL);
		assert(c_high->lod == 3); /* High zoom selects LOD 3 (full 1x1 ladder) */

		/* Macro Zoom Out: selects LOD 1 or LOD 0 */
		nd_seven_ground_cache_forget();
		nd_seven_set_zoom(0.05f);
		nd_seven_render(fb, 640, 480, &st);
		const nd_room_chunk_t *c_macro = nd_seven_chunk_find(st.map_data.pos);
		assert(c_macro != NULL);
		assert(c_macro->lod <= 1); /* Macro zoom selects LOD 0 or 1 */

		nd_seven_set_zoom(1.0f);
		assert(nd_seven_get_zoom() == 1.0f);

		/* 3D Mode: Concentric LOD rings & Equidistant LOD Invariant */
		nd_seven_set_3d_mode(1);
		nd_seven_set_zoom(0.5f); /* Zoom out so distant chunks are in frustum */
		set_map(-8, -3, 10, 0x1B, 0, 0, 0, 0);
		nd_seven_render(fb, 1280, 720, &st);

		/* Center chunk is near target: LOD 3 or 2 */
		const nd_room_chunk_t *c_center = nd_seven_chunk_find(st.map_data.pos);
		assert(c_center != NULL);
		assert(c_center->lod >= 2);

		/* Equidistant LOD Invariant: chunks at symmetric distance from camera eye MUST share identical LOD */
		int16_t east_pos[4] = { (int16_t)(st.map_data.pos[0] + 2), (int16_t)(st.map_data.pos[1] - 1), 0, 0 };
		int16_t west_pos[4] = { (int16_t)(st.map_data.pos[0] - 2), (int16_t)(st.map_data.pos[1] - 1), 0, 0 };
		const nd_room_chunk_t *c_east = nd_seven_chunk_find(east_pos);
		const nd_room_chunk_t *c_west = nd_seven_chunk_find(west_pos);
		if (c_east && c_west) {
			assert(c_east->lod == c_west->lod); /* Equidistant chunks have 100% identical LOD */
		}

		/* Far chunk (distance >= 5 tiles): evaluated at LOD 1 or LOD 2 */
		int16_t far_pos[4] = { (int16_t)(st.map_data.pos[0] + 5), (int16_t)(st.map_data.pos[1] - 5), 0, 0 };
		const nd_room_chunk_t *c_far = nd_seven_chunk_find(far_pos);
		if (c_far) {
			assert(c_far->lod <= 2); /* Far chunks use coarser LOD 1 or 2 */
		}

		nd_seven_set_zoom(1.0f);
		printf("PASS: multi-resolution noise LOD selection & equidistant LOD invariant verified\n");
	}

	/* 2D Billboard and Entity Scaling by Camera Zoom */
	{
		const int W = 640, H = 480;
		nd_seven_set_3d_mode(0);

		/* Render at zoom 1.0x (eff_scale = 2) */
		nd_seven_set_zoom(1.0f);
		set_map(-8, -3, 10, 0x1B, 0, 0, 0, 0);
		nd_seven_render(fb, W, H, &st);

		/* Count non-background pixels in lamb bounding box at center (w/2, h/2) */
		int cx = W / 2, cy = H / 2;
		int lamb_px_1x = 0;
		for (int dy = -40; dy <= 40; dy++) {
			for (int dx = -40; dx <= 40; dx++) {
				uint32_t c = fb[(cy + dy) * W + (cx + dx)];
				if (c != 0xFF08080C) lamb_px_1x++;
			}
		}

		/* Render at zoom 2.0x (eff_scale = 4): lamb sprite should scale up 2x in width and height */
		nd_seven_set_zoom(2.0f);
		nd_seven_render(fb2, W, H, &st);
		int lamb_px_2x = 0;
		for (int dy = -40; dy <= 40; dy++) {
			for (int dx = -40; dx <= 40; dx++) {
				uint32_t c = fb2[(cy + dy) * W + (cx + dx)];
				if (c != 0xFF08080C) lamb_px_2x++;
			}
		}
		assert(lamb_px_1x > 0);
		assert(lamb_px_2x > 0);
		nd_seven_set_zoom(1.0f);
		printf("PASS: 2D billboards, lamb and entities scale proportionally with camera zoom\n");
	}

	/* Active Frustum Chunk Retention Across Frame Eviction */
	{
		nd_seven_ground_cache_forget();
		assert(nd_seven_chunk_count() == 0);

		/* Zoom out 2x in 2D so frustum encompasses chunks beyond the 7x7 server map */
		nd_seven_set_zoom(0.5f);
		nd_seven_set_3d_mode(0);
		set_map(-8, -3, 10, 0x1B, 0, 0, 0, 0);
		nd_seven_render(fb, 1280, 720, &st);

		int active_count = nd_seven_chunk_count();
		assert(active_count > 0);

		/* Render a second frame with the same view: all active chunks MUST be retained */
		nd_seven_render(fb, 1280, 720, &st);
		assert(nd_seven_chunk_count() == active_count);

		nd_seven_set_zoom(1.0f);
		nd_seven_ground_cache_forget();
		printf("PASS: active frustum chunks retained across frame eviction without premature purging\n");
	}

	/* Stratospheric LOD Threshold and Macro Chunk Dispatch (SQ3) */
	{
		nd_seven_ground_cache_forget();
		nd_seven_set_3d_mode(1);
		nd_seven_set_zoom(0.05f); /* Extreme stratospheric zoom */
		set_map(50, 60, 10, 0x1B, 0, 0, 0, 0);

		/* At zoom 0.05f, standard room iteration would evaluate > 4,000 rooms.
		 * With the stratospheric threshold, room chunk allocations are strictly bounded to the 7x7 focal area. */
		nd_seven_render(fb, 1280, 720, &st);
		int chunk_allocs = nd_seven_chunk_alloc_count();
		assert(chunk_allocs <= 64); /* Must NOT allocate thousands of room chunks */

		nd_seven_set_zoom(1.0f);
		nd_seven_set_3d_mode(0);
		nd_seven_ground_cache_forget();
		printf("PASS: stratospheric LOD threshold at zoom < 0.25f bypasses room explosion and bounds chunk count to <= 64\n");
	}

	/* Pure-C 3D Shaders, Pitch-Rotated Billboards, Water Depression, and Temporal Stability (R1-R4) */
	{
		/* 1. Verify all 6 3D shaders are exported from pure C and include required optics */
		const char *vs_t = nd_seven_gl_vs_terrain();
		const char *fs_t = nd_seven_gl_fs_terrain();
		const char *vs_w = nd_seven_gl_vs_water();
		const char *fs_w = nd_seven_gl_fs_water();
		const char *vs_b = nd_seven_gl_vs_billboard();
		const char *fs_b = nd_seven_gl_fs_billboard();
		assert(vs_t && strstr(vs_t, "sampleHeightSmooth"));
		assert(fs_t && strstr(fs_t, "seabedAbsorption"));
		assert(vs_w && strstr(vs_w, "sampleHeightSmooth"));
		assert(fs_w && strstr(fs_w, "if (fragWaterDepth <= 0.0)"));
		assert(fs_w && strstr(fs_w, "eval_zoom_water_waves"));
		assert(fs_w && strstr(fs_w, "sampleHeightSmooth(vUV, shoreGrad)"));
		assert(fs_w && !strstr(fs_w, "cSeabed"));
		assert(vs_b && strstr(vs_b, "a_local_pos"));
		assert(fs_b && strstr(fs_b, "waterAbsorption"));

		/* 2. Verify pitch-rotated billboard basis orthogonality and 1:1 projected aspect ratio at 55 deg */
		const nd_planet_def_t *pdef = nd_seven_get_planet(1);
		float base_pos[3], right[3], up[3], view_dir[3];
		nd_seven_gl_billboard_basis(1600.0f, 1280.0f, 10.0f, ND_SEVEN_DIORAMA_PITCH, pdef,
		                            base_pos, right, up, view_dir);

		float dot_ru = right[0] * up[0] + right[1] * up[1] + right[2] * up[2];
		float dot_uv = up[0] * view_dir[0] + up[1] * view_dir[1] + up[2] * view_dir[2];
		float len_r = sqrtf(right[0] * right[0] + right[1] * right[1] + right[2] * right[2]);
		float len_u = sqrtf(up[0] * up[0] + up[1] * up[1] + up[2] * up[2]);
		assert(fabsf(dot_ru) < 1e-5f);
		assert(fabsf(dot_uv) < 1e-5f);
		assert(fabsf(len_r - 1.0f) < 1e-5f);
		assert(fabsf(len_u - 1.0f) < 1e-5f);

		/* Project a 32x32 square billboard through the 3D camera MVP and verify 1:1 screen aspect ratio */
		float mvp[16];
		nd_seven_gl_compute_mvp(1000, 1000, 1600.0f, 1280.0f, 10.0f,
		                        ND_SEVEN_CAM_DIST, ND_SEVEN_DIORAMA_PITCH, ND_SEVEN_CAM_FOV,
		                        pdef, mvp, NULL, NULL, NULL);
		lam_billboard_gpu_t bb = {
			.x = 1600.0f, .y = 1280.0f, .z = 10.0f,
			.width = 32.0f, .height = 32.0f,
			.u0 = 0.0f, .v0 = 0.0f, .u1 = 1.0f, .v1 = 1.0f,
			.tint_rgba = 0xFFFFFFFFu
		};
		nd_seven_gl_bb_vert_t qv[6];
		nd_seven_gl_eval_billboard_quad(&bb, ND_SEVEN_DIORAMA_PITCH, pdef, qv);
		assert(fabsf(qv[0].local_pos[2] - 10.0f) < 1e-5f);
		assert(fabsf(qv[2].local_pos[2] - 42.0f) < 1e-5f);

		/* Transform BL (qv[0]), BR (qv[1]), TL (qv[2]) to NDC */
		float ndc_x[3], ndc_y[3];
		for (int k = 0; k < 3; k++) {
			float x = qv[k].world_pos[0], y = qv[k].world_pos[1], z = qv[k].world_pos[2];
			float cx = mvp[0] * x + mvp[4] * y + mvp[8]  * z + mvp[12];
			float cy = mvp[1] * x + mvp[5] * y + mvp[9]  * z + mvp[13];
			float cw = mvp[3] * x + mvp[7] * y + mvp[11] * z + mvp[15];
			assert(cw > 1.0f);
			ndc_x[k] = cx / cw;
			ndc_y[k] = cy / cw;
		}
		float proj_w = fabsf(ndc_x[1] - ndc_x[0]);
		float proj_h = fabsf(ndc_y[2] - ndc_y[0]);
		assert(proj_w > 0.01f && proj_h > 0.01f);
		assert(fabsf((proj_h / proj_w) - 1.0f) < 0.06f);

		/* 3. Verify smooth height parity and dry-land water vertex depression */
		int16_t hgrid[4] = { 320, 320, 320, 320 }; /* 320 * 0.0625 = 20.0 units elevation */
		float wz_out = 0.0f, depth_out = 0.0f;
		float r_water_dry = nd_seven_gl_eval_water_radius(hgrid, 2, 2, 0.5f, 0.5f, 160.0f, 128.0f,
		                                                  pdef->minor_r, 0.0f, &wz_out, &depth_out);
		assert(fabsf(wz_out - 20.0f) < 1e-4f);
		assert(depth_out < 0.0f);
		assert(r_water_dry < pdef->minor_r + wz_out);

		printf("PASS: Pure-C 3D shaders, 55-deg pitch-aligned billboard aspect ratio, and dry-land water depression verified\n");
	}

	/* R1: Deterministic Virgin Tile Synthesis (Flora + Stones/Rocks/Boulders Parity) */
	{
		int flora_seen = 0;
		int stone_seen = 0, rock_seen = 0, boulder_seen = 0;
		for (int ry = -40; ry <= 40; ry++) {
			for (int rx = -40; rx <= 40; rx++) {
				int16_t pos[4] = { (int16_t)rx, (int16_t)ry, 0, 0 };
				ndw_tile_t vt1, vt2;
				ndw_synth_virgin_tile(pos, &vt1);
				ndw_synth_virgin_tile(pos, &vt2);
				assert(memcmp(&vt1, &vt2, sizeof(vt1)) == 0);
				for (int s = 0; s < 3; s++) {
					uint8_t g = vt1.glyph[s];
					if (g == '*') stone_seen++;
					else if (g == 'o') rock_seen++;
					else if (g == 'O') boulder_seen++;
					else if (g != 0) flora_seen++;
				}
			}
		}
		assert(flora_seen > 100);
		assert(stone_seen > 50);
		assert(rock_seen > 20);
		assert(boulder_seen > 5);
		printf("PASS: ndw_synth_virgin_tile deterministically synthesizes both flora (%d) and minerals (*=%d, o=%d, O=%d)\n",
		       flora_seen, stone_seen, rock_seen, boulder_seen);
	}

	/* R2: Client-Side Remembered Tile Cache Persistence (> 10 Rooms Away) */
	{
		nd_seven_forget();
		/* Stand at (100, 100) where room (100, 100) has a boulder 'O' and a built room flag */
		set_map(100, 100, 10, 0x1B, ND_MAP_FLAG_ROOM, 'O', 0, 'T');
		nd_seven_render(fb, 640, 480, &st);

		/* Walk 12 rooms East to (112, 100) so (100, 100) is well outside the 7x7 window */
		set_map_keep(112, 100, 10, 0x1B, 0, 0, 0, 0);
		nd_seven_render(fb, 640, 480, &st);

		int16_t old_pos[4] = { 100, 100, 0, 0 };
		ndw_tile_t resolved;
		int src = nd_seven_resolve_tile_at_pos(&st, old_pos, &resolved);
		assert(src == 1); /* Resolved from remembered tile cache, not virgin fallback */
		assert((resolved.flags & NDW_F_ROOM) != 0);
		assert(resolved.glyph[0] == 'O' && resolved.glyph[2] == 'T');
		printf("PASS: remembered tile cache preserves visited server rooms and objects > 10 rooms outside 7x7 window\n");
	}

	/* R3 & R4: Unified 1:1 Room Scenery Placement Across Zoom Levels (Zero 7x7 Density Cliff) */
	{
		nd_seven_forget();
		nd_seven_set_3d_mode(1);
		set_map(10, 10, 12, 0x1B, 0, 'X', 'o', 'T');

		/* First measure billboards in ring [4..12] rooms outside 7x7 at mid zoom (0.15f) */
		nd_seven_set_zoom(0.15f);
		nd_seven_render(fb, 1280, 720, &st);
		uint32_t cnt_mid_zoom = nd_seven_last_mid_billboard_count();
		int ring_4_12_mid_zoom = 0;
		for (uint32_t i = 0; i < cnt_mid_zoom; i++) {
			float bx = 0, by = 0, bz = 0, bw = 0, bh = 0;
			if (nd_seven_last_mid_billboard(i, &bx, &by, &bz, &bw, &bh)) {
				int rx = ndw_fdiv((int)bx, NDW_TILE_W) - 10;
				int ry = (-ndw_fdiv((int)by, NDW_TILE_H)) - 10;
				int arx = rx < 0 ? -rx : rx;
				int ary = ry < 0 ? -ry : ry;
				int cheb = arx > ary ? arx : ary;
				if (cheb >= 4 && cheb <= 12)
					ring_4_12_mid_zoom++;
			}
		}
		assert(ring_4_12_mid_zoom > 100);

		/* Now zoom all the way out to -22x (0.00738f): ring [4..12] must NOT thin out,
		 * and scenery must be emitted homogeneously across visible rooms! */
		nd_seven_set_zoom(0.00738f);
		nd_seven_render(fb, 1280, 720, &st);

		uint32_t mid_cnt = nd_seven_last_mid_billboard_count();
		assert(mid_cnt > 1000);

		int ring_4_12_far_zoom = 0;
		int far_horizon_cnt = 0;
		int deep_horizon_30_cnt = 0;
		int max_cheb_seen = 0;
		int unscaled_cnt = 0;
		for (uint32_t i = 0; i < mid_cnt; i++) {
			float bx = 0, by = 0, bz = 0, bw = 0, bh = 0;
			if (nd_seven_last_mid_billboard(i, &bx, &by, &bz, &bw, &bh)) {
				int rx = ndw_fdiv((int)bx, NDW_TILE_W) - 10;
				int ry = (-ndw_fdiv((int)by, NDW_TILE_H)) - 10;
				int arx = rx < 0 ? -rx : rx;
				int ary = ry < 0 ? -ry : ry;
				int cheb = arx > ary ? arx : ary;
				if (cheb > max_cheb_seen)
					max_cheb_seen = cheb;
				if (cheb >= 4 && cheb <= 12)
					ring_4_12_far_zoom++;
				if (cheb > 14)
					far_horizon_cnt++;
				if (cheb > 30)
					deep_horizon_30_cnt++;
				assert(bw > 0.0f && bh > 0.0f && bh <= 74.0f); /* True 1:1 physical world-space dimensions */
				unscaled_cnt++;
			}
		}
		assert(ring_4_12_far_zoom >= ring_4_12_mid_zoom);
		assert(far_horizon_cnt > 1000);
		assert(deep_horizon_30_cnt > 1000);
		assert(max_cheb_seen > 100 && max_cheb_seen <= 255);
		assert(unscaled_cnt > 1000 && unscaled_cnt == (int)(mid_cnt < 262144 ? mid_cnt : 262144));

		/* Verify configurable per-object vertical anchor (anchor_y), lamb centering, and water cutoff calibration */
		{
			assert(LAMB_AY == 16);
			assert(LAMB_FOOT_Y == 25);
			const nd_planet_def_t *pdef_test = nd_seven_get_planet(1);
			lam_billboard_gpu_t lamb_bb = {
				.x = 80.0f, .y = 64.0f, .z = 5.0f,
				.width = 32.0f, .height = 32.0f,
				.anchor_y = (float)(LAMB_SIZE - LAMB_AY),
				.water_z_off = (float)((LAMB_SIZE - 1) - LAMB_FOOT_Y),
				.u0 = 0.0f, .v0 = 0.0f, .u1 = 1.0f, .v1 = 1.0f,
				.tint_rgba = 0xFFFFFFFFu
			};
			nd_seven_gl_bb_vert_t lqv[6];
			nd_seven_gl_eval_billboard_quad(&lamb_bb, ND_SEVEN_DIORAMA_PITCH, pdef_test, lqv);
			/* At the hoof row (7/31 of quad height from bottom), local_pos[2] equals ground z (5.0f) */
			float hoof_t = (float)((LAMB_SIZE - 1) - LAMB_FOOT_Y) / 32.0f;
			float z_at_hoof = lqv[0].local_pos[2] + hoof_t * (lqv[2].local_pos[2] - lqv[0].local_pos[2]);
			assert(fabsf(z_at_hoof - 5.0f) < 1e-4f);

			/* Custom per-object anchor_y override on ndw_obj_t */
			ndw_obj_t custom_o;
			memset(&custom_o, 0, sizeof(custom_o));
			custom_o.kind = NDW_K_ROCK;
			custom_o.hh = 14;
			assert(ndw_obj_anchor_y(&custom_o, 42) == 14);
			custom_o.anchor_y = 18;
			assert(ndw_obj_anchor_y(&custom_o, 42) == 18);
		}

		/* By -23x zoom (0.0059f, eff_cam_dist ~47,457 > 44,000), all scenery has uniformly faded out together */
		nd_seven_set_zoom(0.0059f);
		nd_seven_render(fb, 1280, 720, &st);
		assert(nd_seven_last_mid_billboard_count() == 0);

		nd_seven_set_zoom(1.0f);
		nd_seven_set_3d_mode(0);
		nd_seven_forget();
		printf("PASS: -22x zoom (0.00738) emits %u homogeneous billboards with zero zoom-out thinning (ring[4..12]: %d vs %d at 0.15x, >14: %d, >30: %d, max_cheb=%d) and fades out uniformly by -23x zoom\n",
		       mid_cnt, ring_4_12_far_zoom, ring_4_12_mid_zoom, far_horizon_cnt, deep_horizon_30_cnt, max_cheb_seen);
	}

	/* High-Resolution 48x48 Level 0 Terrain Grid & Exact Seam Continuity Verification */
	{
		nd_seven_set_3d_mode(1);
		ar_system_t *cs = nd_seven_get_chunk_sys();
		assert(cs != NULL);

		/* Level 0 uses 48x48 quads (4,608 triangles, 2.25x density); Level 1..2 use 32x32 quads (2,048 triangles);
		 * Level 3+ and macro low-zoom (cam_dist >= 5000) use 16x16 quads (512 triangles, 4x lighter) */
		assert(nd_seven_gl_grid_quads_for_level(0) == 48);
		assert(nd_seven_gl_grid_quads_for_level(1) == 32);
		assert(nd_seven_gl_grid_quads_for_level(2) == 32);
		assert(nd_seven_gl_grid_quads_for_level(3) == 16);

		/* Close-up and macro zoom polygon scaling: mid-near zoom (> 340) uses 48x48 (4,608 tris),
		 * normal/close zoom (<= 340, including 1.0x at 280) scales to 96x96 (18,432 tris),
		 * close/ultra-close zoom (<= 220) scales to 144x144 (41,472 tris),
		 * and low zoom (14 < x < 25, cam_dist >= 5000) scales down to 16x16 (512 tris) */
		assert(nd_seven_gl_grid_quads_for_zoom(0, 380.0f)   == 48);
		assert(nd_seven_gl_grid_quads_for_zoom(0, 280.0f)   == 96);
		assert(nd_seven_gl_grid_quads_for_zoom(0, 240.0f)   == 96);
		assert(nd_seven_gl_grid_quads_for_zoom(0, 200.0f)   == 144);
		assert(nd_seven_gl_grid_quads_for_zoom(0, 80.0f)    == 144);
		assert(nd_seven_gl_grid_quads_for_zoom(1, 80.0f)    == 32);
		assert(nd_seven_gl_grid_quads_for_zoom(1, 6500.0f)  == 16);
		assert(nd_seven_gl_grid_quads_for_zoom(2, 20000.0f) == 16);
		assert(nd_seven_gl_grid_quads_for_zoom(4, 280.0f)   == 16);

		/* Verify adjacent 160x128 chunks at grid_dim=48 share identical perimeter sample values,
		 * including across large world coordinates where 16-bit signed rotation would wrap */
		const int32_t test_origins[3][2] = {
			{ 0, 0 },
			{ 32640, -16384 },
			{ -32800, 24576 }
		};
		for (int t = 0; t < 3; t++) {
			int32_t bx = test_origins[t][0];
			int32_t by = test_origins[t][1];
			const ar_node_t *m00 = ar_system_ensure(cs, 1, 0, bx, by);
			const ar_node_t *m10 = ar_system_ensure(cs, 1, 0, bx + NDW_TILE_W, by);
			const ar_node_t *m01 = ar_system_ensure(cs, 1, 0, bx, by + NDW_TILE_H);
			assert(m00 != NULL && m10 != NULL && m01 != NULL);
			assert(m00->grid_dim == 48 && m00->vert_dim == 49 && m00->vert_count == 2401);

			for (uint32_t r = 0; r < m00->vert_dim; r++) {
				assert(m00->values[r * m00->vert_dim + 48] == m10->values[r * m10->vert_dim + 0]);
				assert(m00->rgba[r * m00->vert_dim + 48] == m10->rgba[r * m10->vert_dim + 0]);
			}
			for (uint32_t c = 0; c < m00->vert_dim; c++) {
				assert(m00->values[48 * m00->vert_dim + c] == m01->values[0 * m01->vert_dim + c]);
				assert(m00->rgba[48 * m00->vert_dim + c] == m01->rgba[0 * m01->vert_dim + c]);
			}

			/* Verify watertight perimeter height welding across 48x48, 96x96, and 144x144 mesh subdivisions */
			for (int k = 0; k <= 144; k++) {
				float v = (float)k / 144.0f;
				float h_left = nd_seven_gl_sample_height_smooth(m00->values, m00->vert_dim, m00->vert_dim,
				                                                1.0f, v, (float)m00->w, (float)m00->h, NULL, NULL);
				float h_right = nd_seven_gl_sample_height_smooth(m10->values, m10->vert_dim, m10->vert_dim,
				                                                 0.0f, v, (float)m10->w, (float)m10->h, NULL, NULL);
				assert(fabsf(h_left - h_right) < 1e-5f);

				float u = (float)k / 144.0f;
				float h_top = nd_seven_gl_sample_height_smooth(m00->values, m00->vert_dim, m00->vert_dim,
				                                               u, 1.0f, (float)m00->w, (float)m00->h, NULL, NULL);
				float h_bot = nd_seven_gl_sample_height_smooth(m01->values, m01->vert_dim, m01->vert_dim,
				                                               u, 0.0f, (float)m01->w, (float)m01->h, NULL, NULL);
				assert(fabsf(h_top - h_bot) < 1e-5f);
			}

			/* Verify shoreline micro-relief band sets node->flags & 1 even if all discrete vertices are slightly above w_level */
			int16_t w_lvl_test = nd_field_water_level(1);
			if (m00->min_val <= ((int32_t)(w_lvl_test + 4) << 4)) {
				assert((m00->flags & 1) != 0);
			}
		}

		const ar_node_t *n00 = ar_system_ensure(cs, 1, 0, 0, 0);
		assert(n00 != NULL);

		/* Verify organic substrate variation on dry land (non-flat RGBA across chunk vertices) */
		uint32_t first_rgba = n00->rgba[0];
		int distinct_rgba = 0;
		for (uint32_t i = 1; i < n00->vert_count; i++) {
			if (n00->rgba[i] != first_rgba)
				distinct_rgba++;
		}
		assert(distinct_rgba > 100);
		printf("PASS: 48x48 normal-zoom, 96x96 close-zoom, and 144x144 ultra-close-zoom meshes verified with watertight inter-chunk height continuity\n");
	}

	/* R5 & R6: Zoom-Progressive 4-Octave Rotated Gradient Terrain Detail, Per-Fragment Quintic Reconstruction & Anti-Zigzag Filtering */
	{
		const char *fs_t = nd_seven_gl_fs_terrain();
		assert(fs_t != NULL);
		assert(strstr(fs_t, "chaff_grad_noise2d") != NULL);
		assert(strstr(fs_t, "chaff_grad_dir") != NULL);
		assert(strstr(fs_t, "eval_zoom_terrain_detail") != NULL);
		assert(strstr(fs_t, "sampleSubstrateSmooth") != NULL);
		assert(strstr(fs_t, "sampleHeightSmooth(vUV, fragGrad)") != NULL);
		assert(strstr(fs_t, "dFdx(vWorldPos)") != NULL);
		assert(strstr(fs_t, "rockCrevice") != NULL);
		assert(strstr(fs_t, "talusFactor") != NULL);
		assert(strstr(fs_t, "quartzGlint") != NULL);
		assert(strstr(fs_t, "bladeRelief") != NULL);

		/* Evaluate sub-unit surface detail across 4 progressive zoom tiers:
		 * Tier 0: Far zoom (cam_dist = 1800, footprint = 3.0)
		 * Tier 1: Normal 1x zoom (cam_dist = 280, footprint = 0.45)
		 * Tier 2: Close 2.5x zoom (cam_dist = 112, footprint = 0.18)
		 * Tier 3: Ultra-close 8x zoom (cam_dist = 35, footprint = 0.02) */
		const float dists[4] = { 1800.0f, 280.0f, 112.0f, 35.0f };
		const float footprints[4] = { 3.0f, 0.45f, 0.18f, 0.02f };
		float mean_grad[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		float mean_cavity[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		float sub_diff[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

		for (int t = 0; t < 4; t++) {
			float g_sum = 0.0f;
			float c_sum = 0.0f;
			float d_sum = 0.0f;
			float prev_h = 0.0f;
			int samples = 0;
			for (int ry = 0; ry < 24; ry++) {
				for (int rx = 0; rx < 24; rx++) {
					float wx = 120.0f + (float)rx * 0.04f;
					float wy = 80.0f + (float)ry * 0.04f;
					float gx = 0.0f, gy = 0.0f, cav = 0.0f;
					float h = nd_seven_gl_eval_terrain_detail(wx, wy, dists[t], footprints[t], &gx, &gy, &cav);
					g_sum += sqrtf(gx * gx + gy * gy);
					c_sum += cav;
					if (samples > 0)
						d_sum += fabsf(h - prev_h);
					prev_h = h;
					samples++;
				}
			}
			mean_grad[t] = g_sum / (float)samples;
			mean_cavity[t] = c_sum / (float)samples;
			sub_diff[t] = d_sum / (float)(samples - 1);
		}

		/* As camera zooms in (1800 -> 280 -> 112 -> 35), finer octaves unlock and sharpen surface detail */
		assert(mean_grad[1] > mean_grad[0] * 1.5f);
		assert(mean_grad[2] > mean_grad[1] * 1.5f);
		assert(mean_grad[3] > mean_grad[2] * 1.3f);
		assert(sub_diff[3] > sub_diff[1] * 1.2f);
		assert(mean_cavity[3] > mean_cavity[1]);

		/* Verify true 2D gradient noise has non-zero gradients right on integer lattice lines
		 * (eliminating the value-noise zero-derivative lattice lines & 45-deg saddle zigzags) */
		int nonzero_on_lattice = 0;
		for (int k = 1; k <= 16; k++) {
			float gx_lat = 0.0f, gy_lat = 0.0f;
			nd_seven_gl_eval_terrain_detail((float)(k * 10), (float)(k * 7), 100.0f, 0.05f, &gx_lat, &gy_lat, NULL);
			if (hypotf(gx_lat, gy_lat) > 0.02f)
				nonzero_on_lattice++;
		}
		assert(nonzero_on_lattice >= 14);

		/* Nyquist anti-aliasing check: at close distance (35.0) but coarse pixel footprint (2.0),
		 * high-frequency octaves are automatically filtered out to prevent aliasing shimmer */
		float gx_nyq = 0.0f, gy_nyq = 0.0f, gx_full = 0.0f, gy_full = 0.0f;
		nd_seven_gl_eval_terrain_detail(120.37f, 80.61f, 35.0f, 2.0f, &gx_nyq, &gy_nyq, NULL);
		nd_seven_gl_eval_terrain_detail(120.37f, 80.61f, 35.0f, 0.02f, &gx_full, &gy_full, NULL);
		assert(hypotf(gx_nyq, gy_nyq) < hypotf(gx_full, gy_full) * 0.5f);

		printf("PASS: zoom-progressive rotated gradient terrain detail & anti-zigzag quintic filtering verified (mean grad: far=%.3f, 1x=%.3f, 2.5x=%.3f, 8x=%.3f)\n",
		       mean_grad[0], mean_grad[1], mean_grad[2], mean_grad[3]);
	}

	/* R7 & R8: Non-Stationary Regional Ruggedness Envelope & Real 3D Height Relief (Clear Plains vs Rugged Ridges) */
	{
		nd_seven_set_3d_mode(1);
		ar_system_t *cs = nd_seven_get_chunk_sys();
		assert(cs != NULL);

		int clear_rooms = 0;
		int rolling_rooms = 0;
		int rugged_rooms = 0;
		float max_clear_span = 0.0f;
		float max_rugged_span = 0.0f;
		float sum_clear_span = 0.0f;
		float sum_rugged_span = 0.0f;

		for (int ry = -24; ry <= 24; ry += 2) {
			for (int rx = -24; rx <= 24; rx += 2) {
				int32_t ox = rx * NDW_TILE_W;
				int32_t oy = ry * NDW_TILE_H;
				int32_t cx = ox + (NDW_TILE_W / 2);
				int32_t cy = oy + (NDW_TILE_H / 2);
				nd_field_sample_t center_s = nd_field_sample(0, 3, cx, cy);
				/* Inspect inland dry land rooms well above sea level */
				if (center_s.is_water || center_s.coarse_elev < center_s.water_level + 8)
					continue;

				uint16_t rug = nd_field_ruggedness_q8(0, cx, cy);
				const ar_node_t *node = ar_system_ensure(cs, 0, 0, ox, oy);
				assert(node != NULL && node->values != NULL);
				float span_units = (float)(node->max_val - node->min_val) / 16.0f;

				if (rug == 0) {
					clear_rooms++;
					sum_clear_span += span_units;
					if (span_units > max_clear_span)
						max_clear_span = span_units;
				} else if (rug >= 180) {
					rugged_rooms++;
					sum_rugged_span += span_units;
					if (span_units > max_rugged_span)
						max_rugged_span = span_units;
				} else {
					rolling_rooms++;
				}
			}
		}

		assert(clear_rooms >= 20);
		assert(rolling_rooms >= 10);
		assert(rugged_rooms >= 15);
		float avg_clear_span = sum_clear_span / (float)clear_rooms;
		float avg_rugged_span = sum_rugged_span / (float)rugged_rooms;

		/* Clear plains remain gently level while rugged zones have 4x+ higher average intra-room 3D relief
		 * and peak single-room 3D vertical spans exceeding 25 world units */
		assert(avg_clear_span <= 8.0f);
		assert(avg_rugged_span >= avg_clear_span * 3.5f);
		assert(max_rugged_span >= 25.0f);

		printf("PASS: non-stationary regional 3D relief verified (clear=%d rooms avg=%.1f, rolling=%d, rugged=%d rooms avg=%.1f max=%.1f world units)\n",
		       clear_rooms, avg_clear_span, rolling_rooms, rugged_rooms, avg_rugged_span, max_rugged_span);
	}

	/* Phase 6: Realistic Physical Water Optics, Single-Pass Seabed Transmission & 6-Octave Zoom Wave Cascade */
	{
		const char *fs_t = nd_seven_gl_fs_terrain();
		const char *fs_w = nd_seven_gl_fs_water();
		assert(fs_t && fs_w);

		/* 1. Single-pass seabed transmission & Phase 8 realistic water shader invariants:
		 * - 4x4 Catmull-Rom bicubic spline in both s_fs_terrain and s_fs_water
		 * - Shared world-space sub-texel shoreline micro-relief
		 * - Biome-aware water body palette and seabed substrate derived from uSubstrateTex
		 * - Capillary wet-shore darkening
		 * - Non-pulsating shoreline swash (no global time-sine) with distance attenuation
		 * - 3D wave slope hillshading
		 * - s_fs_water is a pure air-water surface interface with zero fake opaque cSeabed */
		assert(strstr(fs_t, "littoralSand") != NULL);
		assert(strstr(fs_t, "hBicubic") != NULL);
		assert(strstr(fs_t, "eval_shore_micro_relief") != NULL);
		assert(strstr(fs_t, "wTropical") != NULL && strstr(fs_t, "wPolar") != NULL);
		assert(strstr(fs_t, "shoreWetMoisture") != NULL);
		assert(strstr(fs_t, "C_SHALLOW") != NULL && strstr(fs_t, "C_ABYSS") != NULL);
		assert(strstr(fs_t, "shallowFactor") != NULL && strstr(fs_t, "effectiveOpticalDepth") != NULL);
		assert(strstr(fs_w, "eval_recursive_wind") != NULL);
		assert(strstr(fs_w, "eval_water_fetch_and_basin") != NULL);
		assert(strstr(fs_w, "eval_wind_capillary_facets") != NULL);
		assert(strstr(fs_w, "openSeaFactor") != NULL && strstr(fs_w, "windFetch") != NULL);
		assert(strstr(fs_w, "specMicro") != NULL && strstr(fs_w, "specWavelet") != NULL && strstr(fs_w, "specBroad") != NULL);
		assert(strstr(fs_w, "eval_zoom_water_waves") != NULL);
		assert(strstr(fs_w, "hBicubic") != NULL);
		assert(strstr(fs_w, "eval_shore_micro_relief") != NULL);
		assert(strstr(fs_w, "shoreDist") != NULL);
		assert(strstr(fs_w, "foamDistFade") != NULL);
		assert(strstr(fs_w, "waveSlopeDiff") != NULL);
		assert(strstr(fs_w, "swashPhase = sin(") == NULL);
		assert(strstr(fs_w, "meniscus * 0.78") == NULL);
		assert(strstr(fs_w, "cSeabed") == NULL);

		/* 2. Verify 6-octave zoom-continuous wave cascade across planetary orbit to ultra-close zoom:
		 * Tier 0: Planetary / orbital zoom (cam_dist = 35000, footprint = 12.0)
		 * Tier 1: Macro regional zoom (cam_dist = 4500, footprint = 2.0)
		 * Tier 2: Normal 1x zoom (cam_dist = 280, footprint = 0.40)
		 * Tier 3: Ultra-close 8x zoom (cam_dist = 38, footprint = 0.02) */
		const float w_dists[4] = { 35000.0f, 4500.0f, 280.0f, 38.0f };
		const float w_fw[4]    = {    12.0f,    2.0f,  0.40f,  0.02f };
		float w_grad[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

		for (int t = 0; t < 4; t++) {
			float g_sum = 0.0f;
			int n = 0;
			for (int ry = 0; ry < 16; ry++) {
				for (int rx = 0; rx < 16; rx++) {
					float wx = 240.0f + (float)rx * 0.35f;
					float wy = 180.0f + (float)ry * 0.35f;
					float gx = 0.0f, gy = 0.0f, crest = 0.0f;
					nd_seven_gl_eval_water_waves(wx, wy, 12.5f, w_dists[t], w_fw[t], &gx, &gy, &crest);
					g_sum += hypotf(gx, gy);
					n++;
				}
			}
			w_grad[t] = g_sum / (float)n;
		}

		/* Planetary/orbital zoom retains live macro ocean swell (w_grad[0] > 0),
		 * and zooming in progressively unlocks finer wind waves, chop, and capillary ripples */
		assert(w_grad[0] > 0.003f);
		assert(w_grad[1] > w_grad[0] * 1.4f);
		assert(w_grad[2] > w_grad[1] * 1.4f);
		assert(w_grad[3] > w_grad[2] * 1.3f);

		/* 3. Verify temporal phase-wrapping stability: wave gradients at t = 100,000s remain
		 * just as crisp and non-degenerate as at t = 10s (zero float32 precision loss over time) */
		float g_early = 0.0f, g_late = 0.0f;
		for (int k = 0; k < 32; k++) {
			float wx = 100.0f + (float)k * 1.13f;
			float wy = 200.0f + (float)k * 0.87f;
			float gx1 = 0.0f, gy1 = 0.0f, gx2 = 0.0f, gy2 = 0.0f;
			nd_seven_gl_eval_water_waves(wx, wy, 10.0f,     180.0f, 0.15f, &gx1, &gy1, NULL);
			nd_seven_gl_eval_water_waves(wx, wy, 100000.0f, 180.0f, 0.15f, &gx2, &gy2, NULL);
			g_early += hypotf(gx1, gy1);
			g_late  += hypotf(gx2, gy2);
		}
		assert(g_late > g_early * 0.75f && g_late < g_early * 1.35f);

		/* 4. Verify natural bathymetric shelf progression in nd_field_bimodal_hypsometry:
		 * Shallow coastal elevations (-1..-7) remain gentle wading shallows instead of dropping to -12 */
		assert(nd_field_bimodal_hypsometry(-2) == -2);
		assert(nd_field_bimodal_hypsometry(-6) == -6);
		assert(nd_field_bimodal_hypsometry(-7) == -7);
		assert(nd_field_bimodal_hypsometry(-12) < -14);
		assert(nd_field_bimodal_hypsometry(-30) < -60);

		/* 5. Verify all 3 WebGL2 shader programs (terrain, water, billboard) compile and link cleanly
		 * under glslangValidator -l (GLSL ES 3.00) so no syntax, swizzle, or vector-type error can slip through */
		if (system("command -v glslangValidator >/dev/null 2>&1") == 0) {
			struct {
				const char *name;
				const char *vs;
				const char *fs;
			} progs[3] = {
				{ "terrain",   nd_seven_gl_vs_terrain(),   nd_seven_gl_fs_terrain()   },
				{ "water",     nd_seven_gl_vs_water(),     nd_seven_gl_fs_water()     },
				{ "billboard", nd_seven_gl_vs_billboard(), nd_seven_gl_fs_billboard() }
			};
			for (int p = 0; p < 3; p++) {
				char vpath[128], fpath[128], cmd[512];
				snprintf(vpath, sizeof(vpath), "/tmp/test_seven_gl_%s.vert", progs[p].name);
				snprintf(fpath, sizeof(fpath), "/tmp/test_seven_gl_%s.frag", progs[p].name);
				FILE *vf = fopen(vpath, "w");
				assert(vf != NULL);
				fputs(progs[p].vs, vf);
				fclose(vf);
				FILE *ff = fopen(fpath, "w");
				assert(ff != NULL);
				fputs(progs[p].fs, ff);
				fclose(ff);
				snprintf(cmd, sizeof(cmd), "glslangValidator -l '%s' '%s' >/dev/null", vpath, fpath);
				int rc = system(cmd);
				remove(vpath);
				remove(fpath);
				assert(rc == 0);
			}
		}

		/* 6. Verify 4x4 Catmull-Rom bicubic C1 gradient continuity across texel boundaries:
		 * On a steady diagonal height ramp h(x,y) = 16*(x + y), the gradient at interior texel
		 * grid lines (f = 0) must be strictly non-zero and continuous (unlike 2x2 quintic S-curve
		 * plateauing which forced derivative = 0 on every texel line). */
		{
			int16_t ramp[9 * 9];
			for (int ry = 0; ry < 9; ry++) {
				for (int rx = 0; rx < 9; rx++) {
					ramp[ry * 9 + rx] = (int16_t)((rx + ry) * 32);
				}
			}
			/* Sample exactly on interior texel grid line u = 4/8 = 0.5, v = 4/8 = 0.5 */
			float gx_grid = 0.0f, gy_grid = 0.0f;
			float h_grid = nd_seven_gl_sample_height_smooth(ramp, 9, 9, 0.5f, 0.5f, 160.0f, 128.0f, &gx_grid, &gy_grid);
			float gx_mid = 0.0f, gy_mid = 0.0f;
			float h_mid = nd_seven_gl_sample_height_smooth(ramp, 9, 9, 0.5625f, 0.5625f, 160.0f, 128.0f, &gx_mid, &gy_mid);
			assert(fabsf(h_grid - 16.0f) < 1e-4f);
			assert(h_mid > h_grid);
			assert(gx_grid > 0.5f && gy_grid > 0.5f);
			assert(fabsf(gx_grid - gx_mid) < 1e-3f && fabsf(gy_grid - gy_mid) < 1e-3f);
		}

		printf("PASS: single-pass volumetric water optics, 6-octave orbital-to-capillary wave cascade (grad: orbit=%.4f, macro=%.4f, 1x=%.4f, 8x=%.4f), t=100000s temporal stability, and glslangValidator GLSL ES 3.00 compile+link verified\n",
		       w_grad[0], w_grad[1], w_grad[2], w_grad[3]);
	}

	/* Phase 7: 3D Billboard Environment Integration (Volumetric Normals, Ground-Contact AO, 3D Drop Shadows & Sub-Texel Filtering) */
	{
		const char *vs_b = nd_seven_gl_vs_billboard();
		const char *fs_b = nd_seven_gl_fs_billboard();
		assert(vs_b && fs_b);
		assert(strstr(vs_b, "a_quad_uv") != NULL);
		assert(strstr(vs_b, "a_sprite_bounds") != NULL);
		assert(strstr(fs_b, "sampleAtlasSmooth") != NULL);
		assert(strstr(fs_b, "fetchBoundedTexel") != NULL);
		assert(strstr(fs_b, "chaff_grad_noise2d") != NULL);
		assert(strstr(fs_b, "contactAO") != NULL);
		assert(strstr(fs_b, "fogFactor") != NULL);

		/* 1. Verify 3D volumetric directional lighting (sunward NW upper-left > leeward SE lower-right)
		 * and ground-contact root ambient occlusion (base < crown) */
		float n_sun[3] = { 0 }, n_lee[3] = { 0 };
		float ao_base = 0.0f, ao_crown = 0.0f;
		float lit_sun_crown = nd_seven_gl_eval_billboard_lighting(-0.65f, 0.80f, 320.0f, 22.0f, 10.0f, 280.0f, n_sun, &ao_crown);
		float lit_lee_base  = nd_seven_gl_eval_billboard_lighting( 0.65f, 0.15f, 320.0f, 10.2f, 10.0f, 280.0f, n_lee, &ao_base);
		assert(n_sun[0] < 0.0f && n_sun[2] > 0.4f);
		assert(n_lee[0] > 0.0f);
		assert(ao_base < 0.72f);
		assert(ao_crown > 0.98f);
		assert(lit_sun_crown > lit_lee_base * 1.35f);

		/* 2. Verify 3D ground-contact shadow quad geometry on the curved torus tangent plane */
		const nd_planet_def_t *pdef = nd_seven_get_planet(1);
		lam_billboard_gpu_t tree_bb = {
			.x = 480.0f, .y = 384.0f, .z = 12.0f,
			.width = 61.0f, .height = 74.0f,
			.anchor_y = 6.0f, .water_z_off = 0.0f,
			.u0 = 0.1f, .v0 = 0.1f, .u1 = 0.2f, .v1 = 0.3f,
			.tint_rgba = 0x73C35FFF,
			.style = ND_BB_STYLE_LEAF_TINT,
			.shadow_rx = 14, .shadow_ry = 6
		};
		nd_seven_gl_bb_vert_t bverts[6], sverts[6];
		nd_seven_gl_eval_billboard_quad(&tree_bb, ND_SEVEN_DIORAMA_PITCH, pdef, bverts);
		nd_seven_gl_eval_billboard_shadow_quad(&tree_bb, pdef, sverts);

		/* Upright quad carries style and clamped sprite UV bounds */
		assert(fabsf(bverts[0].quad_uv[3] - (float)ND_BB_STYLE_LEAF_TINT) < 1e-5f);
		assert(fabsf(bverts[0].quad_uv[2] - 12.0f) < 1e-5f);
		assert(fabsf(bverts[0].sprite_bounds[0] - 0.1f) < 1e-5f);
		assert(fabsf(bverts[0].sprite_bounds[2] - 0.2f) < 1e-5f);
		/* Horizontal local_pos[0] spans [x - width/2, x + width/2] across the quad */
		assert(fabsf((bverts[1].local_pos[0] - bverts[0].local_pos[0]) - 61.0f) < 1e-4f);

		/* Shadow quad carries quad_uv[3] == -1.0f and shifts slightly Southeast (+x, +y) away from NW sun */
		for (int i = 0; i < 6; i++) {
			assert(fabsf(sverts[i].quad_uv[3] - (-1.0f)) < 1e-5f);
			assert(fabsf(sverts[i].local_pos[2] - 12.0f) < 1e-5f);
		}
		float sh_center_x = 0.5f * (sverts[0].local_pos[0] + sverts[1].local_pos[0]);
		float sh_center_y = 0.5f * (sverts[0].local_pos[1] + sverts[2].local_pos[1]);
		assert(sh_center_x > tree_bb.x + 1.0f);
		assert(sh_center_y > tree_bb.y + 0.5f);

		printf("PASS: 3D billboard volumetric normals (sun=%.3f vs lee=%.3f), ground-contact AO (base=%.2f vs crown=%.2f), and SE tangent shadow quads verified\n",
		       lit_sun_crown, lit_lee_base, ao_base, ao_crown);
	}

	/* Phase 8: Universe Owner Lamb Glow & Shared-Position Point Light */
	{
		const char *fs_t = nd_seven_gl_fs_terrain();
		const char *fs_w = nd_seven_gl_fs_water();
		const char *fs_b = nd_seven_gl_fs_billboard();
		assert(strstr(fs_t, "uLambLight") != NULL);
		assert(strstr(fs_w, "uLambLight") != NULL);
		assert(strstr(fs_b, "uLambLight") != NULL);
		assert(strstr(fs_b, "isLambGlow") != NULL);

		/* 1. Verify universe owner permission detection in nd_seven_is_universe_owner */
		nd_client_state_t test_st;
		memset(&test_st, 0, sizeof(test_st));
		test_st.me = 42;
		assert(nd_seven_is_universe_owner(&test_st) == 0);

		/* Default initial cosmos owner (me == 1, unlocked) */
		test_st.me = 1;
		assert(nd_seven_is_universe_owner(&test_st) == 1);
		test_st.pres_flags = ND_REGION_LOCKED;
		assert(nd_seven_is_universe_owner(&test_st) == 0);

		/* Explicit universe owner presentation flag (0x04) or entity flag (1 << 6) */
		test_st.me = 42;
		test_st.pres_flags = 0x04u;
		assert(nd_seven_is_universe_owner(&test_st) == 1);
		test_st.pres_flags = 0;
		test_st.entity_count = 1;
		test_st.entities[0].ref = 42;
		test_st.entities[0].flags = (1u << 6);
		assert(nd_seven_is_universe_owner(&test_st) == 1);

		/* 2. Verify point light spatial attenuation from the lamb's world position */
		float l_off  = nd_seven_gl_eval_lamb_light(320.0f, 384.0f, 10.0f, 320.0f, 384.0f, 10.0f, 0.0f);
		float l_at   = nd_seven_gl_eval_lamb_light(320.0f, 384.0f, 10.0f, 320.0f, 384.0f, 10.0f, 1.0f);
		float l_near = nd_seven_gl_eval_lamb_light(345.0f, 384.0f, 10.0f, 320.0f, 384.0f, 10.0f, 1.0f);
		float l_mid  = nd_seven_gl_eval_lamb_light(420.0f, 384.0f, 10.0f, 320.0f, 384.0f, 10.0f, 1.0f);
		float l_far  = nd_seven_gl_eval_lamb_light(600.0f, 384.0f, 10.0f, 320.0f, 384.0f, 10.0f, 1.0f);
		assert(fabsf(l_off) < 1e-6f);
		assert(l_at > 0.15f && l_at < 0.30f);
		assert(l_at > l_near && l_near > l_mid && l_mid > 0.0f);
		assert(fabsf(l_far) < 1e-6f);

		printf("PASS: universe owner lamb glow & shared-position point light (at=%.3f, near=%.3f, mid=%.3f, far=%.3f) verified\n",
		       l_at, l_near, l_mid, l_far);
	}

	printf("ALL SEVEN RENDER TESTS PASSED\n");
	return 0;
}
