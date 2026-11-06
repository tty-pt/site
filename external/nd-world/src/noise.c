#include "noise.h"
#include <string.h>
#include <nd/st.h>
#include <nd/type.h>
#include <nd/field.h>

#define CHUNK_Y 5
#define CHUNK_SIZE (1 << CHUNK_Y)
#define CHUNK_M (CHUNK_SIZE * CHUNK_SIZE)

#define NOISE_OCT(to, x, seed) \
	noise_oct(to, s, sizeof(x) / sizeof(octave_t), x, seed, CHUNK_Y, 2)

struct tilemap {
	struct rect r;
	struct bio *bio;
};

static struct bio chunks_bio_raw[CHUNK_M * 4],
		  chunks_bio[CHUNK_M * 4],
		  *bio;
static struct rect chunks_r;
static coord_t chunks_obits = -1;
static uint32_t n_he[CHUNK_M],
	       n_cl[CHUNK_M],
	       n_tm[CHUNK_M];

/* Canonical field primitives forwarded from <nd/field.h> */
static inline uint32_t water_level(ucoord_t obits) {
	return nd_field_water_noise(obits);
}

static inline unsigned rain(ucoord_t obits __attribute__((unused)),
                           uint32_t w __attribute__((unused)),
                           uint32_t he __attribute__((unused)),
                           uint32_t cl,
                           uint32_t tmp __attribute__((unused))) {
	return nd_field_rain(cl);
}

static inline uint32_t sun_dist(ucoord_t obits) {
	return nd_field_sun_dist(obits);
}

static inline coord_t temp(ucoord_t obits, uint32_t he, uint32_t tm, coord_t pos_y) {
	return nd_field_temp(obits, he, tm, pos_y);
}

static inline unsigned bio_idx(ucoord_t rn, coord_t tmp) {
	return nd_field_bio_idx(rn, tmp);
}


static inline void
noise_full(size_t i, point_t s, ucoord_t obits)
{
	static octave_t
		x1[] = {{ 7, 2 }, { 6, 2 }, { 5, 2 }, { 4, 3 }, { 3, 3 }, { 2, 3 }},
		/* x2[] = {{ 8, 1 }, { 7, 2 }, { 6, 2 }, { 5, 3 }, { 3, 3 }}, */
		x4[] = {{ 5, 1 }, { 2, 1 }},
		x3[] = {{ 5, 1 }, { 8, 1 }};

	int j;

	point_t s_yx = { s[Y_COORD], s[X_COORD] };
	noise_oct(n_he, s_yx, sizeof(x1) / sizeof(octave_t), x1, 55 + obits, CHUNK_Y, 2);
	noise_oct(n_cl, s_yx, sizeof(x4) / sizeof(octave_t), x4, 1 + obits, CHUNK_Y, 2);
	noise_oct(n_tm, s_yx, sizeof(x3) / sizeof(octave_t), x3, 53 + obits, CHUNK_Y, 2);

	/* syslog_err(LOG_ERR, "noise max: %u, rain max: %u", NOISE_MAX, rn_max); */

	for (j = 0; j < CHUNK_M; j++) {
		/* x_pos s[1] + (j % (1 << CHUNK_Y)); */
		struct bio r = chunks_bio_raw[i + j];
		uint32_t _cl = n_cl[j], _tm = n_tm[j];
		register uint32_t _he = n_he[j], w = water_level(obits);
		r.tmp = temp(obits, _he, _tm, s[Y_COORD] + (j >> CHUNK_Y));
		r.rn = rain(obits, w, _he, _cl, r.tmp);

		/* FIX: Preserve authentic substrate ALWAYS */
		r.bio_idx = (uint8_t)bio_idx(r.rn, r.tmp);

		/* Physical signed elevation and water table in world units */
		r.elevation = (coord_t)((int64_t)_he * 200 / NOISE_MAX - 100);
		r.water_level = (coord_t)((int64_t)w * 200 / NOISE_MAX - 100);

		/* Submersion is physical depth, not an eraser */
		if (r.elevation < r.water_level) {
			r.is_water = 1;
			r.water_depth = (coord_t)(r.water_level - r.elevation);
		} else {
			r.is_water = 0;
			r.water_depth = 0;
		}

		r = nd_evt_noise(r, _he, w, _tm, _cl);
		chunks_bio_raw[i + j] = r;
	}
}

static inline uint32_t
view_idx(point_t pos)
{
	/* Offsets are taken in uint16_t so the cache window may straddle
	 * the int16 wrap boundary (32767 + 1 == -32768) like the noise. */
	return (uint32_t)(uint16_t)(pos[Y_COORD] - chunks_r.s[Y_COORD]) * chunks_r.l[X_COORD]
		+ (uint16_t)(pos[X_COORD] - chunks_r.s[X_COORD]);
}

/* im pretty sures squares must be involved */

static inline void
view_print(struct bio *to, point_t pos)
{
	register int row, col;
	for (row = 0; row < VIEW_SIZE; row++) {
		for (col = 0; col < VIEW_SIZE; col++) {
			point_t p = {
				pos[0] - VIEW_AROUND + col,
				pos[1] + VIEW_AROUND - row,
			};
			to[row * VIEW_SIZE + col] = bio[view_idx(p)];
		}
	}
}

static inline void __attribute__((hot))
spread(unsigned ny)
{
	static const size_t bio_line_size = sizeof(struct bio) << CHUNK_Y;
	static const unsigned nx = 2;
	unsigned w, p;

	/* p < nx * CHUNK_M, not <=: the loop body writes TWO CHUNK_SIZE lines
	 * (one at p, one at p + CHUNK_SIZE), so the last legal p is the one
	 * whose second line ends exactly at the end of the row. The <= let p run
	 * one step too far and the second row's first memcpy started 2432 bytes
	 * past the end of chunks_bio, overwriting the `bio` pointer and whatever
	 * globals the linker placed after it (ASan: global-buffer-overflow,
	 * WRITE of size 2432 "0 bytes after chunks_bio", src/noise.c:191). */
	for (w = 0; w < ny * nx * CHUNK_M; w += nx * CHUNK_M)
		for (p = 0; p < nx * CHUNK_M; p += nx << CHUNK_Y) {
			memcpy(&chunks_bio[w + p],
			       &chunks_bio_raw[w + (p >> 1)],
			       bio_line_size);

			memcpy(&chunks_bio[w + p + CHUNK_SIZE],
			       &chunks_bio_raw[w + (p >> 1) + CHUNK_M],
			       bio_line_size);
		}
}

static inline void
noise_chunks(pos_t pos)
{
	struct rect r;
	size_t n[DIM], i, cx, cy;
	point_t s, vprs = {
		pos[0] - VIEW_AROUND,
		pos[1] - VIEW_AROUND
	};

	noise_get_s(r.s, vprs, COORDMASK_LSHIFT(CHUNK_Y), 2);

	n[0] = ((VIEW_SIZE + vprs[0] - r.s[0]) >> CHUNK_Y) + 1;
	r.l[0] = n[0] << CHUNK_Y;

	n[1] = ((VIEW_SIZE + vprs[1] - r.s[1]) >> CHUNK_Y) + 1;
	r.l[1] = n[1] << CHUNK_Y;

	/* Containment and the chunk loops use wrapped (uint16_t) offsets
	 * and chunk counts, not int16 coordinate comparisons: at the int16
	 * wrap boundary s += CHUNK_SIZE wraps to -32768, which never reached
	 * the (int-promoted) end bound and overran chunks_bio_raw. */
	if (pos[3] == chunks_obits
	    && (uint16_t)(r.s[0] - chunks_r.s[0]) + r.l[0] <= chunks_r.l[0]
	    && (uint16_t)(r.s[1] - chunks_r.s[1]) + r.l[1] <= chunks_r.l[1])
		return;

	i = 0;

	for (cy = 0; cy < n[Y_COORD]; cy++) {
		s[Y_COORD] = (coord_t)(r.s[Y_COORD] + (cy << CHUNK_Y));
		for (cx = 0; cx < n[X_COORD]; cx++) {
			s[X_COORD] = (coord_t)(r.s[X_COORD] + (cx << CHUNK_Y));
			noise_full(i, s, pos[3]);
			i += CHUNK_M;
		}
	}

	bio = chunks_bio_raw;

	if (n[X_COORD] > 1) {
		spread(n[Y_COORD]);
		bio = chunks_bio;
	}

	chunks_obits = pos[3];
	memcpy(&chunks_r, &r, sizeof(r));
}

struct bio empty[VIEW_M] = {
	[0 ... VIEW_M - 1] = { .bio_idx = BIOME_VOLCANIC },
};

void
noise_view(struct bio to[VIEW_M], pos_t pos)
{
	noise_chunks(pos);
	view_print(to, pos);
}

struct bio
eng_noise_point(pos_t p)
{
	noise_chunks(p);
	return bio[view_idx(p)];
}
