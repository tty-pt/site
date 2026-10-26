#ifndef BIOME_H
#define BIOME_H

#include "uapi/skel.h"
#include <string.h>
#include <ttypt/corm.h>
#include <nd/law-types.h>
#include <nd/world-types.h>

static inline unsigned *
biome_map_get(uint64_t position)
{
	static unsigned biome_map[BIOME_MAX];
	const void *v;
	unsigned ref;
	unsigned hd = biome_hd;

	for (int i = 0; i < 16; i++) {
		ref = ((position >> (48 + i)) << 4) | i;
		v = corm_get(hd, &ref);
		if (v) {
			memcpy(biome_map, v, sizeof(biome_map));
			return biome_map;
		}
	}

	ref = 16;
	v = corm_get(hd, &ref);
	if (v) {
		memcpy(biome_map, v, sizeof(biome_map));
		return biome_map;
	}

	return NULL;
}

#endif
