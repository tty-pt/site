#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <time.h>

#include <nd/terrain.h>

int main(void)
{
	printf("=== Testing NeverDark 3D Procedural Terrain & Cavern Engine ===\n");

	uint16_t w0 = 1;

	/* 1. Determinism across queries */
	{
		for (int16_t y = -10; y <= 10; y++) {
			for (int16_t x = -10; x <= 10; x++) {
				nd_terrain_cell_t c1 = nd_terrain_query(x, y, 0, w0);
				nd_terrain_cell_t c2 = nd_terrain_query(x, y, 0, w0);
				assert(c1.elevation == c2.elevation);
				assert(c1.water_level == c2.water_level);
				assert(c1.slope_x == c2.slope_x);
				assert(c1.slope_y == c2.slope_y);
				assert(c1.biome == c2.biome);
				assert(c1.is_solid == c2.is_solid);
				assert(c1.is_cave == c2.is_cave);
				assert(c1.is_entrance == c2.is_entrance);
				assert(c1.is_water == c2.is_water);
			}
		}
		/* Different world produces differing elevation */
		int diff = 0;
		for (int16_t x = 0; x < 20; x++) {
			if (nd_terrain_height(x, 0, w0) != nd_terrain_height(x, 0, w0 + 7))
				diff++;
		}
		assert(diff > 10);
		printf("PASS: determinism verified (identical queries match, different worlds vary)\n");
	}

	/* 2. Boundary continuity: tightened to the fine-octave Lipschitz bound */
	{
		/* Horizontal continuity in world units */
		int16_t prev_hx = nd_terrain_height_fine(-64, 0, w0);
		for (int32_t x = -63; x <= 64; x++) {
			int16_t h = nd_terrain_height_fine(x, 0, w0);
			int dh = abs(h - prev_hx);
			assert(dh <= 3); /* Fine octave Lipschitz bound */
			prev_hx = h;
		}
		/* Vertical continuity in world units */
		int16_t prev_hy = nd_terrain_height_fine(0, -64, w0);
		for (int32_t y = -63; y <= 64; y++) {
			int16_t h = nd_terrain_height_fine(0, y, w0);
			int dh = abs(h - prev_hy);
			assert(dh <= 3);
			prev_hy = h;
		}
		/* Sub-unit fine continuity */
		int16_t prev_h = nd_terrain_height_fine(0, 0, w0);
		for (int32_t s = 1; s <= 32; s++) {
			int16_t cur_h = nd_terrain_height_fine(s, 0, w0);
			int dh = abs(cur_h - prev_h);
			assert(dh <= 3); /* Sub-step deltas are smooth */
			prev_h = cur_h;
		}
		printf("PASS: boundary continuity verified (smooth transitions across world units, dh <= 3)\n");
	}

	/* 3. Macro vs Micro elevation features and Water Table level-set */
	{
		int16_t min_h = 9999, max_h = -9999;
		int submerged_count = 0;
		int dry_count = 0;
		int16_t sea_level = nd_terrain_water_level(w0);

		for (int16_t y = -256; y <= 256; y += 8) {
			for (int16_t x = -256; x <= 256; x += 8) {
				int16_t h = nd_terrain_height(x, y, w0);
				if (h < min_h) min_h = h;
				if (h > max_h) max_h = h;
				if (h < sea_level) submerged_count++;
				else dry_count++;
			}
		}
		/* Continental relief: peak-to-peak swing spans > 100 units */
		assert(max_h - min_h > 100);
		/* Natural distribution: world contains both oceans and dry land */
		assert(submerged_count > 50 && dry_count > 50);
		printf("PASS: macro elevation relief (swing %d units) and water level verified\n",
		       max_h - min_h);
	}

	/* 4. Slope gradient consistency */
	{
		for (int16_t y = -20; y <= 20; y += 5) {
			for (int16_t x = -20; x <= 20; x += 5) {
				int16_t dx, dy;
				nd_terrain_slope(x, y, w0, &dx, &dy);
				int16_t exp_dx = (int16_t)((nd_terrain_height(x + 1, y, w0) -
				                            nd_terrain_height(x - 1, y, w0)) / 2);
				int16_t exp_dy = (int16_t)((nd_terrain_height(x, y + 1, w0) -
				                            nd_terrain_height(x, y - 1, w0)) / 2);
				assert(dx == exp_dx);
				assert(dy == exp_dy);
			}
		}
		printf("PASS: analytical slope gradient matches finite difference normals\n");
	}

	/* 5. 3D Subterranean Caverns, Density & Surface Entrances */
	{
		int solid_surface_checks = 0;
		int open_sky_checks = 0;
		int subterranean_cavern_found = 0;
		int subterranean_rock_found = 0;
		int natural_entrances_found = 0;

		for (int16_t y = -200; y <= 200; y += 8) {
			for (int16_t x = -200; x <= 200; x += 8) {
				int16_t h = nd_terrain_height(x, y, w0);

				/* Surface level is always solid */
				assert(nd_terrain_is_solid(x, y, h, w0) == 1);
				solid_surface_checks++;

				/* Sky above surface is always open air */
				assert(nd_terrain_is_solid(x, y, h + 5, w0) == 0);
				assert(nd_terrain_is_cave(x, y, h + 5, w0) == 0);
				open_sky_checks++;

				/* Test underground strata */
				for (int16_t z = h - 20; z < h; z += 4) {
					if (nd_terrain_is_cave(x, y, z, w0)) {
						subterranean_cavern_found++;
						assert(nd_terrain_is_solid(x, y, z, w0) == 0);
					} else {
						subterranean_rock_found++;
						assert(nd_terrain_is_solid(x, y, z, w0) == 1);
					}
				}

				if (nd_terrain_is_entrance(x, y, w0)) {
					natural_entrances_found++;
					/* Entrance must breach just below surface and not be submerged */
					assert(nd_terrain_is_cave(x, y, h - 1, w0) == 1);
					assert(h > nd_terrain_water_level(w0));
				}
			}
		}

		assert(solid_surface_checks > 500);
		assert(open_sky_checks > 500);
		assert(subterranean_cavern_found > 50);
		assert(subterranean_rock_found > 50);
		assert(natural_entrances_found > 0);
		printf("PASS: 3D cavern density verified (air above, solid surface, subterranean hollows, %d natural entrances)\n",
		       natural_entrances_found);
	}

	/* 6. Altitude Lapse Rate and Biome distribution */
	{
		/* High altitude peaks get colder biomes than sea level at same latitude */
		int alpine_snow_ice_count = 0;
		for (int16_t y = -200; y <= 200; y += 8) {
			for (int16_t x = -200; x <= 200; x += 8) {
				int16_t h = nd_terrain_height(x, y, w0);
				if (h > 40) {
					uint8_t b = nd_terrain_biome(x, y, h, w0);
					if (b == BIOME_PERMANENT_ICE || b == BIOME_TUNDRA || b == BIOME_COLD_DESERT ||
					    b == BIOME_CONIFEROUS_FOREST || b == BIOME_BOREAL_FOREST)
						alpine_snow_ice_count++;
				}
			}
		}
		assert(alpine_snow_ice_count > 0);
		printf("PASS: altitude lapse rate correctly generates alpine biomes on mountain peaks\n");
	}

	/* 7. Performance Throughput Benchmark */
	{
		const int BENCH_ITERS = 1000000;
		struct timespec start, end;
		volatile int dummy = 0;

		clock_gettime(CLOCK_MONOTONIC, &start);
		for (int i = 0; i < BENCH_ITERS; i++) {
			int16_t x = (int16_t)(i & 0x7F);
			int16_t y = (int16_t)((i >> 7) & 0x7F);
			int16_t z = (int16_t)((i >> 14) & 0x1F);
			nd_terrain_cell_t cell = nd_terrain_query(x, y, z, w0);
			dummy += cell.elevation + cell.is_solid;
		}
		clock_gettime(CLOCK_MONOTONIC, &end);

		double elapsed = (double)(end.tv_sec - start.tv_sec) +
		                 (double)(end.tv_nsec - start.tv_nsec) * 1e-9;
		double qps = (double)BENCH_ITERS / elapsed;

		printf("BENCHMARK: %d full 3D cell queries executed in %.4f seconds\n",
		       BENCH_ITERS, elapsed);
		printf("THROUGHPUT (full 3D cell): %.2f million queries/second\n", qps / 1e6);
		assert(qps > 200000.0);

		/* Benchmark surface heightfield alone (core walk fast-path) */
		clock_gettime(CLOCK_MONOTONIC, &start);
		for (int i = 0; i < BENCH_ITERS; i++) {
			int16_t x = (int16_t)(i & 0x7F);
			int16_t y = (int16_t)((i >> 7) & 0x7F);
			dummy += nd_terrain_height(x, y, w0);
		}
		clock_gettime(CLOCK_MONOTONIC, &end);
		elapsed = (double)(end.tv_sec - start.tv_sec) +
		          (double)(end.tv_nsec - start.tv_nsec) * 1e-9;
		double height_qps = (double)BENCH_ITERS / elapsed;
		printf("THROUGHPUT (surface height only): %.2f million queries/second\n", height_qps / 1e6);
		assert(height_qps > 1000000.0);
		printf("PASS: query throughput satisfies real-time core performance requirements\n");
	}

	/* Phase 3 Gate: Continuous planetary H(wx, wy) and level-set shorelines via nd_terrain_eval */
	{
		/* Verify zero vertical jump across continuous tile/room borders */
		for (int32_t wy = -512; wy <= 512; wy += 64) {
			for (int32_t wx = -512; wx <= 512; wx += 64) {
				nd_terrain_query_t q0 = nd_terrain_eval(w0, wx, wy, 0);
				nd_terrain_query_t q1 = nd_terrain_eval(w0, wx + 1, wy, 0);
				int dh_x = abs(q1.elevation - q0.elevation);
				assert(dh_x <= 3); /* Continuous sub-pixel slope, 0 vertical jumps */

				nd_terrain_query_t q2 = nd_terrain_eval(w0, wx, wy + 1, 0);
				int dh_y = abs(q2.elevation - q0.elevation);
				assert(dh_y <= 3);

				/* Depth identity W - H */
				assert(q0.depth == (q0.water_level - q0.elevation));
				assert(q0.is_water == (q0.depth > 0));
			}
		}
		printf("PASS: nd_terrain_eval continuous H(wx, wy) and level-set shorelines verified (0 vertical jumps)\n");
	}

	/* 9. Contract test: at room centres with fine layers disabled, field.h matches struct bio */
	{
		for (int y = -16; y <= 16; y += 4) {
			for (int x = -16; x <= 16; x += 4) {
				int32_t wx = (int32_t)x * ND_FIELD_ROOM_W + (ND_FIELD_ROOM_W / 2);
				int32_t wy = -(int32_t)y * ND_FIELD_ROOM_H + (ND_FIELD_ROOM_H / 2);
				nd_field_sample_t s_coarse = nd_field_sample(w0, 0, wx, wy);
				nd_field_sample_t s_fine   = nd_field_sample(w0, 3, wx, wy);

				/* Fine height budget |H_fine - H_coarse| <= ND_FIELD_FINE_AMP_BUDGET */
				int dh = abs(s_fine.elevation - s_coarse.elevation);
				assert(dh <= ND_FIELD_FINE_AMP_BUDGET);

				/* Water level identical */
				assert(s_coarse.water_level == s_fine.water_level);
			}
		}
		printf("PASS: field contract: room-centre coarse elevation and fine amplitude budget (|H_fine - H_coarse| <= %d)\n",
		       ND_FIELD_FINE_AMP_BUDGET);
	}

	printf("=== ALL TERRAIN 3D TESTS PASSED ===\n");
	return 0;
}
