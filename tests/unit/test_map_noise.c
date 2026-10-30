#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include "noise.h"
#include <nd/st.h>

void noise_view(struct bio to[VIEW_M], pos_t pos);

/* Mock event handler for standalone test */
struct bio nd_evt_noise(struct bio bio, uint32_t he, uint32_t w, uint32_t tm, uint32_t cl)
{
	(void)he; (void)w; (void)tm; (void)cl;
	return bio;
}

int main(void)
{
	printf("=== Testing NeverDark World Map Procedural Noise Continuity ===\n");

	pos_t p = { 0, 0, 0, 0 };

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

	printf("ALL MAP NOISE CONTINUITY TESTS PASSED\n");
	return 0;
}
