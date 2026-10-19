#ifndef ND_NOISE_H
#define ND_NOISE_H

#include "noise_decl.h"
#include "biome.h"
#include "st.h"

static inline unsigned
_bio_idx(
	coord_t tmp_floor,
	coord_t tmp_ceil,
	ucoord_t rn_floor,
	ucoord_t rn_ceil,
	coord_t tmp,
	ucoord_t rn)
{
	ucoord_t rn_bit = (4 * (rn - rn_floor) / (rn_ceil - rn_floor)) & 3;
	coord_t tmp_bit = (4 * (tmp - tmp_floor) / (tmp_ceil - tmp_floor)) & 3;
	return rn_bit + (tmp_bit << 2);
}

#endif
