/* Region gating for client map/chunk work (quest 1xPS5i / SQ1).
 *
 * A region that does not enable a canvas map (ascii-bud, ascii, none, or
 * nothing at all) must not build any terrain chunks: neither on the tick nor
 * when a server frame arrives. A lamina region builds chunks, but never inside
 * the websocket handler, at most ND_CHUNK_BUILDS_PER_TICK per tick, and frees
 * them when the region stops enabling the canvas. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "nd/bcp.h"
#include "nd_ui.h"
#include "nd_ui_internal.h"
#include "nd_seven_ground.h"

int respond_html(int fd, const char *html)
{
	(void)fd;
	(void)html;
	return 0;
}

void wasm_tick(float dt, int width, int height);
void wasm_ws_on_channel(uint8_t channel, const uint8_t *buf, size_t len);
int nd_map_canvas_enabled(const nd_client_state_t *st);

static void fill_wilderness(nd_client_state_t *st)
{
	nd_map_data_t *m = &st->map_data;
	memset(m, 0, sizeof(*m));
	m->width = 7;
	m->height = 7;
	m->player_x = 3;
	m->player_y = 3;
	m->room_ref = 77;
	m->pos[0] = 40;
	m->pos[1] = -12;
	m->pos[2] = 0;
	m->pos[3] = 0;
	for (int i = 0; i < 49; i++) {
		m->tiles[i].bio_idx = (uint8_t)(i % 3 ? 10 : 14);
		m->tiles[i].bg = 2;
	}
	m->tiles[24].flags |= ND_MAP_FLAG_PLAYER;
	st->room.ref = 77;
}

static size_t view_frame(uint8_t *frame)
{
	size_t n = 0;
	const char *row = "\033[42m   \033[0m\n";
	frame[n++] = ND_BCP_MAGIC_0;
	frame[n++] = ND_BCP_MAGIC_1;
	frame[n++] = ND_BCP_VIEW_BUFFER;
	memcpy(frame + n, row, strlen(row));
	n += strlen(row);
	return n;
}

static void set_drivers(const char *stage, const char *map)
{
	snprintf(client_state.preferred_driver, sizeof(client_state.preferred_driver), "%s", stage);
	snprintf(client_state.preferred_map_driver, sizeof(client_state.preferred_map_driver), "%s", map);
	fill_wilderness(&client_state);
}

static void pump(int ticks, int frames)
{
	static uint8_t frame[64];
	size_t n = view_frame(frame);
	for (int i = 0; i < ticks; i++) {
		if (i < frames)
			wasm_ws_on_channel(1, frame, n);
		wasm_tick(0.016f, 0, 0);
	}
}

int main(void)
{
	printf("=== Region gating for map/chunk work ===\n");

	/* The canvas predicate matches what render_room_map puts on the page. */
	{
		nd_client_state_t s;
		memset(&s, 0, sizeof(s));
		assert(!nd_map_canvas_enabled(&s));
		strcpy(s.preferred_map_driver, "ascii-bud");
		assert(!nd_map_canvas_enabled(&s));
		strcpy(s.preferred_map_driver, "ascii");
		assert(!nd_map_canvas_enabled(&s));
		strcpy(s.preferred_map_driver, "none");
		assert(!nd_map_canvas_enabled(&s));
		strcpy(s.preferred_map_driver, "lamina");
		assert(nd_map_canvas_enabled(&s));
		strcpy(s.preferred_map_driver, "classic");
		assert(nd_map_canvas_enabled(&s));
		printf("PASS: nd_map_canvas_enabled agrees with the page markup\n");
	}

	const char *off[] = { "ascii-bud", "ascii", "none", "" };
	for (unsigned k = 0; k < sizeof(off) / sizeof(off[0]); k++) {
		nd_seven_ground_cache_forget();
		set_drivers("tty", off[k]);
		pump(50, 10);
		if (nd_seven_chunk_alloc_count() != 0) {
			fprintf(stderr, "FAIL: map driver '%s' built %d chunks\n",
			        off[k], nd_seven_chunk_alloc_count());
			return 1;
		}
	}
	printf("PASS: ascii-bud/ascii/none/unset regions build no chunks (50 ticks, 10 frames)\n");

	/* Seven stage: frames alone never build (the websocket handler only marks the
	 * map dirty), ticks build at most ND_CHUNK_BUILDS_PER_TICK each. */
	nd_seven_ground_cache_forget();
	set_drivers("lamina-seven", "lamina");
	{
		static uint8_t frame[64];
		size_t n = view_frame(frame);
		for (int i = 0; i < 10; i++)
			wasm_ws_on_channel(1, frame, n);
		assert(nd_seven_chunk_alloc_count() == 0);
		printf("PASS: websocket frames do not build chunks\n");
	}
	int prev = 0;
	for (int i = 0; i < 200; i++) {
		wasm_tick(0.016f, 640, 480);
		int now = nd_seven_chunk_alloc_count();
		assert(now - prev <= ND_CHUNK_BUILDS_PER_TICK);
		prev = now;
	}
	assert(nd_seven_chunk_alloc_count() > 0);
	assert(nd_seven_chunk_count() <= 49);
	printf("PASS: lamina-seven region builds %d chunks, <= %d per tick, bounded by the window\n",
	       nd_seven_chunk_alloc_count(), ND_CHUNK_BUILDS_PER_TICK);

	/* Switching the region to ascii-bud frees every chunk. */
	set_drivers("tty", "ascii-bud");
	pump(3, 1);
	assert(nd_seven_chunk_count() == 0);
	printf("PASS: disabling the canvas frees all chunks\n");

	printf("ALL REGION GATING TESTS PASSED\n");
	return 0;
}
