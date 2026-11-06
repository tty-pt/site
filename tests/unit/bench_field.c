#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>

#include <nd/field.h>
#include <nd/terrain.h>
#include <nd/seven-world.h>
#include "nd_seven_ground.h"
#include "nd_lamina.h"

static double now_sec(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

int main(void)
{
	printf("=== Benchmarking NeverDark Field & Chunk Subsystem ===\n");

	/* 1. Benchmark lod-3 field chunk build (160x128 samples) */
	const int CHUNK_ITERS = 100;
	static int16_t h_buf[160 * 128];
	static uint8_t bio_buf[160 * 128];
	static uint8_t bio2_buf[160 * 128];
	static uint8_t blend_buf[160 * 128];
	static uint8_t depth_buf[160 * 128];
	nd_field_chunk_t fc = {
		.world = 1,
		.lod = 3,
		.w = 160,
		.h = 128,
		.ox = 0,
		.oy = 0,
		.height = h_buf,
		.bio = bio_buf,
		.bio2 = bio2_buf,
		.blend = blend_buf,
		.depth = depth_buf
	};

	double t0 = now_sec();
	for (int i = 0; i < CHUNK_ITERS; i++) {
		fc.ox = (int32_t)(i % 10) * 160;
		fc.oy = (int32_t)(i / 10) * 128;
		nd_field_fill(&fc);
	}
	double t1 = now_sec();
	double chunk_ms = ((t1 - t0) * 1000.0) / CHUNK_ITERS;
	printf("BENCHMARK: lod-3 field chunk fill (160x128 samples): %.3f ms/chunk (%.1f chunks/sec)\n",
	       chunk_ms, 1000.0 / chunk_ms);
	fflush(stdout);

	/* 2. Benchmark Seven ground tile render (320x256 pixels) */
	static uint32_t fb[320 * 256];
	nd_client_state_t st;
	memset(&st, 0, sizeof(st));
	st.map_data.width = 7;
	st.map_data.height = 7;
	st.map_data.player_x = 3;
	st.map_data.player_y = 3;
	int16_t pos[4] = { 0, 0, 0, 1 };
	ndw_tile_t own = { .biome = 10, .flags = 0 };

	nd_seven_ground_cache_forget();
	const int SEVEN_ITERS = 50;
	double t2 = now_sec();
	for (int i = 0; i < SEVEN_ITERS; i++) {
		pos[0] = (int16_t)i;
		nd_seven_ground_tile_render(fb, 320, 256, &st, 0, 0, pos, &own, 0, 0);
	}
	double t3 = now_sec();
	double seven_chunk_ms = ((t3 - t2) * 1000.0) / SEVEN_ITERS;
	printf("BENCHMARK: Seven room ground chunk build + render: %.3f ms/chunk (%.1f chunks/sec)\n",
	       seven_chunk_ms, 1000.0 / seven_chunk_ms);
	fflush(stdout);

	/* 3. Benchmark Lamina minimap redraw (224x224 pixels) */
	static uint32_t map_fb[224 * 224];
	for (int i = 0; i < 49; i++) {
		st.map_data.tiles[i].bio_idx = (i % 2 == 0) ? 10 : 0;
		st.map_data.tiles[i].exits = 0x1B;
	}
	const int MAP_ITERS = 200;
	double t4 = now_sec();
	for (int i = 0; i < MAP_ITERS; i++) {
		st.map_data.pos[0] = (int16_t)i;
		nd_lamina_render_map_themed(map_fb, 224, 224, &st.map_data, "lamina", 43200, 0);
	}
	double t5 = now_sec();
	double map_ms = ((t5 - t4) * 1000.0) / MAP_ITERS;
	printf("BENCHMARK: Lamina minimap redraw (224x224): %.3f ms/redraw (%.1f redraws/sec)\n",
	       map_ms, 1000.0 / map_ms);
	fflush(stdout);

	/* Assert targets:
	 * - field chunk build < 50.0 ms
	 * - minimap redraw < 50.0 ms (> 20 fps full cold re-renders)
	 */
	assert(chunk_ms < 50.0);
	assert(map_ms < 50.0);

	printf("PASS: All field and chunk performance benchmarks meet real-time latency budgets!\n");
	return 0;
}
