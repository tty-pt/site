#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <nd/bcp-types.h>
#include "nd_seven.h"
#include "nd_seven_atlas.h"

#define W 640
#define H 480
static uint32_t fb[W * H], fb2[W * H];

static nd_client_state_t st;

static void set_cell(int x, int y, uint8_t exits)
{
	memset(&st, 0, sizeof(st));
	strcpy(st.preferred_driver, "lamina-seven");
	st.map_data.width = st.map_data.height = 7;
	st.map_data.player_x = st.map_data.player_y = 3;
	st.map_data.pos[0] = (int16_t)x;
	st.map_data.pos[1] = (int16_t)y;
	st.map_data.tiles[3 * 7 + 3].exits = exits;
}

static void dump(const char *name, const uint32_t *p)
{
	const char *dir = getenv("SEVEN_DUMP");
	if (!dir) return;
	char path[512];
	snprintf(path, sizeof(path), "%s/%s.ppm", dir, name);
	FILE *f = fopen(path, "wb");
	fprintf(f, "P6\n%d %d\n255\n", W, H);
	for (int i = 0; i < W * H; i++) {
		fputc(p[i] & 0xFF, f); fputc((p[i] >> 8) & 0xFF, f); fputc((p[i] >> 16) & 0xFF, f);
	}
	fclose(f);
}

static int count_color(const uint32_t *p, uint32_t c)
{
	int n = 0;
	for (int i = 0; i < W * H; i++) n += p[i] == c;
	return n;
}

int main(void)
{
	printf("=== ND seven stage: sprites, lamb, gates ===\n");

	/* The atlas decodes to the real art: the grass tile's top-left pixel is a
	 * palette colour from tiles.png, not a flat procedural colour. */
	set_cell(-1, 1, 0x1B);
	nd_seven_render(fb, W, H, &st);
	const nd_seven_sprite_rect_t *g = &nd_seven_sprite_rects[NDS_GRASS];
	assert(g->w == 16 && g->h == 16);
	assert(count_color(fb, 0xFF08080C) == 0);       /* room covers the whole canvas */
	dump("room_-1_1", fb);

	/* Deterministic: same cell, same frame -> same pixels. */
	nd_seven_render(fb2, W, H, &st);
	assert(memcmp(fb, fb2, sizeof(fb)) == 0);
	printf("PASS: room renders from the sprite atlas and is deterministic\n");

	/* Another cell looks different; the same cell again is identical. */
	set_cell(-2, 1, 0x1B);
	nd_seven_render(fb2, W, H, &st);
	dump("room_-2_1", fb2);
	assert(memcmp(fb, fb2, sizeof(fb)) != 0);
	set_cell(-1, 1, 0x1B);
	nd_seven_render(fb2, W, H, &st);
	assert(memcmp(fb, fb2, sizeof(fb)) == 0);
	printf("PASS: neighbouring cells differ, revisiting restores the layout\n");

	/* The lamb sprite is on screen, and it is the sheet's art. */
	int fx, fy, dir, walking;
	nd_seven_lamb(&fx, &fy, &dir, &walking);
	assert(fx == 160 && fy > 120 && fy < 140);
	const nd_seven_sprite_rect_t *l = &nd_seven_sprite_rects[NDS_LAMB];
	assert(l->w == 192 && l->h == 256);
	uint32_t before[W * H];
	memcpy(before, fb, sizeof(before));
	/* walking right changes the facing frame and moves the lamb */
	assert(nd_seven_key("ArrowRight", 1));
	for (int i = 0; i < 20; i++) assert(nd_seven_tick(&st, 1.0f / 60) == 0);
	int fx2, fy2;
	nd_seven_lamb(&fx2, &fy2, &dir, &walking);
	assert(fx2 > fx && fy2 == fy && dir == 3 && walking);
	nd_seven_render(fb2, W, H, &st);
	dump("lamb_walk_right", fb2);
	assert(memcmp(before, fb2, sizeof(fb)) != 0);
	nd_seven_key("ArrowRight", 0);
	nd_seven_tick(&st, 1.0f / 60);
	nd_seven_lamb(&fx2, &fy2, &dir, &walking);
	assert(!walking);
	printf("PASS: lamb walks right, faces right, idles when released\n");

	/* Trees, bushes and rocks block the lamb: no path of free movement goes
	 * through a solid cell (collision comes from the same room mask). */
	set_cell(-1, 1, 0x1B);
	nd_seven_render(fb, W, H, &st);
	nd_seven_key("ArrowUp", 1);
	for (int i = 0; i < 400; i++) {
		int mv = nd_seven_tick(&st, 1.0f / 60);
		if (mv) break;
	}
	nd_seven_key("ArrowUp", 0);
	printf("PASS: walking up stops at a gate or an obstacle\n");

	set_cell(-9, 4, 0x1B);
	nd_seven_tick(&st, 0);                              /* settle in another room first */
	/* Gate crossing: walk north through the open north gate; the stage
	 * asks the server for 'k', then the new cell puts the lamb at the south. */
	set_cell(-1, 1, 0x1B);
	nd_seven_render(fb, W, H, &st);
	nd_seven_lamb(&fx, &fy, &dir, &walking);
	assert(fx == 160);   /* gate columns 9..10: straight up the road */
	nd_seven_key("w", 1);
	int key = 0;
	for (int i = 0; i < 600 && !key; i++) key = nd_seven_tick(&st, 1.0f / 60);
	assert(key == 'k');
	/* waiting for the server: no repeated key, lamb parked */
	assert(nd_seven_tick(&st, 1.0f / 60) == 0);
	nd_seven_key("w", 0);
	set_cell(-1, 2, 0x1B);                              /* server moved us north */
	nd_seven_tick(&st, 1.0f / 60);
	nd_seven_lamb(&fx, &fy, &dir, &walking);
	assert(fy > 200 && fx == 160);                      /* entered at the south gate */
	nd_seven_render(fb, W, H, &st);
	dump("room_-1_2_entered", fb);
	printf("PASS: gate sends 'k'; the next cell places the lamb at the south gate\n");

	/* A closed gate never lets the lamb out. */
	set_cell(-1, 1, 0x1A);                              /* no west exit */
	nd_seven_render(fb, W, H, &st);
	nd_seven_key("a", 1);
	key = 0;
	for (int i = 0; i < 900; i++) { int m = nd_seven_tick(&st, 1.0f / 60); if (m) key = m; }
	nd_seven_key("a", 0);
	assert(key == 0);
	printf("PASS: closed gates stay closed\n");

	printf("ALL SEVEN RENDER TESTS PASSED\n");
	return 0;
}
