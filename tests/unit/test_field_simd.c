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
		int16_t q[4];
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
		int16_t h = nd_field_room_hyp_coarse(0, rx, ry);
		if (h <= 0)
			continue;
		land_cnt++;
		if (h > max_elev) max_elev = h;
		if (h < 100) plains_cnt++;
		if (h >= 500) mtn_cnt++;
		if (h >= 1000) alpine_cnt++;
		if (h >= 3000) towering_cnt++;
		if (h >= 300) {
			int16_t hx = nd_field_room_hyp_coarse(0, (int16_t)(rx + 1), ry);
			int16_t hy = nd_field_room_hyp_coarse(0, rx, (int16_t)(ry + 1));
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
	assert(towering_cnt >= 5 && max_elev >= 3500);
	assert(max_slope_dz >= 160); /* >= 45 deg alpine flanks over 160 wu room span */

	/* World 0 spawn valley [-24..24] keeps zero tectonic mountain spires on top of spawn,
	 * while distant alpine ranges loom prominently (>= 10 deg) on Spawn's horizon. */
	uint32_t w0_seed = 0x48454947u;
	for (int ry = -24; ry <= 24; ry += 4) {
		for (int rx = -24; rx <= 24; rx += 4) {
			int16_t h = nd_field_room_hyp_coarse(0, (int16_t)rx, (int16_t)ry);
			assert(nd_field_orogeny_wu(w0_seed, 0, rx, ry, h) == 0);
		}
	}
	{
		int16_t h0 = nd_field_room_hyp_coarse(0, 0, 0);
		double max_hor_deg = 0.0;
		for (int dy = -128; dy <= 128; dy += 4) {
			for (int dx = -128; dx <= 128; dx += 4) {
				if (dx == 0 && dy == 0) continue;
				double d = sqrt((double)dx * 160.0 * (double)dx * 160.0 +
				                (double)dy * 128.0 * (double)dy * 128.0);
				if (d < 400.0 || d > 25000.0) continue;
				int16_t h = nd_field_room_hyp_coarse(0, (int16_t)dx, (int16_t)dy);
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

	printf("=== ALL ND FIELD SIMD TESTS PASSED ===\n");
	return 0;
}
