#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include "noise.h"
#include <nd/st.h>
#include <nd/field.h>

void noise_view(struct bio to[VIEW_M], pos_t pos);

/* Mock event handler for standalone test. It records the raw noise
 * samples in bio.raw[] so the worldgen golden pins the noise values
 * themselves, not only the derived climate/biome fields. */
struct bio nd_evt_noise(struct bio bio, uint32_t he, uint32_t w, uint32_t tm, uint32_t cl)
{
	bio.raw[0] = he;
	bio.raw[1] = w;
	bio.raw[2] = tm;
	bio.raw[3] = cl;
	return bio;
}

/* ---- Worldgen golden (keeps existing worlds bit-identical) ----------
 *
 * For worlds (pos[3]) 0..3 the golden pins every derived field of
 * struct bio plus the raw height/water/temperature/rain noise samples
 * over a 256x256-room area centred on the origin and a 64x64-room area
 * that straddles the int16 wrap boundary (x and y in 32736..-32737).
 * One FNV-1a-64 digest per world row is stored (text, diffable) in
 * tests/unit/data/worldgen_golden.txt, so a mismatch names the row.
 * WORLDGEN_GOLDEN_WRITE=1 regenerates the file; only do that for an
 * intentional worldgen change. */

#ifndef WORLDGEN_GOLDEN
#define WORLDGEN_GOLDEN "tests/unit/data/worldgen_golden.txt"
#endif

static uint64_t
fnv_u32(uint64_t h, uint32_t v)
{
	for (int i = 0; i < 4; i++) {
		h ^= (v >> (8 * i)) & 0xffu;
		h *= 0x100000001b3ull;
	}
	return h;
}

static uint64_t
bio_row_digest(int obits, int y, int x0, int n)
{
	uint64_t h = 0xcbf29ce484222325ull;
	for (int k = 0; k < n; k++) {
		pos_t pt = { (coord_t)(x0 + k), (coord_t)y, 0, (coord_t)obits };
		struct bio b = eng_noise_point(pt);
		h = fnv_u32(h, (uint32_t)(uint16_t)b.tmp);
		h = fnv_u32(h, (uint32_t)b.rn);
		h = fnv_u32(h, b.bio_idx);
		h = fnv_u32(h, b.is_water);
		h = fnv_u32(h, (uint32_t)(uint16_t)b.elevation);
		h = fnv_u32(h, (uint32_t)(uint16_t)b.water_level);
		h = fnv_u32(h, (uint32_t)(uint16_t)b.water_depth);
		for (int r = 0; r < 4; r++)
			h = fnv_u32(h, b.raw[r]);
	}
	return h;
}

struct golden_area { int x0, y0, n; };
static const struct golden_area golden_areas[] = {
	{ -128, -128, 256 },
#ifndef GOLDEN_NO_WRAP
	{ 32736, 32736, 64 }, /* wraps: 32736..32767, -32768..-32737 */
#endif
};

static int
worldgen_golden(void)
{
	const char *path = getenv("WORLDGEN_GOLDEN_PATH");
	int write = getenv("WORLDGEN_GOLDEN_WRITE") != NULL;
	FILE *f;
	int rows = 0, bad = 0;

	if (!path)
		path = WORLDGEN_GOLDEN;
	f = fopen(path, write ? "w" : "r");
	if (!f) {
		fprintf(stderr, "FAIL: cannot open worldgen golden %s\n", path);
		return 1;
	}
	for (int w = 0; w < 4; w++)
		for (size_t a = 0; a < sizeof(golden_areas) / sizeof(golden_areas[0]); a++)
			for (int i = 0; i < golden_areas[a].n; i++) {
				int y = (int16_t)(golden_areas[a].y0 + i);
				uint64_t h = bio_row_digest(w, y, golden_areas[a].x0, golden_areas[a].n);
				if (write) {
					fprintf(f, "%d %zu %d %016llx\n", w, a, y, (unsigned long long)h);
				} else {
					int gw, gy; size_t ga; unsigned long long gh;
					if (fscanf(f, "%d %zu %d %llx", &gw, &ga, &gy, &gh) != 4
					    || gw != w || ga != a || gy != y) {
						fprintf(stderr, "FAIL: worldgen golden malformed at w=%d a=%zu y=%d\n", w, a, y);
						fclose(f);
						return 1;
					}
					if (gh != h) {
						if (bad < 8)
							fprintf(stderr, "FAIL: worldgen differs: world %d area %zu row y=%d\n", w, a, y);
						bad++;
					}
				}
				rows++;
			}
	fclose(f);
	if (write) {
		printf("WROTE: worldgen golden %s (%d rows)\n", path, rows);
		return 0;
	}
	if (bad) {
		fprintf(stderr, "FAIL: %d of %d worldgen golden rows differ\n", bad, rows);
		return 1;
	}
	printf("PASS: worldgen bit-identical to golden (%d rows, worlds 0..3)\n", rows);
	return 0;
}

int main(void)
{
	printf("=== Testing NeverDark World Map Procedural Noise Continuity ===\n");

	if (worldgen_golden())
		return 1;

	/* R1 & R3: Continuous smooth transitions across horizontal chunk boundary y=0 */
	{
		for (int x = -16; x <= 16; x++) {
			pos_t p_north = { (coord_t)x, 0, 0, 0 };
			pos_t p_south = { (coord_t)x, -1, 0, 0 };
			struct bio bn = eng_noise_point(p_north);
			struct bio bs = eng_noise_point(p_south);

			int dt = bn.tmp > bs.tmp ? bn.tmp - bs.tmp : bs.tmp - bn.tmp;
			int dr = bn.rn > bs.rn ? bn.rn - bs.rn : bs.rn - bn.rn;

			/* Bounded neighbor deltas across the chunk seam: no cliff or discontinuity */
			assert(dt <= 15);
			assert(dr <= 60);
		}
		printf("PASS: horizontal chunk boundary y=0 is smooth and continuous (no cliff)\n");
	}

	/* R1 & R3: Continuous smooth transitions across vertical chunk boundary x=0 */
	{
		for (int y = -16; y <= 16; y++) {
			pos_t p_east = { 0, (coord_t)y, 0, 0 };
			pos_t p_west = { -1, (coord_t)y, 0, 0 };
			struct bio be = eng_noise_point(p_east);
			struct bio bw = eng_noise_point(p_west);

			int dt = be.tmp > bw.tmp ? be.tmp - bw.tmp : bw.tmp - be.tmp;
			int dr = be.rn > bw.rn ? be.rn - bw.rn : bw.rn - be.rn;

			/* Bounded neighbor deltas across the chunk seam: no cliff or discontinuity */
			assert(dt <= 15);
			assert(dr <= 60);
		}
		printf("PASS: vertical chunk boundary x=0 is smooth and continuous (no cliff)\n");
	}

	/* R2: Latitude gradient in temperature */
	{
		pos_t p_top = { 0, 100, 0, 0 };
		pos_t p_bot = { 0, -100, 0, 0 };
		struct bio bt = eng_noise_point(p_top);
		struct bio bb = eng_noise_point(p_bot);

		/* Temperature decreases with positive y (north) due to tmp_min / pos_y gradient */
		assert(bt.tmp != bb.tmp);
		printf("PASS: temperature varies with latitude coordinate pos_y\n");
	}

	/* R3: Chunk boundaries at x=32 and y=32 */
	{
		for (int k = -10; k <= 10; k++) {
			pos_t px1 = { 31, (coord_t)k, 0, 0 };
			pos_t px2 = { 32, (coord_t)k, 0, 0 };
			struct bio bx1 = eng_noise_point(px1);
			struct bio bx2 = eng_noise_point(px2);
			int dt_x = bx1.tmp > bx2.tmp ? bx1.tmp - bx2.tmp : bx2.tmp - bx1.tmp;
			int dr_x = bx1.rn > bx2.rn ? bx1.rn - bx2.rn : bx2.rn - bx1.rn;
			assert(dt_x <= 15);
			assert(dr_x <= 60);

			pos_t py1 = { (coord_t)k, 31, 0, 0 };
			pos_t py2 = { (coord_t)k, 32, 0, 0 };
			struct bio by1 = eng_noise_point(py1);
			struct bio by2 = eng_noise_point(py2);
			int dt_y = by1.tmp > by2.tmp ? by1.tmp - by2.tmp : by2.tmp - by1.tmp;
			int dr_y = by1.rn > by2.rn ? by1.rn - by2.rn : by2.rn - by1.rn;
			assert(dt_y <= 15);
			assert(dr_y <= 60);
		}
		printf("PASS: positive chunk boundaries at x=32 and y=32 are seamless\n");
	}

	/* R3: noise_view matches eng_noise_point across view grid */
	{
		struct bio view[VIEW_M];
		pos_t p_center = { 0, 0, 0, 0 };
		noise_view(view, p_center);

		for (int row = 0; row < VIEW_SIZE; row++) {
			for (int col = 0; col < VIEW_SIZE; col++) {
				pos_t pt = {
					(coord_t)(p_center[0] - VIEW_AROUND + col),
					(coord_t)(p_center[1] + VIEW_AROUND - row),
					0,
					0
				};
				struct bio bp = eng_noise_point(pt);
				struct bio bv = view[row * VIEW_SIZE + col];
				assert(bv.bio_idx == bp.bio_idx);
				assert(bv.tmp == bp.tmp);
				assert(bv.rn == bp.rn);
			}
		}
		printf("PASS: noise_view matches eng_noise_point exactly across all cells\n");
	}

	/* Phase 2 Gate: Submerged points preserve authentic substrate biome and valid depth */
	{
		int submerged_count = 0;
		for (int y = -32; y <= 32; y += 4) {
			for (int x = -32; x <= 32; x += 4) {
				pos_t pt = { (coord_t)x, (coord_t)y, 0, 0 };
				struct bio b = eng_noise_point(pt);
				if (b.elevation < b.water_level) {
					assert(b.bio_idx != 0); /* Authentic ground substrate preserved (never erased to 0) */
					assert(b.is_water == 1);
					assert(b.water_depth == (b.water_level - b.elevation));
					submerged_count++;
				} else {
					assert(b.is_water == 0);
					assert(b.water_depth == 0);
				}
			}
		}
		assert(submerged_count > 0);
		printf("PASS: authentic substrate biome preserved on all %d submerged sample points (bio_idx != 0)\n",
		       submerged_count);
	}

	/* Phase 3 Gate: Server Worldgen Field Parity with nd_field_sample */
	{
		/* 1. Exact mathematical identity at coordinate origin */
		pos_t origin_pt = { 0, 0, 0, 0 };
		struct bio b_orig = eng_noise_point(origin_pt);
		struct bio b_sample = nd_worldgen_point_sample(origin_pt);
		assert(b_orig.elevation == b_sample.elevation);
		assert(b_orig.water_level == b_sample.water_level);
		assert(b_orig.is_water == b_sample.is_water);

		int32_t orig_wx = 80;
		int32_t orig_wy = 64;
		nd_field_sample_t fs_orig = nd_field_sample(0, 0, orig_wx, orig_wy);
		assert(b_orig.elevation == fs_orig.elevation);
		assert(b_orig.water_level == fs_orig.water_level);
		assert(b_orig.is_water == fs_orig.is_water);

		/* 2. Bounded mathematical convergence across integer rooms:
		 * Server worldgen uses matrix forward-stepping (noise_quad cubic Hermite: 3t^2 - 2t^3),
		 * whereas client continuous field uses analytical C2 quintic Hermite (noise_wfade5: 6t^5 - 15t^4 + 10t^3).
		 * Across all octaves and rooms, variance is strictly bounded (|Delta H| <= 18 units, <9% of range). */
		int compared_rooms = 0;
		for (int w = 0; w < 4; w++) {
			for (int y = -16; y <= 16; y += 4) {
				for (int x = -16; x <= 16; x += 4) {
					pos_t pt = { (coord_t)x, (coord_t)y, 0, (coord_t)w };
					struct bio b = eng_noise_point(pt);

					int32_t wx = (int32_t)x * ND_FIELD_ROOM_W + (ND_FIELD_ROOM_W / 2);
					int32_t wy = -(int32_t)y * ND_FIELD_ROOM_H + (ND_FIELD_ROOM_H / 2);
					nd_field_sample_t fs = nd_field_sample((uint16_t)w, 0, wx, wy);

					int diff_h = abs((int)b.elevation - (int)fs.elevation);
					assert(diff_h <= 18);
					assert(b.water_level == fs.water_level);
					compared_rooms++;
				}
			}
		}
		printf("PASS: field parity between server eng_noise_point and client nd_field_sample verified (%d rooms)\n",
		       compared_rooms);
	}

	/* Phase 4 Gate: Toroidal wrapping boundary continuity (32767 -> -32768) & solar azimuth rotation */
	{
		for (int k = -16; k <= 16; k++) {
			/* Y wrap boundary at 32767 -> -32768 (inner circumference of torus tube) */
			pos_t py_max = { (coord_t)k, 32767, 0, 0 };
			pos_t py_min = { (coord_t)k, -32768, 0, 0 };
			struct bio by_max = eng_noise_point(py_max);
			struct bio by_min = eng_noise_point(py_min);
			int dt_y = abs((int)by_max.tmp - (int)by_min.tmp);
			assert(dt_y <= 15);

			/* X wrap boundary at 32767 -> -32768 */
			pos_t px_max = { 32767, (coord_t)k, 0, 0 };
			pos_t px_min = { -32768, (coord_t)k, 0, 0 };
			struct bio bx_max = eng_noise_point(px_max);
			struct bio bx_min = eng_noise_point(px_min);
			int dt_x = abs((int)bx_max.tmp - (int)bx_min.tmp);
			assert(dt_x <= 15);
		}

		/* Verify solar azimuth rotation updates server worldgen temperature */
		pos_t p_origin = { 0, 0, 0, 0 };
		pos_t p_far_outer = { -32768, 0, 0, 0 };
		pos_t p_near_inner = { 0, -32768, 0, 0 };
		pos_t p_far_inner = { -32768, -32768, 0, 0 };

		nd_field_set_sun_azimuth(0);
		struct bio b_orig_0 = eng_noise_point(p_origin);
		struct bio b_far_outer_0 = eng_noise_point(p_far_outer);
		struct bio b_near_inner_0 = eng_noise_point(p_near_inner);
		struct bio b_far_inner_0 = eng_noise_point(p_far_inner);
		assert(b_orig_0.tmp > b_far_outer_0.tmp);
		assert(b_far_inner_0.tmp > b_near_inner_0.tmp);

		/* Rotate Sun by 180 degrees (lambda = pi -> 32768): illuminated and shadowed sides swap */
		nd_field_set_sun_azimuth(32768);
		struct bio b_orig_pi = eng_noise_point(p_origin);
		struct bio b_far_outer_pi = eng_noise_point(p_far_outer);
		struct bio b_near_inner_pi = eng_noise_point(p_near_inner);
		struct bio b_far_inner_pi = eng_noise_point(p_far_inner);
		assert(b_far_outer_pi.tmp > b_orig_pi.tmp);
		assert(b_near_inner_pi.tmp > b_far_inner_pi.tmp);

		nd_field_set_sun_azimuth(0);
		printf("PASS: toroidal wrap boundary continuity (32767 -> -32768) and solar azimuth rotation verified\n");
	}

	printf("ALL MAP NOISE CONTINUITY TESTS PASSED\n");
	return 0;
}
