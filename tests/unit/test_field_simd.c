#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <nd/field.h>

static uint32_t s_rng = 0xD1B54A35u;
static uint32_t next_u32(void)
{
	uint32_t x = s_rng;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	s_rng = x;
	return x;
}

static void assert_sample_eq(const nd_field_sample_t *a, const nd_field_sample_t *b,
                             const char *ctx, uint16_t world, int32_t step_q16,
                             int32_t wx, int32_t wy)
{
	if (a->elevation != b->elevation ||
	    a->elevation_q12_4 != b->elevation_q12_4 ||
	    a->coarse_elev != b->coarse_elev ||
	    a->fine_elev != b->fine_elev ||
	    a->water_level != b->water_level ||
	    a->depth != b->depth ||
	    a->is_water != b->is_water ||
	    a->temp != b->temp ||
	    a->climate_temp != b->climate_temp ||
	    a->rain != b->rain ||
	    a->bio != b->bio ||
	    a->bio2 != b->bio2 ||
	    a->blend != b->blend ||
	    a->river_carve_q4 != b->river_carve_q4) {
		fprintf(stderr,
		        "MISMATCH [%s] world=%u step=%d wx=%d wy=%d\n"
		        "  scalar: elev=%d q12_4=%d coarse=%d fine=%d wl=%d depth=%d w=%u temp=%d ctemp=%d rain=%u bio=%u/%u blend=%u rv=%u\n"
		        "  simd4x: elev=%d q12_4=%d coarse=%d fine=%d wl=%d depth=%d w=%u temp=%d ctemp=%d rain=%u bio=%u/%u blend=%u rv=%u\n",
		        ctx, world, step_q16, wx, wy,
		        a->elevation, a->elevation_q12_4, a->coarse_elev, a->fine_elev,
		        a->water_level, a->depth, a->is_water, a->temp, a->climate_temp,
		        a->rain, a->bio, a->bio2, a->blend, a->river_carve_q4,
		        b->elevation, b->elevation_q12_4, b->coarse_elev, b->fine_elev,
		        b->water_level, b->depth, b->is_water, b->temp, b->climate_temp,
		        b->rain, b->bio, b->bio2, b->blend, b->river_carve_q4);
		abort();
	}
}

int main(void)
{
	printf("=== Testing nd/field.h 4-Wide SIMD Bit-Exactness ===\n");

	/* 1. 10,000 random coordinates across multiple worlds, steps, negative coords, and torus seams */
	const int32_t steps[] = {
		0,
		16 << 16,
		128 << 16,
		512 << 16,
		2048 << 16,
		8192 << 16
	};
	int water_hits = 0, river_hits = 0, high_hits = 0, seam_hits = 0;

	for (int iter = 0; iter < 2500; iter++) {
		uint16_t world = (uint16_t)(iter % 5);
		int32_t step_q16 = steps[iter % (int)(sizeof(steps) / sizeof(steps[0]))];
		int32_t wy;
		int32_t wx[4];

		if (iter < 200) {
			/* Torus seam band: rx/ry near 32512..33023 (world units ~5,201,920..5,283,680) */
			int32_t base_rx = 32500 + (int32_t)(next_u32() % 540u);
			int32_t base_ry = 32500 + (int32_t)(next_u32() % 540u);
			wy = (64 - base_ry * 128) + (int32_t)(next_u32() % 128u) - 64;
			for (int k = 0; k < 4; k++) {
				wx[k] = (base_rx + k) * 160 + (int32_t)(next_u32() % 160u);
			}
			seam_hits += 4;
		} else if (iter < 1000) {
			/* Coherent row stride (chunk vertices) */
			wy = (int32_t)(next_u32() % 400000u) - 200000;
			int32_t ox = (int32_t)(next_u32() % 400000u) - 200000;
			int32_t dx = 1 + (int32_t)(next_u32() % 512u);
			for (int k = 0; k < 4; k++)
				wx[k] = ox + k * dx;
		} else {
			/* Fully random scattered X on same row (including negative & wrap extremes) */
			wy = (int32_t)next_u32() >> 9;
			for (int k = 0; k < 4; k++)
				wx[k] = (int32_t)next_u32() >> 9;
		}

		nd_field_sample_t got[4];
		nd_field_sample_4x(world, step_q16, wx, wy, got);

		for (int k = 0; k < 4; k++) {
			nd_field_sample_t ref = nd_field_sample_step(world, step_q16, wx[k], wy);
			assert_sample_eq(&ref, &got[k], "nd_field_sample_4x", world, step_q16, wx[k], wy);
			if (ref.is_water) water_hits++;
			if (ref.river_carve_q4 > 0) river_hits++;
			if (ref.elevation > 400) high_hits++;
		}
	}
	assert(water_hits > 100);
	assert(seam_hits == 800);
	printf("PASS: nd_field_sample_4x bit-exact across 10,000 coords (water=%d river=%d high=%d seam=%d)\n",
	       water_hits, river_hits, high_hits, seam_hits);

	/* 2. Row stepper bit-exactness across near, mid, wide, and coarse_direct spans */
	const int32_t spans[] = { 160, 640, 2560, 10240, 163840 };
	for (int s = 0; s < (int)(sizeof(spans) / sizeof(spans[0])); s++) {
		int32_t span = spans[s];
		uint32_t count = (s & 1) ? 49u : 35u;
		int32_t gdim = (int32_t)(count - 1u);
		int32_t ox = -12345 + s * 7777;
		int32_t wy = 54321 - s * 9999;
		int32_t step_q16 = (s == 4) ? (4096 << 16) : 0;
		nd_field_sample_t row[64];
		nd_field_sample_row_step((uint16_t)(s % 3), step_q16, wy, ox, span, count, row);
		for (uint32_t c = 0; c < count; c++) {
			int32_t wx = ox + (int32_t)(((int64_t)c * span) / gdim);
			nd_field_sample_t ref = nd_field_sample_step((uint16_t)(s % 3), step_q16, wx, wy);
			assert_sample_eq(&ref, &row[c], "nd_field_sample_row_step",
			                 (uint16_t)(s % 3), step_q16, wx, wy);
		}
	}
	printf("PASS: nd_field_sample_row_step bit-exact across 5 spans (35 & 49 cols)\n");

	/* 3. Direct primitive parity: ruggedness_4x, fine_4x (with mixed pre-computed rug), coarse_quad */
	for (int iter = 0; iter < 500; iter++) {
		uint16_t world = (uint16_t)(iter % 4);
		int32_t wx[4], wy[4], rug_in[4], fine_out[4];
		uint16_t rug_out[4];
		for (int k = 0; k < 4; k++) {
			wx[k] = (int32_t)next_u32() >> 10;
			wy[k] = (int32_t)next_u32() >> 10;
			rug_in[k] = ((iter + k) & 1) ? (int32_t)nd_field_ruggedness_q8(world, wx[k], wy[k]) : -1;
		}
		nd_field_ruggedness_q8_4x(world, wx, wy, rug_out);
		nd_field_elevation_fine_step_q12_4_4x(world, 0, wx, wy, rug_in, fine_out);
		for (int k = 0; k < 4; k++) {
			uint16_t rref = nd_field_ruggedness_q8(world, wx[k], wy[k]);
			assert(rug_out[k] == rref);
			assert(rug_in[k] == (int32_t)rref);
			int32_t rtmp = -1;
			int32_t fref = nd_field_elevation_fine_step_q12_4(world, 0, wx[k], wy[k], &rtmp);
			assert(fine_out[k] == fref);
		}
		int16_t rx = (int16_t)(next_u32() & 0xFFFFu);
		int16_t ry = (int16_t)(next_u32() & 0xFFFFu);
		int32_t q[4];
		nd_field_room_hyp_coarse_quad(world, rx, ry, q);
		assert(q[0] == nd_field_room_hyp_coarse(world, rx, ry));
		assert(q[1] == nd_field_room_hyp_coarse(world, (int16_t)(rx + 1), ry));
		assert(q[2] == nd_field_room_hyp_coarse(world, rx, (int16_t)(ry + 1)));
		assert(q[3] == nd_field_room_hyp_coarse(world, (int16_t)(rx + 1), (int16_t)(ry + 1)));
	}
	printf("PASS: ruggedness_4x, fine_4x (mixed rug cache), and coarse_quad bit-exact (2,000 samples)\n");

	/* 4. Conservative nd_field_elevation_bounds stress test across small..macro patches */
	const int32_t patch_sizes[] = { 160, 640, 2560, 10240, 32768 };
	int bounds_ocean_tight = 0;
	int bounds_mountain_hits = 0;
	for (int iter = 0; iter < 2000; iter++) {
		uint16_t world = (uint16_t)(iter % 2);
		int32_t sz = patch_sizes[iter % (int)(sizeof(patch_sizes) / sizeof(patch_sizes[0]))];
		int32_t ox = (int32_t)(next_u32() % 6000000u) - 3000000;
		int32_t oy = (int32_t)(next_u32() % 6000000u) - 3000000;
		float z_min = 0.0f, z_max = 0.0f;
		nd_field_elevation_bounds(world, ox, oy, sz, sz, &z_min, &z_max);
		assert(z_min <= z_max);
		if (z_max < 150.0f)
			bounds_ocean_tight++;

		for (int sy = 0; sy <= 2; sy++) {
			for (int sx = 0; sx <= 2; sx++) {
				int32_t wx = ox + (sz * sx) / 2;
				int32_t wy = oy + (sz * sy) / 2;
				nd_field_sample_t sp = nd_field_sample_step(world, 0, wx, wy);
				float z = (float)sp.elevation_q12_4 * 0.0625f;
				if (z < z_min - 0.5f || z > z_max + 0.5f) {
					fprintf(stderr,
					        "BOUNDS VIOLATION world=%u ox=%d oy=%d sz=%d wx=%d wy=%d z=%.2f bounds=[%.2f, %.2f]\n",
					        world, ox, oy, sz, wx, wy, z, z_min, z_max);
					abort();
				}
				if (z > 600.0f)
					bounds_mountain_hits++;
			}
		}
	}
	assert(bounds_ocean_tight > 100);
	assert(bounds_mountain_hits > 20);
	printf("PASS: nd_field_elevation_bounds conservative across 2,000 patches (18,000 pts, tight_ocean=%d, mtn_pts=%d)\n",
	       bounds_ocean_tight, bounds_mountain_hits);

	/* 5. Earth-like planetary mountain distribution, slopes & calm World 0 spawn */
	int land_cnt = 0, plains_cnt = 0, mtn_cnt = 0, alpine_cnt = 0, towering_cnt = 0;
	int max_slope_dz = 0, max_elev = 0;
	for (int iter = 0; iter < 4096; iter++) {
		int16_t rx = (int16_t)(next_u32() & 0xFFFFu);
		int16_t ry = (int16_t)(next_u32() & 0xFFFFu);
		int32_t h = nd_field_room_hyp_coarse(0, rx, ry);
		if (h <= 0)
			continue;
		land_cnt++;
		if (h > max_elev) max_elev = h;
		if (h < 100) plains_cnt++;
		if (h >= 500) mtn_cnt++;
		if (h >= 1000) alpine_cnt++;
		if (h >= 3000) towering_cnt++;
		if (h >= 300) {
			int32_t hx = nd_field_room_hyp_coarse(0, (int16_t)(rx + 1), ry);
			int32_t hy = nd_field_room_hyp_coarse(0, rx, (int16_t)(ry + 1));
			int dx = h > hx ? (h - hx) : (hx - h);
			int dy = h > hy ? (h - hy) : (hy - h);
			if (dx > max_slope_dz) max_slope_dz = dx;
			if (dy > max_slope_dz) max_slope_dz = dy;
		}
	}
	assert(land_cnt > 500);
	assert(plains_cnt * 100 >= land_cnt * 35 && plains_cnt * 100 <= land_cnt * 70);
	assert(mtn_cnt * 100 >= land_cnt * 8);
	assert(alpine_cnt * 100 >= land_cnt * 2);
	assert(towering_cnt >= 5 && max_elev >= 45000 && max_elev <= 81000);
	assert(max_slope_dz >= 160); /* >= 45 deg alpine flanks over 160 wu room span */

	/* World 0 spawn valley [-24..24] keeps zero tectonic mountain spires on top of spawn,
	 * while distant alpine ranges loom prominently (>= 10 deg) on Spawn's horizon. */
	uint32_t w0_seed = 0x48454947u;
	for (int ry = -24; ry <= 24; ry += 4) {
		for (int rx = -24; rx <= 24; rx += 4) {
			int32_t h = nd_field_room_hyp_coarse(0, (int16_t)rx, (int16_t)ry);
			assert(nd_field_orogeny_wu(w0_seed, 0, rx, ry, h) == 0);
		}
	}
	{
		int32_t h0 = nd_field_room_hyp_coarse(0, 0, 0);
		double max_hor_deg = 0.0;
		for (int dy = -128; dy <= 128; dy += 4) {
			for (int dx = -128; dx <= 128; dx += 4) {
				if (dx == 0 && dy == 0) continue;
				double d = sqrt((double)dx * 160.0 * (double)dx * 160.0 +
				                (double)dy * 128.0 * (double)dy * 128.0);
				if (d < 400.0 || d > 25000.0) continue;
				int32_t h = nd_field_room_hyp_coarse(0, (int16_t)dx, (int16_t)dy);
				if (h <= h0) continue;
				double ang = atan2((double)(h - h0), d) * 180.0 / 3.14159265358979323846;
				if (ang > max_hor_deg) max_hor_deg = ang;
			}
		}
		assert(max_hor_deg >= 10.0);
		printf("PASS: Earth-like orogeny (land=%d plains=%.1f%% mtn>=500=%.1f%% alpine>=1000=%.1f%% peak>=3000=%d max=%d max_dz=%d spawn_horizon=%.1fdeg)\n",
		       land_cnt,
		       100.0 * (double)plains_cnt / (double)land_cnt,
		       100.0 * (double)mtn_cnt / (double)land_cnt,
		       100.0 * (double)alpine_cnt / (double)land_cnt,
		       towering_cnt, max_elev, max_slope_dz, max_hor_deg);
	}

	/* 6. Phase 0: 1:1 physical metric scale (45 wu/m), int32_t Q27.4 elevation headroom,
	 *    and scalable planet_bits (16..23) periodic continuity + epoch cache invalidation. */
	{
		assert(fabsf(ND_FIELD_WU_PER_METER - 45.0f) < 1e-6f);
		assert(fabsf(ND_FIELD_LAMB_BODY_WU / ND_FIELD_LAMB_HEIGHT_M - 45.0f) < 1e-6f);

		nd_planet_scale_t ps16 = nd_planet_scale_for_bits(16, 160, 128);
		assert(ps16.planet_bits == 16);
		assert(ps16.rooms_per_axis == 65536u);
		assert(ps16.world_w_wu == 10485760LL);
		assert(ps16.world_h_wu == 8388608LL);
		assert(ps16.areole_max_levels == 17);
		assert(ps16.max_elev_wu == 81000); /* 1800 m * 45 wu/m */

		nd_planet_scale_t ps18 = nd_planet_scale_for_bits(18, 160, 128);
		assert(ps18.rooms_per_axis == 262144u);
		assert(ps18.world_w_wu == 41943040LL);
		assert(ps18.world_h_wu == 33554432LL);
		assert(ps18.areole_max_levels == 19);
		assert(ps18.max_elev_wu == 202500); /* 4500 m * 45 wu/m */

		nd_planet_scale_t ps20 = nd_planet_scale_for_bits(20, 160, 128);
		assert(ps20.rooms_per_axis == 1048576u);
		assert(ps20.world_w_wu == 167772160LL);
		assert(ps20.world_h_wu == 134217728LL);
		assert(ps20.areole_max_levels == 21);
		assert(ps20.max_elev_wu == 270000); /* 6000 m * 45 wu/m */

		nd_planet_scale_t ps22 = nd_planet_scale_for_bits(22, 160, 128);
		assert(ps22.rooms_per_axis == 4194304u);
		assert(ps22.areole_max_levels == 23);
		assert(ps22.max_elev_wu == 398250); /* 8850 m * 45 wu/m (Everest) */

		nd_planet_scale_t ps23 = nd_planet_scale_for_bits(23, 160, 128);
		assert(ps23.rooms_per_axis == 8388608u);
		assert(ps23.world_w_wu == 1342177280LL);
		assert(ps23.world_h_wu == 1073741824LL);
		assert(ps23.areole_max_levels == 24);
		assert(ps23.max_elev_wu == 398250);

		/* Verify int32_t Q27.4 elevation headroom at Everest altitude (400,000 wu) */
		int32_t everest_wu = 400000;
		int32_t everest_q4 = everest_wu << 4;
		assert(nd_field_elevation_to_wu(everest_q4) == everest_wu);
		assert(nd_field_elevation_to_block(everest_q4) == 32767);
		assert(nd_field_elevation_to_block(-(400000 << 4)) == -32768);

		/* Verify exact periodic torus wrap of nd_field_room_hyp_coarse at b = 18 and b = 20 */
		int32_t h16_ref = nd_field_room_hyp_coarse(0, 123, -456);
		uint32_t epoch0 = nd_field_get_planet_epoch();

		const uint8_t test_bits[] = { 18u, 20u };
		for (size_t bi = 0; bi < sizeof(test_bits) / sizeof(test_bits[0]); bi++) {
			uint8_t b = test_bits[bi];
			int32_t span = (int32_t)(1u << b);
			nd_field_set_planet_bits(b);
			assert(nd_field_get_planet_bits() == b);
			assert(nd_field_get_planet_epoch() != epoch0);

			int32_t test_rx[] = { 0, 17, -1234, (span >> 1) - 1, -(span >> 1), (span >> 1) - 128 };
			int32_t test_ry[] = { 0, -42, 7777, -(span >> 1) + 5, (span >> 1) - 3, (span >> 1) - 64 };
			for (int k = 0; k < 6; k++) {
				int32_t rx = test_rx[k];
				int32_t ry = test_ry[k];
				int32_t h_base = nd_field_room_hyp_coarse(0, rx, ry);
				int32_t h_wx   = nd_field_room_hyp_coarse(0, rx + span, ry);
				int32_t h_wy   = nd_field_room_hyp_coarse(0, rx, ry + span);
				int32_t h_wxy  = nd_field_room_hyp_coarse(0, rx - span, ry - span);
				assert(h_base == h_wx);
				assert(h_base == h_wy);
				assert(h_base == h_wxy);
			}
		}

		/* Restore default b = 16 and verify bit-exact recovery via epoch cache invalidation */
		nd_field_set_planet_bits(16);
		assert(nd_field_get_planet_bits() == 16);
		assert(nd_field_room_hyp_coarse(0, 123, -456) == h16_ref);

		printf("PASS: Phase 0 metric scale (45 wu/m), Q27.4 headroom (400k wu), and periodic wrap (b=16,18,20,22,23)\n");
	}

	/* 7. Phase 2: Geological Province Synthesis & Metric Hypsometry (10 Landform Archetypes) */
	{
		assert(sizeof(nd_geology_t) == 8);

		int prov_counts[ND_GEO_PROVINCE_COUNT] = {0};
		int prov_core_cnt[ND_GEO_PROVINCE_COUNT] = {0};
		int64_t prov_elev_sum[ND_GEO_PROVINCE_COUNT] = {0};
		int32_t prov_elev_max[ND_GEO_PROVINCE_COUNT] = {0};
		int prov_land_cnt[ND_GEO_PROVINCE_COUNT] = {0};
		int64_t prov_flat_sum[ND_GEO_PROVINCE_COUNT] = {0};
		int64_t prov_strata_sum[ND_GEO_PROVINCE_COUNT] = {0};

		for (int gy = 0; gy < 64; gy++) {
			for (int gx = 0; gx < 64; gx++) {
				int32_t rx = -32768 + gx * 1024 + 128;
				int32_t ry = -32768 + gy * 1024 + 128;
				nd_geology_t g = nd_field_geology(0, rx, ry);
				assert(g.province < ND_GEO_PROVINCE_COUNT);
				assert(g.secondary < ND_GEO_PROVINCE_COUNT);
				prov_counts[g.province]++;
				if (g.blend_q8 < 64) {
					prov_core_cnt[g.province]++;
					prov_flat_sum[g.province] += (int64_t)g.basin_flatness;
					prov_strata_sum[g.province] += (int64_t)g.strata_hardness;
				}

				int32_t h = nd_field_room_hyp_coarse(0, rx, ry);
				if (h > 6) {
					prov_land_cnt[g.province]++;
					prov_elev_sum[g.province] += h;
					if (h > prov_elev_max[g.province])
						prov_elev_max[g.province] = h;
				}
			}
		}

		/* All 10 geological archetypes must occur naturally across the planet */
		for (int p = 0; p < ND_GEO_PROVINCE_COUNT; p++) {
			assert(prov_counts[p] >= 150);
			assert(prov_core_cnt[p] >= 20);
			assert(prov_land_cnt[p] >= 20);
		}

		/* Distinct geomorphic signatures on province interiors:
		 *  - Alluvial plains are much flatter than alpine cordilleras
		 *  - Canyon mesas have harder sedimentary strata than alluvial plains
		 *  - Alpine cordilleras build significantly higher peaks than alluvial plains */
		assert(prov_flat_sum[ND_GEO_ALLUVIAL_PLAIN] / prov_core_cnt[ND_GEO_ALLUVIAL_PLAIN] >
		       prov_flat_sum[ND_GEO_ALPINE_CORDILLERA] / prov_core_cnt[ND_GEO_ALPINE_CORDILLERA] + 150);
		assert(prov_strata_sum[ND_GEO_CANYON_MESA] / prov_core_cnt[ND_GEO_CANYON_MESA] >
		       prov_strata_sum[ND_GEO_ALLUVIAL_PLAIN] / prov_core_cnt[ND_GEO_ALLUVIAL_PLAIN] + 150);
		assert(prov_elev_max[ND_GEO_ALPINE_CORDILLERA] > prov_elev_max[ND_GEO_ALLUVIAL_PLAIN] * 2);

		/* Exact periodic torus wrap of nd_field_geology() across b = 16, 18, 20 */
		const uint8_t geo_bits[] = { 16u, 18u, 20u };
		for (size_t bi = 0; bi < sizeof(geo_bits) / sizeof(geo_bits[0]); bi++) {
			uint8_t b = geo_bits[bi];
			int32_t span = (int32_t)(1u << b);
			nd_field_set_planet_bits(b);
			int32_t coords[4][2] = {
				{ 0, 0 }, { 1234, -5678 }, { (span >> 1) - 1, -(span >> 1) + 7 }, { -9999, 17777 }
			};
			for (int k = 0; k < 4; k++) {
				int32_t rx = coords[k][0], ry = coords[k][1];
				nd_geology_t g0  = nd_field_geology(0, rx, ry);
				nd_geology_t gx  = nd_field_geology(0, rx + span, ry);
				nd_geology_t gy  = nd_field_geology(0, rx, ry + span);
				nd_geology_t gxy = nd_field_geology(0, rx - span, ry - span);
				assert(memcmp(&g0, &gx, sizeof(g0)) == 0);
				assert(memcmp(&g0, &gy, sizeof(g0)) == 0);
				assert(memcmp(&g0, &gxy, sizeof(g0)) == 0);
			}
		}

		/* Metric summit scaling law across planet_bits = 16, 18, 20, 22 */
		const uint8_t scale_bits[4] = { 16u, 18u, 20u, 22u };
		int32_t peak_by_b[4] = {0};
		for (int bi = 0; bi < 4; bi++) {
			uint8_t b = scale_bits[bi];
			nd_field_set_planet_bits(b);
			nd_planet_scale_t ps = nd_field_get_planet_scale();
			int32_t pk = 0;
			uint32_t st_rng = 0xCAFEBABEu ^ ((uint32_t)b * 0x9E3779B1u);
			uint32_t mask = (1u << b) - 1u;
			int32_t half = (int32_t)(1u << (b - 1u));
			for (int it = 0; it < 4096; it++) {
				st_rng ^= st_rng << 13; st_rng ^= st_rng >> 17; st_rng ^= st_rng << 5;
				int32_t rx = (int32_t)(st_rng & mask) - half;
				st_rng ^= st_rng << 13; st_rng ^= st_rng >> 17; st_rng ^= st_rng << 5;
				int32_t ry = (int32_t)(st_rng & mask) - half;
				int32_t h = nd_field_room_hyp_coarse(0, rx, ry);
				assert(h <= ps.max_elev_wu);
				if (h > pk) pk = h;
			}
			peak_by_b[bi] = pk;
		}
		nd_field_set_planet_bits(16);

		assert(peak_by_b[0] >= 45000 && peak_by_b[0] <= 81000);
		assert(peak_by_b[1] > peak_by_b[0] && peak_by_b[1] >= 100000);
		assert(peak_by_b[2] > peak_by_b[1] && peak_by_b[2] >= 140000);
		assert(peak_by_b[3] > peak_by_b[2] && peak_by_b[3] >= 200000);

		printf("PASS: Phase 2 10-archetype geology, periodic wrap, and metric summits (b16=%d b18=%d b20=%d b22=%d wu)\n",
		       peak_by_b[0], peak_by_b[1], peak_by_b[2], peak_by_b[3]);
	}

	printf("=== ALL ND FIELD SIMD TESTS PASSED ===\n");
	return 0;
}
