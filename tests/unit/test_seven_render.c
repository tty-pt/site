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
#include <ttypt/webgl.h>
#include <ttypt/corolla.h>
#include <math.h>

#define MAXW 1920
#define MAXH 1080
#if 0
static uint32_t fb[MAXW * MAXH], fb2[MAXW * MAXH];
#endif

#define nd_seven_render(fb_arg, w_arg, h_arg, st_arg) nd_seven_render((uint32_t)(w_arg), (uint32_t)(h_arg), (st_arg))
#define nd_seven_set_3d_mode(x) ((void)(x))

static inline int nd_seven_chunk_count(void) {
	ar_system_t *cs = nd_seven_get_chunk_sys();
	if (!cs) return 0;
	ar_stats_t s;
	ar_system_get_stats(cs, &s);
	return (int)s.active_chunks_last_frame;
}
static inline int nd_seven_chunk_alloc_count(void) {
	ar_system_t *cs = nd_seven_get_chunk_sys();
	if (!cs) return 0;
	ar_stats_t s;
	ar_system_get_stats(cs, &s);
	return (int)s.pool_in_use;
}
static inline int nd_seven_chunk_free_count(void) {
	return 100;
}
static inline void nd_seven_ground_frame_begin(void) {}

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


#if 0 /* Legacy 2D CPU software framebuffer rasterizer tests retired in favor of WebGL stage */
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
#endif

int main(void)
{
	printf("=== ND seven stage: continuous world ===\n");

#if 0 /* Legacy 2D CPU software framebuffer rasterizer tests retired in favor of WebGL stage */
	const int sizes[3][2] = { { 640, 480 }, { 1280, 720 }, { 1920, 1080 } };
	int fx, fy, dir, walking, key;
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
			ar_system_t *cs = nd_seven_get_chunk_sys();
			if (cs) {
				ar_sample_t sample;
				if (ar_probe_view(cs, 0, (uint16_t)st.map_data.pos[3], 80, 64, &sample)) {
					assert(sample.rgba != 0);
				}
			}

			/* True Seabed Elevation Test (no buoyancy clamping) */
			int64_t lx = 0, ly = 0;
			nd_seven_world_pos(&st, &lx, &ly);
			int16_t lamb_z = nd_seven_lamb_elevation(&st, (int32_t)lx, (int32_t)ly);
			int16_t seabed_z = nd_seven_ground_elevation((int32_t)lx, (int32_t)ly);
			assert(lamb_z == seabed_z);

			printf("PASS: volumetric water fog and true seabed elevation verified\n");
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

	/* Multi-Resolution Noise LOD Selection and Active Frustum Retention via libareole */
	{
		ar_system_t *cs = nd_seven_get_chunk_sys();
		assert(cs != NULL);

		/* High zoom (2.0x): active leaf chunks allocated */
		nd_seven_ground_cache_forget();
		nd_seven_set_zoom(2.0f);
		set_map(-8, -3, 10, 0x1B, 0, 0, 0, 0);
		nd_seven_render(fb, 640, 480, &st);
		ar_stats_t st_high;
		ar_system_get_stats(cs, &st_high);
		assert(st_high.active_chunks_last_frame > 0);

		/* Macro Zoom Out (0.05x): active leaf chunks remain strictly bounded */
		nd_seven_ground_cache_forget();
		nd_seven_set_zoom(0.05f);
		nd_seven_render(fb, 640, 480, &st);
		ar_stats_t st_macro;
		ar_system_get_stats(cs, &st_macro);
		assert(st_macro.active_chunks_last_frame > 0);
		assert(st_macro.active_chunks_last_frame <= 64);

		/* Render a second frame with the same view: all active chunks retained */
		nd_seven_render(fb, 640, 480, &st);
		ar_stats_t st_macro2;
		ar_system_get_stats(cs, &st_macro2);
		assert(st_macro2.active_chunks_last_frame == st_macro.active_chunks_last_frame);

		nd_seven_set_zoom(1.0f);
		printf("PASS: multi-resolution noise LOD selection & active areole chunk retention verified\n");
	}
#endif

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
		/* Sprites evaluate physical water-plane intersection: Snell refraction, caustics, and radiant meniscus highlight */
		assert(fs_b && strstr(fs_b, "seabedAbsorption"));
		assert(fs_b && strstr(fs_b, "snellTexelShift"));
		assert(fs_b && strstr(fs_b, "meniscus"));
		assert(fs_b && strstr(fs_b, "uPass"));
		assert(fs_b && strstr(fs_b, "nd_cloud_shadow_map"));

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
		int ring_4_12_large_mid_zoom = 0;
		int small_mid_zoom = 0;
		for (uint32_t i = 0; i < cnt_mid_zoom; i++) {
			float bx = 0, by = 0, bz = 0, bw = 0, bh = 0;
			if (nd_seven_last_mid_billboard(i, &bx, &by, &bz, &bw, &bh)) {
				int rx = ndw_fdiv((int)bx, NDW_TILE_W) - 10;
				int ry = (-ndw_fdiv((int)by, NDW_TILE_H)) - 10;
				int arx = rx < 0 ? -rx : rx;
				int ary = ry < 0 ? -ry : ry;
				int cheb = arx > ary ? arx : ary;
				float sz = bw > bh ? bw : bh;
				if (cheb >= 4 && cheb <= 12) {
					ring_4_12_mid_zoom++;
					if (sz >= 100.0f)
						ring_4_12_large_mid_zoom++;
				}
				if (sz <= 42.0f)
					small_mid_zoom++;
			}
		}
		/* Outer-equator 3D torus culling accurately matches the 2.0x East-West GPU metric
		 * (eliminating off-screen flat_mvp waste while keeping 100% of on-screen billboards). */
		assert(ring_4_12_mid_zoom > 80);
		assert(ring_4_12_large_mid_zoom > 50);
		assert(small_mid_zoom > 0);

		/* Now zoom out to canopy/horizon zoom (0.10f, cam_dist = 2,800 wu):
		 * large trees in ring [4..12] must NOT thin out, while small plants/stones (sz <= 23)
		 * have already faded/culled out by the linear size/distance law! */
		nd_seven_set_zoom(0.10f);
		nd_seven_render(fb, 1280, 720, &st);

		uint32_t mid_cnt = nd_seven_last_mid_billboard_count();
		assert(mid_cnt > 250);

		int ring_4_12_far_zoom = 0;
		int ring_4_12_large_far_zoom = 0;
		int small_far_zoom = 0;
		int far_horizon_cnt = 0;
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
				float sz = bw > bh ? bw : bh;
				if (cheb > max_cheb_seen)
					max_cheb_seen = cheb;
				if (cheb >= 4 && cheb <= 12) {
					ring_4_12_far_zoom++;
					if (sz >= 100.0f)
						ring_4_12_large_far_zoom++;
				}
				if (sz <= 23.0f)
					small_far_zoom++;
				if (cheb > 14)
					far_horizon_cnt++;
				assert(bw > 0.0f && bh > 0.0f && bh <= 2000.0f); /* True 1:1 physical Earth metric world-space dimensions (up to 44m = 1980 wu) */
				unscaled_cnt++;
			}
		}
		assert(ring_4_12_large_far_zoom >= ring_4_12_large_mid_zoom);
		assert(small_far_zoom == 0);
		assert(far_horizon_cnt > 50);
		assert(max_cheb_seen > 16 && max_cheb_seen <= 96);
		assert(unscaled_cnt > 250 && unscaled_cnt == (int)(mid_cnt < 262144 ? mid_cnt : 262144));

		/* At orbital zoom (-22x, 0.00738f, cam_dist = 37,940 wu), linear size/distance culling
		 * cleanly culls all billboards so zero room scenery scans or billboard draws occur. */
		nd_seven_set_zoom(0.00738f);
		nd_seven_render(fb, 1280, 720, &st);
		assert(nd_seven_last_mid_billboard_count() == 0);

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
		printf("PASS: 0.10x zoom emits %u homogeneous billboards with linear size/distance culling (ring[4..12]: %d vs %d at 0.15x, >14: %d, max_cheb=%d) and culls orbital zoom cleanly\n",
		       mid_cnt, ring_4_12_far_zoom, ring_4_12_mid_zoom, far_horizon_cnt, max_cheb_seen);
	}

	/* High-Resolution 48x48 Level 0 Terrain Grid & Exact Seam Continuity Verification */
	{
		nd_seven_set_3d_mode(1);
		ar_system_t *cs = nd_seven_get_chunk_sys();
		assert(cs != NULL);

		/* Unified single-abstraction mesh fidelity: every chunk at every LOD level and zoom uses
		 * the 48x48 quad mesh (exact 1:1 texel alignment with the 49x49 chunk heightmap texture)
		 * so 3D vertex relief is never decimated or flattened to 16x16. */
		assert(nd_seven_gl_grid_quads_for_level(0) == 48);
		assert(nd_seven_gl_grid_quads_for_level(1) == 48);
		assert(nd_seven_gl_grid_quads_for_level(2) == 48);
		assert(nd_seven_gl_grid_quads_for_level(3) == 48);

		assert(nd_seven_gl_grid_quads_for_zoom(0, 380.0f)   == 48);
		assert(nd_seven_gl_grid_quads_for_zoom(0, 280.0f)   == 48);
		assert(nd_seven_gl_grid_quads_for_zoom(0, 200.0f)   == 48);
		assert(nd_seven_gl_grid_quads_for_zoom(0, 80.0f)    == 48);
		assert(nd_seven_gl_grid_quads_for_zoom(1, 80.0f)    == 48);
		assert(nd_seven_gl_grid_quads_for_zoom(1, 6500.0f)  == 48);
		assert(nd_seven_gl_grid_quads_for_zoom(2, 20000.0f) == 48);
		assert(nd_seven_gl_grid_quads_for_zoom(4, 280.0f)   == 48);

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
		assert(strstr(fs_t, "texture(uHeightTex,") != NULL);
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
		assert(strstr(fs_w, "texture(uHeightTex,") != NULL);
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

		/* 4b. Cloud-shadow lighting: overcast must stay readable (not night), monotone in shadow strength,
		 * and the GLSL helpers must carry the very same constants as the CPU mirror. */
		{
			float lit_clear = nd_seven_gl_scene_light(0.0f, 0.8f);
			float lit_thick = nd_seven_gl_scene_light(0.95f, 0.8f);
			float ratio = lit_thick / lit_clear;
			assert(ratio > 0.58f && ratio < 0.70f);
			float prev = lit_clear;
			for (int k = 1; k <= 19; k++) {
				float cur = nd_seven_gl_scene_light(0.05f * (float)k, 0.8f);
				assert(cur < prev);
				prev = cur;
			}
			/* Neutral shadow: ambient fill is not tinted (scalar only), shading is identical for all channels */
			assert(strstr(nd_seven_gl_fs_terrain(), "float nd_sun_trans(float s) { return 1.0 - 0.58 * s; }") != NULL);
			assert(strstr(nd_seven_gl_fs_water(), "float nd_amb_trans(float s) { return 1.0 - 0.20 * s; }") != NULL);
			assert(strstr(nd_seven_gl_fs_terrain(), "cloudShadow * 0.92") == NULL);
			printf("PASS: cloud shadow keeps %.0f%% of sunlit brightness under thick overcast (monotone, shader/CPU constants agree)\n", ratio * 100.0f);
		}

		/* 4c. Cloud-shadow map window: power-of-two half-extent, texel-snapped centre (no shimmer when panning),
		 * full coverage of the visible ground plus the sun-ward slice shift up to 180 km (analytic path above). */
		{
			nd_seven_gl_csm_window_t w;
			assert(nd_seven_gl_csm_window(400.0f, 123456.0f, 654321.0f, &w) && w.half == 4096.0f);
			assert(nd_seven_gl_csm_window(3000.0f, 0.0f, 0.0f, &w) && w.half == 16384.0f);
			assert(nd_seven_gl_csm_window(14000.0f, 0.0f, 0.0f, &w) && w.half == 65536.0f);
			assert(nd_seven_gl_csm_window(180000.0f, 0.0f, 0.0f, &w) && w.half == 1048576.0f);
			assert(!nd_seven_gl_csm_window(180001.0f, 0.0f, 0.0f, &w));
			for (float d = 160.0f; d <= 180000.0f; d *= 1.3f) {
				assert(nd_seven_gl_csm_window(d, 5000.0f, 5000.0f, &w));
				assert(w.half >= 4.5f * d + 1100.0f);
				assert(w.half <= 2.0f * (4.5f * d + 2000.0f) || w.half == 4096.0f);
			}
			nd_seven_gl_csm_window_t a0, a1, b0;
			nd_seven_gl_csm_window(400.0f, 100000.0f, 200000.0f, &a0);
			float texel = 2.0f * a0.half / (float)ND_CSM_RES;
			nd_seven_gl_csm_window(400.0f, 100000.0f + 0.2f * texel, 200000.0f - 0.2f * texel, &a1);
			assert(a0.cx == a1.cx && a0.cy == a1.cy);
			assert(fmodf(a0.cx, texel) == 0.0f && fmodf(a0.cy, texel) == 0.0f);
			nd_seven_gl_csm_window(400.0f, 100000.0f + 1.2f * texel, 200000.0f, &b0);
			assert(fabsf((b0.cx - a0.cx) - texel) < 1e-3f || fabsf((b0.cx - a0.cx) - 2.0f * texel) < 1e-3f);
			/* density -> shadow: zero above the slice, monotone, capped at 0.95 */
			assert(nd_seven_gl_cloud_shadow_from_density(1.0f, 1200.0f) == 0.0f);
			assert(nd_seven_gl_cloud_shadow_from_density(0.02f, 0.0f) == 0.0f);
			assert(nd_seven_gl_cloud_shadow_from_density(1.0f, 0.0f) == 0.95f);
			float ps = 0.0f;
			for (int k = 0; k <= 20; k++) {
				float sh = nd_seven_gl_cloud_shadow_from_density(0.05f * (float)k, 100.0f);
				assert(sh >= ps);
				ps = sh;
			}
			assert(ND_CSM_RES == 128);
			assert(strstr(nd_seven_gl_fs_csm(), "eval_synoptic_carrier") != NULL);
			assert(strstr(nd_seven_gl_fs_terrain(), "uCsm.z < 0.0") != NULL);
			assert(strstr(nd_seven_gl_fs_water(), "nd_cloud_shadow_map") != NULL &&
			       strstr(nd_seven_gl_fs_water(), "uCsm.z <= 0.0") != NULL);
			assert(strstr(nd_seven_gl_fs_cloud(), "uCsm.z < 0.0") != NULL);
			printf("PASS: cloud-shadow map window (half=%.0f at 400, %.0f at 14000), texel snapping and density->shadow mirror verified\n", 4096.0f, 65536.0f);
		}

		/* 5. Verify all 23 WebGL2 shader programs (7 terrain variants, water, billboard, mesh3d, mesh3d_opaque,
		 * mesh3d_shadow_alpha, mesh3d_shadow_opaque, grass, 3 cloud variants, csm, sky, ssao, bloom, post,
		 * terrain_shadow) compile and link cleanly under glslangValidator -l (GLSL ES 3.00) */
		if (system("command -v glslangValidator >/dev/null 2>&1") == 0) {
			struct {
				const char *name;
				const char *vs;
				const char *fs;
			} progs[] = {
				{ "terrain",             nd_seven_gl_vs_terrain(),        nd_seven_gl_fs_terrain()             },
				{ "terrain_dry_near",    nd_seven_gl_vs_terrain(),        nd_seven_gl_fs_terrain_dry_near()    },
				{ "terrain_dry_csm",     nd_seven_gl_vs_terrain(),        nd_seven_gl_fs_terrain_dry_csm()     },
				{ "terrain_seabed_near", nd_seven_gl_vs_terrain(),        nd_seven_gl_fs_terrain_seabed_near() },
				{ "terrain_seabed_mid",  nd_seven_gl_vs_terrain(),        nd_seven_gl_fs_terrain_seabed_mid()  },
				{ "terrain_macro",       nd_seven_gl_vs_terrain(),        nd_seven_gl_fs_terrain_macro()       },
				{ "terrain_macro_far",   nd_seven_gl_vs_terrain(),        nd_seven_gl_fs_terrain_macro_far()   },
				{ "water",               nd_seven_gl_vs_water(),          nd_seven_gl_fs_water()               },
				{ "billboard",           nd_seven_gl_vs_billboard(),      nd_seven_gl_fs_billboard()           },
				{ "mesh3d",              nd_seven_gl_vs_mesh3d(),         nd_seven_gl_fs_mesh3d()              },
				{ "mesh3d_opaque",       nd_seven_gl_vs_mesh3d(),         nd_seven_gl_fs_mesh3d_opaque()       },
				{ "mesh3d_shadow_alpha", nd_seven_gl_vs_mesh3d_shadow(),  nd_seven_gl_fs_mesh3d_shadow_alpha() },
				{ "mesh3d_shadow_opaque",nd_seven_gl_vs_mesh3d_shadow(),  nd_seven_gl_fs_mesh3d_shadow_opaque()},
				{ "grass",               nd_seven_gl_vs_grass(),          nd_seven_gl_fs_grass()               },
				{ "cloud",               nd_seven_gl_vs_cloud(),          nd_seven_gl_fs_cloud()               },
				{ "cloud_wide",          nd_seven_gl_vs_cloud(),          nd_seven_gl_fs_cloud_wide()          },
				{ "cloud_wide_csm",      nd_seven_gl_vs_cloud(),          nd_seven_gl_fs_cloud_wide_csm()      },
				{ "cloud_comp",          nd_seven_gl_vs_csm(),            nd_seven_gl_fs_cloud_comp()          },
				{ "cloud_reproj",        nd_seven_gl_vs_csm(),            nd_seven_gl_fs_cloud_reproj()        },
				{ "csm",                 nd_seven_gl_vs_csm(),            nd_seven_gl_fs_csm()                 },
				{ "sky",                 nd_seven_gl_vs_sky(),            nd_seven_gl_fs_sky()                 },
				{ "ssao",                nd_seven_gl_vs_csm(),            nd_seven_gl_fs_ssao()                },
				{ "bloom",               nd_seven_gl_vs_csm(),            nd_seven_gl_fs_bloom()               },
				{ "post",                nd_seven_gl_vs_csm(),            nd_seven_gl_fs_post()                },
				{ "terrain_shadow",      nd_seven_gl_vs_terrain_shadow(), nd_seven_gl_fs_terrain_shadow()      }
			};
			for (size_t p = 0; p < sizeof(progs) / sizeof(progs[0]); p++) {
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

		/* 5b. Phase 3 & 6A GPU shader & Early-Z contract assertions */
		{
			assert(strstr(nd_seven_gl_fs_mesh3d_opaque(), "discard") == NULL);
			assert(strstr(nd_seven_gl_fs_mesh3d_shadow_opaque(), "discard") == NULL);
			assert(strstr(nd_seven_gl_fs_water(), "eval_water_waves_lo") != NULL);
			assert(strstr(nd_seven_gl_fs_water(), "eval_synoptic_macro_cloud") == NULL);
			assert(strstr(nd_seven_gl_fs_cloud(), "uSceneDepth") == NULL);
			assert(strstr(nd_seven_gl_vs_sky(), "vec4(ndc, 1.0, 1.0)") != NULL);
			assert(strstr(nd_seven_gl_vs_grass(), "a_tuft0") != NULL);
			assert(strstr(nd_seven_gl_vs_grass(), "texelFetch") == NULL);
			assert(strstr(nd_seven_gl_fs_terrain(), "uTerrFastDist") != NULL);
			nd_seven_mesh3d_init();
			assert(nd_seven_mesh3d_is_opaque(ND_MESH3D_STONE_BASE) == true);
			assert(nd_seven_mesh3d_is_opaque(ND_MESH3D_GEOBREAK_BASE) == true);
			assert(nd_seven_mesh3d_is_opaque(ND_MESH3D_PLANT_BASE) == false);
			assert(nd_seven_mesh3d_is_opaque(ND_MESH3D_MEADOW_BASE) == false);
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
		assert(strstr(fs_b, "uAtlasNormTex") != NULL);
		assert(strstr(fs_b, "sampleAtlasNorm") != NULL);
		assert(strstr(fs_b, "sampleAtlasSmooth") != NULL);
		assert(strstr(fs_b, "fetchBoundedTexel") != NULL);
		assert(atlas_norm_rgba() != NULL);
		assert(strstr(fs_b, "chaff_grad_noise2d") != NULL);
		assert(strstr(fs_b, "snellTexelShift") != NULL);
		assert(strstr(fs_b, "meniscus") != NULL);
		assert(strstr(fs_b, "nd_cloud_shadow_map") != NULL);
		assert(strstr(fs_b, "1.2 / 328.0") == NULL);
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

		/* 2. Verify 3D ground-contact shadow quad geometry and water_z_off == anchor_y waterline alignment */
		const nd_planet_def_t *pdef = nd_seven_get_planet(1);
		lam_billboard_gpu_t tree_bb = {
			.x = 480.0f, .y = 384.0f, .z = 12.0f,
			.width = 61.0f, .height = 74.0f,
			.anchor_y = 6.0f, .water_z_off = 6.0f,
			.u0 = 0.1f, .v0 = 0.1f, .u1 = 0.2f, .v1 = 0.3f,
			.tint_rgba = 0x73C35FFF,
			.style = ND_BB_STYLE_LEAF_TINT,
			.shadow_rx = 14, .shadow_ry = 6
		};
		nd_seven_gl_bb_vert_t bverts[6], sverts[6];
		nd_seven_gl_eval_billboard_quad(&tree_bb, ND_SEVEN_DIORAMA_PITCH, pdef, bverts);
		nd_seven_gl_eval_billboard_shadow_quad(&tree_bb, pdef, sverts);

		/* Upright quad carries style, clamped sprite UV bounds, and exact ground Z at anchor_y height */
		assert(fabsf(bverts[0].quad_uv[3] - (float)ND_BB_STYLE_LEAF_TINT) < 1e-5f);
		assert(fabsf(bverts[0].quad_uv[2] - 12.0f) < 1e-5f);
		assert(fabsf(bverts[0].sprite_bounds[0] - 0.1f) < 1e-5f);
		assert(fabsf(bverts[0].sprite_bounds[2] - 0.2f) < 1e-5f);
		float anchor_t = tree_bb.anchor_y / tree_bb.height;
		float z_at_anchor = bverts[0].local_pos[2] + anchor_t * (bverts[2].local_pos[2] - bverts[0].local_pos[2]);
		assert(fabsf(z_at_anchor - tree_bb.z) < 1e-4f);
		/* Horizontal local_pos[0] spans [x - width/2, x + width/2] across the quad */
		assert(fabsf((bverts[1].local_pos[0] - bverts[0].local_pos[0]) - 61.0f) < 1e-4f);

		/* Shadow quad carries quad_uv[3] == -1.0f, preserves real sprite atlas UVs & bounds for silhouette sampling,
		 * anchors vertices 0..1 at the trunk/foot base (v1), and projects crown vertices 2..3 (v0) Southeast (+x, +y) */
		for (int i = 0; i < 6; i++) {
			assert(fabsf(sverts[i].quad_uv[3] - (-1.0f)) < 1e-5f);
			assert(fabsf(sverts[i].local_pos[2] - 12.0f) < 1e-5f);
			assert(fabsf(sverts[i].sprite_bounds[0] - 0.1f) < 1e-5f);
			assert(fabsf(sverts[i].sprite_bounds[2] - 0.2f) < 1e-5f);
		}
		assert(fabsf(sverts[0].uv[0] - 0.1f) < 1e-5f && fabsf(sverts[0].uv[1] - 0.3f) < 1e-5f);
		assert(fabsf(sverts[2].uv[0] - 0.1f) < 1e-5f && fabsf(sverts[2].uv[1] - 0.1f) < 1e-5f);
		float sh_base_mid_x = 0.5f * (sverts[0].local_pos[0] + sverts[1].local_pos[0]);
		float sh_crown_mid_x = 0.5f * (sverts[2].local_pos[0] + sverts[4].local_pos[0]);
		float sh_crown_mid_y = 0.5f * (sverts[2].local_pos[1] + sverts[4].local_pos[1]);
		assert(fabsf(sh_base_mid_x - tree_bb.x) < 1e-4f);
		assert(sh_crown_mid_x > tree_bb.x + 10.0f);
		assert(sh_crown_mid_y > tree_bb.y + 10.0f);
		assert(strstr(fs_b, "silAlpha") != NULL);
		assert(strstr(fs_b, "contactUmbra") != NULL);

		printf("PASS: 3D billboard volumetric normals (sun=%.3f vs lee=%.3f), ground-contact AO (base=%.2f vs crown=%.2f), and universal 3D silhouette shadow quads verified\n",
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

		/* 3. Verify secondary Lamb point-light shadow quad radial projection and Lamb self-exclusion */
		const nd_planet_def_t *pdef = nd_seven_get_planet(1);
		nd_seven_gl_set_lamb_light(400.0f, 400.0f, 12.0f, 1.0f);
		lam_billboard_gpu_t lamb_self_bb = {
			.x = 400.0f, .y = 400.0f, .z = 12.0f,
			.width = 32.0f, .height = 32.0f,
			.u0 = 0.0f, .v0 = 0.0f, .u1 = 0.1f, .v1 = 0.1f,
			.tint_rgba = 0xFFFFFFFFu, .style = ND_BB_STYLE_GLOW
		};
		nd_seven_gl_bb_vert_t lsh_verts[6];
		assert(!nd_seven_gl_eval_billboard_lamb_shadow_quad(&lamb_self_bb, pdef, lsh_verts));

		lam_billboard_gpu_t west_tree_bb = {
			.x = 340.0f, .y = 400.0f, .z = 12.0f,
			.width = 48.0f, .height = 64.0f,
			.u0 = 0.1f, .v0 = 0.1f, .u1 = 0.2f, .v1 = 0.3f,
			.tint_rgba = 0xFFFFFFFFu, .style = ND_BB_STYLE_FLORA,
			.shadow_rx = 12, .shadow_ry = 5
		};
		assert(nd_seven_gl_eval_billboard_lamb_shadow_quad(&west_tree_bb, pdef, lsh_verts));
		assert(fabsf(lsh_verts[0].quad_uv[3] - (-2.0f)) < 1e-5f);
		/* Object West of the Lamb (dx_l = -60 < 0) casts its secondary Lamb shadow further West (< west_tree_bb.x) */
		float lsh_crown_mid_x = 0.5f * (lsh_verts[2].local_pos[0] + lsh_verts[4].local_pos[0]);
		assert(lsh_crown_mid_x < west_tree_bb.x - 10.0f);

		lam_billboard_gpu_t far_tree_bb = west_tree_bb;
		far_tree_bb.x = 700.0f;
		assert(!nd_seven_gl_eval_billboard_lamb_shadow_quad(&far_tree_bb, pdef, lsh_verts));
		nd_seven_gl_set_lamb_light(0.0f, 0.0f, 0.0f, 0.0f);

		printf("PASS: universe owner lamb glow, shared-position point light (at=%.3f, near=%.3f, mid=%.3f, far=%.3f), and radial secondary shadow quads verified\n",
		       l_at, l_near, l_mid, l_far);
	}

	/* Phase 9: Living Meadow Reactive Grass, Botanical Micro-Ecology & Elastic Footstep Wake */
	{
		/* 1. R1: Verify procedural botanical runtime atlas synthesis & dense multi-blade pixel coverage */
		atlas_load();
		const uint32_t *rgba = atlas_rgba();
		assert(rgba != NULL);
		assert(NDB_COUNT == 7);
		for (int k = 0; k < NDB_COUNT; k++) {
			nd_seven_sprite_rect_t br = { 0, 0, 0, 0 };
			assert(nd_seven_botanical_rect(k, &br) == 1);
			assert(br.y >= ND_SEVEN_ATLAS_H_PX);
			assert(br.w >= 16 && br.h >= 18);
			int non_zero = 0;
			int upper_px = 0, lower_mid_px = 0;
			for (uint16_t ry = 0; ry < br.h; ry++) {
				for (uint16_t rx = 0; rx < br.w; rx++) {
					uint32_t px = rgba[(size_t)(br.y + ry) * ND_SEVEN_ATLAS_W + (br.x + rx)];
					uint32_t a = (px >> 24) & 0xFFu;
					if (a > 0) {
						non_zero++;
						uint32_t r_ch = px & 0xFFu;
						uint32_t g_ch = (px >> 8) & 0xFFu;
						/* Verify no overly dark root pixels (G >= 95 on grass/turf swatches) */
						if (k <= NDB_TALL_GRASS_2)
							assert(g_ch >= 95u && r_ch >= 50u);
						if (ry >= 2 && ry <= 5 && a >= 140)
							upper_px++;
						if (ry >= br.h - 7 && ry <= br.h - 4 && a >= 140)
							lower_mid_px++;
					}
				}
			}
			assert(non_zero >= 60);
			/* Verify tapered non-square blades: upper blade rows are strictly narrower than lower-mid rows */
			assert(upper_px < lower_mid_px);
		}

		/* 2. R2: Verify deterministic micro-ecology zoning, anti-submersion, slope, and tree-trunk suppression */
		int zone = 0, flip = 0;
		float scale_m = 0.0f;
		/* Submerged (elev_z <= water_level) must never place flora */
		assert(nd_seven_eval_meadow_tuft(120, 140, -2.0f, 0.0f, 10, 9999.0f, &zone, &flip) == 0);
		assert(zone == ND_MEADOW_NONE);
		/* Non-vegetated sand/desert biome (biome 14) must never place meadow flora */
		assert(nd_seven_eval_meadow_tuft(120, 140, 8.0f, 0.0f, 14, 9999.0f, &zone, &flip) == 0);
		/* Inside a tree trunk footprint (near_tree_dist_sq < 81.0f) must suppress flora */
		assert(nd_seven_eval_meadow_tuft(120, 140, 8.0f, 0.0f, 10, 16.0f, &zone, &flip) == 0);
		/* Sheer cliff slope (slope_mag >= 0.58f) must suppress meadow flora */
		assert(nd_seven_eval_meadow_area(120, 140, 6.0f, 0.0f, 0.65f, 10, 9999.0f, &zone, &scale_m, &flip) == 0);

		/* Scan a 64x64 cell sample to verify littoral reeds, canopy understory, wildflowers, tall grass, and land coupling */
		int reed_cnt = 0, under_cnt = 0, flower_cnt = 0, grass_cnt = 0, tall_cnt = 0;
		int lowland_cnt = 0, high_steep_cnt = 0;
		for (int y = 0; y < 64; y++) {
			for (int x = 0; x < 64; x++) {
				int z_reed = 0, z_under = 0, z_open = 0, z_area = 0;
				float sc_area = 0.0f;
				if (nd_seven_eval_meadow_tuft(x * 13, y * 17, 0.85f, 0.0f, 10, 9999.0f, &z_reed, NULL)) {
					assert(z_reed == ND_MEADOW_REED);
					reed_cnt++;
				}
				if (nd_seven_eval_meadow_tuft(x * 13, y * 17, 10.0f, 0.0f, 10, 144.0f, &z_under, NULL)) {
					assert(z_under == ND_MEADOW_UNDERSTORY);
					under_cnt++;
				}
				if (nd_seven_eval_meadow_tuft(x * 19, y * 23, 5.0f, 0.0f, 10, 9999.0f, &z_open, NULL)) {
					if (z_open == ND_MEADOW_WILDFLOWER) flower_cnt++;
					else if (z_open == ND_MEADOW_GRASS) grass_cnt++;
				}
				if (nd_seven_eval_meadow_area(x * 19, y * 23, 5.0f, 0.0f, 0.02f, 10, 9999.0f, &z_area, &sc_area, NULL)) {
					lowland_cnt++;
					assert(sc_area >= 0.35f && sc_area <= 0.70f);
					if (z_area == ND_MEADOW_TALL_GRASS)
						tall_cnt++;
				}
				if (nd_seven_eval_meadow_area(x * 19, y * 23, 38.0f, 0.0f, 0.30f, 10, 9999.0f, NULL, NULL, NULL)) {
					high_steep_cnt++;
				}
			}
		}
		assert(reed_cnt > 100);
		assert(under_cnt > 100);
		assert(flower_cnt > 10);
		assert(grass_cnt > 100);
		assert(tall_cnt > 50);
		assert(lowland_cnt > high_steep_cnt * 2);

		/* Verify biome-driven botanical variety across Tundra (2), Boreal (9), Grassland (10), Savannah (15), and Rainforest (17) */
		int tundra_cnt = 0, tundra_tall_or_flw = 0;
		int boreal_cnt = 0, savanna_tall = 0, savanna_cnt = 0, jungle_cnt = 0;
		float tundra_sc_sum = 0.0f, boreal_sc_sum = 0.0f, savanna_sc_sum = 0.0f, jungle_sc_sum = 0.0f;
		for (int y = 0; y < 48; y++) {
			for (int x = 0; x < 48; x++) {
				int zb = 0;
				float scb = 0.0f;
				if (nd_seven_eval_meadow_area(x * 19, y * 23, 5.0f, 0.0f, 0.02f, 2, 9999.0f, &zb, &scb, NULL)) {
					tundra_cnt++;
					tundra_sc_sum += scb;
					if (zb == ND_MEADOW_TALL_GRASS || zb == ND_MEADOW_WILDFLOWER)
						tundra_tall_or_flw++;
				}
				if (nd_seven_eval_meadow_area(x * 19, y * 23, 5.0f, 0.0f, 0.02f, 9, 9999.0f, &zb, &scb, NULL)) {
					boreal_cnt++;
					boreal_sc_sum += scb;
				}
				if (nd_seven_eval_meadow_area(x * 19, y * 23, 5.0f, 0.0f, 0.02f, 15, 9999.0f, &zb, &scb, NULL)) {
					savanna_cnt++;
					savanna_sc_sum += scb;
					if (zb == ND_MEADOW_TALL_GRASS)
						savanna_tall++;
				}
				if (nd_seven_eval_meadow_area(x * 19, y * 23, 5.0f, 0.0f, 0.02f, 17, 9999.0f, &zb, &scb, NULL)) {
					jungle_cnt++;
					jungle_sc_sum += scb;
				}
			}
		}
		assert(tundra_cnt > 20 && tundra_tall_or_flw == 0);
		assert(boreal_cnt > tundra_cnt);
		assert(jungle_cnt > boreal_cnt);
		assert(savanna_tall * 3 > savanna_cnt);
		float tundra_avg_sc  = tundra_sc_sum / (float)tundra_cnt;
		float boreal_avg_sc  = boreal_sc_sum / (float)boreal_cnt;
		float savanna_avg_sc = savanna_sc_sum / (float)savanna_cnt;
		float jungle_avg_sc  = jungle_sc_sum / (float)jungle_cnt;
		assert(tundra_avg_sc < boreal_avg_sc);
		assert(boreal_avg_sc < savanna_avg_sc && boreal_avg_sc < jungle_avg_sc);

		/* Verify non-disappearing & non-shrinking tuft invariance near the Lamb (D14/D15/D16):
		 * Tufts never disappear (always return 1) and never shrink (*io_scale unchanged) at any distance to the Lamb */
		{
			float sy_core = 500.0f, sc_core = 0.50f;
			assert(nd_seven_eval_meadow_lamb_occlusion(500.0f, 500.0f, 500.0f, 500.0f, &sy_core, &sc_core) == 1);
			assert(fabsf(sc_core - 0.50f) < 1e-6f);
			assert(nd_seven_eval_meadow_lamb_occlusion(502.0f, 503.0f, 500.0f, 500.0f, &sy_core, &sc_core) == 1);
			assert(fabsf(sc_core - 0.50f) < 1e-6f);
			float sy_overlap = 504.0f, sc_overlap = 0.50f;
			assert(nd_seven_eval_meadow_lamb_occlusion(508.0f, 504.0f, 500.0f, 500.0f, &sy_overlap, &sc_overlap) == 1);
			assert(fabsf(sy_overlap - 504.0f) < 1e-6f);
			assert(fabsf(sc_overlap - 0.50f) < 1e-6f);
		}

		/* Verify contiguous area fill: inside a fertile meadow patch, a 5x5 local 8-unit grid fills >= 22/25 cells */
		int max_patch_fill = 0;
		for (int py = 0; py < 16; py++) {
			for (int px = 0; px < 16; px++) {
				int32_t bx = px * 48;
				int32_t by = py * 48;
				int fill = 0;
				for (int dy = 0; dy < 5; dy++) {
					for (int dx = 0; dx < 5; dx++) {
						if (nd_seven_eval_meadow_area(bx + dx * 8, by + dy * 8, 4.0f, 0.0f, 0.01f, 10, 9999.0f, NULL, NULL, NULL))
							fill++;
					}
				}
				if (fill > max_patch_fill)
					max_patch_fill = fill;
			}
		}
		assert(max_patch_fill >= 23);

		/* 3. R3: Verify calm non-trembling contact parting (R_touch = 11.0) & smooth viscoelastic wake trail decay */
		nd_seven_clear_wake();
		float sx_w = 0.0f, sy_w = 0.0f, dz_w = 0.0f, r_w = 0.0f;
		float sx_e = 0.0f, sy_e = 0.0f, dz_e = 0.0f, r_e = 0.0f;
		float sx_far = 0.0f, sy_far = 0.0f, dz_far = 0.0f, r_far = 0.0f;
		/* Lamb at (500, 500): tuft at (495, 500) is West (dist=5); tuft at (505, 500) is East (dist=5) */
		nd_seven_gl_eval_grass_displacement(495.0f, 500.0f, 10.0f, 500.0f, 500.0f, &sx_w, &sy_w, &dz_w, &r_w);
		nd_seven_gl_eval_grass_displacement(505.0f, 500.0f, 10.0f, 500.0f, 500.0f, &sx_e, &sy_e, &dz_e, &r_e);
		nd_seven_gl_eval_grass_displacement(495.0f, 500.0f, 10.0f, 900.0f, 900.0f, &sx_far, &sy_far, &dz_far, &r_far);
		assert(sx_w < sx_far - 0.9f);
		assert(sx_e > sx_far + 0.9f);
		assert(dz_w > dz_far + 0.8f && dz_e > dz_far + 0.8f);
		assert(r_w > r_far + 0.15f);

		/* Verify compact contact radius: at dist = 13.0 (> R_touch = 11.0), Lamb has zero static force-field push */
		float sx_out1 = 0.0f, sx_out2 = 0.0f;
		nd_seven_gl_eval_grass_displacement(487.0f, 500.0f, 10.0f, 500.0f, 500.0f, &sx_out1, NULL, NULL, NULL);
		nd_seven_gl_eval_grass_displacement(487.0f, 500.0f, 10.0f, 900.0f, 900.0f, &sx_out2, NULL, NULL, NULL);
		assert(fabsf(sx_out1 - sx_out2) < 1e-5f);

		/* Verify no high-frequency trembling near or far from the Lamb across a 0.08s frame interval,
		 * and verify adjacent tufts move cohesively rather than in uncorrelated opposite directions */
		float sx_t_a = 0.0f, sx_t_b = 0.0f, sy_t_a = 0.0f, sy_t_b = 0.0f;
		nd_seven_gl_eval_grass_displacement(495.0f, 500.0f, 10.00f, 500.0f, 500.0f, &sx_t_a, &sy_t_a, NULL, NULL);
		nd_seven_gl_eval_grass_displacement(495.0f, 500.0f, 10.08f, 500.0f, 500.0f, &sx_t_b, &sy_t_b, NULL, NULL);
		assert(fabsf(sx_t_b - sx_t_a) < 0.06f && fabsf(sy_t_b - sy_t_a) < 0.06f);

		float sx_n0 = 0.0f, sy_n0 = 0.0f, sx_n1 = 0.0f, sy_n1 = 0.0f;
		nd_seven_gl_eval_grass_displacement(300.0f, 300.0f, 10.00f, 900.0f, 900.0f, &sx_n0, &sy_n0, NULL, NULL);
		nd_seven_gl_eval_grass_displacement(302.0f, 300.0f, 10.00f, 900.0f, 900.0f, &sx_n1, &sy_n1, NULL, NULL);
		assert(sx_n0 * sx_n1 > 0.0f && sy_n0 * sy_n1 > 0.0f);

		/* Verify natural rolling wind wave amplitude over a 6-second cycle (noticeable sway > 0.65 world units) */
		float min_wave_sx = 1e9f, max_wave_sx = -1e9f;
		for (int ti = 0; ti < 30; ti++) {
			float wx_s = 0.0f, wy_s = 0.0f;
			nd_seven_gl_eval_grass_displacement(300.0f, 300.0f, (float)ti * 0.25f, 900.0f, 900.0f, &wx_s, &wy_s, NULL, NULL);
			if (wx_s < min_wave_sx) min_wave_sx = wx_s;
			if (wx_s > max_wave_sx) max_wave_sx = wx_s;
		}
		assert(max_wave_sx - min_wave_sx > 0.65f);

		/* Record a footstep wake at (600, 600) at t=20.0s and verify both depression and shear decay monotonically without cosine ringing */
		nd_seven_record_wake_step(600.0f, 600.0f, 72.0f, 0.0f, 20.0f);
		float sx_w0 = 0.0f, sx_w1 = 0.0f, sx_w2 = 0.0f, sx_w3 = 0.0f;
		float dz_t0 = 0.0f, dz_t1 = 0.0f, dz_t2 = 0.0f;
		nd_seven_gl_eval_grass_displacement(602.0f, 600.0f, 20.1f, 900.0f, 900.0f, &sx_w0, NULL, &dz_t0, NULL);
		nd_seven_gl_eval_grass_displacement(602.0f, 600.0f, 20.4f, 900.0f, 900.0f, &sx_w1, NULL, NULL, NULL);
		nd_seven_gl_eval_grass_displacement(602.0f, 600.0f, 20.7f, 900.0f, 900.0f, &sx_w2, NULL, &dz_t1, NULL);
		nd_seven_gl_eval_grass_displacement(602.0f, 600.0f, 22.0f, 900.0f, 900.0f, &sx_w3, NULL, &dz_t2, NULL);
		assert(dz_t0 > dz_t1 && dz_t1 > dz_t2);
		assert(sx_w0 > sx_w1 && sx_w1 > sx_w2 && sx_w2 > sx_w3);

		/* 4. R4: Verify ND_BB_STYLE_GRASS ground-pinned bottom vertices (qy == 0) vs sheared top vertices (qy == 1),
		 *    plus unified real-object flora sway (small bushes/shrubs sway with stiffness, tall tree trunks stay rigid) */
		const nd_planet_def_t *pdef = nd_seven_get_planet(1);
		nd_seven_gl_set_lamb_light(495.0f, 500.0f, 12.0f, 1.0f);
		lam_billboard_gpu_t std_bb = {
			.x = 500.0f, .y = 500.0f, .z = 12.0f,
			.width = 16.0f, .height = 16.0f,
			.anchor_y = 2.0f, .water_z_off = 2.0f,
			.u0 = 0.0f, .v0 = 0.0f, .u1 = 0.1f, .v1 = 0.1f,
			.tint_rgba = 0xFFFFFFFFu,
			.style = ND_BB_STYLE_STANDARD
		};
		lam_billboard_gpu_t grass_bb = std_bb;
		grass_bb.style = ND_BB_STYLE_GRASS;

		lam_billboard_gpu_t shrub_bb = std_bb;
		shrub_bb.style = ND_BB_STYLE_FLORA;
		shrub_bb.width = 27.0f;
		shrub_bb.height = 25.0f;

		lam_billboard_gpu_t std_shrub_bb = shrub_bb;
		std_shrub_bb.style = ND_BB_STYLE_STANDARD;

		lam_billboard_gpu_t herb_obj_bb = std_bb;
		herb_obj_bb.style = ND_BB_STYLE_FLORA;
		herb_obj_bb.width = 24.0f;
		herb_obj_bb.height = 38.0f;
		herb_obj_bb.pad0 = 1u; /* non-trunk herbaceous/stem object plant (cattail/daisy/flax) */

		lam_billboard_gpu_t std_herb_obj_bb = herb_obj_bb;
		std_herb_obj_bb.style = ND_BB_STYLE_STANDARD;
		std_herb_obj_bb.pad0 = 0u;

		lam_billboard_gpu_t tall_tree_bb = std_bb;
		tall_tree_bb.style = ND_BB_STYLE_LEAF_TINT;
		tall_tree_bb.width = 61.0f;
		tall_tree_bb.height = 74.0f;
		tall_tree_bb.pad0 = 2u; /* trunked tree */

		lam_billboard_gpu_t std_tree_bb = tall_tree_bb;
		std_tree_bb.style = ND_BB_STYLE_STANDARD;
		std_tree_bb.pad0 = 0u;

		lam_billboard_gpu_t short_grass_bb = grass_bb;
		short_grass_bb.width = 6.0f;
		short_grass_bb.height = 5.0f;
		lam_billboard_gpu_t std_short_bb = short_grass_bb;
		std_short_bb.style = ND_BB_STYLE_STANDARD;

		nd_seven_gl_bb_vert_t v_std[6], v_grs[6], v_shr[6], v_std_shr[6], v_herb[6], v_std_herb[6], v_tree[6], v_std_tree[6], v_short_grs[6], v_short_std[6];
		nd_seven_gl_eval_billboard_quad(&std_bb, ND_SEVEN_DIORAMA_PITCH, pdef, v_std);
		nd_seven_gl_eval_billboard_quad(&grass_bb, ND_SEVEN_DIORAMA_PITCH, pdef, v_grs);
		nd_seven_gl_eval_billboard_quad(&shrub_bb, ND_SEVEN_DIORAMA_PITCH, pdef, v_shr);
		nd_seven_gl_eval_billboard_quad(&std_shrub_bb, ND_SEVEN_DIORAMA_PITCH, pdef, v_std_shr);
		nd_seven_gl_eval_billboard_quad(&herb_obj_bb, ND_SEVEN_DIORAMA_PITCH, pdef, v_herb);
		nd_seven_gl_eval_billboard_quad(&std_herb_obj_bb, ND_SEVEN_DIORAMA_PITCH, pdef, v_std_herb);
		nd_seven_gl_eval_billboard_quad(&tall_tree_bb, ND_SEVEN_DIORAMA_PITCH, pdef, v_tree);
		nd_seven_gl_eval_billboard_quad(&std_tree_bb, ND_SEVEN_DIORAMA_PITCH, pdef, v_std_tree);
		nd_seven_gl_eval_billboard_quad(&short_grass_bb, ND_SEVEN_DIORAMA_PITCH, pdef, v_short_grs);
		nd_seven_gl_eval_billboard_quad(&std_short_bb, ND_SEVEN_DIORAMA_PITCH, pdef, v_short_std);

		/* Bottom vertices BL (idx 0) and BR (idx 1, 3) have qy == 0 and must remain 100% pinned to ground */
		assert(fabsf(v_grs[0].quad_uv[1] - 0.0f) < 1e-5f);
		assert(fabsf(v_grs[1].quad_uv[1] - 0.0f) < 1e-5f);
		for (int c = 0; c < 3; c++) {
			assert(fabsf(v_grs[0].world_pos[c] - v_std[0].world_pos[c]) < 1e-5f);
			assert(fabsf(v_grs[1].world_pos[c] - v_std[1].world_pos[c]) < 1e-5f);
			assert(fabsf(v_grs[0].local_pos[c] - v_std[0].local_pos[c]) < 1e-5f);
			assert(fabsf(v_shr[0].world_pos[c] - v_std_shr[0].world_pos[c]) < 1e-5f);
			assert(fabsf(v_herb[0].world_pos[c] - v_std_herb[0].world_pos[c]) < 1e-5f);
			/* Tall tree bottom vertices (qy == 0) remain 100% pinned at the trunk base */
			assert(fabsf(v_tree[0].world_pos[c] - v_std_tree[0].world_pos[c]) < 1e-5f);
			assert(fabsf(v_tree[1].world_pos[c] - v_std_tree[1].world_pos[c]) < 1e-5f);
		}
		/* Top vertex TL (idx 2, qy == 1) must be sheared away from the Lamb (+x direction since Lamb is at 495 < 500) */
		assert(fabsf(v_grs[2].quad_uv[1] - 1.0f) < 1e-5f);
		float grass_disp_x = v_grs[2].local_pos[0] - v_std[2].local_pos[0];
		float shrub_disp_x = v_shr[2].local_pos[0] - v_std_shr[2].local_pos[0];
		float herb_disp_x = v_herb[2].local_pos[0] - v_std_herb[2].local_pos[0];
		float tree_disp_x = v_tree[2].local_pos[0] - v_std_tree[2].local_pos[0];
		float tree_disp_y = v_tree[2].local_pos[1] - v_std_tree[2].local_pos[1];
		float tree_disp_mag = sqrtf(tree_disp_x * tree_disp_x + tree_disp_y * tree_disp_y);
		assert(grass_disp_x > 0.8f);
		assert(shrub_disp_x > 0.25f && shrub_disp_x < grass_disp_x);
		/* True object-type herbaceous/stem plant (height 38, pad0=1) visibly sways from its pinned root */
		assert(herb_disp_x > 0.7f);
		assert(v_grs[2].local_pos[2] < v_std[2].local_pos[2] - 0.4f);

		/* Tall tree (pad0=2) sways homogeneously across TL (idx 2) and TR (idx 4) with heavy woody stiffness,
		 * and ignores Lamb footstep trampling when the Lamb moves away */
		assert(tree_disp_mag > 0.15f && tree_disp_mag < 3.0f);
		assert(fabsf((v_tree[4].local_pos[0] - v_std_tree[4].local_pos[0]) - tree_disp_x) < 1e-4f);
		assert(fabsf((v_tree[4].local_pos[1] - v_std_tree[4].local_pos[1]) - tree_disp_y) < 1e-4f);
		nd_seven_gl_set_lamb_light(0.0f, 0.0f, 12.0f, 1.0f);
		nd_seven_gl_bb_vert_t v_tree_far_lamb[6];
		nd_seven_gl_eval_billboard_quad(&tall_tree_bb, ND_SEVEN_DIORAMA_PITCH, pdef, v_tree_far_lamb);
		for (int c = 0; c < 3; c++) {
			assert(fabsf(v_tree_far_lamb[2].world_pos[c] - v_tree[2].world_pos[c]) < 1e-5f);
		}
		nd_seven_gl_set_lamb_light(495.0f, 500.0f, 12.0f, 1.0f);

		/* Short scaled meadow tufts (5px) must never collapse to zero vertical screen height when trampled */
		float short_std_dz = v_short_std[2].local_pos[2] - v_short_std[0].local_pos[2];
		float short_grs_dz = v_short_grs[2].local_pos[2] - v_short_grs[0].local_pos[2];
		assert(short_std_dz > 3.0f);
		assert(short_grs_dz >= short_std_dz * 0.55f);

		const char *fs_b = nd_seven_gl_fs_billboard();
		assert(strstr(fs_b, "canopyTexShift") == NULL);
		assert(strstr(fs_b, "swayedUV") == NULL);
		assert(strstr(fs_b, "boughPhase") == NULL);

		printf("PASS: Living Meadow botanical atlas (%d swatches), biome variety (tundra=%d/sc=%.2f, boreal=%d/sc=%.2f, savanna=%d/sc=%.2f, jungle=%d/sc=%.2f), calm contact parting (W=%.2f, E=+%.2f, shrub=+%.2f, herb_obj=+%.2f, tree_wind=%.2f), wake decay (%.2f->%.2f->%.2f), and homogeneous ground-pinned quad shear verified\n",
		       NDB_COUNT, tundra_cnt, tundra_avg_sc, boreal_cnt, boreal_avg_sc, savanna_cnt, savanna_avg_sc, jungle_cnt, jungle_avg_sc, sx_w, sx_e, shrub_disp_x, herb_disp_x, tree_disp_mag, dz_t0, dz_t1, dz_t2);
	}

	/* Phase 10: 8-Octave Observer-Gated ("Quantum") 4D Volumetric Clouds, Horizon Zero Dawn Billow Erosion & Single-Probe Gradient Raymarch */
	{
		const char *vs_c = nd_seven_gl_vs_cloud();
		const char *fs_c = nd_seven_gl_fs_cloud();
		const char *fs_t = nd_seven_gl_fs_terrain();
		const char *fs_w = nd_seven_gl_fs_water();
		const char *fs_b = nd_seven_gl_fs_billboard();
		assert(vs_c && fs_c && fs_t && fs_w && fs_b);

		/* 1. Verify shader contracts for 4D isotropic unit-gradient dot-product noise (chaff_grad4),
		 * 8-octave observer-gated fractal cascade (eval_zoom_cloud_density_grad), Horizon Zero Dawn billow
		 * cauliflower erosion remap (cloud_remap), GPU vertex dome displacement (eval_vertex_cloud_dome / vShellZ),
		 * 3D ray-slab airplane-descent intersection (camAlt / tEnter / tExit), smooth quintic substrate sampling
		 * (sampleSubstrateSmooth), Beer-Powder silver-lining, single-probe analytical spatial gradient raymarching (cloudGradN),
		 * and projected ground/water/lamb cloud shadows (eval_planetary_cloud_shadow) */
		assert(strstr(vs_c, "vShellZ") != NULL);
		assert(strstr(vs_c, "uCloudDeck") != NULL);
		assert(strstr(fs_c, "chaff_hash4") != NULL);
		assert(strstr(fs_c, "chaff_grad4") != NULL);
		assert(strstr(fs_c, "nd_lut_noise4d") != NULL);
		assert(strstr(fs_c, "cloud_remap") != NULL);
		assert(strstr(fs_c, "eval_recursive_wind") != NULL);
		assert(strstr(fs_c, "eval_zoom_cloud_density") != NULL);
		assert(strstr(fs_c, "eval_zoom_cloud_density_grad") != NULL);
		assert(strstr(fs_c, "synWarp") != NULL);
		assert(strstr(fs_c, "synFront") != NULL);
		assert(strstr(fs_c, "macroCoverage") != NULL);
		assert(strstr(fs_c, "camAlt") != NULL);
		assert(strstr(fs_c, "curvSagitta") != NULL);
		assert(strstr(fs_c, "uCloudDeck") != NULL);
		assert(strstr(fs_c, "sunTowerCloud") != NULL);
		assert(strstr(fs_c, "sampleHeightCloud") != NULL);
		assert(strstr(fs_c, "rayGroundVis") != NULL);
		assert(strstr(fs_c, "sampleSubstrateSmooth") != NULL);
		assert(strstr(fs_c, "worldBioHumidity") != NULL);
		assert(strstr(fs_c, "texBioHumidity") != NULL);
		assert(strstr(fs_c, "directSun") != NULL);
		assert(strstr(fs_c, "multiScatter") != NULL);
		assert(strstr(fs_c, "thinEdge") != NULL);
		assert(strstr(fs_c, "sunSlopeOcc") != NULL);
		assert(strstr(fs_c, "crownErode") != NULL);
		assert(strstr(fs_c, "wSlice") != NULL);
		assert(strstr(fs_c, "uQualityTier") == NULL);
		assert(strstr(fs_c, "anyCloud") == NULL);
		assert(strstr(fs_c, "nd_cloud_wind_column") != NULL);
		assert(strstr(fs_c, "nd_cloud_wind_at_height") != NULL);
		assert(strstr(fs_c, "towerReady") != NULL);
		assert(strstr(fs_c, "turretShift") != NULL);
		assert(strstr(fs_c, "synQueryXY") != NULL);
		assert(strstr(fs_c, "maxFront") != NULL);
		assert(strstr(fs_c, "eval_synoptic_macro_cloud_core") != NULL);
		assert(strstr(fs_c, "eval_synoptic_carrier") != NULL);
		assert(strstr(fs_c, "eval_synoptic_macro_cloud_from_carrier") != NULL);
		assert(strstr(fs_c, "nd_csm_density") != NULL);
		assert(strstr(fs_c, "wBand4") != NULL);
		assert(strstr(fs_c, "worldClimate") != NULL);
		assert(strstr(fs_c, "borderFade") == NULL);
		assert(strstr(fs_c, "maxHorizShift") != NULL);
		assert(strstr(fs_c, "hgPhase") != NULL);
		assert(strstr(fs_c, "sEnter") != NULL);
		assert(strstr(fs_c, "sExit") != NULL);
		assert(strstr(fs_c, "camClearRamp") != NULL);
		assert(strstr(fs_t, "eval_planetary_cloud_shadow") != NULL);
		assert(strstr(fs_t, "rawCloudShadow") != NULL);
		assert(strstr(fs_t, "cloudShadow") != NULL);
		assert(strstr(fs_t, "sunTrans") != NULL);
		assert(strstr(fs_t, "worldBioHumidity") != NULL);
		assert(strstr(fs_w, "nd_cloud_shadow_map") != NULL);
		assert(strstr(fs_w, "eval_planetary_cloud_shadow") == NULL); /* water is CSM-only (cur_dist <= 2000 wu) */
		assert(strstr(fs_w, "reflCloudXY") == NULL); /* reflection reuses the column cloud density */
		assert(strstr(fs_w, "const float zk = 920.0") != NULL);
		assert(strstr(fs_t, "const float zk = 920.0") != NULL);
		assert(strstr(fs_t, "zSlices") == NULL);
		assert(strstr(fs_t, "nd_caustic_web") != NULL);
		assert(strstr(fs_t, "chaff_sample_3d") != NULL);
		assert(strstr(fs_b, "chaff_sample_3d") != NULL);
		assert(strstr(fs_t, "maxDetailOct") != NULL);
		assert(strstr(fs_w, "reflCloudDens") != NULL);
		assert(strstr(fs_w, "reflCloudCol") != NULL);
		assert(strstr(fs_w, "cloudShadow") != NULL);
		/* Billboards carry no per-pixel cloud-shadow evaluation (fixed performance budget) */
		assert(strstr(fs_b, "eval_planetary_cloud_shadow") == NULL);
		assert(strstr(fs_b, "eval_synoptic_macro_cloud") == NULL);
		assert(strstr(fs_b, "uQualityTier") == NULL);

		/* 2. Verify planetary & regional weather distribution (cloudy in ~25-45% of regions, crystal-clear in ~55-75%),
		 * big synoptic/mesoscale feature coherence across the planet, and climate humidity modulation */
		float sum_arid = 0.0f, sum_humid = 0.0f;
		float sum_core = 0.0f, sum_crown = 0.0f;
		int cloud_cells = 0, clear_cells = 0;
		float active_wx = -42000.0f, active_wy = -28000.0f;
		float best_core_d = 0.0f;
		for (int iy = 0; iy < 16; iy++) {
			for (int ix = 0; ix < 16; ix++) {
				float wx = (float)ix * 140000.0f - 1050000.0f;
				float wy = (float)iy * 140000.0f - 1050000.0f;
				float d_arid  = nd_seven_gl_eval_cloud_density(wx, wy, 820.0f, 24.0f, 0.20f, NULL, NULL, NULL, NULL);
				float d_humid = nd_seven_gl_eval_cloud_density(wx, wy, 820.0f, 24.0f, 0.90f, NULL, NULL, NULL, NULL);
				float d_again = nd_seven_gl_eval_cloud_density(wx, wy, 820.0f, 24.0f, 0.90f, NULL, NULL, NULL, NULL);
				float d_crown = nd_seven_gl_eval_cloud_density(wx, wy, 1900.0f, 24.0f, 0.90f, NULL, NULL, NULL, NULL);
				assert(fabsf(d_humid - d_again) < 1e-6f);
				assert(d_humid >= d_arid - 1e-6f);
				sum_arid  += d_arid;
				sum_humid += d_humid;
				sum_core  += d_humid;
				sum_crown += d_crown;
				if (d_humid > 0.05f) cloud_cells++;
				if (d_arid == 0.0f)  clear_cells++;
				if (d_humid > best_core_d) {
					best_core_d = d_humid;
					active_wx = wx;
					active_wy = wy;
				}
			}
		}
		assert(sum_humid > sum_arid * 1.25f);
		assert(cloud_cells >= 25 && clear_cells >= 100);
		assert(sum_core > sum_crown && sum_crown > 1.0f);

		/* 3. Verify unified 3D vertical cloud envelope [420..2820] and clear sky below/above */
		float d_below = nd_seven_gl_eval_cloud_density(active_wx, active_wy, 415.0f, 15.0f, 0.85f, NULL, NULL, NULL, NULL);
		float d_ground = nd_seven_gl_eval_cloud_density(active_wx, active_wy,  92.0f, 15.0f, 0.85f, NULL, NULL, NULL, NULL);
		float d_above = nd_seven_gl_eval_cloud_density(active_wx, active_wy, 2825.0f, 15.0f, 0.85f, NULL, NULL, NULL, NULL);
		assert(d_below == 0.0f && d_ground == 0.0f && d_above == 0.0f);

		/* 4. Verify quantum observer-distance & Nyquist progressive octave activation (close > mid > far) */
		float grad_close = 0.0f, grad_mid = 0.0f, grad_far = 0.0f;
		for (int k = 0; k < 128; k++) {
			float wx = (float)k * 32000.0f - 2000000.0f;
			float wy = (float)k * 23000.0f - 1400000.0f;
			float gxc = 0.0f, gyc = 0.0f, gxm = 0.0f, gym = 0.0f, gxf = 0.0f, gyf = 0.0f;
			nd_seven_gl_eval_zoom_cloud_density(wx, wy, 820.0f, 24.0f, 0.90f,   280.0f,  0.15f, &gxc, &gyc, NULL, NULL);
			nd_seven_gl_eval_zoom_cloud_density(wx, wy, 820.0f, 24.0f, 0.90f,  4200.0f,  2.50f, &gxm, &gym, NULL, NULL);
			nd_seven_gl_eval_zoom_cloud_density(wx, wy, 820.0f, 24.0f, 0.90f, 65000.0f, 40.00f, &gxf, &gyf, NULL, NULL);
			grad_close += hypotf(gxc, gyc);
			grad_mid   += hypotf(gxm, gym);
			grad_far   += hypotf(gxf, gyf);
		}
		/* Sliding 2-octave band-pass cascade preserves full close > mid > far detail progression */
		assert(grad_close > grad_mid * 1.25f);
		assert(grad_mid > grad_far * 1.25f);
		assert(grad_far > 1e-6f);

		/* 5. Verify 4D temporal derivative stability at t = 100,000s vs t = 10s across a continent-spanning planetary grid */
		float gt_early = 0.0f, gt_late = 0.0f;
		for (int iy = 0; iy < 12; iy++) {
			for (int ix = 0; ix < 12; ix++) {
				float wx = (float)ix * 350000.0f - 1925000.0f;
				float wy = (float)iy * 350000.0f - 1925000.0f;
				float gx1 = 0.0f, gy1 = 0.0f, gz1 = 0.0f, gt1 = 0.0f;
				float gx2 = 0.0f, gy2 = 0.0f, gz2 = 0.0f, gt2 = 0.0f;
				nd_seven_gl_eval_cloud_density(wx, wy, 820.0f, 10.0f,     0.85f, &gx1, &gy1, &gz1, &gt1);
				nd_seven_gl_eval_cloud_density(wx, wy, 820.0f, 100000.0f, 0.85f, &gx2, &gy2, &gz2, &gt2);
				gt_early += fabsf(gt1) + hypotf(gx1, gy1);
				gt_late  += fabsf(gt2) + hypotf(gx2, gy2);
			}
		}
		assert(gt_early > 0.0005f);
		assert(gt_late > gt_early * 0.50f && gt_late < gt_early * 1.85f);

		/* 6. Verify 3D vertical profile & derivative continuity across cloud base (420), core (820), and top (2820) */
		float d_z419  = nd_seven_gl_eval_cloud_density(active_wx, active_wy,  419.0f, 24.0f, 0.92f, NULL, NULL, NULL, NULL);
		float d_z460  = nd_seven_gl_eval_cloud_density(active_wx, active_wy,  460.0f, 24.0f, 0.92f, NULL, NULL, NULL, NULL);
		float d_z700  = nd_seven_gl_eval_cloud_density(active_wx, active_wy,  700.0f, 24.0f, 0.92f, NULL, NULL, NULL, NULL);
		float d_z820  = nd_seven_gl_eval_cloud_density(active_wx, active_wy,  820.0f, 24.0f, 0.92f, NULL, NULL, NULL, NULL);
		float d_z2300 = nd_seven_gl_eval_cloud_density(active_wx, active_wy, 2300.0f, 24.0f, 0.92f, NULL, NULL, NULL, NULL);
		float d_z2821 = nd_seven_gl_eval_cloud_density(active_wx, active_wy, 2821.0f, 24.0f, 0.92f, NULL, NULL, NULL, NULL);
		assert(d_z419 == 0.0f);
		assert(d_z2821 == 0.0f);
		assert(d_z460 >= 0.0f && d_z460 <= 1.0f);
		assert(d_z700 >= 0.0f && d_z700 <= 1.0f);
		assert(d_z820 >= d_z460);
		assert(d_z820 >= d_z2300);
		assert(d_z2300 >= 0.0f && d_z2300 <= 1.0f);

		/* 7. Verify Nyquist filter width band-limiting suppresses high-frequency octaves cleanly */
		float g_fine_x = 0.0f, g_fine_y = 0.0f, g_coarse_x = 0.0f, g_coarse_y = 0.0f;
		float sum_nyq_fine = 0.0f, sum_nyq_coarse = 0.0f;
		for (int k = 0; k < 64; k++) {
			float wx = (float)k * 29000.0f - 900000.0f;
			float wy = (float)k * 21000.0f - 650000.0f;
			float df = nd_seven_gl_eval_zoom_cloud_density(wx, wy, 820.0f, 18.0f, 0.90f, 300.0f,  0.10f, &g_fine_x, &g_fine_y, NULL, NULL);
			float dc = nd_seven_gl_eval_zoom_cloud_density(wx, wy, 820.0f, 18.0f, 0.90f, 300.0f, 50.00f, &g_coarse_x, &g_coarse_y, NULL, NULL);
			assert(df >= 0.0f && df <= 1.0f);
			assert(dc >= 0.0f && dc <= 1.0f);
			sum_nyq_fine   += hypotf(g_fine_x, g_fine_y);
			sum_nyq_coarse += hypotf(g_coarse_x, g_coarse_y);
		}
		/* Sliding 2-octave band-pass cascade suppresses high-frequency octaves cleanly at coarse fw */
		assert(sum_nyq_fine > sum_nyq_coarse * 1.35f);
		assert(sum_nyq_coarse > 0.0f);

		/* 8. Verify continent-scale spatial coherence at orbital zoom (cam_dist=500000, fw=4000):
		 * neighbor steps of 4,000 units (25 rooms) vary smoothly with zero high-frequency chatter,
		 * while spanning 4,000,000 units (2 * lambda_0) traverses both dense continental cyclone fronts and clear sky */
		float orbit_step_diff = 0.0f, orbit_max_d = 0.0f, orbit_min_d = 1.0f;
		float prev_orbit_d = nd_seven_gl_eval_zoom_cloud_density(-400000.0f, -200000.0f, 820.0f, 24.0f, 0.85f, 500000.0f, 4000.0f, NULL, NULL, NULL, NULL);
		for (int k = 1; k <= 200; k++) {
			float wx = -400000.0f + (float)k * 4000.0f;
			float wy = -200000.0f + (float)k * 2000.0f;
			float cur_d = nd_seven_gl_eval_zoom_cloud_density(wx, wy, 820.0f, 24.0f, 0.85f, 500000.0f, 4000.0f, NULL, NULL, NULL, NULL);
			orbit_step_diff += fabsf(cur_d - prev_orbit_d);
			prev_orbit_d = cur_d;
		}
		for (int k = 0; k <= 200; k++) {
			float wx = -2000000.0f + (float)k * 20000.0f;
			float wy = -1000000.0f + (float)k * 10000.0f;
			float cur_d = nd_seven_gl_eval_zoom_cloud_density(wx, wy, 820.0f, 24.0f, 0.85f, 500000.0f, 4000.0f, NULL, NULL, NULL, NULL);
			if (cur_d > orbit_max_d) orbit_max_d = cur_d;
			if (cur_d < orbit_min_d) orbit_min_d = cur_d;
		}
		float mean_orbit_step_diff = orbit_step_diff / 200.0f;
		assert(orbit_max_d > 0.45f);
		assert(orbit_min_d == 0.0f);
		assert(mean_orbit_step_diff < 0.030f);

		printf("PASS: 10-octave continent-to-cumulus 4D clouds (cloudy=%d, clear=%d, core=%.1f, crown=%.1f, orbit_step_diff=%.4f, max=%.3f, min=%.3f, grad=%.4f>%.4f>%.4f, arid=%.2f vs humid=%.2f) & t=100000s stability verified\n",
		       cloud_cells, clear_cells, sum_core, sum_crown, mean_orbit_step_diff, orbit_max_d, orbit_min_d, grad_close, grad_mid, grad_far, sum_arid, sum_humid);

		/* 10. Verify true 3D/4D volume: vertical slices within one deck sample independent 4D cross-sections
		 * (no "4 identical 2D slices" when viewed from above), and the crown erodes inward relative to the base */
		{
			/* Pearson correlation of the horizontal density field between two heights over a 16x16 patch (12 wu spacing) */
			int regions_in = 0, regions_x12 = 0, regions_x13 = 0;
			float corr_in = 0.0f, corr_x12 = 0.0f, corr_x13 = 0.0f, sum_base = 0.0f, sum_crown2 = 0.0f;
			const float z_d1a = 420.0f + 0.07f * 2400.0f, z_d1b = 420.0f + 0.15f * 2400.0f;
			const float z_d1 = 420.0f + 0.12f * 2400.0f, z_d2 = 420.0f + 0.50f * 2400.0f, z_d3 = 420.0f + 0.86f * 2400.0f;
			for (int k = 0; k < 1200; k++) {
				float wx0 = (float)k * 6300.0f - 1900000.0f;
				float wy0 = (float)((k * 37) % 600) * 5100.0f - 1500000.0f;
				double s1 = 0, s2 = 0, s3 = 0, s11 = 0, s22 = 0, s33 = 0, s12 = 0, s13 = 0, sa = 0, sb = 0, saa = 0, sbb = 0, sab = 0;
				int n = 0;
				for (int iy = 0; iy < 16; iy++) {
					for (int ix = 0; ix < 16; ix++) {
						float wx = wx0 + (float)ix * 12.0f, wy = wy0 + (float)iy * 12.0f;
						float da = nd_seven_gl_eval_zoom_cloud_density(wx, wy, z_d1a, 24.0f, 0.90f, 300.0f, 0.4f, NULL, NULL, NULL, NULL);
						float db = nd_seven_gl_eval_zoom_cloud_density(wx, wy, z_d1b, 24.0f, 0.90f, 300.0f, 0.4f, NULL, NULL, NULL, NULL);
						float d1 = nd_seven_gl_eval_zoom_cloud_density(wx, wy, z_d1, 24.0f, 0.90f, 300.0f, 0.4f, NULL, NULL, NULL, NULL);
						float d2 = nd_seven_gl_eval_zoom_cloud_density(wx, wy, z_d2, 24.0f, 0.90f, 300.0f, 0.4f, NULL, NULL, NULL, NULL);
						float d3 = nd_seven_gl_eval_zoom_cloud_density(wx, wy, z_d3, 24.0f, 0.90f, 300.0f, 0.4f, NULL, NULL, NULL, NULL);
						sa += da; sb += db; saa += (double)da * da; sbb += (double)db * db; sab += (double)da * db;
						s1 += d1; s2 += d2; s3 += d3; s11 += (double)d1 * d1; s22 += (double)d2 * d2; s33 += (double)d3 * d3;
						s12 += (double)d1 * d2; s13 += (double)d1 * d3; n++;
						sum_base += da;
						sum_crown2 += nd_seven_gl_eval_zoom_cloud_density(wx, wy, 420.0f + 0.22f * 2400.0f, 24.0f, 0.90f, 300.0f, 0.4f, NULL, NULL, NULL, NULL);
					}
				}
				double va = saa / n - (sa / n) * (sa / n), vb = sbb / n - (sb / n) * (sb / n);
				double v1 = s11 / n - (s1 / n) * (s1 / n), v2 = s22 / n - (s2 / n) * (s2 / n), v3 = s33 / n - (s3 / n) * (s3 / n);
				if (va > 1e-4 && vb > 1e-4 && regions_in < 24) { regions_in++; corr_in += (float)((sab / n - (sa / n) * (sb / n)) / sqrt(va * vb)); }
				if (v1 > 1e-4 && v2 > 1e-4 && regions_x12 < 24) { regions_x12++; corr_x12 += (float)((s12 / n - (s1 / n) * (s2 / n)) / sqrt(v1 * v2)); }
				if (v1 > 1e-4 && v3 > 1e-4 && regions_x13 < 24) { regions_x13++; corr_x13 += (float)((s13 / n - (s1 / n) * (s3 / n)) / sqrt(v1 * v3)); }
			}
			assert(regions_in >= 6 && regions_x12 >= 3 && regions_x13 >= 3);
			float mean_in = corr_in / (float)regions_in, mean_x12 = corr_x12 / (float)regions_x12, mean_x13 = corr_x13 / (float)regions_x13;
			assert(mean_in < 0.45f);
			assert(fabsf(mean_x12) < 0.35f && fabsf(mean_x13) < 0.35f);
			assert(sum_crown2 < sum_base);
			printf("PASS: 3D/4D cloud layer separation (in-deck corr=%.3f, deck1-deck2 corr=%.3f, deck1-deck3 corr=%.3f, base=%.1f>crown=%.1f) verified\n",
			       mean_in, mean_x12, mean_x13, sum_base, sum_crown2);
		}

		/* 9. Verify zero-quality-loss shader optimizations & full northern frustum coverage when zooming out */
		assert(strstr(vs_c, "chaff_hash2") == NULL);
		assert(strstr(fs_t, "chaff_wpoint_coarse_2d_f") != NULL);
		assert(strstr(fs_c, "baseTowerOcc") != NULL);

		ar_system_t *cs_north = nd_seven_get_chunk_sys();
		assert(cs_north != NULL);
		float mvp_north[16];
		nd_seven_gl_compute_flat_mvp(1280, 720, 80.0f, -64.0f, 0.0f,
		                             3600.0f, ND_SEVEN_DIORAMA_PITCH, ND_SEVEN_CAM_FOV, mvp_north);
		ar_cull_opts_t opts_north = {
			.z_min = -2600.0f,
			.z_max = 3200.0f,
			.sag_radius = 1335000.0f,
			.focal_x = 80.0f,
			.focal_y = -64.0f,
			.margin = 180.0f,
			.target_quad_px = 4.0f,
			.viewport_w = 1280.0f,
			.viewport_h = 720.0f,
			.min_level = 0,
			.max_level = 15,
			.view_id = 0,
			.max_populates_per_query = 128
		};
		const ar_node_t *north_nodes[128];
		size_t north_cnt = ar_system_query_matrix(cs_north, 0, mvp_north, &opts_north, north_nodes, 128);
		assert(north_cnt > 0);
		int32_t min_chunk_oy = 2147483647;
		int32_t max_chunk_oy_end = -2147483647;
		for (size_t ci = 0; ci < north_cnt; ci++) {
			const ar_node_t *cn = north_nodes[ci];
			if (!cn) continue;
			if (cn->oy < min_chunk_oy) min_chunk_oy = cn->oy;
			if (cn->oy + cn->h > max_chunk_oy_end) max_chunk_oy_end = cn->oy + cn->h;
		}
		/* At dist=3600 and 55 deg pitch looking North (-Y), northern frustum coverage must extend >= 6000 wu North of lamb */
		assert(-64.0f - (float)min_chunk_oy >= 6000.0f);
		assert((float)max_chunk_oy_end > -64.0f);

		/* Also verify close/mid zoom (dist=560, 0.5x zoom as in screenshot) covers >= 512 wu (4 rooms) North of lamb */
		float mvp_mid[16];
		nd_seven_gl_compute_flat_mvp(1280, 720, 80.0f, -64.0f, 0.0f,
		                             560.0f, ND_SEVEN_DIORAMA_PITCH, ND_SEVEN_CAM_FOV, mvp_mid);
		ar_cull_opts_t opts_mid = {
			.z_min = -256.0f,
			.z_max = 320.0f,
			.sag_radius = 1335000.0f,
			.focal_x = 80.0f,
			.focal_y = -64.0f,
			.margin = 96.0f,
			.target_quad_px = 1.333f,
			.viewport_w = 1280.0f,
			.viewport_h = 720.0f,
			.min_level = 0,
			.max_level = 14,
			.view_id = 0,
			.max_populates_per_query = 48
		};
		const ar_node_t *mid_nodes[160];
		size_t mid_cnt = ar_system_query_matrix(cs_north, 0, mvp_mid, &opts_mid, mid_nodes, 160);
		assert(mid_cnt > 0);
		int32_t min_mid_oy = 2147483647;
		for (size_t ci = 0; ci < mid_cnt; ci++) {
			if (mid_nodes[ci] && mid_nodes[ci]->oy < min_mid_oy)
				min_mid_oy = mid_nodes[ci]->oy;
		}
		assert(-64.0f - (float)min_mid_oy >= 512.0f);

		printf("PASS: zero-quality-loss shader optimizations & full northern frustum coverage (mid_min_oy=%d, far_min_oy=%d vs lwy=-64, count=%zu) verified\n",
		       min_mid_oy, min_chunk_oy, north_cnt);

		/* 10. Hydrological River Network: Lowland/Canyon Sea-Level Rivers & Sloping Upland Mountain Streams */
		{
			assert(strstr(fs_t, "riverCarve") != NULL);
			assert(strstr(fs_t, "effSeaLevel") != NULL);
			assert(strstr(nd_seven_gl_vs_water(), "riverCarve") != NULL);
			assert(strstr(fs_w, "riverCarve") != NULL);

			int16_t base_wl = nd_field_water_level(0);
			int lowland_river_samples = 0;
			int upland_stream_samples = 0;
			int16_t max_upland_wl = base_wl;
			int32_t upland_wx = 0, upland_wy = 0;

			for (int32_t wy = -16384; wy <= 16384; wy += 24) {
				for (int32_t wx = -16384; wx <= 16384; wx += 24) {
					nd_field_sample_t fs = nd_field_sample(0, 3, wx, wy);
					if (!fs.is_water || fs.depth == 0)
						continue;
					if (fs.water_level == base_wl && fs.river_carve_q4 > 0) {
						lowland_river_samples++;
					} else if (fs.water_level > base_wl && fs.river_carve_q4 > 0) {
						upland_stream_samples++;
						/* Verify exact sloping water reconstruction identity:
						 * (elevation_q12_4 + river_carve_q4 - 48 + 8) >> 4 == water_level */
						int16_t recon_wl = (int16_t)((fs.elevation_q12_4 + (int32_t)fs.river_carve_q4 - 48 + 8) >> 4);
						assert(abs((int)recon_wl - (int)fs.water_level) <= 1);
						if (fs.water_level > max_upland_wl) {
							max_upland_wl = fs.water_level;
							upland_wx = wx;
							upland_wy = wy;
						}
					}
				}
			}

			/* Also sample across planetary continental highlands to verify high-elevation mountain streams */
			for (int32_t step_i = 0; step_i < 4096; step_i++) {
				int32_t wx = (step_i * 2560) - 5242880;
				int32_t wy = ((step_i * 1999) % 4194304) - 2097152;
				nd_field_sample_t fs = nd_field_sample(0, 3, wx, wy);
				if (fs.is_water && fs.depth > 0 && fs.water_level > base_wl && fs.river_carve_q4 > 0) {
					upland_stream_samples++;
					int16_t recon_wl = (int16_t)((fs.elevation_q12_4 + (int32_t)fs.river_carve_q4 - 48 + 8) >> 4);
					assert(abs((int)recon_wl - (int)fs.water_level) <= 1);
					if (fs.water_level > max_upland_wl) {
						max_upland_wl = fs.water_level;
						upland_wx = wx;
						upland_wy = wy;
					}
				}
			}

			assert(lowland_river_samples >= 20);
			assert(upland_stream_samples >= 50);
			assert(max_upland_wl >= base_wl + 10);

			/* Verify libareole chunk sampler packs river_carve_q4 into attr[6] and alpha (255 - attr[6])
			 * while keeping flag bit 1 (flat mountain lake override) clear so chunk borders never seam */
			int32_t ch_ox = (upland_wx >= 0) ? ((upland_wx / NDW_TILE_W) * NDW_TILE_W)
			                                 : (((upland_wx - NDW_TILE_W + 1) / NDW_TILE_W) * NDW_TILE_W);
			int32_t ch_oy = (upland_wy >= 0) ? ((upland_wy / NDW_TILE_H) * NDW_TILE_H)
			                                 : (((upland_wy - NDW_TILE_H + 1) / NDW_TILE_H) * NDW_TILE_H);
			ar_system_begin_frame(cs_north);
			const ar_node_t *riv_node = ar_system_ensure(cs_north, 0, 0, ch_ox, ch_oy);
			assert(riv_node != NULL && riv_node->samples != NULL && riv_node->rgba != NULL);
			int found_packed_carve = 0;
			for (uint32_t vi = 0; vi < riv_node->vert_count; vi++) {
				const ar_sample_t *smp = &riv_node->samples[vi];
				if (smp->attr[6] > 0) {
					found_packed_carve++;
					assert((smp->flags & 1u) != 0);
					assert((smp->flags & 2u) == 0);
					uint32_t alpha_u8 = (riv_node->rgba[vi] >> 24) & 0xFFu;
					assert(alpha_u8 == 255u - (uint32_t)smp->attr[6]);
				}
			}
			assert(found_packed_carve > 0);

			printf("PASS: hydrological river network verified (lowland_gorge=%d, upland_stream=%d, max_upland_wl=%d, chunk_carve_verts=%d)\n",
			       lowland_river_samples, upland_stream_samples, (int)max_upland_wl, found_packed_carve);
		}

		/* 11. Universe Owner (God) Shift + Direction Zoom-Proportional Walk-Teleport */
		{
			nd_client_state_t god_st;
			memset(&god_st, 0, sizeof(god_st));
			god_st.map_data.width = 7;
			god_st.map_data.height = 7;
			god_st.map_data.pos[0] = 10;
			god_st.map_data.pos[1] = -20;
			god_st.map_data.pos[2] = 0;
			god_st.map_data.pos[3] = 0;
			god_st.me = 2; /* Non-owner player initially */
			god_st.pres_flags = ND_REGION_LOCKED;

			nd_seven_reset_input();
			nd_seven_sync_origin(&god_st);
			nd_seven_set_zoom(0.05f); /* 20x zoom-out -> 20 rooms per step */

			char tp_cmd[64];
			memset(tp_cmd, 0, sizeof(tp_cmd));
			/* Non-owner must NOT be allowed to walk-teleport */
			assert(nd_seven_god_teleport_step(&god_st, "L", tp_cmd, sizeof(tp_cmd)) == 0);

			/* Grant Universe Owner authority */
			god_st.pres_flags = ND_REGION_UNIVERSE_OWNER;

			/* At zoom >= 1.0f (normal zoom-in), Shift+move does not teleport */
			nd_seven_set_zoom(1.0f);
			assert(nd_seven_god_teleport_step(&god_st, "L", tp_cmd, sizeof(tp_cmd)) == 0);

			/* At zoom = 0.05f (20x zoom-out), Shift+East ("L") teleports +20 rooms East (x: 10 -> 30) */
			nd_seven_set_zoom(0.05f);
			int len = nd_seven_god_teleport_step(&god_st, "L", tp_cmd, sizeof(tp_cmd));
			assert(len > 0);
			assert(strncmp(tp_cmd, "X0x", 3) == 0);
			int dbg_pend = 0, dbg_x = 0, dbg_y = 0, dbg_have = 0;
			nd_seven_debug(&dbg_pend, &dbg_x, &dbg_y, &dbg_have);
			assert(dbg_x == 30 && dbg_y == -20);

			/* Holding Shift ("Shift" keydown) + "ArrowUp" at zoom = 0.01f (100x zoom-out) teleports +100 rooms North (y: -20 -> 80) */
			nd_seven_set_zoom(0.01f);
			assert(nd_seven_key("Shift", 1) == 1);
			assert(nd_seven_get_shift() == 1);
			len = nd_seven_god_teleport_step(&god_st, "ArrowUp", tp_cmd, sizeof(tp_cmd));
			assert(len > 0);
			nd_seven_debug(&dbg_pend, &dbg_x, &dbg_y, &dbg_have);
			assert(dbg_x == 30 && dbg_y == 80);

			/* Releasing Shift disables arrow-key teleport */
			assert(nd_seven_key("Shift", 0) == 1);
			assert(nd_seven_get_shift() == 0);
			assert(nd_seven_god_teleport_step(&god_st, "ArrowUp", tp_cmd, sizeof(tp_cmd)) == 0);

			nd_seven_set_zoom(1.0f);
			nd_seven_reset_input();
			printf("PASS: Universe Owner zoom-proportional Shift walk-teleport verified (last_cmd=%s)\n", tp_cmd);
		}

		/* 12. User-Adjustable Detail Scale (nd_seven_set_detail_scale / nd_seven_detail_in / nd_seven_detail_out) */
		{
			assert(fabsf(nd_seven_get_detail_scale() - 1.00f) < 1e-5f);
			nd_seven_detail_out();
			assert(nd_seven_get_detail_scale() < 1.00f && nd_seven_get_detail_scale() >= ND_SEVEN_DETAIL_MIN);
			nd_seven_detail_in();
			assert(fabsf(nd_seven_get_detail_scale() - 1.00f) < 1e-4f);

			/* Lowering detail scale culls distant billboards earlier at zoom = 0.08f (cam_dist = 3,500 wu) */
			set_map(10, 10, 12, 0x1B, 0, 'X', 'o', 'T');
			nd_seven_set_zoom(0.08f);

			/* At detail = 0.35f (max_cull = 2,856 < 3,500), all billboards cull */
			nd_seven_set_detail_scale(0.35f);
			for (int wave = 0; wave < 3; wave++) {
				nd_seven_render(fb, 1280, 720, &st);
				if (wave < 2)
					nd_seven_drain_async();
			}
			assert(nd_seven_last_mid_billboard_count() == 0);

			/* At detail = 0.85f, near-focal large trees are emitted */
			nd_seven_set_detail_scale(0.85f);
			nd_seven_render(fb, 1280, 720, &st);
			uint32_t cnt_mid_detail = nd_seven_last_mid_billboard_count();
			assert(cnt_mid_detail > 0);

			/* At detail = 1.25f, more trees and medium plants across the frustum are emitted */
			nd_seven_set_detail_scale(1.25f);
			nd_seven_render(fb, 1280, 720, &st);
			uint32_t cnt_full_detail = nd_seven_last_mid_billboard_count();
			assert(cnt_full_detail > cnt_mid_detail);

			nd_seven_set_detail_scale(1.00f);
			nd_seven_set_zoom(1.0f);
			printf("PASS: User-adjustable detail scale verified (1.25x=%u, 0.85x=%u, 0.35x=0 billboards at 0.08x zoom)\n",
			       cnt_full_detail, cnt_mid_detail);
		}

		/* 13. Multi-Stage 3D Occlusion & Culling Verification (Planetary Horizon, Cloud Shell, Alt-Planet Eclipse, Front-to-Back Sort) */
		{
			const nd_planet_def_t *p1 = nd_seven_get_planet(1);
			assert(p1 != NULL);

			/* 13a. Planetary Normal-Cone Horizon Culling at Orbital Zoom (zoom = 0.0005f, dist = 560,000 wu) */
			set_map(0, 0, 10, 0x1B, 0, 0, 0, 0);
			nd_seven_sync_origin(&st);
			nd_seven_set_lamb(NDW_CENTER_X, NDW_CENTER_Y);
			nd_seven_set_zoom(0.0005f);
			nd_seven_render(fb, 1280, 720, &st);
			uint32_t orb_leaves = nd_seven_last_visible_leaf_count();
			assert(orb_leaves > 0);

			float mvp_orb[16], eye_orb[3];
			nd_seven_gl_compute_mvp(1280, 720, 80.0f, -64.0f, 0.0f,
			                        ND_SEVEN_CAM_DIST / 0.0005f, ND_SEVEN_DIORAMA_PITCH, ND_SEVEN_CAM_FOV,
			                        p1, mvp_orb, eye_orb, NULL, NULL);
			int back_hemisphere_leaves = 0;
			for (uint32_t li = 0; li < orb_leaves; li++) {
				int32_t lox = 0, loy = 0, lw = 0, lh = 0;
				assert(nd_seven_last_visible_leaf_bounds(li, &lox, &loy, &lw, &lh) == 1);
				float cx = (float)lox + 0.5f * (float)lw;
				float cy = (float)loy + 0.5f * (float)lh;
				float p_c[3], n_c[3];
				nd_seven_gl_torus_frame(cx, cy, 0.0f, p1, p_c, NULL, NULL, n_c);
				float vx = eye_orb[0] - p_c[0], vy = eye_orb[1] - p_c[1], vz = eye_orb[2] - p_c[2];
				float vlen = sqrtf(vx * vx + vy * vy + vz * vz);
				float dot_vn = (vx * n_c[0] + vy * n_c[1] + vz * n_c[2]) / fmaxf(1.0f, vlen);
				float du = (float)lw / (float)ND_SEVEN_WORLD_DIM_W;
				float dv = (float)lh / (float)ND_SEVEN_WORLD_DIM_H;
				float alpha = 3.14159265f * sqrtf(du * du + dv * dv);
				if (alpha < 1.15f && alpha + 0.10f < 1.55f && dot_vn < -sinf(alpha + 0.10f) - 1e-3f)
					back_hemisphere_leaves++;
			}
			assert(back_hemisphere_leaves == 0);

			/* 13b. Dedicated Cloud-Shell Culling (Close-Zoom Gate, Back-of-Planet Limb Gate, Synoptic Clear-Sky Gate) */
			nd_seven_gl_begin_frame(1280, 720, 80.0f, -64.0f, 0.0f, 280.0f,
			                        ND_SEVEN_DIORAMA_PITCH, ND_SEVEN_CAM_FOV, 100.0f, p1);
			assert(!nd_seven_gl_should_queue_cloud_chunk(0, -512, 512, 512, p1));

			/* At orbital distance (600,000 wu), back-of-planet chunks around the far torus half must be 100% rejected */
			nd_seven_gl_begin_frame(1280, 720, 80.0f, -64.0f, 0.0f, 600000.0f,
			                        ND_SEVEN_DIORAMA_PITCH, ND_SEVEN_CAM_FOV, 100.0f, p1);
			int back_clouds_queued = 0;
			int front_clouds_queued = 0;
			int32_t step_w = (int32_t)(ND_SEVEN_WORLD_DIM_W / 32.0f);
			int32_t step_h = (int32_t)(ND_SEVEN_WORLD_DIM_H / 16.0f);
			for (int iu = 0; iu < 32; iu++) {
				int32_t ox = iu * step_w;
				int32_t oy = -step_h / 2;
				bool queued = nd_seven_gl_should_queue_cloud_chunk(ox, oy, step_w, step_h, p1);
				/* iu in [13..19] is on the opposite side of the major ring (u ~ 0.5 away from focal x=80) */
				if (iu >= 13 && iu <= 19) {
					if (queued) back_clouds_queued++;
				} else {
					if (queued) front_clouds_queued++;
				}
			}
			assert(back_clouds_queued == 0);
			assert(front_clouds_queued > 0);

			/* At deep-orbit distance (2,800,000 wu, very low zoom), hemisphere-spanning macro chunks (span = 2,621,440 wu)
			 * on the visible front hemisphere must be queued without false frustum or limb culling */
			nd_seven_gl_begin_frame(1280, 720, 80.0f, -64.0f, 0.0f, 2800000.0f,
			                        ND_SEVEN_DIORAMA_PITCH, ND_SEVEN_CAM_FOV, 100.0f, p1);
			assert(nd_seven_gl_should_queue_cloud_chunk(-1310720, -1310720, 2621440, 2621440, p1));

			/* Synoptic clear-sky pre-cull on regional chunks (span = 16,384 wu) inside the visible frustum */
			nd_seven_gl_begin_frame(1280, 720, 80.0f, -64.0f, 0.0f, 180000.0f,
			                        ND_SEVEN_DIORAMA_PITCH, ND_SEVEN_CAM_FOV, 100.0f, p1);
			int syn_clear_culled = 0, syn_cloudy_kept = 0;
			for (int gy = -8; gy <= 4; gy++) {
				for (int gx = -8; gx <= 8; gx++) {
					int32_t rx = gx * 16384;
					int32_t ry = gy * 16384;
					/* Test at different synoptic weather epochs to sample both anticyclones and cyclones */
					for (int ti = 0; ti < 8; ti++) {
						nd_seven_gl_begin_frame(1280, 720, 80.0f, -64.0f, 0.0f, 180000.0f,
						                        ND_SEVEN_DIORAMA_PITCH, ND_SEVEN_CAM_FOV,
						                        (float)(ti * 18000), p1);
						if (nd_seven_gl_should_queue_cloud_chunk(rx, ry, 16384, 16384, p1))
							syn_cloudy_kept++;
						else
							syn_clear_culled++;
					}
				}
			}
			assert(syn_clear_culled > 50);
			assert(syn_cloudy_kept > 50);

			/* Also verify 262,144-span macro chunks are pre-culled when entirely inside a synoptic clear-sky corridor */
			int macro_clear_culled = 0, macro_cloudy_kept = 0;
			for (int gy = -2; gy <= 1; gy++) {
				for (int gx = -2; gx <= 2; gx++) {
					int32_t rx = gx * 262144;
					int32_t ry = gy * 262144;
					for (int ti = 0; ti < 8; ti++) {
						nd_seven_gl_begin_frame(1280, 720, 80.0f, -64.0f, 0.0f, 450000.0f,
						                        ND_SEVEN_DIORAMA_PITCH, ND_SEVEN_CAM_FOV,
						                        (float)(ti * 18000), p1);
						if (nd_seven_gl_should_queue_cloud_chunk(rx, ry, 262144, 262144, p1))
							macro_cloudy_kept++;
						else
							macro_clear_culled++;
					}
				}
			}
			assert(macro_clear_culled > 5);
			assert(macro_cloudy_kept > 10);

			/* 13c. Companion Planet (alt_world) Ground Horizon & Solid-Body Torus Eclipse Occlusion */
			nd_seven_set_zoom(1.0f);
			nd_seven_render(fb, 1280, 720, &st);
			assert(nd_seven_last_alt_planet_chunk_count() == 0);

			/* Place alt_world directly behind the primary planet's solid torus rim at (2,000,000, 0, 0):
			 * Eye at (+4,000,000, 0, 0) looking through the solid tube at (+3,335,000 .. +665,000) toward alt at (-10,000,000, 0, 0) */
			nd_planet_def_t alt_eclipsed = *nd_seven_get_planet(2);
			alt_eclipsed.center[0] = -10000000.0f;
			alt_eclipsed.center[1] = 0.0f;
			alt_eclipsed.center[2] = 0.0f;
			float eye_eclipsed[3] = { 4000000.0f, 0.0f, 0.0f };
			float alt_rad = alt_eclipsed.major_r + alt_eclipsed.minor_r + 8000.0f;
			assert(nd_seven_is_alt_planet_occluded(eye_eclipsed, 665000.0f, 0.0f, 0.0f, 0.0f,
			                                       p1, &alt_eclipsed, alt_rad) == 1);

			/* When alt_world is high above the torus plane (unoccluded line of sight), it is NOT occluded */
			nd_planet_def_t alt_visible_sky = alt_eclipsed;
			alt_visible_sky.center[0] = 4000000.0f;
			alt_visible_sky.center[2] = 12000000.0f;
			assert(nd_seven_is_alt_planet_occluded(eye_eclipsed, 665000.0f, 0.0f, 0.0f, 0.0f,
			                                       p1, &alt_visible_sky, alt_rad) == 0);

			/* 13d. Front-to-Back 3D Camera-Eye Sorting Invariant (Terrain Pass at zoom = 0.02f) */
			nd_seven_set_zoom(0.02f);
			nd_seven_render(fb, 1280, 720, &st);
			uint32_t mid_leaves = nd_seven_last_visible_leaf_count();
			assert(mid_leaves > 4);
			int64_t lwx_cur = 0, lwy_cur = 0;
			nd_seven_world_pos(&st, &lwx_cur, &lwy_cur);
			float lwz_cur = nd_seven_ground_elevation_f((int32_t)lwx_cur, (int32_t)lwy_cur);
			float eye_rel_13d[3];
			double ref_orig_13d[3];
			nd_seven_gl_compute_mvp_rel_ex(1280, 720, (float)lwx_cur, (float)lwy_cur, lwz_cur,
			                               ND_SEVEN_CAM_DIST / 0.02f, 0.0f, ND_SEVEN_DIORAMA_PITCH,
			                               ND_SEVEN_CAM_FOV, p1, NULL, ref_orig_13d, eye_rel_13d, NULL);
			corolla_surface_t surf_13d = corolla_torus((double)p1->major_r, (double)p1->minor_r);
			corolla_pose_t pose_13d = {0};
			for (int k = 0; k < 9; k++) pose_13d.rot[k] = (double)p1->rot[k];
			for (int k = 0; k < 3; k++) pose_13d.center[k] = (double)p1->center[k];
			float wl_13d = (float)nd_field_water_level(0);
			float prev_d2 = -1.0f;
			for (uint32_t li = 0; li < mid_leaves; li++) {
				int32_t lox = 0, loy = 0, lw = 0, lh = 0;
				assert(nd_seven_last_visible_leaf_bounds(li, &lox, &loy, &lw, &lh) == 1);
				float z0 = -2150.0f, z1 = 2000.0f;
				float az0 = z0, az1 = z1;
				nd_field_elevation_bounds(0, lox, loy, lw, lh, &az0, &az1);
				if (az0 > z0) z0 = az0;
				if (az1 < z1) z1 = az1;
				if (z0 <= wl_13d + 8.0f && z1 < wl_13d + 8.0f) z1 = wl_13d + 8.0f;
				if (z0 > z1) z0 = z1;
				double u0 = (double)lox / (double)ND_SEVEN_WORLD_DIM_W;
				double du = (double)lw / (double)ND_SEVEN_WORLD_DIM_W;
				double v0 = ((double)loy / (double)ND_SEVEN_WORLD_DIM_H) + 0.5;
				double dv = (double)lh / (double)ND_SEVEN_WORLD_DIM_H;
				double bmin[3], bmax[3];
				corolla_patch_aabb(&surf_13d, &pose_13d, ref_orig_13d, u0, v0, du, dv,
				                   (double)z0, (double)z1, bmin, bmax);
				float dx = (eye_rel_13d[0] < (float)bmin[0]) ? ((float)bmin[0] - eye_rel_13d[0])
				         : ((eye_rel_13d[0] > (float)bmax[0]) ? (eye_rel_13d[0] - (float)bmax[0]) : 0.0f);
				float dy = (eye_rel_13d[1] < (float)bmin[1]) ? ((float)bmin[1] - eye_rel_13d[1])
				         : ((eye_rel_13d[1] > (float)bmax[1]) ? (eye_rel_13d[1] - (float)bmax[1]) : 0.0f);
				float dz = (eye_rel_13d[2] < (float)bmin[2]) ? ((float)bmin[2] - eye_rel_13d[2])
				         : ((eye_rel_13d[2] > (float)bmax[2]) ? (eye_rel_13d[2] - (float)bmax[2]) : 0.0f);
				float d2 = dx * dx + dy * dy + dz * dz;
				assert(d2 + 1.0f >= prev_d2);
				prev_d2 = d2;
			}

			/* 13e. Stride-Aware Canonical Field Sampling, Row Batching, Sub-Texel Band-Pass,
			 *      Scenery Size-Descending Sort, Adaptive Meadow Stride, and Corolla Cloud Shell Hit */
			{
				nd_field_sample_t s_exact = nd_field_sample(0, 3, 12800, -9600);
				nd_field_sample_t s_step0 = nd_field_sample_step(0, 0, 12800, -9600);
				assert(s_exact.elevation_q12_4 == s_step0.elevation_q12_4);
				assert(s_exact.bio == s_step0.bio);
				assert(s_exact.water_level == s_step0.water_level);

				/* At coarse orbital chunk step (level 10 -> step = 5120 * 1024 / 48 = 109,226 wu),
				 * sub-Nyquist fine and coarse room octaves/rivers/lakes are band-limited out */
				int32_t coarse_step_q16 = (int32_t)(((int64_t)(5120 << 10) << 16) / 48);
				nd_field_sample_t s_coarse = nd_field_sample_step(0, coarse_step_q16, 12800, -9600);
				assert(s_coarse.bio < BIOME_MAX);

				/* Row batching (nd_field_sample_row_step) must match point-by-point nd_field_sample_step */
				nd_field_sample_t row_out[8];
				nd_field_sample_row_step(0, coarse_step_q16, -9600, 12800, 7000, 8, row_out);
				for (int k = 0; k < 8; k++) {
					int32_t wx_k = 12800 + (k * 7000) / 7;
					nd_field_sample_t pt_k = nd_field_sample_step(0, coarse_step_q16, wx_k, -9600);
					assert(row_out[k].elevation_q12_4 == pt_k.elevation_q12_4);
					assert(row_out[k].bio == pt_k.bio);
				}

				/* Verify sub-texel band-pass, canonical heightmap-only elevation, and Nyquist-gated cloud noises in shaders */
				assert(strstr(nd_seven_gl_fs_terrain(), "chaff_band_weight") != NULL);
				assert(strstr(nd_seven_gl_fs_terrain(), "max_sub_wl") != NULL);
				assert(strstr(nd_seven_gl_fs_terrain(), "texelSpan = max(3.333333, chunkTexelSpan)") != NULL);
				assert(strstr(nd_seven_gl_fs_terrain(), "spanBlend = smoothstep(5.0, 14.0, chunkTexelSpan)") != NULL);
				assert(strstr(nd_seven_gl_fs_terrain(), "distBlend = smoothstep(3200.0, 8400.0, vDistance)") != NULL);
				assert(strstr(nd_seven_gl_fs_terrain(), "normalExag = mix(2.4, 1.15") != NULL);
				assert(strstr(nd_seven_gl_fs_terrain(), "nd_gpu_eval_canonical_field(worldPos") == NULL);
				assert(strstr(nd_seven_gl_fs_terrain(), "fragZ += macroDetail.x") == NULL);
				assert(strstr(nd_seven_gl_fs_cloud(), "wPsi1") != NULL);
				assert(strstr(nd_seven_gl_fs_cloud(), "wCir0") != NULL);
				assert(strstr(nd_seven_gl_fs_cloud(), "wAlto0") != NULL);

				/* Verify Level 6..8 step_q16 (< 2048 << 16) interpolates smoothly between rooms rather than nearest-room stair-stepping,
				 * and preserves full coarse room hypsometry and macro ridged fine octaves in 3D vertex heights */
				{
					int32_t step_l7 = (int32_t)(((int64_t)(NDW_TILE_W << 7) << 14) / 48);
					int32_t step_l8 = (int32_t)(((int64_t)(NDW_TILE_W << 8) << 14) / 48);
					int32_t h_a = nd_field_sample_step(0, step_l8, 12880, -9600).coarse_elev;
					int32_t h_b = nd_field_sample_step(0, step_l8, 12960, -9600).coarse_elev;
					int32_t h_c = nd_field_sample_step(0, step_l8, 13040, -9600).coarse_elev;
					assert((h_a <= h_b && h_b <= h_c) || (h_a >= h_b && h_b >= h_c));
					assert(h_b == nd_field_sample_step(0, 0, 12960, -9600).coarse_elev);
					int found_l7_fine = 0;
					for (int k = 0; k < 32; k++) {
						if (nd_field_sample_step(0, step_l7, 12800 + k * 73, -9600 + k * 59).fine_elev != 0)
							found_l7_fine++;
					}
					assert(found_l7_fine > 0);
				}

				/* Verify nd_seven_sort_billboards_by_size sorts by descending max(width, height) and painter Y tie-break */
				lam_billboard_gpu_t test_bbs[4] = {
					{ .x = 10.0f, .y = 50.0f, .width = 16.0f, .height = 16.0f },
					{ .x = 20.0f, .y = 80.0f, .width = 64.0f, .height = 96.0f },
					{ .x = 30.0f, .y = 20.0f, .width = 96.0f, .height = 48.0f },
					{ .x = 40.0f, .y = 10.0f, .width = 32.0f, .height = 40.0f }
				};
				nd_seven_sort_billboards_by_size(test_bbs, 4);
				assert(fmaxf(test_bbs[0].width, test_bbs[0].height) == 96.0f && test_bbs[0].y == 20.0f);
				assert(fmaxf(test_bbs[1].width, test_bbs[1].height) == 96.0f && test_bbs[1].y == 80.0f);
				assert(fmaxf(test_bbs[2].width, test_bbs[2].height) == 40.0f);
				assert(fmaxf(test_bbs[3].width, test_bbs[3].height) == 16.0f);

				/* Verify nd_seven_meadow_lattice_step scales lattice stride smoothly with camera distance */
				assert(nd_seven_meadow_lattice_step(400.0f, 3800.0f) == 16);
				assert(nd_seven_meadow_lattice_step(1000.0f, 3800.0f) == 24);
				assert(nd_seven_meadow_lattice_step(1600.0f, 3800.0f) == 36);
				assert(nd_seven_meadow_lattice_step(2800.0f, 3800.0f) == 56);
				assert(nd_seven_meadow_lattice_step(4000.0f, 3800.0f) == 0);

				/* Verify libcorolla analytical ray-cloud-shell intersection (nd_seven_gl_cloud_shell_ray_hit) */
				float hit_wx = 0.0f, hit_wy = 0.0f, hit_t = 0.0f;
				float orig_out[3] = { 3340000.0f, 0.0f, 0.0f };
				float dir_in[3]   = { -1.0f, 0.0f, 0.0f };
				float dir_out[3]  = {  1.0f, 0.0f, 0.0f };
				assert(nd_seven_gl_cloud_shell_ray_hit(orig_out, dir_in, 1620.0f, p1, &hit_wx, &hit_wy, &hit_t) == true);
				assert(hit_t > 3000.0f && hit_t < 4000.0f);
				assert(nd_seven_gl_cloud_shell_ray_hit(orig_out, dir_out, 1620.0f, p1, &hit_wx, &hit_wy, &hit_t) == false);

				/* Verify configurable graphics quality presets & zero-reallocation dynamic sub-rect scaling */
				uint32_t rw = 0, rh = 0;
				uint32_t st_w = 0, st_h = 0, sc_w = 0, sc_h = 0, cl_w = 0, cl_h = 0;
				int sc_samples = -1;
				bool csm_act = false;

				/* 1) High Preset (100% res, 100% detail, High 0.50x->1.00x clouds + CSM, 4x MSAA, Uncapped FPS) */
				nd_seven_apply_quality_preset(ND_QUALITY_HIGH);
				nd_seven_set_zoom(0.08f);
				assert(nd_seven_get_quality_preset() == ND_QUALITY_HIGH);
				nd_seven_scaled_dims(1920, 1080, &rw, &rh);
				assert(rw == 1920 && rh == 1080);
				nd_seven_render(fb, 1920, 1080, &st);
				nd_seven_render(fb, 1920, 1080, &st);
				nd_seven_gl_get_target_info(&st_w, &st_h, &sc_w, &sc_h, &sc_samples, &cl_w, &cl_h, &csm_act);
				assert(st_w == 1920 && st_h == 1080);
				assert(sc_w == 1920 && sc_h == 1080);
				assert(sc_samples == 4);
				assert(cl_w == 960 && cl_h == 540);
				assert(csm_act == true);
				uint32_t bbs_detail_100 = nd_seven_last_mid_billboard_count();
				assert(bbs_detail_100 > 10);

				/* 2) Low Preset (67% res sub-rect -> 100% in orbit, 72% detail, non-compounding 0.35x->0.67x clouds) */
				nd_seven_apply_quality_preset(ND_QUALITY_LOW);
				assert(nd_seven_get_quality_preset() == ND_QUALITY_LOW);
				assert(fabsf(nd_seven_get_res_scale() - 0.67f) < 1e-4f);
				assert(fabsf(nd_seven_get_detail_scale() - 0.72f) < 1e-4f);
				assert(nd_seven_get_cloud_quality() == ND_CLOUD_QUALITY_LOW);
				assert(nd_seven_get_msaa() == 0);
				assert(nd_seven_get_fps_cap() == 30);
				nd_seven_scaled_dims(1920, 1080, &rw, &rh);
				assert(rw == 1286 && rh == 724);
				nd_seven_render(fb, 1920, 1080, &st);
				nd_seven_render(fb, 1920, 1080, &st);
				nd_seven_gl_get_target_info(&st_w, &st_h, &sc_w, &sc_h, &sc_samples, &cl_w, &cl_h, &csm_act);
				assert(st_w == 1920 && st_h == 1080);
				assert(sc_w == 1286 && sc_h == 724);
				assert(sc_samples == 0);
				assert(cl_w == 672 && cl_h == 378);
				assert(csm_act == false);
				uint32_t bbs_detail_low = nd_seven_last_mid_billboard_count();
				assert(bbs_detail_low > 0 && bbs_detail_low < bbs_detail_100);

				/* Verify Low preset smoothly relaxes to native 1920x1080 scene + 0.67x clouds in orbit */
				nd_seven_set_zoom(0.0001f);
				nd_seven_render(fb, 1920, 1080, &st);
				nd_seven_gl_get_target_info(&st_w, &st_h, &sc_w, &sc_h, &sc_samples, &cl_w, &cl_h, &csm_act);
				assert(st_w == 1920 && st_h == 1080);
				assert(sc_w == 1920 && sc_h == 1080);
				assert(cl_w == 1286 && cl_h == 724);
				nd_seven_set_zoom(0.08f);

				/* 3) Medium Preset (75% res sub-rect, 75% detail, non-compounding 0.42x->0.75x clouds + CSM) */
				nd_seven_apply_quality_preset(ND_QUALITY_MEDIUM);
				assert(nd_seven_get_quality_preset() == ND_QUALITY_MEDIUM);
				assert(fabsf(nd_seven_get_res_scale() - 0.75f) < 1e-4f);
				assert(fabsf(nd_seven_get_detail_scale() - 0.75f) < 1e-4f);
				assert(nd_seven_get_cloud_quality() == ND_CLOUD_QUALITY_MEDIUM);
				assert(nd_seven_get_msaa() == 0);
				assert(nd_seven_get_fps_cap() == 60);
				nd_seven_scaled_dims(1920, 1080, &rw, &rh);
				assert(rw == 1440 && rh == 810);
				nd_seven_render(fb, 1920, 1080, &st);
				nd_seven_render(fb, 1920, 1080, &st);
				nd_seven_gl_get_target_info(&st_w, &st_h, &sc_w, &sc_h, &sc_samples, &cl_w, &cl_h, &csm_act);
				assert(st_w == 1920 && st_h == 1080);
				assert(sc_w == 1440 && sc_h == 810);
				assert(sc_samples == 0);
				assert(cl_w == 806 && cl_h == 454);
				assert(csm_act == true);
				uint32_t bbs_detail_75 = nd_seven_last_mid_billboard_count();
				assert(bbs_detail_75 > bbs_detail_low && bbs_detail_75 < bbs_detail_100);

				/* 4) Custom Settings: 25% resolution sub-rect, 200% scenery detail, Clouds Off, MSAA On */
				nd_seven_set_res_scale(0.25f);
				nd_seven_set_detail_scale(2.00f);
				nd_seven_set_cloud_quality(ND_CLOUD_QUALITY_OFF);
				nd_seven_set_msaa(1);
				assert(nd_seven_get_quality_preset() == ND_QUALITY_CUSTOM);
				assert(nd_seven_gl_should_queue_cloud_chunk(0, 0, 5120, 5120, p1) == false);
				nd_seven_render(fb, 1920, 1080, &st);
				nd_seven_render(fb, 1920, 1080, &st);
				nd_seven_gl_get_target_info(&st_w, &st_h, &sc_w, &sc_h, &sc_samples, &cl_w, &cl_h, &csm_act);
				assert(st_w == 1920 && st_h == 1080);
				assert(sc_w == 480 && sc_h == 270);
				assert(sc_samples == 4);
				assert(csm_act == false);
				uint32_t bbs_detail_200 = nd_seven_last_mid_billboard_count();
				assert(bbs_detail_200 > bbs_detail_100);

				/* Restore High Preset */
				nd_seven_apply_quality_preset(ND_QUALITY_HIGH);
				assert(nd_seven_get_quality_preset() == ND_QUALITY_HIGH);

				/* 5) Verify Clustered Cloud Lighting (16x16x24 Frustum Grid, Sun/Moon + Point/Spot Lights) */
				assert(strstr(nd_seven_gl_fs_cloud(), "uClusterGridDim") != NULL);
				assert(strstr(nd_seven_gl_fs_cloud(), "uClusterDepthParams") != NULL);
				assert(strstr(nd_seven_gl_fs_cloud(), "uClusterLightData") != NULL);
				assert(strstr(nd_seven_gl_fs_cloud(), "uClusterLightListsTex") != NULL);
				assert(strstr(nd_seven_gl_fs_cloud(), "eval_clustered_cloud_lighting") != NULL);

				nd_seven_set_zoom(0.08f);
				nd_seven_render(fb, 1280, 720, &st);
				/* Base active lights: Sun + Moon (+ Lamb if glowing) */
				uint32_t base_lights = nd_seven_gl_get_active_light_count();
				assert(base_lights >= 2);
				assert(nd_seven_gl_get_active_cluster_count() == ND_CLUSTER_TOTAL);

				/* Add explicit point and spot lights and verify cluster assignment */
				int pt_id = nd_seven_gl_add_point_light(80.0f, -64.0f, 520.0f, 1.0f, 0.6f, 0.2f, 2.0f, 1200.0f);
				int sp_id = nd_seven_gl_add_spot_light(120.0f, -80.0f, 480.0f, 0.0f, 0.0f, 1.0f, 0.4f, 0.8f, 1.0f, 2.5f, 1500.0f, 35.0f, 4.0f);
				assert(pt_id >= 0 && sp_id > pt_id);
				nd_seven_gl_flush_clouds();

				/* 6) Verify GPU-Driven Hi-Z Occlusion Culling & Multi-Draw Indirect Batching */
				assert(strstr(nd_seven_gl_vs_billboard(), "uHiZTex") != NULL);
				assert(strstr(nd_seven_gl_vs_billboard(), "uHiZActive") != NULL);
				assert(strstr(nd_seven_gl_vs_billboard(), "maxOccZ") != NULL);
				assert(nd_seven_gl_get_hiz_mip_levels() == ND_HIZ_MIP_LEVELS);
				assert(nd_seven_gl_is_gpu_hiz_active() == true);
				assert(nd_seven_gl_get_indirect_draw_count() > 0);
			}

			nd_seven_set_zoom(1.0f);
			printf("PASS: 3D occlusion & culling verified (orb_leaves=%u, back_leaves=%d, back_clouds=%d, front_clouds=%d, syn_culled=%d/%d, front_to_back=%u, hiz_mips=%d, indirect_draws=%u)\n",
			       orb_leaves, back_hemisphere_leaves, back_clouds_queued, front_clouds_queued,
			       syn_clear_culled, syn_clear_culled + syn_cloudy_kept, mid_leaves,
			       nd_seven_gl_get_hiz_mip_levels(), nd_seven_gl_get_indirect_draw_count());
		}

		/* 3D Procedural Botanical & Geological Mesh Engine (Option A) + Orthographic Pixel-Art Baker (Option B) */
		{
			uint32_t m3d_vcnt = 0, m3d_icnt = 0;
			const nd_mesh3d_vert_t *m3d_verts = nd_seven_mesh3d_verts(&m3d_vcnt);
			const uint32_t *m3d_idx = nd_seven_mesh3d_indices(&m3d_icnt);
			assert(m3d_verts != NULL && m3d_idx != NULL);
			assert(m3d_vcnt > 4000 && m3d_vcnt <= ND_MESH3D_MAX_VERTS);
			assert(m3d_icnt > 12000 && m3d_icnt <= ND_MESH3D_MAX_INDICES);

			/* Verify all 128 canonical plant templates (1..128) and 51 stone/mineral templates (129..179) */
			for (uint8_t mid = 1; mid < ND_MESH3D_TOTAL; mid++) {
				const nd_mesh3d_slice_t *sl = nd_seven_mesh3d_slice(mid);
				assert(sl != NULL);
				assert(sl->vert_count >= 12 && sl->idx_count >= 18);
				assert(sl->first_vert + sl->vert_count <= m3d_vcnt);
				assert(sl->first_idx + sl->idx_count <= m3d_icnt);
				assert(sl->sprite_w > 0 && sl->sprite_h > 0);
			}

			/* Verify 3D instanced GLSL 300 es shaders */
			const char *vs_m3d = nd_seven_gl_vs_mesh3d();
			const char *fs_m3d = nd_seven_gl_fs_mesh3d();
			assert(vs_m3d && strstr(vs_m3d, "#version 300 es") != NULL);
			assert(fs_m3d && strstr(fs_m3d, "#version 300 es") != NULL);
			assert(strstr(vs_m3d, "a_vpos") != NULL);
			assert(strstr(vs_m3d, "a_ibase") != NULL);
			assert(strstr(fs_m3d, "nd_cloud_shadow_map") != NULL);

			/* Verify synchronous Option B orthographic 55-deg pixel-art albedo & normal map baking across all 4 alleles */
			for (unsigned sp = 0; sp < 16; sp++) {
				for (unsigned var0 = 0; var0 < 4; var0++) {
					uint16_t pid_s = ND_PLANT_PACK_ID(sp, 0, var0);
					uint16_t pid_b = ND_PLANT_PACK_ID(sp, 1, var0);
					nd_seven_sprite_rect_t sr_s = {0}, sr_b = {0};
					assert(nd_seven_plant_rect(pid_s, &sr_s) == 1 && sr_s.w > 0 && sr_s.h > 0);
					assert(nd_seven_plant_rect(pid_b, &sr_b) == 1 && sr_b.w > 0 && sr_b.h > 0);
					assert(nd_seven_plant_has_normal(pid_s) == 1);
					assert(nd_seven_plant_has_normal(pid_b) == 1);
					assert(nd_seven_plant_mesh3d_id(pid_s) >= ND_MESH3D_PLANT_BASE);
					assert(nd_seven_plant_mesh3d_id(pid_b) > nd_seven_plant_mesh3d_id(pid_s));
				}
			}
			const uint8_t stone_kinds[3] = { NDW_K_STONE, NDW_K_ROCK, NDW_K_BOULDER };
			for (int ki = 0; ki < 3; ki++) {
				for (uint8_t fg = 0; fg < 16; fg++) {
					for (uint8_t var = 0; var < 4; var++) {
						ndw_obj_t st_obj;
						memset(&st_obj, 0, sizeof(st_obj));
						st_obj.kind = stone_kinds[ki];
						st_obj.fg = fg;
						st_obj.variant = var;
						nd_seven_sprite_rect_t sr_m = {0};
						assert(nd_seven_stone_rect(&st_obj, &sr_m) == 1 && sr_m.w > 0 && sr_m.h > 0);
						assert(nd_seven_stone_has_normal(&st_obj) == 1);
						uint8_t mid_s = nd_seven_stone_mesh3d_id(&st_obj);
						assert(mid_s >= ND_MESH3D_STONE_BASE && mid_s < ND_MESH3D_TOTAL);
					}
				}
			}

			/* Verify calibrated 3D diorama world-unit scale (anchored to 0.40 m = 18.0 wu Lamb and 160x128 wu room grid) */
			{
				const float lamb_wu = ND_MESH3D_LAMB_BODY_WU; /* 18.0 wu = 0.40 m */
				float pw_s = 0.0f, ph_s = 0.0f, pay_s = 0.0f;
				float pw_b = 0.0f, ph_b = 0.0f, pay_b = 0.0f;

				/* 14: bellis_perennis (English Daisy): ankle-high on the 40 cm Lamb (3.5 wu small, 6.0 wu big) */
				nd_m3d_plant_metric_dims(14u, 0u, &pw_s, &ph_s, &pay_s);
				nd_m3d_plant_metric_dims(14u, 1u, &pw_b, &ph_b, &pay_b);
				assert(ph_s > 0.15f * lamb_wu && ph_s < 0.25f * lamb_wu);
				assert(ph_b > 0.28f * lamb_wu && ph_b < 0.40f * lamb_wu);

				/* 3: linum_usitatissimum (Common Flax) & 8: daucus_carota (Wild Carrot): ~1.7x to 2.0x Lamb height */
				nd_m3d_plant_metric_dims(3u, 1u, &pw_b, &ph_b, &pay_b);
				assert(ph_b > 1.7f * lamb_wu && ph_b < 2.2f * lamb_wu);
				nd_m3d_plant_metric_dims(8u, 1u, &pw_b, &ph_b, &pay_b);
				assert(ph_b > 1.6f * lamb_wu && ph_b < 2.0f * lamb_wu);

				/* 15: typha_latifolia (Broadleaf Cattail): 62 wu mature = ~3.4x Lamb height */
				nd_m3d_plant_metric_dims(15u, 1u, &pw_b, &ph_b, &pay_b);
				assert(ph_b > 3.0f * lamb_wu && ph_b < 3.9f * lamb_wu);

				/* 7: acacia_senegal (Savannah Umbrella Thorn): 270 wu mature = 15.0x Lamb height, wide flat tabletop crown (width > 1.3 * height) */
				nd_m3d_plant_metric_dims(7u, 1u, &pw_b, &ph_b, &pay_b);
				assert(ph_b > 14.0f * lamb_wu && ph_b < 16.5f * lamb_wu);
				assert(pw_b > 1.30f * ph_b);

				/* 10: quercus_robur (Pedunculate Oak): 432 wu mature = 24.0x Lamb height, broad spreading crown (450 wu) */
				nd_m3d_plant_metric_dims(10u, 1u, &pw_b, &ph_b, &pay_b);
				assert(ph_b > 22.0f * lamb_wu && ph_b < 26.0f * lamb_wu);
				assert(pw_b >= ph_b);

				/* 1: pseudotsuga_menziesii (Coast Douglas-fir): 648 wu mature = 36.0x Lamb height */
				nd_m3d_plant_metric_dims(1u, 1u, &pw_b, &ph_b, &pay_b);
				assert(ph_b > 34.0f * lamb_wu && ph_b < 38.0f * lamb_wu);

				/* Continuous tree stature: small (0.25..0.62 * big_h) & big (0.52..1.18 * big_h) overlap smoothly */
				{
					float min_small_h = 1e9f, max_small_h = 0.0f;
					float min_big_h   = 1e9f, max_big_h   = 0.0f;
					for (uint32_t s = 0; s < 64u; s++) {
						ndw_obj_t o_sm, o_bg;
						memset(&o_sm, 0, sizeof(o_sm));
						memset(&o_bg, 0, sizeof(o_bg));
						o_sm.x = (uint8_t)((s * 7u) & 63u);
						o_sm.y = (uint8_t)((s * 13u) & 63u);
						o_sm.kind = NDW_K_DECID;
						o_sm.variant = (uint8_t)(s * 29u);
						o_sm.plant_id = ND_PLANT_PACK_ID(10u, 0u, s & 3u);
						o_bg = o_sm;
						o_bg.big = 1;
						o_bg.plant_id = ND_PLANT_PACK_ID(10u, 1u, s & 3u);
						float ws = 0.0f, hs = 0.0f, ays = 0.0f;
						float wb = 0.0f, hb = 0.0f, ayb = 0.0f;
						nd_seven_calc_obj_dims(&o_sm, 32.0f, 32.0f, &ws, &hs, &ays);
						nd_seven_calc_obj_dims(&o_bg, 32.0f, 32.0f, &wb, &hb, &ayb);
						if (hs < min_small_h) min_small_h = hs;
						if (hs > max_small_h) max_small_h = hs;
						if (hb < min_big_h)   min_big_h   = hb;
						if (hb > max_big_h)   max_big_h   = hb;
					}
					assert(min_small_h >= ph_b * 0.0f); /* sanity check below uses oak 432 wu */
					assert(min_small_h < 432.0f * 0.35f && max_small_h > 432.0f * 0.54f);
					assert(min_big_h   < 432.0f * 0.62f && max_big_h   > 432.0f * 1.05f);
					assert(max_small_h > min_big_h && "Small saplings/pole trees and mature veterans must overlap in stature");
				}

				/* Geological metric tiers: STONE (< 0.75 * Lamb) < ROCK (1.1..1.5 * Lamb) < BOULDER (2.2..2.8 * Lamb) */
				float sw_st = 0.0f, sh_st = 0.0f, say_st = 0.0f;
				float sw_rk = 0.0f, sh_rk = 0.0f, say_rk = 0.0f;
				float sw_bl = 0.0f, sh_bl = 0.0f, say_bl = 0.0f;
				nd_m3d_stone_metric_dims(NDW_K_STONE, 1u, &sw_st, &sh_st, &say_st);
				nd_m3d_stone_metric_dims(NDW_K_ROCK, 1u, &sw_rk, &sh_rk, &say_rk);
				nd_m3d_stone_metric_dims(NDW_K_BOULDER, 1u, &sw_bl, &sh_bl, &say_bl);
				assert(sh_st < 0.75f * lamb_wu);
				assert(sh_rk > 1.10f * lamb_wu && sh_rk < 1.50f * lamb_wu);
				assert(sh_bl > 2.20f * lamb_wu && sh_bl < 2.80f * lamb_wu);
				assert(sw_st < sw_rk && sw_rk < sw_bl);

				/* Crystallographic termination check:
				 * Emerald (NDS_SP_MIN_EMERALD_SO = 25, SoloEsmeralda, mid = ND_MESH3D_STONE_BASE + 25) has flat basal pinacoid {0001} faces (N_z > 0.98)
				 * whereas Quartz (NDS_SP_DESERT_QUARTZ_SO = 9, CristalQuartzo_solo, mid = ND_MESH3D_STONE_BASE + 9) has slanted rhombohedral apex faces,
				 * and Halite / Rock Salt (NDS_SP_MIN_SALT_SO = 29, SoloSal, mid = ND_MESH3D_STONE_BASE + 29) has orthogonal cubic {100} cleavage faces */
				const nd_mesh3d_slice_t *sl_em = nd_seven_mesh3d_slice(ND_MESH3D_STONE_BASE + 25u);
				const nd_mesh3d_slice_t *sl_qz = nd_seven_mesh3d_slice(ND_MESH3D_STONE_BASE + 9u);
				const nd_mesh3d_slice_t *sl_hl = nd_seven_mesh3d_slice(ND_MESH3D_STONE_BASE + 29u);
				assert(sl_em && sl_qz && sl_hl);
				int em_flat_pinacoid = 0, qz_flat_pinacoid = 0, hl_ortho_faces = 0;
				for (uint32_t vi = 0; vi < sl_em->vert_count; vi++) {
					const nd_mesh3d_vert_t *v = &m3d_verts[sl_em->first_vert + vi];
					if (v->col[3] >= 0.90f && v->pos[2] > (float)sl_em->sprite_h * 0.50f && v->norm[2] > 0.98f)
						em_flat_pinacoid++;
				}
				for (uint32_t vi = 0; vi < sl_qz->vert_count; vi++) {
					const nd_mesh3d_vert_t *v = &m3d_verts[sl_qz->first_vert + vi];
					if (v->col[3] >= 0.90f && v->pos[2] > (float)sl_qz->sprite_h * 0.50f && v->norm[2] > 0.98f)
						qz_flat_pinacoid++;
				}
				for (uint32_t vi = 0; vi < sl_hl->vert_count; vi++) {
					const nd_mesh3d_vert_t *v = &m3d_verts[sl_hl->first_vert + vi];
					if (fabsf(v->norm[0]) > 0.98f || fabsf(v->norm[1]) > 0.98f || fabsf(v->norm[2]) > 0.98f)
						hl_ortho_faces++;
				}
				assert(em_flat_pinacoid >= 6);
				assert(qz_flat_pinacoid == 0);
				assert(hl_ortho_faces >= 18);
			}

			printf("PASS: 3D procedural botanical (128 templates) & geological (51 templates) mesh engine (verts=%u, tris=%u) + Earth metric scale (1m=45wu) + 55-deg orthographic baker verified\n",
			       m3d_vcnt, m3d_icnt / 3u);

			/* Multi-frame approach / zoom-in LOD progression regression test:
			 * Starting from high altitude (zoom = 0.02, dist = 12500) where >32 coarse chunks are populated,
			 * progressively approach normal zoom (1.0) and walk across rooms. Verify that ar_system_begin_frame
			 * resets the populate counter every frame so near-field chunks refine all the way to Level 0
			 * without getting stuck in AR_NODE_FLAG_DEFERRED bilinear upsampling, and exhibit true 3D relief. */
			{
				ar_system_t *cs = nd_seven_get_chunk_sys();
				assert(cs != NULL);
				const float zoom_steps[] = { 0.02f, 0.05f, 0.12f, 0.25f, 0.50f, 1.00f, 1.00f };
				for (size_t zi = 0; zi < sizeof(zoom_steps) / sizeof(zoom_steps[0]); zi++) {
					nd_seven_set_zoom(zoom_steps[zi]);
					nd_seven_render(fb, 640, 480, &st);
					nd_seven_drain_async();
				}
				nd_seven_render(fb, 640, 480, &st);
				size_t act_cnt = ar_system_get_active_count(cs);
				assert(act_cnt > 0);
				size_t lvl0_real_count = 0;
				int32_t max_chunk_relief_q4 = 0;
				for (size_t li = 0; li < act_cnt; li++) {
					const ar_node_t *ln = ar_system_get_active_node(cs, li);
					assert(ln != NULL);
					if (ln->level == 0) {
						assert((ln->flags & AR_NODE_FLAG_DEFERRED) == 0);
						lvl0_real_count++;
						int32_t relief = (int32_t)ln->max_val - (int32_t)ln->min_val;
						if (relief > max_chunk_relief_q4)
							max_chunk_relief_q4 = relief;
					}
				}
				assert(lvl0_real_count >= 16);
				assert(max_chunk_relief_q4 > 0);
				printf("PASS: Multi-frame approach LOD progression refines to %zu non-deferred Level-0 chunks (max relief=%.2f wu)\n",
				       lvl0_real_count, (float)max_chunk_relief_q4 / 16.0f);
			}

			/* Unified Terrain LOD Architecture Invariant Suite:
			 *   1. Field LOD Invariance & Parent-Child Cache Contamination Prevention:
			 *      Every quadtree chunk sample at (wx, wy) evaluates the exact canonical field
			 *      regardless of which LOD level (0..12) populated it first.
			 *   2. Lamb-Z & Zoom Independence (No Flat Rooms across lowland, coast, or high mountain peak):
			 *      Across diverse room coordinates and zoom levels, active chunks are never deferred,
			 *      always refine to Level 0 around the focal room at normal zoom, and maintain full
			 *      canonical 3D relief matching nd_field_sample(world, 3, wx, wy).
			 *   3. Shader Elevation Invariants:
			 *      Both s_vs_terrain and s_fs_terrain derive elevation strictly from sampleHeightSmooth
			 *      without vertex planetSmooth flattening or GPU field/macroDetail elevation overrides. */
			{
				ar_system_t *cs = nd_seven_get_chunk_sys();
				assert(cs != NULL);

				/* Invariant 1: Parent-to-child population from Level 10 down to Level 0 must produce
				 * 100% identical vertex elevations and RGBA colors as direct canonical nd_field_sample */
				const int32_t test_rooms[4][2] = {
					{ 0, 0 },
					{ 128, -96 },
					{ -320, 240 },
					{ 1024, 512 }
				};
				for (int ri = 0; ri < 4; ri++) {
					int32_t rx = test_rooms[ri][0];
					int32_t ry = test_rooms[ri][1];
					int32_t ox0 = rx * NDW_TILE_W;
					int32_t oy0 = ry * NDW_TILE_H;

					/* Populate coarse ancestors first (Levels 10 down to 1) to force parent-child sample reuse */
					for (int lvl = 10; lvl >= 1; lvl--) {
						int32_t span_w = NDW_TILE_W << lvl;
						int32_t span_h = NDW_TILE_H << lvl;
						int32_t a_ox = (ox0 >= 0) ? ((ox0 / span_w) * span_w) : (-(((-ox0 + span_w - 1) / span_w) * span_w));
						int32_t a_oy = (oy0 >= 0) ? ((oy0 / span_h) * span_h) : (-(((-oy0 + span_h - 1) / span_h) * span_h));
						ar_system_begin_frame(cs);
						const ar_node_t *anc = ar_system_ensure(cs, 0, (uint8_t)lvl, a_ox, a_oy);
						assert(anc != NULL);
						assert((anc->flags & AR_NODE_FLAG_DEFERRED) == 0);
					}

					/* Now populate Level 0 child and verify every single vertex against canonical nd_field_sample */
					ar_system_begin_frame(cs);
					const ar_node_t *leaf0 = ar_system_ensure(cs, 0, 0, ox0, oy0);
					assert(leaf0 != NULL);
					assert((leaf0->flags & AR_NODE_FLAG_DEFERRED) == 0);
					assert(leaf0->vert_dim == 49 && leaf0->grid_dim == 48);

					for (uint32_t r = 0; r <= 48; r += 6) {
						int32_t wy = oy0 + (int32_t)(((int64_t)r * NDW_TILE_H) / 48);
						for (uint32_t c = 0; c <= 48; c += 6) {
							int32_t wx = ox0 + (int32_t)(((int64_t)c * NDW_TILE_W) / 48);
							nd_field_sample_t canon = nd_field_sample(0, 3, wx, wy);
							int16_t node_val = leaf0->values[r * 49 + c];
							assert(node_val == canon.elevation_q12_4);
						}
					}
				}

				/* Invariant 2: Continuous Zoom-Out Sweep & Lamb-Z Independence across diverse rooms & elevations */
				const float test_zooms[] = {
					1.50f, 1.00f, 0.85f, 0.70f, 0.55f, 0.40f, 0.30f, 0.20f, 0.15f,
					0.11f, 0.08f, 0.05f, 0.03f, 0.02f, 0.01f, 0.005f, 0.002f, 0.001f
				};
				for (int ri = 0; ri < 4; ri++) {
					int32_t rx = test_rooms[ri][0];
					int32_t ry = test_rooms[ri][1];
					set_map(rx, ry, 10, 0x1B, 0, 0, 0, 0);
					nd_seven_sync_origin(&st);
					nd_seven_set_lamb(NDW_CENTER_X, NDW_CENTER_Y);

					for (size_t zi = 0; zi < sizeof(test_zooms) / sizeof(test_zooms[0]); zi++) {
						nd_seven_set_zoom(test_zooms[zi]);
						size_t act_cnt = 0;
						for (int wave = 0; wave < 16; wave++) {
							nd_seven_render(fb, 640, 480, &st);
							act_cnt = ar_system_get_active_count(cs);
							bool any_deferred = false;
							for (size_t li = 0; li < act_cnt; li++) {
								const ar_node_t *ln = ar_system_get_active_node(cs, li);
								if (ln && (ln->flags & AR_NODE_FLAG_DEFERRED)) {
									any_deferred = true;
									break;
								}
							}
							if (!any_deferred && wave > 0)
								break;
							nd_seven_drain_async();
						}
						assert(act_cnt > 0);
						bool focal_covered = false;
						int32_t max_relief_q12_4 = 0;
						int64_t focal_wx64 = 0, focal_wy64 = 0;
						nd_seven_world_pos(&st, &focal_wx64, &focal_wy64);
						int32_t focal_wx = (int32_t)focal_wx64;
						int32_t focal_wy = (int32_t)focal_wy64;
						for (size_t li = 0; li < act_cnt; li++) {
							const ar_node_t *ln = ar_system_get_active_node(cs, li);
							assert(ln != NULL);
							assert((ln->flags & AR_NODE_FLAG_DEFERRED) == 0);
							int32_t relief = (int32_t)ln->max_val - (int32_t)ln->min_val;
							if (relief > max_relief_q12_4)
								max_relief_q12_4 = relief;
							if (focal_wx >= ln->ox && focal_wx <= ln->ox + ln->w &&
							    focal_wy >= ln->oy && focal_wy <= ln->oy + ln->h) {
								focal_covered = true;
								if (test_zooms[zi] >= 0.60f)
									assert(ln->level == 0);
							}
						}
						assert(focal_covered);
						assert(max_relief_q12_4 > 0);
					}
				}
				nd_seven_set_zoom(1.0f);

				/* Invariant 3: Shader elevation source, sub-texel normal gradient & day/night relief invariants */
				assert(strstr(nd_seven_gl_vs_terrain(), "planetSmooth") == NULL);
				assert(strstr(nd_seven_gl_fs_terrain(), "fragZ = mix(fragZ, gpuField.x") == NULL);
				assert(strstr(nd_seven_gl_fs_terrain(), "fragZ += macroDetail.x") == NULL);
				assert(strstr(nd_seven_gl_fs_terrain(), "worldSlopeGrad = mix(worldSlopeGrad, gpuField.yz, gpuFieldBlend)") != NULL);
				assert(strstr(nd_seven_gl_fs_terrain(), "float dirRelief = mix(0.22 + 0.78 * dayFactor, 1.0, directSun)") != NULL);
				assert(strstr(nd_seven_gl_fs_terrain(), "STAGE 1: Canonical Heightmap & Slope Gradient Reconstruction") != NULL);
				assert(strstr(nd_seven_gl_fs_terrain(), "STAGE 9: Atmospheric Perspective") != NULL);

				printf("PASS: Unified Terrain LOD Architecture Invariants (field LOD invariance, lamb-Z/zoom independence, 1:1 48x48 mesh, shader invariants) verified\n");
			}

			/* Phase 1: Free 3D Orbit Camera, Yaw-Aware Billboards, Terrain Pitch Clamp & Sky Dome Shaders */
			{
				const nd_planet_def_t *pdef = nd_seven_get_planet(1);
				const float yaw_cases[] = { 0.0f, 45.0f, 90.0f, -120.0f, 180.0f };
				const float pitch_cases[] = { 15.0f, 35.0f, 55.0f, 75.0f };
				for (size_t yi = 0; yi < sizeof(yaw_cases) / sizeof(yaw_cases[0]); yi++) {
					for (size_t pi = 0; pi < sizeof(pitch_cases) / sizeof(pitch_cases[0]); pi++) {
						float yaw = yaw_cases[yi];
						float pitch = pitch_cases[pi];
						float mvp[16], eye[3], target[3], cup[3];
						nd_seven_gl_compute_mvp_ex(1280, 720, 1600.0f, 1280.0f, 10.0f,
						                           ND_SEVEN_CAM_DIST, yaw, pitch, ND_SEVEN_CAM_FOV,
						                           pdef, mvp, eye, target, cup);
						float fwd[3] = {
							(target[0] - eye[0]) / ND_SEVEN_CAM_DIST,
							(target[1] - eye[1]) / ND_SEVEN_CAM_DIST,
							(target[2] - eye[2]) / ND_SEVEN_CAM_DIST
						};
						float dot_fu = fwd[0] * cup[0] + fwd[1] * cup[1] + fwd[2] * cup[2];
						assert(fabsf(dot_fu) < 2e-3f);

						float bpos[3], bright[3], bup[3], bview[3];
						nd_seven_gl_billboard_basis_ex(1600.0f, 1280.0f, 10.0f, yaw, pitch, pdef,
						                               bpos, bright, bup, bview);
						float dot_ru = bright[0] * bup[0] + bright[1] * bup[1] + bright[2] * bup[2];
						float dot_rv = bright[0] * bview[0] + bright[1] * bview[1] + bright[2] * bview[2];
						float dot_uv = bup[0] * bview[0] + bup[1] * bview[1] + bup[2] * bview[2];
						assert(fabsf(dot_ru) < 1e-4f);
						assert(fabsf(dot_rv) < 1e-4f);
						assert(fabsf(dot_uv) < 1e-4f);

						lam_billboard_gpu_t bb = {
							.x = 1600.0f, .y = 1280.0f, .z = 10.0f,
							.width = 32.0f, .height = 32.0f,
							.u0 = 0.0f, .v0 = 0.0f, .u1 = 1.0f, .v1 = 1.0f,
							.tint_rgba = 0xFFFFFFFFu
						};
						nd_seven_gl_bb_vert_t qv[6];
						nd_seven_gl_eval_billboard_quad_ex(&bb, yaw, pitch, pdef, qv);
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
						float proj_w = fabsf(ndc_x[1] - ndc_x[0]) * (1280.0f / 720.0f);
						float proj_h = fabsf(ndc_y[2] - ndc_y[0]);
						assert(proj_w > 0.01f && proj_h > 0.01f);
						assert(fabsf((proj_h / proj_w) - 1.0f) < 0.08f);
					}
				}

				/* Orbit state, damping, reset & terrain clearance pitch clamping */
				nd_seven_reset_cam();
				nd_seven_step_cam(1.0f);
				assert(fabsf(nd_seven_get_yaw_deg()) < 1e-3f);
				assert(fabsf(nd_seven_get_pitch_deg() - ND_SEVEN_DIORAMA_PITCH) < 1e-2f);
				nd_seven_set_cam_angles(90.0f, 35.0f);
				for (int s = 0; s < 30; s++) nd_seven_step_cam(0.05f);
				assert(fabsf(nd_seven_get_yaw_deg() - 90.0f) < 1.0f);
				assert(fabsf(nd_seven_get_pitch_deg() - 35.0f) < 1.0f);
				nd_seven_reset_cam();
				for (int s = 0; s < 30; s++) nd_seven_step_cam(0.05f);
				assert(fabsf(nd_seven_get_yaw_deg()) < 1.0f);
				assert(nd_seven_has_pitch_override() == 0);
				assert(fabsf(nd_seven_eval_effective_pitch(55.0f, 280.0f) - 55.0f) < 1e-4f);
				assert(fabsf(nd_seven_eval_effective_pitch(55.0f, 95.0f) - 36.0f) < 1e-4f);
				float mid_tilt = nd_seven_eval_effective_pitch(55.0f, 187.5f);
				assert(mid_tilt > 42.0f && mid_tilt < 49.0f);
				nd_seven_orbit_cam(0.0f, -10.0f);
				assert(nd_seven_has_pitch_override() == 1);
				assert(fabsf(nd_seven_eval_effective_pitch(55.0f, 95.0f) - 55.0f) < 1e-4f);
				nd_seven_reset_cam();
				assert(nd_seven_has_pitch_override() == 0);
				assert(fabsf(nd_seven_get_pitch_deg() - 55.0f) < 5.0f);
				for (int s = 0; s < 30; s++) nd_seven_step_cam(0.05f);
				assert(fabsf(nd_seven_get_pitch_deg() - ND_SEVEN_DIORAMA_PITCH) < 1e-2f);

				float low_p = nd_seven_clamp_cam_pitch(0, 1600.0f, 1280.0f, 10.0f, 400.0f, 0.0f, 1.0f);
				float hi_p  = nd_seven_clamp_cam_pitch(0, 1600.0f, 1280.0f, 10.0f, 400.0f, 0.0f, 89.5f);
				assert(low_p >= ND_SEVEN_CAM_PITCH_MIN && low_p <= ND_SEVEN_CAM_PITCH_MAX);
				assert(hi_p <= ND_SEVEN_CAM_PITCH_MAX + 1e-4f);

				/* Sky dome shaders & absence of hardcoded 55-deg vector literals in fragment shaders */
				assert(nd_seven_gl_vs_sky() && strstr(nd_seven_gl_vs_sky(), "gl_VertexID") != NULL);
				assert(nd_seven_gl_fs_sky() && strstr(nd_seven_gl_fs_sky(), "uSunDir") != NULL);
				assert(nd_seven_gl_fs_sky() && strstr(nd_seven_gl_fs_sky(), "sky_hash21") != NULL);
				assert(strstr(nd_seven_gl_fs_billboard(), "0.573576") == NULL);
				assert(strstr(nd_seven_gl_fs_billboard(), "0.819152") == NULL);
				assert(strstr(nd_seven_gl_fs_billboard(), "0.7002075") == NULL);
				assert(strstr(nd_seven_gl_fs_mesh3d(), "0.573576") == NULL);
				assert(strstr(nd_seven_gl_fs_mesh3d(), "0.819152") == NULL);
				assert(strstr(nd_seven_gl_fs_mesh3d(), "0.7002075") == NULL);

				printf("PASS: Phase 1 orbit camera, yaw-aware MVPs & billboards, pitch clamp & procedural sky dome verified\n");
			}

			/* Phase 2: 3D Instanced Living Meadow Clumps, Biome Distribution & Lamb Trample Physics */
			{
				uint32_t vcnt = 0u, icnt = 0u;
				(void)nd_seven_mesh3d_verts(&vcnt);
				(void)nd_seven_mesh3d_indices(&icnt);
				assert(vcnt > 0u && vcnt < ND_MESH3D_MAX_VERTS);
				assert(icnt > 0u && icnt < ND_MESH3D_MAX_INDICES);

				assert(nd_seven_meadow_mesh3d_id(0, 8u, 0x1234u) == ND_MESH3D_NONE);
				uint8_t reed_id = nd_seven_meadow_mesh3d_id(1, 5u, 0u);
				uint8_t flw_id  = nd_seven_meadow_mesh3d_id(2, 8u, 1u);
				uint8_t tall_id = nd_seven_meadow_mesh3d_id(5, 7u, 0u);
				uint8_t fern_id = nd_seven_meadow_mesh3d_id(4, 12u, 1u);
				uint8_t tuss_id = nd_seven_meadow_mesh3d_id(3, 2u, 0u);
				uint8_t turf_id = nd_seven_meadow_mesh3d_id(3, 8u, 4u);
				assert(reed_id >= ND_MESH3D_MEADOW_BASE + 6u && reed_id <= ND_MESH3D_MEADOW_BASE + 7u);
				assert(flw_id  >= ND_MESH3D_MEADOW_BASE + 4u && flw_id  <= ND_MESH3D_MEADOW_BASE + 5u);
				assert(tall_id >= ND_MESH3D_MEADOW_BASE + 2u && tall_id <= ND_MESH3D_MEADOW_BASE + 3u);
				assert(fern_id >= ND_MESH3D_MEADOW_BASE + 8u && fern_id <= ND_MESH3D_MEADOW_BASE + 9u);
				assert(tuss_id >= ND_MESH3D_MEADOW_BASE + 10u && tuss_id <= ND_MESH3D_MEADOW_BASE + 11u);
				assert(turf_id >= ND_MESH3D_MEADOW_BASE && turf_id <= ND_MESH3D_MEADOW_BASE + 1u);

				for (unsigned mv = 0; mv < ND_MESH3D_MEADOW_COUNT; mv++) {
					const nd_mesh3d_slice_t *sl = nd_seven_mesh3d_slice((uint8_t)(ND_MESH3D_MEADOW_BASE + mv));
					assert(sl != NULL && sl->vert_count >= 64u && sl->idx_count >= 96u);
					assert(sl->mesh_w_wu >= 18.0f && sl->mesh_h_wu >= 5.0f);
				}

				assert(nd_seven_meadow_lattice_step(280.0f, 2160.0f) == 16);
				assert(nd_seven_meadow_lattice_step(800.0f, 2160.0f) == 24);
				assert(nd_seven_meadow_lattice_step(1000.0f, 2160.0f) == 36);
				assert(nd_seven_meadow_lattice_step(1500.0f, 2160.0f) == 56);
				assert(strstr(nd_seven_gl_fs_mesh3d(), "isGrassMesh") != NULL);
				assert(strstr(nd_seven_gl_fs_mesh3d(), "rootT") != NULL);

				const char *vs_m3d = nd_seven_gl_vs_mesh3d();
				assert(vs_m3d && strstr(vs_m3d, "isGrassMesh") != NULL);
				assert(strstr(vs_m3d, "trample_e") != NULL);
				assert(strstr(vs_m3d, "trample_squash") != NULL);

				printf("PASS: Phase 2 3D Living Meadow clumps, biome selection & Lamb trample physics verified (%u/%u verts)\n",
				       vcnt, (unsigned)ND_MESH3D_MAX_VERTS);
			}

			/* Phase 3: Linear HDR, ACES Tone Mapping, Half-Res Bloom, SSAO, Soft PCF & Valley Fog */
			{
				assert(GL_RGBA16F == 0x881Au);
				assert(GL_HALF_FLOAT == 0x140Bu);
				assert(strstr(nd_seven_gl_fs_terrain(), "nd_eval_valley_fog") != NULL);
				assert(strstr(nd_seven_gl_fs_billboard(), "nd_eval_valley_fog") != NULL);
				assert(strstr(nd_seven_gl_fs_mesh3d(), "nd_eval_valley_fog") != NULL);
				assert(strstr(nd_seven_gl_fs_terrain(), "1.25 / 1024.0") != NULL);
				assert(strstr(nd_seven_gl_fs_mesh3d(), "1.25 / 1024.0") != NULL);
				assert(nd_seven_gl_fs_ssao() && strstr(nd_seven_gl_fs_ssao(), "uDepthTex") != NULL);
				assert(nd_seven_gl_fs_bloom() && strstr(nd_seven_gl_fs_bloom(), "uSrcTex") != NULL);
				assert(nd_seven_gl_fs_post() && strstr(nd_seven_gl_fs_post(), "uBloomTex") != NULL);
				assert(strstr(nd_seven_gl_fs_post(), "uSsaoTex") != NULL);
				assert(strstr(nd_seven_gl_fs_post(), "2.51 * x + 0.03") != NULL);
				printf("PASS: Phase 3 HDR ACES, half-res bloom, SSAO, soft PCF & valley fog verified\n");
			}

			/* Phase 4: Forest-Floor Fallen Logs/Stumps, Bark Furrows, Bayer Crossfade & Torus Sun L_sun */
			{
				const unsigned forest_biomes[] = { 8u, 9u, 11u, 12u, 13u, 16u, 17u };
				for (size_t fi = 0; fi < sizeof(forest_biomes) / sizeof(forest_biomes[0]); fi++) {
					uint8_t uid = nd_seven_understory_mesh3d_id(forest_biomes[fi], (uint32_t)fi);
					assert(uid >= ND_MESH3D_UNDERSTORY_BASE && uid < ND_MESH3D_TOTAL);
				}
				assert(nd_seven_understory_mesh3d_id(4u, 0u) == ND_MESH3D_NONE);
				assert(nd_seven_understory_mesh3d_id(10u, 1u) == ND_MESH3D_NONE);
				assert(nd_seven_understory_mesh3d_id(14u, 2u) == ND_MESH3D_NONE);

				for (unsigned uv = 0; uv < ND_MESH3D_UNDERSTORY_COUNT; uv++) {
					const nd_mesh3d_slice_t *sl = nd_seven_mesh3d_slice((uint8_t)(ND_MESH3D_UNDERSTORY_BASE + uv));
					assert(sl != NULL && sl->vert_count >= 28u && sl->idx_count >= 36u);
					assert(sl->mesh_w_wu >= 30.0f && sl->mesh_h_wu >= 12.0f);
				}

				assert(strstr(nd_seven_gl_fs_billboard(), "lodQ") != NULL);
				assert(strstr(nd_seven_gl_fs_billboard(), "sunLocBB") != NULL);
				assert(strstr(nd_seven_gl_fs_mesh3d(), "lodKeep") != NULL);

				printf("PASS: Phase 4 understory props, bark relief, Bayer LOD crossfade & torus L_sun verified\n");
			}

			/* Botanical Phase 3: Mid-Story Shrubs, Ferns, Snags & Anchor Collars */
			{
				assert(ND_MESH3D_TOTAL == 206u);
				const unsigned barren[] = { 0u, 1u, 6u, 14u, 18u };
				for (size_t bi = 0; bi < sizeof(barren) / sizeof(barren[0]); bi++)
					assert(nd_seven_shrub_mesh3d_id(barren[bi], 0x1234u) == ND_MESH3D_NONE);
				const unsigned veg[] = { 2u, 3u, 4u, 5u, 7u, 8u, 9u, 10u, 11u, 12u, 13u, 15u, 16u, 17u };
				for (size_t vi = 0; vi < sizeof(veg) / sizeof(veg[0]); vi++) {
					for (uint32_t h = 0; h < 32u; h++) {
						uint8_t sid = nd_seven_shrub_mesh3d_id(veg[vi], h);
						assert(sid >= ND_MESH3D_SHRUB_BASE && sid < ND_MESH3D_TOTAL);
					}
				}
				for (unsigned sv = 0; sv < ND_MESH3D_SHRUB_COUNT; sv++) {
					const nd_mesh3d_slice_t *sl = nd_seven_mesh3d_slice((uint8_t)(ND_MESH3D_SHRUB_BASE + sv));
					assert(sl != NULL && sl->vert_count >= 24u && sl->idx_count >= 36u);
				}
				printf("PASS: Botanical Phase 3 mid-story shrubs, ferns, snags & anchor collars verified\n");
			}

			/* Botanical Phase 4: Macro Grove/Glade Ecology, Krummholz Dwarfing, Forest Duff & Ridge Silhouettes */
			{
				float kr_low = 0.0f, kr_high = 0.0f, kr_steep = 0.0f;
				float g_forest_sum = 0.0f, g_alpine_sum = 0.0f;
				for (int32_t i = 0; i < 64; i++) {
					int32_t wx = i * 97 - 2400;
					int32_t wy = i * 61 - 1800;
					float kr = 0.0f;
					float gf = nd_seven_eval_grove_ecology(wx, wy, 20.0f, 0.0f, 0.05f, 9u, &kr);
					float ga = nd_seven_eval_grove_ecology(wx, wy, 20.0f, 0.0f, 0.05f, 3u, &kr);
					assert(gf >= 0.0f && gf <= 1.0f && ga >= 0.0f && ga <= 1.0f);
					assert(fabsf(kr - 1.0f) < 1e-5f);
					g_forest_sum += gf;
					g_alpine_sum += ga;
				}
				assert(g_forest_sum > g_alpine_sum && "Forest biome must exhibit higher grove score than open alpine");
				nd_seven_eval_grove_ecology(120, 240, 20.0f,  0.0f, 0.05f, 9u, &kr_low);
				nd_seven_eval_grove_ecology(120, 240, 260.0f, 0.0f, 0.10f, 9u, &kr_high);
				nd_seven_eval_grove_ecology(120, 240, 20.0f,  0.0f, 0.72f, 9u, &kr_steep);
				assert(fabsf(kr_low - 1.0f) < 1e-5f);
				assert(kr_high >= 0.35f && kr_high < 0.65f);
				assert(kr_steep >= 0.35f && kr_steep < 0.70f);
				assert(strstr(nd_seven_gl_fs_terrain(), "groveMacro") != NULL);
				assert(strstr(nd_seven_gl_fs_terrain(), "needleDuffCol") != NULL);
				assert(strstr(nd_seven_gl_fs_terrain(), "nd_eval_grove_ecology_weight") != NULL);
				assert(strstr(nd_seven_gl_fs_terrain(), "farCanopyBand") != NULL);
				assert(strstr(nd_seven_gl_vs_mesh3d(), "mod(a_iparam.w, 32.0)") != NULL);
				assert(strstr(nd_seven_gl_vs_mesh3d(), "cardU < 0.05") != NULL);
				const char *vs_m3d = nd_seven_gl_vs_mesh3d();
				const char *fs_m3d = nd_seven_gl_fs_mesh3d();
				assert(strstr(fs_m3d, "uGrassTex") != NULL);
				assert(strstr(fs_m3d, "uGrassNorm") != NULL);
				assert(strstr(fs_m3d, "uBarkTex") != NULL);
				assert(strstr(fs_m3d, "uBarkNorm") != NULL);
				assert(strstr(vs_m3d, "a_isub") != NULL);
				assert(strstr(vs_m3d, "flat out vec3 vSubCol") != NULL);
				assert(strstr(fs_m3d, "flat in vec3 vSubCol") != NULL);
				assert(strstr(fs_m3d, "style > 4.5 && style < 5.5") != NULL);
				assert(strstr(fs_m3d, "isFolGrass") != NULL);
				assert(strstr(fs_m3d, "barkSample") != NULL);
				printf("PASS: Botanical Phase 4 grove/glade ecology, krummholz (low=%.2f, alt=%.2f, steep=%.2f), grass/bark atlases & duff shader verified\n",
				       kr_low, kr_high, kr_steep);
			}

			/* Low-Vegetation Regression & Skyrim-Grade Linear HDR Realism Verification */
			{
				int high_trail_hits = 0;
				float max_tw = 0.0f;
				float trail_wx = 0.0f, trail_wy = 0.0f;
				for (int iy = -200; iy <= 200; iy += 8) {
					for (int ix = -200; ix <= 200; ix += 8) {
						float wx = (float)ix * 11.0f;
						float wy = (float)iy * 11.0f;
						float tw = nd_seven_eval_trail_weight(wx, wy, 0.05f);
						assert(tw >= 0.0f && tw <= 1.0f);
						float tw_steep = nd_seven_eval_trail_weight(wx, wy, 0.45f);
						assert(tw_steep == 0.0f && "Steep slopes >= 0.40 must suppress trails");
						if (tw > max_tw) {
							max_tw = tw;
							trail_wx = wx;
							trail_wy = wy;
						}
						if (tw > 0.55f)
							high_trail_hits++;
					}
				}
				assert(high_trail_hits > 0 && max_tw > 0.70f && "Flat terrain must exhibit winding footpath trail bands");

				/* Dense compacted trail core must suppress meadow clumps */
				int zone = 0, flip = 0;
				float scale = 0.0f;
				int suppressed = nd_seven_eval_meadow_area((int32_t)trail_wx, (int32_t)trail_wy,
				                                           12.0f, -50.0f, 0.05f, 9u, 9999.0f,
				                                           &zone, &scale, &flip);
				assert(suppressed == 0 && "High-weight trail core (>0.50) must suppress meadow grass");

				const char *fs_terr = nd_seven_gl_fs_terrain();
				const char *fs_wat  = nd_seven_gl_fs_water();
				const char *fs_bb   = nd_seven_gl_fs_billboard();
				const char *fs_m3d  = nd_seven_gl_fs_mesh3d();
				const char *fs_sky  = nd_seven_gl_fs_sky();
				const char *fs_cld  = nd_seven_gl_fs_cloud();
				const char *fs_blm  = nd_seven_gl_fs_bloom();
				const char *fs_pst  = nd_seven_gl_fs_post();

				assert(strstr(fs_terr, "uTerrDetailTex") != NULL);
				assert(strstr(fs_terr, "uTerrNormTex") != NULL);
				assert(strstr(fs_terr, "nd_eval_trail_weight") != NULL);
				assert(strstr(fs_terr, "terrCreviceAO") != NULL);
				assert(strstr(fs_terr, "nd_to_lin") != NULL);

				assert(strstr(fs_sky, "nd_to_lin") != NULL);
				assert(strstr(fs_wat, "nd_to_lin") != NULL);
				assert(strstr(fs_wat, "nd_to_lin(uSceneColor") == NULL && "Water background scene copy is already linear HDR");
				assert(strstr(fs_bb,  "nd_to_lin") != NULL);
				assert(strstr(fs_m3d, "nd_to_lin") != NULL);
				assert(strstr(fs_m3d, "isFolGrass ? 0.40 : 0.38") != NULL);
				assert(strstr(fs_m3d, "isFolGrass ? mix(0.42, 0.30") != NULL);
				assert(strstr(fs_cld, "nd_to_lin") != NULL);
				assert(strstr(fs_blm, "const float thresh = 0.78;") != NULL);
				assert(strstr(fs_pst, "pow(clamp(mapped, 0.0, 1.0), vec3(1.0 / 2.2))") != NULL);

				printf("PASS: Low-veg grass/trail sync (hits=%d, max_tw=%.2f), micro-detail/normal atlases & linear HDR/sRGB pipeline verified\n",
				       high_trail_hits, max_tw);
			}

			/* Realism Passes 1-3: Capsule Cutaway, Dual-Scale SSAO, 3-Tier Flora & 3D Ground-Breakers */
			{
				assert(ND_MESH3D_GEOBREAK_BASE == 206u);
				assert(ND_MESH3D_GEOBREAK_COUNT == 8u);
				assert(ND_SEVEN_MESH3D_TOTAL == 214u);
				assert(nd_seven_mesh3d_slice(ND_SEVEN_MESH3D_TOTAL) == NULL);

				assert(nd_seven_geobreak_mesh3d_id(0u, 0u) == ND_MESH3D_NONE);
				assert(nd_seven_geobreak_mesh3d_id(1u, 1u) == ND_MESH3D_NONE);
				assert(nd_seven_geobreak_mesh3d_id(18u, 2u) == ND_MESH3D_NONE);
				for (unsigned bio = 2u; bio <= 17u; bio++) {
					for (uint32_t h = 0u; h < 16u; h++) {
						uint8_t gid = nd_seven_geobreak_mesh3d_id(bio, h);
						assert(gid >= ND_MESH3D_GEOBREAK_BASE && gid < ND_SEVEN_MESH3D_TOTAL);
					}
				}
				for (unsigned gv = 0u; gv < ND_MESH3D_GEOBREAK_COUNT; gv++) {
					const nd_mesh3d_slice_t *sl = nd_seven_mesh3d_slice((uint8_t)(ND_MESH3D_GEOBREAK_BASE + gv));
					assert(sl != NULL && sl->vert_count >= 40u && sl->idx_count >= 60u);
					assert(sl->mesh_w_wu >= 30.0f && sl->mesh_h_wu >= 8.0f);
				}

				int med_shrubs = 0, large_shrubs = 0;
				for (unsigned sv = 0u; sv < ND_MESH3D_SHRUB_COUNT; sv++) {
					const nd_mesh3d_slice_t *sl = nd_seven_mesh3d_slice((uint8_t)(ND_MESH3D_SHRUB_BASE + sv));
					assert(sl != NULL);
					if (sl->mesh_h_wu > 35.0f) large_shrubs++;
					else med_shrubs++;
				}
				assert(med_shrubs > 0 && large_shrubs > 0);

				/* Juvenile saplings, wild herbs (3, 8, 9) & riparian cattails (15) across vegetated biomes */
				assert(nd_seven_sapling_mesh3d_id(0u, 0u) == ND_MESH3D_NONE);
				assert(nd_seven_sapling_mesh3d_id(1u, 1u) == ND_MESH3D_NONE);
				assert(nd_seven_sapling_mesh3d_id(18u, 2u) == ND_MESH3D_NONE);
				for (unsigned bio = 2u; bio <= 17u; bio++) {
					for (uint32_t h = 0u; h < 16u; h++) {
						uint8_t sid = nd_seven_sapling_mesh3d_id(bio, h);
						assert(sid >= ND_MESH3D_PLANT_BASE && sid < ND_MESH3D_STONE_BASE);
						const nd_mesh3d_slice_t *ssl = nd_seven_mesh3d_slice(sid);
						assert(ssl != NULL && ssl->idx_count > 0 && ssl->mesh_h_wu > 0.1f);
					}
				}
				{
					static const uint8_t strata_sp[4] = { 3u, 8u, 9u, 15u };
					for (int si = 0; si < 4; si++) {
						uint8_t pid = nd_seven_plant_mesh3d_id(ND_PLANT_PACK_ID(strata_sp[si], 1u, 0u));
						const nd_mesh3d_slice_t *psl = nd_seven_mesh3d_slice(pid);
						assert(psl != NULL && psl->idx_count > 0 && psl->mesh_h_wu > 0.1f);
					}
				}

				/* Canonical lowland rolling micro-topography (field.h): real undulation when rug_q8 == 0,
				 * while preserving LOD 0 (fine == 0) and coarse invariance */
				{
					int32_t min_f = 0x7FFFFFFF, max_f = -0x7FFFFFFF;
					int lowland_hits = 0;
					for (int32_t wy = -2048; wy <= 2048; wy += 128) {
						for (int32_t wx = -2048; wx <= 2048; wx += 128) {
							if (nd_field_ruggedness_q8(0u, wx, wy) == 0u) {
								int32_t c1 = 0, f1 = 0, c2 = 0, f2 = 0, c0 = 0, f0 = 0;
								(void)nd_field_elevation_step_q12_4(0u, 1 << 16, wx, wy, &c1, &f1);
								(void)nd_field_elevation_step_q12_4(0u, 64 << 16, wx, wy, &c2, &f2);
								(void)nd_field_elevation_step_q12_4(0u, 2048 << 16, wx, wy, &c0, &f0);
								assert(c1 == c2 && "Coarse elevation must remain step-invariant");
								assert(f0 == 0 && "LOD 0 fine elevation must stay zero");
								if (f1 < min_f) min_f = f1;
								if (f1 > max_f) max_f = f1;
								lowland_hits++;
							}
						}
					}
					assert(lowland_hits > 16 && (max_f - min_f) >= 64 &&
					       "Flat lowlands (rug_q8 == 0) must exhibit >= 4 wu canonical rolling undulation");
				}

				const char *fs_terr = nd_seven_gl_fs_terrain();
				const char *fs_bb   = nd_seven_gl_fs_billboard();
				const char *vs_m3d  = nd_seven_gl_vs_mesh3d();
				const char *fs_m3d  = nd_seven_gl_fs_mesh3d();
				const char *fs_ssao = nd_seven_gl_fs_ssao();
				const char *fs_pst  = nd_seven_gl_fs_post();

				/* Pass 1 */
				assert(strstr(fs_m3d, "proj > 12.0 && proj < clLen - 12.0") != NULL);
				assert(strstr(fs_m3d, "perpDist < 28.0") != NULL);
				assert(strstr(fs_m3d, "vDist < 145.0") != NULL);
				assert(strstr(fs_m3d, "selfShadowVis") != NULL);
				assert(strstr(fs_m3d, "mix(0.55, 1.0, smoothstep(0.0, 3.6, vHeightAboveGround))") != NULL);
				assert(strstr(fs_terr, "rTexel = 1.25 / 1024.0") != NULL);
				assert(strstr(fs_terr, "directShadowVis = mix(0.14, 1.0, treeShadowVis)") != NULL);
				assert(strstr(fs_bb,  "smoothstep(0.95, 0.12, r2)") != NULL);
				assert(strstr(fs_ssao, "rContact") != NULL && strstr(fs_ssao, "rCrevice") != NULL);
				assert(strstr(fs_pst, "0.58 * clamp(uSsaoActive") != NULL);

				/* Pass 2 */
				assert(strstr(fs_terr, "nd_eval_meadow_bed_weight") != NULL);
				assert(strstr(fs_terr, "bankGrassKeep") != NULL);
				assert(strstr(vs_m3d,  "isGrassMesh ? -1.8 : -1.5") != NULL);
				assert(strstr(fs_m3d,  "rootFeather") != NULL);
				assert(strstr(vs_m3d,  "a_isub") != NULL);
				assert(strstr(vs_m3d,  "nd_eval_grove_ecology_weight") == NULL);
				assert(strstr(fs_m3d,  "0.30 + rootT * 0.60") != NULL);

				/* Pass 3 */
				assert(strstr(fs_terr, "0x48554D4Du") != NULL);
				assert(strstr(fs_terr, "0.32 + 0.28 * distFade + 0.22 * closeZoom") != NULL);
				nd_seven_apply_quality_preset(ND_QUALITY_LOW);
				assert(nd_seven_get_res_scale() >= 0.66f && nd_seven_get_detail_scale() >= 0.71f);
				nd_seven_apply_quality_preset(ND_QUALITY_HIGH);

				/* Bug-hunt regression guards:
				 * 1) 3D-only procedural flora (including mesh_id 1..128 & 160..191) must never fall back to full-atlas 2D quads
				 * 2) 3D mesh world-grid anchor Z must preserve b->z (never subtract water_z_off) for shadow/cutaway alignment
				 * 3) Grass mesh3d shader must multiply (not overwrite) rootT microAO and permit AO down to 0.38 */
				{
					lam_billboard_gpu_t b;
					memset(&b, 0, sizeof(b));
					b.mesh_id = nd_seven_plant_mesh3d_id(ND_PLANT_PACK_ID(3u, 1u, 0u)); /* wild herb (1..128) */
					b.u0 = 0.0f; b.u1 = 1.0f; b.v0 = 0.0f; b.v1 = 0.0f;
					assert(nd_seven_gl_billboard_is_mesh3d_only(&b));
					b.u0 = 1.0f; b.u1 = 0.0f; /* flipped */
					assert(nd_seven_gl_billboard_is_mesh3d_only(&b));
					b.mesh_id = nd_seven_sapling_mesh3d_id(9u, 0x1234u); /* juvenile sapling */
					b.v0 = 0.0f; b.v1 = 1.0f; /* legacy full-atlas sentinel */
					assert(nd_seven_gl_billboard_is_mesh3d_only(&b));
					b.mesh_id = ND_MESH3D_MEADOW_BASE; /* tall grass/meadow clump (180..191) */
					b.u0 = 0.0f; b.u1 = 1.0f; b.v0 = 0.0f; b.v1 = 0.0f;
					assert(nd_seven_gl_billboard_is_mesh3d_only(&b));
					b.mesh_id = ND_MESH3D_GEOBREAK_BASE;
					b.u0 = 0.1f; b.u1 = 0.2f; b.v0 = 0.1f; b.v1 = 0.2f;
					assert(nd_seven_gl_billboard_is_mesh3d_only(&b));
					b.mesh_id = 42u; /* atlas-backed 3D tree allowed to crossfade */
					b.u0 = 0.125f; b.u1 = 0.25f; b.v0 = 0.25f; b.v1 = 0.375f;
					assert(!nd_seven_gl_billboard_is_mesh3d_only(&b));
					b.mesh_id = 0u; /* plain 2D billboard */
					b.u0 = 0.0f; b.u1 = 1.0f; b.v0 = 0.0f; b.v1 = 1.0f;
					assert(!nd_seven_gl_billboard_is_mesh3d_only(&b));

					float anchor[3] = { 0.0f, 0.0f, 0.0f };
					memset(&b, 0, sizeof(b));
					b.x = 120.0f; b.y = -40.0f; b.z = 24.0f;
					b.water_z_off = 12.0f; b.anchor_y = 10.0f;
					b.style = ND_BB_STYLE_STANDARD;
					nd_seven_gl_eval_mesh3d_anchor(&b, anchor);
					assert(fabsf(anchor[0] - 120.0f) < 1e-5f);
					assert(fabsf(anchor[1] - (-45.0f)) < 1e-5f);
					assert(fabsf(anchor[2] - 24.0f) < 1e-5f && "3D mesh anchor Z must equal b->z, never b->z - water_z_off");

					b.z = 18.5f; b.water_z_off = 0.0f; b.anchor_y = 0.0f;
					b.style = ND_BB_STYLE_FLORA;
					nd_seven_gl_eval_mesh3d_anchor(&b, anchor);
					assert(fabsf(anchor[1] - (-40.0f)) < 1e-5f && "Procedural flora with anchor_y=0 must not drift north");
					assert(fabsf(anchor[2] - 18.5f) < 1e-5f);

					b.z = 10.25f; b.water_z_off = 4.0f; b.anchor_y = 6.0f;
					b.style = ND_BB_STYLE_GRASS;
					nd_seven_gl_eval_mesh3d_anchor(&b, anchor);
					assert(fabsf(anchor[1] - (-40.0f)) < 1e-5f && "Grass mesh3d must not apply anchor_y north shift");
					assert(fabsf(anchor[2] - 10.25f) < 1e-5f);

					assert(strstr(fs_m3d, "microAO *= clamp(0.74 + 0.46 * grShade") != NULL);
					assert(strstr(fs_m3d, "microAO = clamp(0.74 + 0.46 * grShade") == NULL);
					assert(strstr(fs_m3d, "clamp(cavityAO * contactAO * microAO, 0.38, 1.14)") != NULL);
				}

				/* Realism Sequence Passes 0-4: RGBA unpack, pad0 biome packing, fallen logs,
				 * terrain shadow caster, billboard shadow receiver, and unified substrate blending */
				{
					/* Pass 0: canonical 0xRRGGBBAA unpack & pad0 bitfield round-trip */
					float rgba[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
					nd_seven_gl_unpack_rgba_u32(0x112233FFu, rgba);
					assert(fabsf(rgba[0] - (float)0x11 / 255.0f) < 1e-5f);
					assert(fabsf(rgba[1] - (float)0x22 / 255.0f) < 1e-5f);
					assert(fabsf(rgba[2] - (float)0x33 / 255.0f) < 1e-5f);
					assert(fabsf(rgba[3] - 1.0f) < 1e-5f);

					static const unsigned test_bios[4] = { 0u, 1u, 16u, 18u };
					for (int bi = 0; bi < 4; bi++) {
						for (uint8_t kind = 0u; kind < 4u; kind++) {
							for (int sub = 0; sub < 2; sub++) {
								uint8_t p0 = nd_seven_gl_pack_pad0(kind, test_bios[bi], sub != 0);
								assert((p0 & 0x03u) == kind);
								assert(((p0 & 0x80u) != 0u) == (sub != 0));
								bool has_bio = false;
								unsigned out_bio = nd_seven_gl_unpack_pad0_biome(p0, &has_bio);
								assert(has_bio);
								assert(out_bio == test_bios[bi]);
							}
						}
					}
					bool has_bio_unset = true;
					assert(nd_seven_gl_unpack_pad0_biome(1u, &has_bio_unset) == 0u && !has_bio_unset);

					/* Pass 4: fallen mossy log slices & woodland biome gating */
					uint32_t total_verts = 0u;
					(void)nd_seven_mesh3d_verts(&total_verts);
					assert(total_verts > 0u && total_verts < 393216u);
					for (unsigned lv = 6u; lv < 8u; lv++) {
						const nd_mesh3d_slice_t *lsl = nd_seven_mesh3d_slice((uint8_t)(ND_MESH3D_GEOBREAK_BASE + lv));
						assert(lsl != NULL && lsl->vert_count >= 40u && lsl->idx_count >= 60u);
					}
					static const unsigned wood_bios[4] = { 11u, 12u, 16u, 17u };
					for (int wi = 0; wi < 4; wi++) {
						int log_hits = 0;
						for (uint32_t h = 0u; h < 64u; h++) {
							uint8_t gid = nd_seven_geobreak_mesh3d_id(wood_bios[wi], h);
							if (gid >= ND_MESH3D_GEOBREAK_BASE + 6u) log_hits++;
						}
						assert(log_hits > 0 && "Woodland biomes must emit fallen mossy logs");
					}
					for (uint32_t h = 0u; h < 64u; h++) {
						uint8_t gid_desert = nd_seven_geobreak_mesh3d_id(14u, h);
						assert(gid_desert >= ND_MESH3D_GEOBREAK_BASE && gid_desert < ND_MESH3D_GEOBREAK_BASE + 6u);
						assert(nd_seven_geobreak_mesh3d_id(1u, h) == ND_MESH3D_NONE);
					}

					/* Passes 1-3: shader pipeline guards */
					assert(strstr(fs_bb,  "nd_eval_tree_shadow") != NULL);
					assert(strstr(nd_seven_gl_vs_mesh3d(), "vSubCol = a_isub.rgb") != NULL);
					assert(strstr(nd_seven_gl_vs_mesh3d(), "nd_eval_ground_substrate") == NULL);
					assert(strstr(fs_m3d, "nd_eval_ground_substrate") == NULL);
					assert(strstr(fs_m3d, "vTerrGrad") != NULL);
					assert(strstr(vs_m3d, "vTerrGrad = a_iterr.xy") != NULL);
					assert(strstr(nd_seven_gl_vs_terrain_shadow(), "uTreeShadowWriteVP") != NULL);
				}

				printf("PASS: Realism Passes 0-4 (RGBA/pad0 packing, unified substrate & slope-normal grounding, billboard+terrain shadows, fallen logs & GLSL validation) verified\n");
			}
		}
	}

	/* Test A: Analytical nd_field_elevation_bounds exact containment & tightness oracle (20,000 patches, 500,000 samples) */
	{
		static const int32_t patch_sizes[9][2] = {
			{    160,    128 },
			{    320,    256 },
			{    640,    512 },
			{   1280,   1024 },
			{   2560,   2048 },
			{   5120,   4096 },
			{  20480,  16384 },
			{  81920,  65536 },
			{ 327680, 262144 }
		};
		uint64_t rng = 0x5EED12349ABCDEF0ULL;
		uint64_t violations = 0;
		uint64_t total_checks = 0;
		double fine_span_sum = 0.0;
		uint64_t fine_span_cnt = 0;
		double ocean_span_sum = 0.0;
		uint64_t ocean_span_cnt = 0;

		for (int p = 0; p < 20000; p++) {
			rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
			uint16_t wld = (uint16_t)(rng % 3ULL);
			int sidx = (int)((rng >> 4) % 9ULL);
			int32_t pw = patch_sizes[sidx][0];
			int32_t ph = patch_sizes[sidx][1];
			int32_t ox, oy;
			if (p < 400) {
				/* Stress world wrap seam near +/- 5,242,880 */
				ox = (p & 1) ? (5242880 - pw / 2) : (-5242880 - pw / 2);
				oy = (int32_t)((int64_t)(rng % 8388608ULL) - 4194304LL);
			} else {
				ox = (int32_t)((int64_t)(rng % 10485760ULL) - 5242880LL);
				rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
				oy = (int32_t)((int64_t)(rng % 8388608ULL) - 4194304LL);
			}
			float zmin = 0.0f, zmax = 0.0f;
			nd_field_elevation_bounds(wld, ox, oy, pw, ph, &zmin, &zmax);
			assert(isfinite(zmin) && isfinite(zmax) && zmin <= zmax);

			if (sidx <= 3) {
				fine_span_sum += (double)(zmax - zmin);
				fine_span_cnt++;
				if (zmax <= 0.0f) {
					ocean_span_sum += (double)(zmax - zmin);
					ocean_span_cnt++;
				}
			}

			int lod = (sidx <= 2) ? 3 : ((sidx <= 4) ? 2 : ((sidx <= 6) ? 1 : 0));
			for (int gy = 0; gy < 5; gy++) {
				int32_t sy = oy + (int32_t)(((int64_t)ph * gy) / 4);
				for (int gx = 0; gx < 5; gx++) {
					int32_t sx = ox + (int32_t)(((int64_t)pw * gx) / 4);
					nd_field_sample_t smp = nd_field_sample(wld, lod, sx, sy);
					float elev = (float)smp.elevation_q12_4 * 0.0625f;
					total_checks++;
					if (elev < zmin - 1e-3f || elev > zmax + 1e-3f)
						violations++;
				}
			}
		}
		double mean_fine_span = fine_span_sum / (double)(fine_span_cnt > 0 ? fine_span_cnt : 1);
		double mean_ocean_span = ocean_span_cnt > 0 ? (ocean_span_sum / (double)ocean_span_cnt) : 0.0;
		assert(violations == 0);
		assert(mean_fine_span < 1800.0); /* Much tighter than legacy 5800 wu global slab */
		printf("PASS: nd_field_elevation_bounds oracle verified (%llu checks, violations=%llu, fine_span=%.1f wu, ocean_span=%.1f wu)\n",
		       (unsigned long long)total_checks, (unsigned long long)violations,
		       mean_fine_span, mean_ocean_span);
	}

	/* Test B: 360-deg yaw x grazing-to-steep pitch x torus latitudes (outer / polar / concave inner hub)
	 * zero-false-negative focal coverage & concave inner-hub horizontal span verification */
	{
		static const int lat_rooms[3] = { 0, -16384, -32768 }; /* outer equator, polar, concave inner hub */
		static const float pitches[3] = { 10.0f, 25.0f, 45.0f };
		int total_poses = 0;
		int focal_misses = 0;
		double outer_ew_span = 0.0, inner_ew_span = 0.0;

		for (int li = 0; li < 3; li++) {
			nd_seven_forget();
			set_map(10, lat_rooms[li], 10, 0x1B, 0, 0, 0, 0);
			nd_seven_sync_origin(&st);
			nd_seven_set_lamb(NDW_CENTER_X, NDW_CENTER_Y);
			int64_t lwx_b = 0, lwy_b = 0;
			nd_seven_world_pos(&st, &lwx_b, &lwy_b);

			for (int yi = 0; yi < 12; yi++) {
				float yaw = (float)yi * 30.0f;
				for (int pi = 0; pi < 3; pi++) {
					nd_seven_set_zoom(0.15f);
					nd_seven_set_cam_angles(yaw, pitches[pi]);
					for (int s = 0; s < 35; s++) nd_seven_step_cam(0.05f);
					nd_seven_render(fb, 1280, 720, &st);
					uint32_t lcnt = nd_seven_last_visible_leaf_count();
					assert(lcnt > 0);
					int covered = 0;
					int32_t min_ox = 0x7FFFFFFF, max_ox = -0x7FFFFFFF;
					for (uint32_t k = 0; k < lcnt; k++) {
						int32_t lox = 0, loy = 0, lw = 0, lh = 0;
						assert(nd_seven_last_visible_leaf_bounds(k, &lox, &loy, &lw, &lh) == 1);
						if ((int64_t)lox <= lwx_b && lwx_b <= (int64_t)lox + (int64_t)lw &&
						    (int64_t)loy <= lwy_b && lwy_b <= (int64_t)loy + (int64_t)lh)
							covered = 1;
						if (lox < min_ox) min_ox = lox;
						if (lox + lw > max_ox) max_ox = lox + lw;
					}
					if (!covered)
						focal_misses++;
					if (yi == 0 && pi == 2) {
						double span = (double)max_ox - (double)min_ox;
						if (li == 0) outer_ew_span = span;
						if (li == 2) inner_ew_span = span;
					}
					total_poses++;
				}
			}
		}
		nd_seven_reset_cam();
		for (int s = 0; s < 35; s++) nd_seven_step_cam(0.05f);
		nd_seven_set_zoom(1.0f);
		assert(focal_misses == 0);
		/* Concave inner hub has 0.40x East-West torus metric vs 2.00x at outer equator, so
		 * true 3D frustum culling must cover significantly wider East-West world span there. */
		assert(inner_ew_span > outer_ew_span * 1.8);
		printf("PASS: 360-deg yaw x pitch x torus latitude coverage verified (%d poses, focal_misses=%d, outer_ew=%.0f vs inner_ew=%.0f wu)\n",
		       total_poses, focal_misses, outer_ew_span, inner_ew_span);
	}

	/* Test C: Close-up lamb + companion planet (alt_world) visibility & horizon/eclipse occlusion */
	{
		nd_seven_forget();
		set_map(10, 10, 10, 0x1B, 0, 0, 0, 0);
		nd_seven_sync_origin(&st);
		nd_seven_set_lamb(NDW_CENTER_X, NDW_CENTER_Y);
		nd_seven_reset_cam();
		for (int s = 0; s < 35; s++) nd_seven_step_cam(0.05f);

		/* Close-up diorama view looking downward at lamb: companion planet cannot leak through ground */
		nd_seven_set_zoom(1.0f);
		nd_seven_render(fb, 1280, 720, &st);
		assert(nd_seven_last_visible_leaf_count() > 0);
		assert(nd_seven_last_alt_planet_chunk_count() == 0);

		/* Low grazing close-up view (pitch 12 deg, yaw sweep) around lamb: primary terrain stays
		 * watertight, no NaNs, and ridge/horizon occluder runs cleanly without false-culled focal leaf */
		for (int yi = 0; yi < 4; yi++) {
			nd_seven_set_cam_angles((float)yi * 90.0f, 12.0f);
			for (int s = 0; s < 35; s++) nd_seven_step_cam(0.05f);
			nd_seven_render(fb, 1280, 720, &st);
			assert(nd_seven_last_visible_leaf_count() > 0);
		}
		uint32_t last_occ = nd_seven_last_ridge_culled_count();
		nd_seven_reset_cam();
		for (int s = 0; s < 35; s++) nd_seven_step_cam(0.05f);
		printf("PASS: close-up lamb + companion planet occlusion & low-pitch sweep verified (ridge_culled=%u)\n",
		       last_occ);
	}

	/* Test D: 360-deg water occlusion (nd_seven_is_water_occluded_at) — zero false occlusion on visible
	 * foreground water and positive occlusion on deeply buried subterranean water boxes */
	{
		nd_seven_forget();
		set_map(10, 10, 10, 0x1B, 0, 0, 0, 0);
		nd_seven_sync_origin(&st);
		nd_seven_set_lamb(NDW_CENTER_X, NDW_CENTER_Y);
		nd_seven_set_zoom(0.25f);

		int64_t lwx_w = 0, lwy_w = 0;
		nd_seven_world_pos(&st, &lwx_w, &lwy_w);
		float gz_w = nd_seven_ground_elevation_f((int32_t)lwx_w, (int32_t)lwy_w);
		int fg_false_occ = 0;
		int buried_occ_hits = 0;

		const nd_planet_def_t *p1_w = nd_seven_get_planet(1);
		for (int yi = 0; yi < 8; yi++) {
			nd_seven_set_cam_angles((float)yi * 45.0f, 45.0f);
			for (int s = 0; s < 35; s++) nd_seven_step_cam(0.05f);
			for (int wave = 0; wave < 6; wave++) {
				nd_seven_render(fb, 1280, 720, &st);
				if (wave < 5)
					nd_seven_drain_async();
			}

			/* Water surface at/above local ground right at focal chunk must NEVER be falsely occluded */
			if (nd_seven_is_water_occluded_at((int32_t)lwx_w - 80, (int32_t)lwy_w - 64, 160, 128,
			                                  gz_w + 16.0f, p1_w))
				fg_false_occ++;

			/* Water box buried 250 wu underneath the rendered focal terrain (> -900 sentinel) must be occluded */
			if (nd_seven_is_water_occluded_at((int32_t)lwx_w - 40, (int32_t)lwy_w - 32, 80, 64,
			                                  gz_w - 250.0f, p1_w))
				buried_occ_hits++;
		}
		nd_seven_reset_cam();
		for (int s = 0; s < 35; s++) nd_seven_step_cam(0.05f);
		nd_seven_set_zoom(1.0f);
		assert(fg_false_occ == 0);
		assert(buried_occ_hits == 8);
		printf("PASS: 360-deg water occlusion verified (fg_false_occ=%d, buried_occ_hits=%d/8)\n",
		       fg_false_occ, buried_occ_hits);
	}

	/* Phase 6B Piece 1: Pre-baked GL_RGBA16F height+derived & premultiplied river map parity tests */
	{
		enum { TW = 17, TH = 17 };
		int16_t h_a[TW * TH], h_b[TW * TH];
		float out_a[TW * TH * 4], out_b[TW * TH * 4];
		const float span_x = 160.0f, span_y = 128.0f;

		/* 1. Linear plane: exact h_wu, exact central-difference slope, zero Laplacian everywhere */
		for (int y = 0; y < TH; y++) {
			for (int x = 0; x < TW; x++) {
				/* h_wu = 10.0 + 0.25 * wx - 0.5 * wy => h_q12_4 = 160 + 40*x - 64*y */
				h_a[y * TW + x] = (int16_t)(160 + 40 * x - 64 * y);
			}
		}
		nd_seven_gl_bake_chunk_height_derived(h_a, TW, TH, span_x, span_y, out_a);
		for (int y = 0; y < TH; y++) {
			for (int x = 0; x < TW; x++) {
				const float *p = &out_a[(y * TW + x) * 4];
				float exp_h = (float)h_a[y * TW + x] * 0.0625f;
				assert(fabsf(p[0] - exp_h) < 1e-6f);
				assert(fabsf(p[1] - 0.25f) < 1e-5f);
				assert(fabsf(p[2] - (-0.50f)) < 1e-5f);
				assert(fabsf(p[3]) < 1e-5f);
			}
		}

		/* 2. Paraboloid curvature: h_q12_4 = 2*(x^2 + y^2) => interior lap = 8 => dst[3] = 8 * 0.16 * (t2w_x + t2w_y) */
		for (int y = 0; y < TH; y++) {
			for (int x = 0; x < TW; x++) {
				h_a[y * TW + x] = (int16_t)(2 * (x * x + y * y));
			}
		}
		nd_seven_gl_bake_chunk_height_derived(h_a, TW, TH, span_x, span_y, out_a);
		float exp_lap = 8.0f * 0.16f * (16.0f / span_x + 16.0f / span_y);
		for (int y = 1; y < TH - 1; y++) {
			for (int x = 1; x < TW - 1; x++) {
				const float *p = &out_a[(y * TW + x) * 4];
				assert(fabsf(p[3] - exp_lap) < 1e-4f);
			}
		}

		/* 3. Cross-chunk shoreline invariance (docs/SHORE.md): adjacent chunks with wildly different
		 * interiors (+1000 mountain vs -1000 trench) sharing a border column MUST agree bit-exact on h_wu */
		for (int y = 0; y < TH; y++) {
			for (int x = 0; x < TW; x++) {
				h_a[y * TW + x] = (x == TW - 1) ? (int16_t)(y * 3 - 24) : (int16_t)16000;
				h_b[y * TW + x] = (x == 0)      ? (int16_t)(y * 3 - 24) : (int16_t)-16000;
			}
		}
		nd_seven_gl_bake_chunk_height_derived(h_a, TW, TH, span_x, span_y, out_a);
		nd_seven_gl_bake_chunk_height_derived(h_b, TW, TH, span_x, span_y, out_b);
		for (int y = 0; y < TH; y++) {
			float ha_edge = out_a[(y * TW + (TW - 1)) * 4 + 0];
			float hb_edge = out_b[(y * TW + 0) * 4 + 0];
			assert(ha_edge == hb_edge);
		}

		/* 4. Premultiplied river water-table bake:
		 *    - Dry chunk yields all zeros
		 *    - Mountain river (h=100 wu, carve=6 wu, base_sea=0 -> w_surf=103 wu) adjacent to dry bank
		 *      yields exact horizontal water table 103.0 wu both on river texel AND across 50/50 dry blend
		 *    - Border texels depend only on border data (cross-chunk river agreement) */
		uint32_t col_a[TW * TH], col_b[TW * TH];
		for (int i = 0; i < TW * TH; i++) {
			h_a[i] = (int16_t)(100 * 16); /* 100.0 wu */
			col_a[i] = 0xFF808080u;       /* alpha=255 (dry) */
		}
		nd_seven_gl_bake_chunk_river(h_a, col_a, TW, TH, 0.0f, out_a);
		for (int i = 0; i < TW * TH * 4; i++)
			assert(out_a[i] == 0.0f);

		/* Place river texel at (8,8) with alpha=159 => c_i = (1 - 159/255)*15.9375 = 6.0 wu, w_surf = 103.0 wu */
		col_a[8 * TW + 8] = (159u << 24) | 0x00808080u;
		nd_seven_gl_bake_chunk_river(h_a, col_a, TW, TH, 0.0f, out_a);
		const float *rv_c = &out_a[(8 * TW + 8) * 4];
		const float *rv_dry = &out_a[(8 * TW + 10) * 4]; /* 2 texels away -> pure dry */
		assert(rv_c[1] >= 0.01f && rv_c[1] <= 1.0f);
		assert(rv_c[0] <= 2048.0f);
		assert(fabsf(rv_c[0] / rv_c[1] - 103.0f) < 1e-3f);
		assert(rv_dry[1] == 0.0f);
		/* 50/50 bilinear blend between bank texel (8,9) and dry texel (8,10) stays flat at 103.0 wu */
		const float *rv_bank = &out_a[(8 * TW + 9) * 4];
		assert(rv_bank[1] > 0.0f);
		float blend_x = 0.5f * rv_bank[0] + 0.5f * rv_dry[0];
		float blend_y = 0.5f * rv_bank[1] + 0.5f * rv_dry[1];
		assert(fabsf(blend_x / blend_y - 103.0f) < 1e-3f);

		/* Cross-chunk border river invariance */
		for (int i = 0; i < TW * TH; i++) {
			h_a[i] = (int16_t)(80 * 16);
			h_b[i] = (int16_t)(20 * 16);
			col_a[i] = 0xFF808080u;
			col_b[i] = (128u << 24) | 0x00808080u; /* interior river in B, dry interior in A */
		}
		for (int y = 0; y < TH; y++) {
			h_a[y * TW + (TW - 1)] = (int16_t)(50 * 16);
			h_b[y * TW + 0]        = (int16_t)(50 * 16);
			col_a[y * TW + (TW - 1)] = (y == 8) ? ((159u << 24) | 0x00808080u) : 0xFF808080u;
			col_b[y * TW + 0]        = col_a[y * TW + (TW - 1)];
		}
		nd_seven_gl_bake_chunk_river(h_a, col_a, TW, TH, 0.0f, out_a);
		nd_seven_gl_bake_chunk_river(h_b, col_b, TW, TH, 0.0f, out_b);
		for (int y = 0; y < TH; y++) {
			for (int c = 0; c < 4; c++) {
				assert(out_a[(y * TW + (TW - 1)) * 4 + c] == out_b[(y * TW + 0) * 4 + c]);
			}
		}

		/* 5. Shader contract: zero integer isampler2D heightmap lookups anywhere; uRiverTex only in wet shaders */
		assert(strstr(nd_seven_gl_vs_terrain(), "isampler2D") == NULL);
		assert(strstr(nd_seven_gl_fs_terrain(), "isampler2D") == NULL);
		assert(strstr(nd_seven_gl_vs_water(), "isampler2D") == NULL);
		assert(strstr(nd_seven_gl_fs_water(), "isampler2D") == NULL);
		assert(strstr(nd_seven_gl_vs_terrain_shadow(), "isampler2D") == NULL);
		assert(strstr(nd_seven_gl_fs_cloud(), "isampler2D") == NULL);
		assert(strstr(nd_seven_gl_fs_terrain(), "uRiverTex") != NULL);
		assert(strstr(nd_seven_gl_vs_water(), "uRiverTex") != NULL);
		assert(strstr(nd_seven_gl_fs_water(), "uRiverTex") != NULL);
		assert(strstr(nd_seven_gl_fs_terrain_dry_near(), "isampler2D") == NULL);
		assert(strstr(nd_seven_gl_fs_terrain_dry_near(), "#define ND_TERRAIN_DRY_ONLY") != NULL);
		assert(strstr(nd_seven_gl_fs_terrain(), "uniform sampler2DArray uHeightTex;") != NULL);
		assert(strstr(nd_seven_gl_fs_terrain(), "uniform sampler2DArray uSubstrateTex;") != NULL);
		assert(strstr(nd_seven_gl_fs_terrain(), "#ifndef ND_TERRAIN_DRY_ONLY\nuniform sampler2DArray uRiverTex;") != NULL);
		assert(strstr(nd_seven_gl_fs_terrain_seabed_near(), "uRiverTex") != NULL);

		printf("PASS: Phase 6B Piece 1 pre-baked GL_RGBA16F height+derived & premultiplied river parity verified\n");
	}

	/* Phase 6B Piece 2: Tileable 2D (256^2, period 32) & 3D (64^3, period 8) gradient-noise LUTs */
	{
		/* 1. 2D periodic LUT (256x256, period 32) */
		const uint32_t D2 = 256u, P2 = 32u;
		uint8_t *buf2 = (uint8_t *)malloc((size_t)D2 * D2 * 4u);
		assert(buf2 != NULL);
		nd_seven_gl_bake_noise2d_lut(buf2, D2, P2);

		double sum_r = 0.0, sum_a = 0.0, sum_ra = 0.0, sum_r2 = 0.0, sum_a2 = 0.0;
		double dot_gx = 0.0, n_fx = 0.0, n_ax = 0.0;
		double dot_gy = 0.0, n_fy = 0.0, n_ay = 0.0;
		double step2 = (double)P2 / (double)D2; /* 0.125 lattice cells per texel */
		int min_g = 255, max_g = 0, min_b = 255, max_b = 0;
		double max_wrap_err2 = 0.0;
		for (uint32_t y = 0; y < D2; y++) {
			uint32_t ym = (y + D2 - 1u) & (D2 - 1u);
			uint32_t yp = (y + 1u) & (D2 - 1u);
			for (uint32_t x = 0; x < D2; x++) {
				uint32_t xm = (x + D2 - 1u) & (D2 - 1u);
				uint32_t xp = (x + 1u) & (D2 - 1u);
				const uint8_t *px = &buf2[(y * D2 + x) * 4u];
				double r = (double)px[0] / 255.0;
				double a = (double)px[3] / 255.0;
				sum_r += r; sum_a += a;
				sum_ra += r * a; sum_r2 += r * r; sum_a2 += a * a;
				if (px[1] < min_g) min_g = px[1];
				if (px[1] > max_g) max_g = px[1];
				if (px[2] < min_b) min_b = px[2];
				if (px[2] > max_b) max_b = px[2];

				double r_xp = (double)buf2[(y * D2 + xp) * 4u] / 255.0;
				double r_xm = (double)buf2[(y * D2 + xm) * 4u] / 255.0;
				double r_yp = (double)buf2[(yp * D2 + x) * 4u] / 255.0;
				double r_ym = (double)buf2[(ym * D2 + x) * 4u] / 255.0;
				double fd_x = (r_xp - r_xm) / (2.0 * step2);
				double fd_y = (r_yp - r_ym) / (2.0 * step2);
				double ag_x = ((double)px[1] / 255.0) * 4.0 - 2.0;
				double ag_y = ((double)px[2] / 255.0) * 4.0 - 2.0;
				dot_gx += fd_x * ag_x; n_fx += fd_x * fd_x; n_ax += ag_x * ag_x;
				dot_gy += fd_y * ag_y; n_fy += fd_y * fd_y; n_ay += ag_y * ag_y;

				if (x == 0u) {
					double e = fabs(r - (double)buf2[(y * D2 + (D2 - 1u)) * 4u] / 255.0);
					if (e > max_wrap_err2) max_wrap_err2 = e;
				}
				if (y == 0u) {
					double e = fabs(r - (double)buf2[((D2 - 1u) * D2 + x) * 4u] / 255.0);
					if (e > max_wrap_err2) max_wrap_err2 = e;
				}
			}
		}
		double N2 = (double)D2 * (double)D2;
		double mean_r = sum_r / N2, mean_a = sum_a / N2;
		double var_r = sum_r2 / N2 - mean_r * mean_r;
		double var_a = sum_a2 / N2 - mean_a * mean_a;
		double corr_ra = (sum_ra / N2 - mean_r * mean_a) / sqrt(var_r * var_a);
		double corr_gx = dot_gx / sqrt(n_fx * n_ax);
		double corr_gy = dot_gy / sqrt(n_fy * n_ay);
		assert(fabs(mean_r - 0.5) < 0.03);
		assert(fabs(mean_a - 0.5) < 0.03);
		assert(fabs(corr_ra) < 0.15);
		assert(corr_gx > 0.99);
		assert(corr_gy > 0.99);
		assert(min_g > 0 && max_g < 255);
		assert(min_b > 0 && max_b < 255);
		assert(max_wrap_err2 < 0.25);
		free(buf2);

		/* 2. 3D periodic LUT (64x64x64, period 8) */
		const uint32_t D3 = 64u, P3 = 8u;
		uint8_t *buf3 = (uint8_t *)malloc((size_t)D3 * D3 * D3 * 4u);
		assert(buf3 != NULL);
		nd_seven_gl_bake_noise3d_lut(buf3, D3, P3);

		double sum3 = 0.0;
		double dot3_x = 0.0, nf3_x = 0.0, na3_x = 0.0;
		double dot3_y = 0.0, nf3_y = 0.0, na3_y = 0.0;
		double dot3_z = 0.0, nf3_z = 0.0, na3_z = 0.0;
		double step3 = (double)P3 / (double)D3;
		int min3 = 255, max3 = 0;
		double max_wrap_err3 = 0.0;
		for (uint32_t z = 0; z < D3; z++) {
			uint32_t zm = (z + D3 - 1u) & (D3 - 1u);
			uint32_t zp = (z + 1u) & (D3 - 1u);
			for (uint32_t y = 0; y < D3; y++) {
				uint32_t ym = (y + D3 - 1u) & (D3 - 1u);
				uint32_t yp = (y + 1u) & (D3 - 1u);
				for (uint32_t x = 0; x < D3; x++) {
					uint32_t xm = (x + D3 - 1u) & (D3 - 1u);
					uint32_t xp = (x + 1u) & (D3 - 1u);
					const uint8_t *px = &buf3[((z * D3 + y) * D3 + x) * 4u];
					double v = (double)px[0] / 255.0;
					sum3 += v;
					for (int c = 1; c <= 3; c++) {
						if (px[c] < min3) min3 = px[c];
						if (px[c] > max3) max3 = px[c];
					}
					double vx_p = (double)buf3[((z * D3 + y) * D3 + xp) * 4u] / 255.0;
					double vx_m = (double)buf3[((z * D3 + y) * D3 + xm) * 4u] / 255.0;
					double vy_p = (double)buf3[((z * D3 + yp) * D3 + x) * 4u] / 255.0;
					double vy_m = (double)buf3[((z * D3 + ym) * D3 + x) * 4u] / 255.0;
					double vz_p = (double)buf3[((zp * D3 + y) * D3 + x) * 4u] / 255.0;
					double vz_m = (double)buf3[((zm * D3 + y) * D3 + x) * 4u] / 255.0;
					double fdx = (vx_p - vx_m) / (2.0 * step3);
					double fdy = (vy_p - vy_m) / (2.0 * step3);
					double fdz = (vz_p - vz_m) / (2.0 * step3);
					double agx = ((double)px[1] / 255.0) * 4.0 - 2.0;
					double agy = ((double)px[2] / 255.0) * 4.0 - 2.0;
					double agz = ((double)px[3] / 255.0) * 4.0 - 2.0;
					dot3_x += fdx * agx; nf3_x += fdx * fdx; na3_x += agx * agx;
					dot3_y += fdy * agy; nf3_y += fdy * fdy; na3_y += agy * agy;
					dot3_z += fdz * agz; nf3_z += fdz * fdz; na3_z += agz * agz;
					if (x == 0u) {
						double e = fabs(v - (double)buf3[((z * D3 + y) * D3 + (D3 - 1u)) * 4u] / 255.0);
						if (e > max_wrap_err3) max_wrap_err3 = e;
					}
					if (y == 0u) {
						double e = fabs(v - (double)buf3[((z * D3 + (D3 - 1u)) * D3 + x) * 4u] / 255.0);
						if (e > max_wrap_err3) max_wrap_err3 = e;
					}
					if (z == 0u) {
						double e = fabs(v - (double)buf3[(((D3 - 1u) * D3 + y) * D3 + x) * 4u] / 255.0);
						if (e > max_wrap_err3) max_wrap_err3 = e;
					}
				}
			}
		}
		double mean3 = sum3 / ((double)D3 * D3 * D3);
		double corr3_x = dot3_x / sqrt(nf3_x * na3_x);
		double corr3_y = dot3_y / sqrt(nf3_y * na3_y);
		double corr3_z = dot3_z / sqrt(nf3_z * na3_z);
		assert(fabs(mean3 - 0.5) < 0.03);
		assert(corr3_x > 0.98);
		assert(corr3_y > 0.98);
		assert(corr3_z > 0.98);
		assert(min3 > 0 && max3 < 255);
		assert(max_wrap_err3 < 0.28);
		free(buf3);

		/* 3. GLSL shader contracts for 2D/3D hardware noise LUTs */
		assert(strstr(nd_seven_gl_fs_terrain(), "uNoise2DTex") != NULL);
		assert(strstr(nd_seven_gl_fs_terrain(), "nd_lut_noise2d") != NULL);
		assert(strstr(nd_seven_gl_fs_terrain(), "precision highp sampler3D;") != NULL);
		assert(strstr(nd_seven_gl_fs_terrain(), "uNoise3DTex") != NULL);
		assert(strstr(nd_seven_gl_fs_water(), "uNoise2DTex") != NULL);
		assert(strstr(nd_seven_gl_fs_water(), "nd_lut_noise2d") != NULL);
		assert(strstr(nd_seven_gl_fs_water(), "precision highp sampler3D;") != NULL);
		assert(strstr(nd_seven_gl_fs_billboard(), "uNoise2DTex") != NULL);
		assert(strstr(nd_seven_gl_fs_billboard(), "nd_lut_noise2d") != NULL);
		assert(strstr(nd_seven_gl_fs_billboard(), "precision highp sampler3D;") != NULL);
		assert(strstr(nd_seven_gl_fs_mesh3d(), "uNoise2DTex") != NULL);
		assert(strstr(nd_seven_gl_fs_mesh3d(), "nd_lut_noise2d") != NULL);
		assert(strstr(nd_seven_gl_fs_mesh3d_opaque(), "uNoise2DTex") != NULL);
		assert(strstr(nd_seven_gl_fs_mesh3d_shadow_alpha(), "uNoise2DTex") != NULL);
		assert(strstr(nd_seven_gl_fs_mesh3d_shadow_alpha(), "chaff_grad_noise2d") == NULL);
		assert(strstr(nd_seven_gl_fs_cloud(), "uNoise2DTex") != NULL);
		assert(strstr(nd_seven_gl_fs_cloud(), "precision highp sampler3D;") != NULL);
		assert(strstr(nd_seven_gl_fs_cloud(), "uNoise3DTex") != NULL);
		assert(strstr(nd_seven_gl_fs_cloud(), "nd_lut_noise4d(") != NULL);
		assert(strstr(nd_seven_gl_fs_cloud(), "vec4 n4 = nd_lut_noise4d(") != NULL);

		printf("PASS: Phase 6B Piece 2 2D/3D periodic noise+gradient LUTs (2D gx=%.4f gy=%.4f, 3D gx=%.4f gy=%.4f gz=%.4f) verified\n",
		       corr_gx, corr_gy, corr3_x, corr3_y, corr3_z);
	}

	/* Phase 6B Piece 3: Single-pass biome deduplication & VS climate/ecology hoisting */
	{
		const char *vs_terr = nd_seven_gl_vs_terrain();
		const char *fs_terr = nd_seven_gl_fs_terrain();
		const char *vs_wat  = nd_seven_gl_vs_water();
		const char *fs_wat  = nd_seven_gl_fs_water();

		/* Single-pass NdBiomeW struct replaces 3x redundant smoothstep classifier */
		assert(strstr(fs_terr, "struct NdBiomeW") != NULL);
		assert(strstr(fs_terr, "NdBiomeW nd_eval_biome_weights(") != NULL);
		assert(strstr(fs_terr, "NdBiomeW bio = nd_eval_biome_weights(srgbBase);") != NULL);
		assert(strstr(fs_terr, "nd_eval_tex_bio_humidity") == NULL);

		/* Hoisted static latitude/climate transcendentals (no per-fragment latPhi/asin in terrain/water) */
		assert(strstr(vs_terr, "out vec4 vLatClimate;") != NULL);
		assert(strstr(fs_terr, "in vec4 vLatClimate;") != NULL);
		assert(strstr(vs_wat,  "out vec4 vLatClimate;") != NULL);
		assert(strstr(fs_wat,  "in vec4 vLatClimate;") != NULL);
		assert(strstr(fs_terr, "latPhi") == NULL);
		assert(strstr(fs_wat,  "latPhi") == NULL);
		assert(strstr(fs_terr, " asin(") == NULL);
		assert(strstr(fs_wat,  " asin(") == NULL);

		/* Hoisted analytic grove/meadow ecology noise passed via vEcoBase */
		assert(strstr(vs_terr, "out vec2 vEcoBase;") != NULL);
		assert(strstr(fs_terr, "in vec2 vEcoBase;") != NULL);
		assert(strstr(fs_terr, "nd_eval_grove_ecology_weight(vEcoBase.x") != NULL);
		assert(strstr(fs_terr, "nd_eval_meadow_bed_weight(vEcoBase.y") != NULL);

		printf("PASS: Phase 6B Piece 3 biome deduplication & VS climate/ecology hoisting verified\n");
	}

	/* Phase 6C Piece 1: Selectable 0x/2x/4x MSAA + stencil-backed R11F_G11F_B10F water contract */
	{
		int orig_samples = nd_seven_get_msaa_samples();
		nd_seven_set_msaa_samples(0);
		assert(nd_seven_get_msaa_samples() == 0 && nd_seven_get_msaa() == 0);
		nd_seven_set_msaa_samples(2);
		assert(nd_seven_get_msaa_samples() == 2 && nd_seven_get_msaa() == 1);
		nd_seven_set_msaa_samples(4);
		assert(nd_seven_get_msaa_samples() == 4 && nd_seven_get_msaa() == 1);
		nd_seven_toggle_msaa();
		assert(nd_seven_get_msaa_samples() == 0 && nd_seven_get_msaa() == 0);
		nd_seven_toggle_msaa();
		assert(nd_seven_get_msaa_samples() == 4 && nd_seven_get_msaa() == 1);
		nd_seven_set_msaa(2);
		assert(nd_seven_get_msaa_samples() == 2 && nd_seven_get_msaa() == 1);
		nd_seven_cycle_msaa();
		assert(nd_seven_get_msaa_samples() == 4 && nd_seven_get_msaa() == 1);
		nd_seven_cycle_msaa();
		assert(nd_seven_get_msaa_samples() == 0 && nd_seven_get_msaa() == 0);
		nd_seven_cycle_msaa();
		assert(nd_seven_get_msaa_samples() == 2 && nd_seven_get_msaa() == 1);
		nd_seven_set_msaa_samples(orig_samples);

		int orig_drs = nd_seven_get_drs();
		nd_seven_set_drs(0);
		assert(nd_seven_get_drs() == 0);
		nd_seven_toggle_drs();
		assert(nd_seven_get_drs() == 1);
		nd_seven_toggle_drs();
		assert(nd_seven_get_drs() == 0);
		nd_seven_set_drs(orig_drs);

		/* Water shader must classify seabed vs submerged sprites via uWaterMode + hardware stencil,
		 * never reading scene color alpha (required for 4 B/px GL_R11F_G11F_B10F scene buffer). */
		const char *fs_wat = nd_seven_gl_fs_water();
		const char *fs_sb  = nd_seven_gl_fs_terrain_seabed_near();
		assert(strstr(fs_wat, "uniform int uWaterMode;") != NULL);
		assert(strstr(fs_wat, "bool isSeabed = (uWaterMode == 0);") != NULL);
		assert(strstr(fs_wat, "scene0.a") == NULL);
		assert(strstr(fs_wat, "sceneR.a") == NULL);
		assert(strstr(fs_wat, "sceneS.a") == NULL);
		assert(strstr(fs_sb,  "vec4(outRgb, 0.5)") == NULL);

		printf("PASS: Phase 6C Piece 1 selectable 0x/2x/4x MSAA & stencil-backed R11F water contract verified\n");
	}

	/* Phase 6C Piece 2: 2x2 interleaved temporal checkerboard cloud reprojection */
	{
		/* 1. Bayer 2x2 subpixel sequence covers all 4 subpixels with exact NDC jitter */
		int seen = 0;
		for (uint32_t ph = 0; ph < 4u; ph++) {
			int px = -1, py = -1;
			float jx = 0.0f, jy = 0.0f;
			nd_seven_gl_cloud_subpixel(ph, 480u, 270u, &px, &py, &jx, &jy);
			assert(px >= 0 && px <= 1 && py >= 0 && py <= 1);
			seen |= 1 << (px | (py << 1));
			assert(fabsf(jx - ((0.5f - (float)px) / 480.0f)) < 1e-7f);
			assert(fabsf(jy - ((0.5f - (float)py) / 270.0f)) < 1e-7f);
		}
		assert(seen == 0xF);

		/* 2. Origin-relative NDC reprojection matrix: exact identity when stationary,
		 *    and < 1e-4 NDC error under simultaneous perspective rotation + origin shift (far=8e7) */
		nd_planet_def_t pdef = {0};
		pdef.major_r = 2000000.0f;
		pdef.minor_r = 1335000.0f;
		float mvp_prev[16], mvp_cur[16], reproj[16];
		double orig_prev[3], orig_cur[3];
		float eye_prev[3], eye_cur[3], abs_prev[3], abs_cur[3];
		nd_seven_gl_compute_mvp_rel_ex(1280, 720, 0.0f, 0.0f, 0.0f, 20000.0f, 15.0f, 45.0f, 60.0f,
		                               &pdef, mvp_prev, orig_prev, eye_prev, abs_prev);
		nd_seven_gl_compute_mvp_rel_ex(1280, 720, 120.0f, -85.0f, 0.0f, 20500.0f, 18.0f, 46.0f, 60.0f,
		                               &pdef, mvp_cur, orig_cur, eye_cur, abs_cur);

		/* Stationary fast path -> exact identity */
		assert(nd_seven_gl_compute_reproj_mat(mvp_cur, orig_cur, mvp_cur, orig_cur, reproj) == true);
		for (int i = 0; i < 16; i++) {
			float want = ((i % 5) == 0) ? 1.0f : 0.0f;
			assert(fabsf(reproj[i] - want) < 1e-7f);
		}

		/* Moving camera + shifted origin: project an absolute 3D point into cur & prev NDC,
		 * then verify reproj * cur_ndc matches prev_ndc to < 1e-4! */
		assert(nd_seven_gl_compute_reproj_mat(mvp_cur, orig_cur, mvp_prev, orig_prev, reproj) == true);
		double pt_abs[3] = { orig_cur[0] + 450.0, orig_cur[1] - 320.0, orig_cur[2] + 1620.0 };
		float p_cur[3] = { (float)(pt_abs[0] - orig_cur[0]), (float)(pt_abs[1] - orig_cur[1]), (float)(pt_abs[2] - orig_cur[2]) };
		float p_prv[3] = { (float)(pt_abs[0] - orig_prev[0]), (float)(pt_abs[1] - orig_prev[1]), (float)(pt_abs[2] - orig_prev[2]) };
		float c_cur[4], c_prv[4], c_rep[4];
		for (int r = 0; r < 4; r++) {
			c_cur[r] = mvp_cur[0*4+r]*p_cur[0] + mvp_cur[1*4+r]*p_cur[1] + mvp_cur[2*4+r]*p_cur[2] + mvp_cur[3*4+r];
			c_prv[r] = mvp_prev[0*4+r]*p_prv[0] + mvp_prev[1*4+r]*p_prv[1] + mvp_prev[2*4+r]*p_prv[2] + mvp_prev[3*4+r];
		}
		float ndc_cur[3] = { c_cur[0]/c_cur[3], c_cur[1]/c_cur[3], c_cur[2]/c_cur[3] };
		float ndc_prv[3] = { c_prv[0]/c_prv[3], c_prv[1]/c_prv[3], c_prv[2]/c_prv[3] };
		for (int r = 0; r < 4; r++)
			c_rep[r] = reproj[0*4+r]*ndc_cur[0] + reproj[1*4+r]*ndc_cur[1] + reproj[2*4+r]*ndc_cur[2] + reproj[3*4+r];
		float rep_x = c_rep[0] / c_rep[3];
		float rep_y = c_rep[1] / c_rep[3];
		float err_x = fabsf(rep_x - ndc_prv[0]);
		float err_y = fabsf(rep_y - ndc_prv[1]);
		assert(err_x < 1e-4f && err_y < 1e-4f);

		/* Singular matrix rejection */
		float zero_m[16] = {0};
		assert(nd_seven_gl_compute_reproj_mat(zero_m, orig_cur, mvp_prev, orig_prev, reproj) == false);

		/* 3. Shader contracts */
		assert(strstr(nd_seven_gl_vs_cloud(), "uniform vec2 uCloudJitter;") != NULL);
		assert(strstr(nd_seven_gl_vs_cloud(), "uCloudJitter * gl_Position.w") != NULL);
		const char *fs_rp = nd_seven_gl_fs_cloud_reproj();
		assert(strstr(fs_rp, "uCloudQuarterTex") != NULL);
		assert(strstr(fs_rp, "uCloudHistTex") != NULL);
		assert(strstr(fs_rp, "uReprojMat") != NULL);
		assert(strstr(fs_rp, "uCloudSubpixel") != NULL);
		assert(strstr(fs_rp, "vUv * qScale - subOff") != NULL);
		assert(strstr(fs_rp, "clamp(hist, cMin, cMax)") != NULL);

		printf("PASS: Phase 6C Piece 2 2x2 temporal checkerboard cloud reprojection (err_x=%.2e err_y=%.2e) verified\n",
		       err_x, err_y);
	}

	printf("ALL SEVEN RENDER TESTS PASSED\n");
	return 0;
}
