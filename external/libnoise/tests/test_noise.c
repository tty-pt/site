#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <ttypt/noise.h>

int main(void)
{
	printf("=== Testing Standalone Generic N-Dimensional Noise Library ===\n");

	/* 1. Point sampling across 1D, 2D, 3D, 4D */
	{
		int v1 = noise_point_1d(10, 42, 4);
		int v2 = noise_point_2d(10, 20, 42, 4);
		int v3 = noise_point_3d(10, 20, 30, 42, 4);
		int v4 = noise_point_4d(10, 20, 30, 40, 42, 4);

		assert(v1 >= 0 && v1 <= 255);
		assert(v2 >= 0 && v2 <= 255);
		assert(v3 >= 0 && v3 <= 255);
		assert(v4 >= 0 && v4 <= 255);
		printf("PASS: generic point sampling (1D, 2D, 3D, 4D) within valid [0..255] bounds\n");
	}

	/* 2. Determinism of point queries */
	{
		for (int i = -10; i <= 10; i++) {
			int a = noise_point_3d(i, i * 2, i * 3, 99, 3);
			int b = noise_point_3d(i, i * 2, i * 3, 99, 3);
			assert(a == b);
		}
		printf("PASS: point sampling determinism verified\n");
	}

	/* 3. Smoothness / Continuity in 3D */
	{
		int prev = noise_point_3d(0, 0, 0, 1234, 4);
		for (int x = 1; x <= 64; x++) {
			int cur = noise_point_3d(x, 0, 0, 1234, 4);
			int diff = abs(cur - prev);
			assert(diff <= 40); /* Smooth step transitions */
			prev = cur;
		}
		printf("PASS: 3D point continuity verified\n");
	}

	/* 4. Matrix Stepper in 2D and 3D */
	{
		uint32_t m2d[1 << (2 * 4)]; // 16x16 = 256
		int16_t s2d[2] = { 0, 0 };
		octave_t oct2d[] = { { 4, 1 } };
		noise_oct(m2d, s2d, 1, oct2d, 55, 4, 2);
		assert(m2d[0] != 0 && m2d[255] != 0);

		uint32_t m3d[1 << (3 * 3)]; // 8x8x8 = 512
		int16_t s3d[3] = { 0, 0, 0 };
		octave_t oct3d[] = { { 3, 1 } };
		noise_oct(m3d, s3d, 1, oct3d, 55, 3, 3);
		assert(m3d[0] != 0 && m3d[511] != 0);
		printf("PASS: generic matrix stepping in 2D and 3D verified\n");
	}

	/* 5. Original-algorithm wrapping sampler == noise_m (cy > x path) */
	{
		static const int16_t origins[] = { 0, 64, -128, 32704, -32768, 32752 };
		unsigned checked = 0;
		for (uint8_t dim = 2; dim <= 3; dim++)
		for (unsigned x = 2; x <= 4; x++)
		for (size_t oi = 0; oi < sizeof(origins) / sizeof(origins[0]); oi++) {
			unsigned cy = x + 1, w = 4, seed = 77;
			unsigned side = 1u << cy, total = 1u << (cy * dim);
			uint32_t *m = calloc(total, sizeof(uint32_t));
			int16_t ms[3], o = (int16_t) (origins[oi] & COORDMASK_LSHIFT(x));
			for (int j = 0; j < dim; j++)
				ms[j] = (int16_t) (o + 16 * j * (1 << x));
			int16_t msc[3] = { ms[0], ms[1], ms[2] };
			noise_m(m, msc, x, w, seed, cy, dim);
			for (unsigned i = 0; i < total; i++) {
				int16_t p[3];
				unsigned rem = i;
				for (int j = dim - 1; j >= 0; j--) {
					p[j] = (int16_t) (ms[j] + (int) (rem % side));
					rem /= side;
				}
				uint32_t a = noise_wpoint_n(p, x, w, seed, dim, 0);
				if (a != m[i]) {
					printf("FAIL: dim %u x %u origin %d i %u: %u != %u\n",
					       dim, x, o, i, a, m[i]);
					return 1;
				}
				checked++;
			}
			free(m);
		}
		/* cy <= x path (noise_fix_v pre-steps the vertices to a sub-quad,
		 * truncating in another order): equal within a rounding tolerance
		 * of 2^-20 of the value range (observed max 45 / 2^28). */
		for (unsigned x = 3; x <= 5; x++) {
			unsigned cy = 2, w = 4, seed = 9, side = 1u << cy;
			uint32_t m[16];
			int16_t ms[2] = { (int16_t) (32760 & COORDMASK_LSHIFT(cy)), 4 };
			int16_t msc[2] = { ms[0], ms[1] };
			memset(m, 0, sizeof(m));
			noise_m(m, msc, x, w, seed, cy, 2);
			for (unsigned i = 0; i < side * side; i++) {
				int16_t p[2] = { (int16_t) (ms[0] + (int) (i / side)),
				                 (int16_t) (ms[1] + (int) (i % side)) };
				uint32_t a = noise_wpoint_n(p, x, w, seed, 2, 0);
				uint32_t d = a > m[i] ? a - m[i] : m[i] - a;
				if (d > ((NOISE_MAX >> w) >> 20)) {
					printf("FAIL: cy<=x x %u i %u: %u != %u\n", x, i, a, m[i]);
					return 1;
				}
				checked++;
			}
		}
		printf("PASS: wrapping sampler value-equal to noise_m (%u samples)\n", checked);
	}

	/* 6. No seam at the int16 boundary: walk the whole 16-bit circle on
	 * every axis; the 32767 -> -32768 step obeys the same bound as every
	 * other step (the lattice vertex s + 2^x wraps like the coordinate). */
	{
		for (uint8_t dim = 1; dim <= 3; dim++)
		for (int axis = 0; axis < dim; axis++) {
			unsigned x = 4, w = 8;
			uint32_t bound = ((NOISE_MAX >> w) >> x) + 1;
			int16_t p[3] = { 1234, -77, 4321 };
			p[axis] = 0;
			uint32_t prev = noise_wpoint_n(p, x, w, 5, dim, 0), wrapd = 0;
			for (long k = 1; k <= 65536; k++) {
				p[axis] = (int16_t) (uint16_t) k;
				uint32_t cur = noise_wpoint_n(p, x, w, 5, dim, 0);
				uint32_t d = cur > prev ? cur - prev : prev - cur;
				if (d > bound) {
					printf("FAIL: seam dim %u axis %d at %ld: %u > %u\n",
					       dim, axis, k, d, bound);
					return 1;
				}
				if (p[axis] == -32768)
					wrapd = d;
				prev = cur;
			}
			(void) wrapd;
		}
		/* noise_point_n (cell-index keys) does jump there; document why
		 * it is not the animation primitive. */
		int seam = abs(noise_point_1d(32767, 5, 4) - noise_point_1d(-32768, 5, 4));
		printf("PASS: no seam across 32767->-32768 on any axis (legacy point jump %d)\n", seam);
	}

	/* 7. Ever-increasing time: casting a growing counter to int16 is
	 * continuous forever (time never has to be wrapped with %). */
	{
		unsigned x = 5, w = 8;
		uint32_t bound = ((NOISE_MAX >> w) >> x) + 1;
		uint32_t prev = 0;
		for (uint32_t anim = 0; anim < 200000; anim++) {
			int16_t p[3] = { 10, 20, (int16_t) (uint16_t) anim };
			uint32_t cur = noise_wpoint_n(p, x, w, 3, 3, 0);
			if (anim) {
				uint32_t d = cur > prev ? cur - prev : prev - cur;
				assert(d <= bound);
			}
			prev = cur;
		}
		printf("PASS: int16 cast of increasing anim counter is seamless\n");
	}

	/* 8. Corner cache and row stepper are value-equal to the point sampler */
	{
		noise_wcache_t c;
		noise_wcache_init(&c);
		uint32_t rng = 1;
		for (int i = 0; i < 20000; i++) {
			int16_t p[3];
			for (int j = 0; j < 3; j++) {
				rng = rng * 1103515245u + 12345u;
				p[j] = (int16_t) (32700 + (int) ((rng >> 16) % 136));
			}
			unsigned fl = (i & 1) ? NOISE_FADE : 0;
			assert(noise_wpoint_c(&c, p, 3, 8, 11, 3, fl) ==
			       noise_wpoint_n(p, 3, 8, 11, 3, fl));
		}
		static const int16_t steps[] = { 1, 3, -2, 7 };
		for (size_t si = 0; si < 4; si++)
		for (unsigned fl = 0; fl <= NOISE_FADE; fl++) {
			uint32_t row[300];
			int16_t p0[3] = { 5, -9, 32600 };
			noise_wrow(row, 300, p0, steps[si], 4, 8, 21, 3, fl, &c);
			for (int k = 0; k < 300; k++) {
				int16_t p[3] = { 5, -9, (int16_t) (32600 + k * steps[si]) };
				if (row[k] != noise_wpoint_n(p, 4, 8, 21, 3, fl)) {
					printf("FAIL: row step %d fade %u k %d\n", steps[si], fl, k);
					return 1;
				}
			}
		}
		printf("PASS: cached point and row stepper equal the point sampler\n");
	}

	/* 9. Efficiency: the row stepper fetches 2^dim corners once per lattice
	 * cell (like noise_get_v + noise_quad), not once per sample. */
	{
		noise_wcache_t c;
		uint32_t row[256];
		int16_t p0[3] = { 3, 4, 0 };
		noise_wcache_init(&c);
		noise_wrow(row, 256, p0, 1, 4, 8, 1, 3, 0, &c);
		assert(c.fetches == 256 / 16);
		p0[2] = 8; /* unaligned start straddles one extra cell */
		c.fetches = 0;
		noise_wrow(row, 256, p0, 1, 4, 8, 1, 3, 0, &c);
		assert(c.fetches == 256 / 16 + 1);
		printf("PASS: row stepper hashes once per lattice cell (%u fetches / 256 px)\n",
		       c.fetches);
	}

	/* 10. Hermite fade: same lattice values, smooth in between */
	{
		for (int k = 0; k < 4096; k += 16) {
			int16_t p[2] = { 96, (int16_t) k }; /* lattice-aligned */
			assert(noise_wpoint_n(p, 4, 8, 4, 2, NOISE_FADE) ==
			       noise_wpoint_n(p, 4, 8, 4, 2, 0));
		}
		assert(noise_wfade(0, 4) == 0 && noise_wfade(8, 4) == 8 && noise_wfade(15, 4) <= 15);
		printf("PASS: fade keeps lattice values and maps [0,2^x) onto itself\n");
	}

	/* 11. Plane stepper (noise_quad order: one corner fetch per 2D lattice
	 * cell) is value-equal to the point sampler, strided, unaligned, and
	 * across the int16 wrap. */
	{
		static uint32_t pl[70 * 90];
		static const int16_t org[][2] = { { 0, 0 }, { 5, -9 }, { 32760, 32750 }, { -7, 32701 } };
		for (size_t oi = 0; oi < 4; oi++)
		for (uint8_t dim = 2; dim <= 3; dim++)
		for (unsigned fl = 0; fl <= NOISE_FADE; fl++) {
			int16_t p0[3];
			p0[0] = 1234;
			p0[dim - 2] = org[oi][0];
			p0[dim - 1] = org[oi][1];
			noise_wcache_t c;
			noise_wcache_init(&c);
			memset(pl, 0, sizeof(pl));
			noise_wplane(pl, 80, 65, 90, p0, 3, 8, 17, dim, fl, &c);
			for (int r = 0; r < 70; r++)
			for (int k = 0; k < 90; k++) {
				uint32_t want = 0;
				if (r < 65 && k < 80) {
					int16_t p[3] = { p0[0], p0[1], p0[2] };
					p[dim - 2] = (int16_t) (p0[dim - 2] + r);
					p[dim - 1] = (int16_t) (p0[dim - 1] + k);
					want = noise_wpoint_n(p, 3, 8, 17, dim, fl);
				}
				if (pl[r * 90 + k] != want) {
					printf("FAIL: plane o %zu dim %u fade %u at %d,%d\n", oi, dim, fl, r, k);
					return 1;
				}
			}
		}
		noise_wcache_t c;
		int16_t p0[3] = { 9, 0, 0 };
		noise_wcache_init(&c);
		noise_wplane(pl, 64, 64, 64, p0, 4, 8, 1, 3, 0, &c);
		assert(c.fetches == 16);
		printf("PASS: plane stepper equals point sampler; %u fetches for 64x64 px at x=4\n",
		       c.fetches);
	}

	/* 12. Declarative cascade sparse point samplers (noise_sample_2d & 3d) */
	{
		static const noise_octave_t octs[] = {
			{ .shift = 6, .amp = 100, .skew_x = 4, .skew_y = 3, .contrast = 256 },
			{ .shift = 4, .amp = 50,  .skew_x = 0, .skew_y = 0, .contrast = 256 },
		};
		noise_cascade_t cfg = {
			.seed = 0x12345678,
			.count = 2,
			.octaves = octs,
		};
		int32_t s1 = noise_sample_2d(&cfg, 10, 20);
		int32_t s2 = noise_sample_2d(&cfg, 10, 20);
		assert(s1 == s2);

		int32_t s3d_1 = noise_sample_3d(&cfg, 10, 20, 30);
		int32_t s3d_2 = noise_sample_3d(&cfg, 10, 20, 30);
		assert(s3d_1 == s3d_2);
		printf("PASS: declarative point sampling (2D & 3D) deterministic\n");
	}

	/* 13. Declarative bulk matrix fill 2D (noise_matrix_fill_2d) */
	{
		static const noise_octave_t octs[] = {
			{ .shift = 5, .amp = 80, .skew_x = 0, .skew_y = 0, .contrast = 256 },
		};
		noise_cascade_t cfg = {
			.seed = 0x87654321,
			.count = 1,
			.octaves = octs,
		};
		int32_t mat[1024]; /* 32x32 for size_shift = 5 */
		noise_matrix_fill_2d(&cfg, 0, 0, 5, mat);
		int non_zero = 0;
		for (int i = 0; i < 1024; i++) {
			if (mat[i] != 0) non_zero++;
		}
		assert(non_zero > 100);
		printf("PASS: declarative bulk matrix fill 2D evaluated (%d/1024 non-zero)\n", non_zero);
	}

	/* 14. Declarative scanline stepper (noise_scanline_eval_row) */
	{
		static const noise_octave_t octs[] = {
			{ .shift = 4, .amp = 60, .skew_x = 0, .skew_y = 0, .contrast = 256 },
		};
		noise_cascade_t cfg = {
			.seed = 0xCAFE1234,
			.count = 1,
			.octaves = octs,
		};
		noise_scanline_t scan;
		noise_scanline_init(&scan, &cfg, 3);
		int32_t row[256];
		noise_scanline_eval_row(&scan, 0, 10, 5, 256, row);
		int non_zero = 0;
		for (int i = 0; i < 256; i++) {
			if (row[i] != 0) non_zero++;
		}
		assert(non_zero > 50);
		assert(scan.caches[0].fetches > 0);
		printf("PASS: scanline stepper evaluated row with %u corner fetches\n", scan.caches[0].fetches);
	}

	/* 15. Strict agreement between point sampler, scanline row, and matrix fill (2D & 3D) */
	{
		static const noise_octave_t octs_skew[] = {
			{ .shift = 6, .amp = 100, .skew_x = 3,  .skew_y = -5, .contrast = 256 },
			{ .shift = 4, .amp =  50, .skew_x = 0,  .skew_y =  0, .contrast = 512 },
			{ .shift = 3, .amp =  25, .skew_x = -2, .skew_y =  4, .contrast = 256 },
		};
		static const noise_cascade_t cfg_skew = {
			.seed = 0x55AA1234,
			.count = 3,
			.octaves = octs_skew,
		};

		/* Origins: near 0, negative, and straddling int16 wrap boundary (32767 -> -32768) */
		static const int16_t test_origins[][2] = {
			{ 0, 0 },
			{ -120, 85 },
			{ 32750, 32750 }
		};

		for (size_t oi = 0; oi < sizeof(test_origins) / sizeof(test_origins[0]); oi++) {
			int16_t ox = test_origins[oi][0];
			int16_t oy = test_origins[oi][1];

			int32_t mat[32 * 32];
			noise_matrix_fill_2d(&cfg_skew, ox, oy, 5, mat);

			noise_scanline_t scan;
			noise_scanline_init(&scan, &cfg_skew, 2);

			for (int r = 0; r < 32; r++) {
				int32_t row[32];
				int16_t cur_y = (int16_t)(oy + r);
				noise_scanline_eval_row(&scan, ox, cur_y, 0, 32, row);

				for (int c = 0; c < 32; c++) {
					int16_t cur_x = (int16_t)(ox + c);
					int32_t pt = noise_sample_2d(&cfg_skew, cur_x, cur_y);
					assert(row[c] == pt);
					assert(mat[r * 32 + c] == pt);
				}
			}
		}

		/* 3D row vs point sampler across wrap boundary */
		noise_scanline_t scan3d;
		noise_scanline_init(&scan3d, &cfg_skew, 3);
		int16_t ox = 32760, oy = -10, oz = 15;
		int32_t row3d[32];
		noise_scanline_eval_row(&scan3d, ox, oy, oz, 32, row3d);
		for (int c = 0; c < 32; c++) {
			int16_t cur_x = (int16_t)(ox + c);
			int32_t pt3d = noise_sample_3d(&cfg_skew, cur_x, oy, oz);
			assert(row3d[c] == pt3d);
		}
		printf("PASS: point == row[k] == matrix[r][k] across 2D & 3D and int16 wrap boundary\n");
	}

	/* 16. chaff_fill at frac==0 matches noise_oct bit for bit */
	{
		static const chaff_layer_t lay = {
			.shift = 4, .vshift = 1, .amp = 0, .seed_mode = CHAFF_SEED_SHARED,
			.skew = { 0, 0 }, .contrast = 0
		};
		static const chaff_field_t field = {
			.seed = 55, .dim = 2, .flags = 0, .nlayers = 1, .layers = &lay
		};
		int16_t oy = -64, ox = 32;
		int32_t c_out[16 * 16];
		chaff_coord_t origin[2] = { { oy, 0 }, { ox, 0 } };
		int32_t step_q16[2] = { 65536, 65536 };
		uint16_t n[2] = { 16, 16 };
		chaff_fill(&field, 0, 1, origin, step_q16, n, c_out, 16, CHAFF_MODE_SET);

		uint32_t oct_out[16 * 16];
		int16_t s[2] = { oy, ox };
		octave_t oct[] = { { 4, 1 } };
		noise_oct(oct_out, s, 1, oct, 55, 4, 2);

		for (int i = 0; i < 256; i++) {
			assert((uint32_t)c_out[i] == oct_out[i]);
		}
		printf("PASS: chaff_fill at frac==0 equals noise_oct bit-for-bit\n");
	}

	/* 17. Multi-layer nesting: sum(layers 0..k) + sum(layers k..n) == sum(layers 0..n) */
	{
		static const chaff_layer_t layers[] = {
			{ 7, 8, 100, CHAFF_SEED_PER_LAYER, { 3, -5 }, 256 },
			{ 5, 8,  50, CHAFF_SEED_PER_LAYER, { 0,  0 }, 512 },
			{ 3, 8,  20, CHAFF_SEED_PER_LAYER, { 2,  4 }, 256 }
		};
		static const chaff_field_t field = {
			.seed = 0xFEEDFACE, .dim = 2, .flags = CHAFF_FLAG_FADE, .nlayers = 3, .layers = layers
		};
		chaff_coord_t origin[2] = { { -20, 12345 }, { 45, 6789 } };
		int32_t step_q16[2] = { 409, 409 }; /* fractional step: 1/160th of a room */
		uint16_t n[2] = { 16, 16 };

		int32_t full[256], split[256];
		chaff_fill(&field, 0, 3, origin, step_q16, n, full, 16, CHAFF_MODE_SET);

		chaff_fill(&field, 0, 1, origin, step_q16, n, split, 16, CHAFF_MODE_SET);
		chaff_fill(&field, 1, 3, origin, step_q16, n, split, 16, CHAFF_MODE_ADD);

		for (int i = 0; i < 256; i++) {
			assert(full[i] == split[i]);
		}
		printf("PASS: layer nesting sum(0..1) + sum(1..3) == sum(0..3) verified with fractional coordinates\n");
	}

	printf("=== ALL LIBNOISE TESTS PASSED ===\n");

	return 0;
}
