#ifndef ND_REGION_TYPES_H
#define ND_REGION_TYPES_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <nd/law-types.h>

#define ND_ST_MAX_MODS 32
#define ND_ST_MOD_NAME 24
#define ST_ROW_KEY_LEN 18

#define ST_PLEN_ROOT  0
#define ST_PLEN_WORLD 16
#define ST_PLEN_CELL  64
#define ST_SEL_UNSET  255

struct st_rec {
	uint32_t owner;
	uint32_t plen;
	uint32_t nmods;
	uint32_t flags;
	char     mods[ND_ST_MAX_MODS][ND_ST_MOD_NAME];
};

static inline void
st_row_key(char *buf, size_t len, uint64_t id, uint8_t plen)
{
	snprintf(buf, len, "%016llx%02x", (unsigned long long)id, (unsigned)plen);
}

static inline int
st_row_key_parse(const char *key, uint64_t *id, uint8_t *plen)
{
	unsigned long long v = 0;
	unsigned p = 0;

	if (!key || strlen(key) != ST_ROW_KEY_LEN)
		return -1;

	for (int i = 0; i < 16; i++) {
		char c = key[i];
		int d;
		if (c >= '0' && c <= '9')
			d = c - '0';
		else if (c >= 'a' && c <= 'f')
			d = c - 'a' + 10;
		else
			return -1;
		v = (v << 4) | (unsigned long long)d;
	}
	for (int i = 16; i < ST_ROW_KEY_LEN; i++) {
		char c = key[i];
		int d;
		if (c >= '0' && c <= '9')
			d = c - '0';
		else if (c >= 'a' && c <= 'f')
			d = c - 'a' + 10;
		else
			return -1;
		p = (p << 4) | (unsigned)d;
	}
	if (p > 64)
		return -1;

	*id = v;
	*plen = (uint8_t)p;
	return 0;
}

static inline uint64_t st_planet_id(unsigned world)
{
	return (uint64_t)world << 48;
}

static inline unsigned st_world_of(uint64_t id)
{
	return (unsigned)(id >> 48);
}

#endif /* ND_REGION_TYPES_H */
