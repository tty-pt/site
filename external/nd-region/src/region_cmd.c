#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ttypt/xy-mod.h>
#include <ttypt/corm.h>

#include <nd/law.h>
#include <nd/world-types.h>
#include <nd/region-types.h>
#include <nd/regions.h>
#include "region_cmd.h"

XY_DECL(int, map_where, coord_t *, p, unsigned, thing);
XY_DECL(morton_t, map_mwhere, unsigned, thing);
XY_DECL(ENT, ent_get, unsigned, ref);
XY_DECL(int, ent_set, unsigned, ref, ENT *, tmp);

#define NOMATCH_MESSAGE "I don't know what you mean.\n"
#define CANTDO_MESSAGE "You can't do that.\n"

static inline uint64_t spread3_cmd(uint32_t x)
{
    uint64_t v = x & 0xFFFFu;
    v = (v | (v << 32)) & 0x1F00000000FFFFULL;
    v = (v | (v << 16)) & 0x1F0000FF0000FFULL;
    v = (v | (v << 8)) & 0x100F00F00F00F00FULL;
    v = (v | (v << 4)) & 0x10C30C30C30C30C3ULL;
    v = (v | (v << 2)) & 0x1249249249249249ULL;
    return v;
}

static inline uint64_t pos_morton_encode_cmd(coord_t p[4])
{
    uint16_t ux = (uint16_t)((int32_t)p[0] + 32767);
    uint16_t uy = (uint16_t)((int32_t)p[1] + 32767);
    uint16_t uz = (uint16_t)((int32_t)p[2] + 32767);
    return spread3_cmd(ux) | (spread3_cmd(uy) << 1) | (spread3_cmd(uz) << 2) | ((uint64_t)(uint16_t)p[3] << 48);
}

static int
st_target_get(unsigned player_ref, uint64_t *id, uint8_t *plen)
{
	ENT e = ent_get(player_ref);

	if (e.target_plen == ST_SEL_UNSET)
		return 0;
	if (id)
		*id = e.target_id;
	if (plen)
		*plen = e.target_plen;
	return 1;
}

static void
st_target_set(unsigned player_ref, uint64_t id, uint8_t plen)
{
	ENT e = ent_get(player_ref);

	e.target_id = id;
	e.target_plen = plen;
	ent_set(player_ref, &e);
}

static void
st_target_clear(unsigned player_ref)
{
	st_target_set(player_ref, 0, ST_SEL_UNSET);
}

static int
st_target_or_position(unsigned player_ref, uint64_t *id, uint8_t *plen)
{
	if (st_target_get(player_ref, id, plen))
		return XY_OK;
	return st_region_of_player(player_ref, id, plen);
}

static int
st_here_cell(unsigned player_ref, uint64_t *cell)
{
	OBJ player;
	coord_t pos[4];

	if (!cell || player_ref == NOTHING || player_ref == (unsigned)-1)
		return XY_ERR_INVALID;
	if (nd_get(HD_OBJ, &player, &player_ref) != 0)
		return XY_ERR_NOTFOUND;
	map_where(pos, player.location);
	*cell = pos_morton_encode_cmd(pos);
	return XY_OK;
}

static int
st_argc_trim(int argc, char *argv[])
{
	while (argc > 1 && (!argv[argc - 1] || !*argv[argc - 1]))
		argc--;
	return argc;
}

static int
st_cmd_point(unsigned player_ref, int argc, char *argv[], int arg,
	uint64_t *cell, int *consumed)
{
	coord_t pos[4];
	char *end;
	long v;
	int i;
	unsigned long w;

	if (!cell || !consumed || arg >= argc || !argv[arg] || !*argv[arg])
		return XY_ERR_INVALID;
	*consumed = 0;
	if (strcmp(argv[arg], "here") == 0) {
		if (st_here_cell(player_ref, cell) != XY_OK)
			return XY_ERR_NOTFOUND;
		*consumed = 1;
		return XY_OK;
	}
	if (strcmp(argv[arg], "cosmos") == 0) {
		*cell = 0;
		*consumed = 1;
		return XY_OK;
	}
	if (strcmp(argv[arg], "world") == 0) {
		if (arg + 1 >= argc || !argv[arg + 1] || !*argv[arg + 1])
			return XY_ERR_INVALID;
		w = strtoul(argv[arg + 1], &end, 10);
		if (!end || end == argv[arg + 1] || *end || w > 65535)
			return XY_ERR_INVALID;
		*cell = st_planet_id((unsigned)w);
		*consumed = 2;
		return XY_OK;
	}
	if (arg + 3 >= argc)
		return XY_ERR_INVALID;
	for (i = 0; i < 4; i++) {
		if (!argv[arg + i] || !*argv[arg + i])
			return XY_ERR_INVALID;
		v = strtol(argv[arg + i], &end, 10);
		if (end == argv[arg + i] || *end || v < SHRT_MIN || v > SHRT_MAX)
			return XY_ERR_INVALID;
		pos[i] = (coord_t)v;
	}
	*cell = pos_morton_encode_cmd(pos);
	*consumed = 4;
	return XY_OK;
}

static int
st_finest_owned_plen(unsigned ref, uint64_t cell, uint8_t *plen)
{
	unsigned w;
	uint64_t mask;
	uint8_t hit = ST_SEL_UNSET;

	for (w = 0; w <= ST_PLEN_CELL; w++) {
		mask = w ? (~0ULL << (64 - w)) : 0ULL;
		if (st_can(ref, cell & mask, (uint8_t)w))
			hit = (uint8_t)w;
	}
	if (hit == ST_SEL_UNSET)
		return 0;
	if (plen)
		*plen = hit;
	return 1;
}

static int
st_cmd_region(unsigned player_ref, int argc, char *argv[], int region_arg,
	uint64_t *id, uint8_t *plen)
{
	argc = st_argc_trim(argc, argv);
	if (argc > region_arg && argv[region_arg] && *argv[region_arg]) {
		char *end = NULL;
		unsigned long w = strtoul(argv[region_arg], &end, 10);

		/* Check for "<width> <point>" syntax */
		if (end && !*end && w <= ST_PLEN_CELL && argc > region_arg + 1) {
			uint64_t cell;
			int consumed = 0;
			if (st_cmd_point(player_ref, argc, argv, region_arg + 1, &cell, &consumed) == XY_OK &&
			    consumed > 0 && region_arg + 1 + consumed == argc) {
				*plen = (uint8_t)w;
				uint64_t mask = *plen ? (~0ULL << (64 - *plen)) : 0ULL;
				*id = cell & mask;
				return XY_OK;
			}
		}

		/* Check for single world/cosmos argument (backward-compatible) */
		if (st_cmd_world(argv[region_arg], id, plen) == XY_OK)
			return XY_OK;
		return XY_ERR_INVALID;
	}
	return st_target_or_position(player_ref, id, plen);
}

/* --- Governance Commands ------------------------------------------------- */

static void st_row_header(unsigned player_ref, uint64_t id, uint8_t plen,
	const struct st_rec *rec);

static void
do_grant(int fd, int argc, char *argv[])
{
	unsigned player_ref = fd_player(fd);
	char *name = (argc > 1 && argv[1]) ? argv[1] : "";
	char *wstr = (argc > 2 && argv[2]) ? argv[2] : "";
	unsigned target_ref;
	uint64_t cell, id, mask;
	uint8_t plen;
	int consumed = 0;
	struct st_rec rec;
	int have;
	char *end = NULL;
	unsigned long w;
	OBJ target;

	argc = st_argc_trim(argc, argv);
	if (!*name) {
		nd_printf(player_ref, "Usage: grant <player> [<width> [here | world <N> | cosmos | <x> <y> <z> <w>]]\n");
		nd_flush(player_ref);
		return;
	}
	if (strcmp(name, "me") == 0)
		target_ref = player_ref;
	else {
		target_ref = player_lookup(name);
		if (target_ref == NOTHING) {
			nd_printf(player_ref, NOMATCH_MESSAGE);
			nd_flush(player_ref);
			return;
		}
	}
	if (nd_get(HD_OBJ, &target, &target_ref) != 0 || target.type != TYPE_ENTITY) {
		nd_printf(player_ref, CANTDO_MESSAGE);
		nd_flush(player_ref);
		return;
	}
	if (argc > 3) {
		if (st_cmd_point(player_ref, argc, argv, 3, &cell, &consumed) != XY_OK ||
		    consumed <= 0 || 3 + consumed != argc) {
			nd_printf(player_ref, "Usage: grant <player> [<width> [here | world <N> | cosmos | <x> <y> <z> <w>]]\n");
			nd_flush(player_ref);
			return;
		}
	} else if (st_here_cell(player_ref, &cell) != XY_OK) {
		nd_printf(player_ref, "You are nowhere.\n");
		nd_flush(player_ref);
		return;
	}
	if (argc > 2 && *wstr) {
		w = strtoul(wstr, &end, 10);
		if (!end || end == wstr || *end || w > ST_PLEN_CELL) {
			nd_printf(player_ref, "Usage: grant <player> [<width> [here | world <N> | cosmos | <x> <y> <z> <w>]]\n");
			nd_flush(player_ref);
			return;
		}
		plen = (uint8_t)w;
	} else {
		if (st_finest_owned_plen(player_ref, cell, &plen)) {
			if (plen < ST_PLEN_WORLD)
				plen = ST_PLEN_WORLD;
		} else
			plen = ST_PLEN_WORLD;
	}
	mask = plen ? (~0ULL << (64 - plen)) : 0ULL;
	id = cell & mask;

	if (!st_can_region(player_ref, id, plen)) {
		st_refuse_region(player_ref, id, plen);
		return;
	}

	have = st_row_get(id, plen, &rec);
	if (!have) {
		memset(&rec, 0, sizeof(rec));
		rec.plen = plen;
	}
	rec.owner = target_ref;
	st_row_put(id, plen, &rec);

	st_xy_claim_at(id, plen);
	st_xy_claim_at(0, 0);

	st_row_header(player_ref, id, plen, &rec);
	if (target_ref != player_ref) {
		OBJ granter;
		memset(&granter, 0, sizeof(granter));
		nd_get(HD_OBJ, &granter, &player_ref);
		nd_printf(target_ref, "%s granted you a region:\n", granter.name);
		st_row_header(target_ref, id, plen, &rec);
		nd_flush(target_ref);
	}
	nd_flush(player_ref);
}

static void
do_planet(int fd, int argc, char *argv[])
{
	unsigned player_ref = fd_player(fd);
	char *tok = (argc > 1 && argv[1]) ? argv[1] : "";
	uint64_t id;
	uint8_t plen;
	char *end = NULL;
	unsigned long w;
	struct st_rec rec;

	WARN("do_planet entered: player_ref=%u argc=%d tok='%s'\n", player_ref, argc, tok);

	if (!*tok) {
		nd_printf(player_ref, "Usage: planet <world>\n");
		nd_flush(player_ref);
		return;
	}
	w = strtoul(tok, &end, 10);
	if (!end || end == tok || *end || w > 65535) {
		nd_printf(player_ref, "Usage: planet <world>\n");
		nd_flush(player_ref);
		return;
	}
	id = st_planet_id((unsigned)w);
	plen = ST_PLEN_WORLD;

	int have = st_row_get(id, plen, &rec);
	char oname[64];
	if (have && rec.owner != player_ref && rec.owner != NOTHING) {
		st_owner_name(rec.owner, oname, sizeof(oname));
		nd_printf(player_ref, "Planet %lu is already claimed by %s\n", w, oname);
		nd_flush(player_ref);
		return;
	}
	if (!have && !st_can(player_ref, 0, ST_PLEN_ROOT)) {
		WARN("do_planet: permission denied branch taken!\n");
		nd_printf(player_ref, "Permission denied\n");
		nd_flush(player_ref);
		return;
	}
	int claim_rc = st_xy_claim_at(id, plen);
	st_xy_claim_at(0, 0);
	if (claim_rc != XY_OK) {
		WARN("do_planet: xy_claim_at failed rc=%d\n", claim_rc);
		nd_printf(player_ref, "Planet %lu could not be claimed (%s)\n",
			w, xy_strerror(xy_errno()));
		nd_flush(player_ref);
		return;
	}
	if (!have) {
		memset(&rec, 0, sizeof(rec));
		rec.plen = plen;
	}
	rec.owner = player_ref;
	st_row_put(id, plen, &rec);

	st_owner_name(player_ref, oname, sizeof(oname));
	nd_printf(player_ref,
		"planet %lu established: region id=0x%016llx plen=%u world=%lu owner=%s mods=%u\n",
		w, (unsigned long long)id, plen, w, oname, rec.nmods);
	nd_flush(player_ref);
}

static void
st_row_header(unsigned player_ref, uint64_t id, uint8_t plen,
	const struct st_rec *rec)
{
	char oname[64];

	st_owner_name(rec->owner, oname, sizeof(oname));
	if (plen == ST_PLEN_WORLD) {
		nd_printf(player_ref, "[id=0x%016llx plen=%u world=%u owner=%s mods=%u]\n",
			(unsigned long long)id, plen, st_world_of(id),
			oname, rec->nmods);
	} else {
		nd_printf(player_ref, "[id=0x%016llx plen=%u owner=%s mods=%u]\n",
			(unsigned long long)id, plen, oname, rec->nmods);
	}
}

static void
do_planets(int fd, int argc __attribute__((unused)), char *argv[] __attribute__((unused)))
{
	unsigned player_ref = fd_player(fd);
	unsigned c;
	const void *kp, *vp;
	int n = 0;

	if (!st_have_hd())
		return;
	c = corm_iter(owner_hd, NULL, 0);
	while (corm_next(&kp, &vp, c)) {
		const char *key = kp;
		const struct st_rec *rec = vp;
		uint64_t id;
		uint8_t plen;

		if (st_row_key_parse(key, &id, &plen) != 0)
			continue;
		if (rec->plen != plen)
			continue;
		st_row_header(player_ref, id, plen, rec);
		n++;
	}
	corm_fin(c);
	if (!n)
		nd_printf(player_ref, "No regions claimed.\n");
	nd_flush(player_ref);
}

static void
do_here(int fd, int argc __attribute__((unused)), char *argv[] __attribute__((unused)))
{
	unsigned player_ref = fd_player(fd);
	uint64_t id;
	uint8_t plen;
	struct st_rec rec;

	if (st_region_of_player(player_ref, &id, &plen) != XY_OK) {
		nd_printf(player_ref, "You are nowhere.\n");
		nd_flush(player_ref);
		return;
	}
	if (!st_row_get(id, plen, &rec)) {
		memset(&rec, 0, sizeof(rec));
		rec.owner = NOTHING;
		rec.plen = plen;
	}
	st_row_header(player_ref, id, plen, &rec);
	nd_flush(player_ref);
}

static void
do_regions(int fd, int argc, char *argv[])
{
	do_planets(fd, argc, argv);
}

static void
do_region(int fd, int argc, char *argv[])
{
	unsigned player_ref = fd_player(fd);
	char *wstr = (argc > 1 && argv[1]) ? argv[1] : "";
	uint64_t cell, id, mask;
	uint8_t plen;
	int consumed = 0;
	struct st_rec rec;
	int have;
	char *end = NULL;
	unsigned long w;
	char oname[64];

	argc = st_argc_trim(argc, argv);
	if (!*wstr) {
		do_here(fd, argc, argv);
		return;
	}

	w = strtoul(wstr, &end, 10);
	if (!end || end == wstr || *end || w > ST_PLEN_CELL) {
		nd_printf(player_ref, "Usage: region [<width> [here | world <N> | cosmos | <x> <y> <z> <w>]]\n");
		nd_flush(player_ref);
		return;
	}
	plen = (uint8_t)w;

	if (argc > 2) {
		if (st_cmd_point(player_ref, argc, argv, 2, &cell, &consumed) != XY_OK ||
		    consumed <= 0 || 2 + consumed != argc) {
			nd_printf(player_ref, "Usage: region [<width> [here | world <N> | cosmos | <x> <y> <z> <w>]]\n");
			nd_flush(player_ref);
			return;
		}
	} else if (st_here_cell(player_ref, &cell) != XY_OK) {
		nd_printf(player_ref, "You are nowhere.\n");
		nd_flush(player_ref);
		return;
	}

	mask = plen ? (~0ULL << (64 - plen)) : 0ULL;
	id = cell & mask;

	have = st_row_get(id, plen, &rec);
	if (have && rec.owner != player_ref && rec.owner != NOTHING) {
		st_owner_name(rec.owner, oname, sizeof(oname));
		nd_printf(player_ref, "Region is already claimed by %s\n", oname);
		nd_flush(player_ref);
		return;
	}
	if (!st_can_region(player_ref, id, plen)) {
		st_refuse_region(player_ref, id, plen);
		return;
	}

	int claim_rc = st_xy_claim_at(id, plen);
	st_xy_claim_at(0, 0);
	if (claim_rc != XY_OK) {
		nd_printf(player_ref, "Region could not be claimed (%s)\n",
			xy_strerror(xy_errno()));
		nd_flush(player_ref);
		return;
	}

	if (!have) {
		memset(&rec, 0, sizeof(rec));
		rec.plen = plen;
	}
	rec.owner = player_ref;
	st_row_put(id, plen, &rec);

	st_owner_name(player_ref, oname, sizeof(oname));
	nd_printf(player_ref,
		"region established: id=0x%016llx plen=%u owner=%s mods=%u\n",
		(unsigned long long)id, plen, oname, rec.nmods);
	nd_flush(player_ref);
}

static void
do_target(int fd, int argc, char *argv[])
{
	unsigned player_ref = fd_player(fd);
	uint64_t id;
	uint8_t plen;
	char place[64];
	const char *arg;

	argc = st_argc_trim(argc, argv);
	arg = (argc > 1 && argv[1]) ? argv[1] : "";

	if (!*arg) {
		if (st_target_get(player_ref, &id, &plen)) {
			st_ban_place(id, plen, place, sizeof(place));
			nd_printf(player_ref,
				"Default target region: %s.\n", place);
			nd_printf(player_ref,
				"Commands without a region act there: wall, ban, "
				"unban, loadmod, unloadmod, modlist, deny.\n");
		} else {
			nd_printf(player_ref,
				"No default target region set (commands act on "
				"your current region).\n");
			nd_printf(player_ref,
				"Set one with `target <region>', `target here', "
				"or clear it with `target none'.\n");
		}
		nd_flush(player_ref);
		return;
	}

	if (strcmp(arg, "none") == 0) {
		st_target_clear(player_ref);
		nd_printf(player_ref, "Default target region cleared.\n");
		nd_flush(player_ref);
		return;
	}

	if (strcmp(arg, "here") == 0) {
		if (st_region_of_player(player_ref, &id, &plen) != XY_OK) {
			nd_printf(player_ref, "You are nowhere.\n");
			nd_flush(player_ref);
			return;
		}
	} else if (argc > 2 && *arg) {
		char *end = NULL;
		unsigned long wp = strtoul(arg, &end, 10);
		uint64_t cell;
		uint64_t mask;
		int consumed = 0;

		if (!end || *end || wp > ST_PLEN_CELL ||
		    st_cmd_point(player_ref, argc, argv, 2, &cell, &consumed) != XY_OK ||
		    consumed <= 0 || 2 + consumed != argc) {
			nd_printf(player_ref,
				"Usage: target [<world> | cosmos | here | none | <width> <point>]\n");
			nd_flush(player_ref);
			return;
		}
		plen = (uint8_t)wp;
		mask = plen ? (~0ULL << (64 - plen)) : 0ULL;
		id = cell & mask;
	} else if (st_cmd_world(arg, &id, &plen) != XY_OK) {
		nd_printf(player_ref,
			"Usage: target [<world> | cosmos | here | none | <width> <point>]\n");
		nd_flush(player_ref);
		return;
	}

	if (!st_can_region(player_ref, id, plen)) {
		st_refuse_region(player_ref, id, plen);
		return;
	}
	st_target_set(player_ref, id, plen);
	st_ban_place(id, plen, place, sizeof(place));
	nd_printf(player_ref, "Default target region set to %s.\n", place);
	nd_flush(player_ref);
}

struct st_deny_arg {
	char what[ND_ST_MOD_NAME + 32];
	xy_deny_type_t type;
	int rc;
};

static int
st_deny_tramp(void *ud)
{
	struct st_deny_arg *a = ud;
	a->rc = xy_deny(a->what, a->type);
	return a->rc;
}

static void
do_deny(int fd, int argc, char *argv[])
{
	unsigned player_ref = fd_player(fd);
	struct st_deny_arg a;
	uint64_t id;
	uint8_t plen;
	xy_deny_type_t type;
	int rc;

	if (argc < 3 || !argv[1] || !*argv[1] || !argv[2] || !*argv[2]) {
		nd_printf(player_ref, "Usage: deny <hook|module> <name> [world]\n");
		nd_flush(player_ref);
		return;
	}
	if (!strcmp(argv[1], "hook"))
		type = XY_DENY_HOOK;
	else if (!strcmp(argv[1], "module"))
		type = XY_DENY_MODULE;
	else {
		nd_printf(player_ref, "Usage: deny <hook|module> <name> [world]\n");
		nd_flush(player_ref);
		return;
	}
	if (st_cmd_region(player_ref, argc, argv, 3, &id, &plen) != XY_OK) {
		nd_printf(player_ref, "Invalid world\n");
		nd_flush(player_ref);
		return;
	}
	if (!st_can_region(player_ref, id, plen)) {
		st_refuse_region(player_ref, id, plen);
		return;
	}
	if (!st_row_get(id, plen, NULL)) {
		nd_printf(player_ref, "No such region\n");
		nd_flush(player_ref);
		return;
	}

	snprintf(a.what, sizeof(a.what), "%s", argv[2]);
	a.type = type;
	a.rc = XY_ERR_INVALID;

	rc = st_xy_claim_at(id, plen);
	if (rc == XY_OK)
		rc = xy_with_region(id, plen, st_deny_tramp, &a);
	st_xy_claim_at(0, 0);
	if (rc != XY_OK || a.rc != XY_OK) {
		nd_printf(player_ref, "deny failed (%s)\n", xy_strerror(rc != XY_OK ? rc : a.rc));
		nd_flush(player_ref);
		return;
	}
	nd_printf(player_ref, "denied: %s %s in region id=0x%016llx plen=%u\n",
		type == XY_DENY_HOOK ? "hook" : "module", argv[2],
		(unsigned long long)id, plen);
	nd_flush(player_ref);
}

static void
do_loadmod(int fd, int argc, char *argv[])
{
	unsigned player_ref = fd_player(fd);
	char *name;
	uint64_t id = 0;
	uint8_t plen = ST_PLEN_ROOT;
	char namebuf[ND_ST_MOD_NAME + 1];
	int rc;

	argc = st_argc_trim(argc, argv);
	name = (argc > 1 && argv[1]) ? argv[1] : "";

	if (!*name || strchr(name, '/')) {
		nd_printf(player_ref, "Usage: loadmod <module> [world]\n");
		nd_flush(player_ref);
		return;
	}
	if (st_cmd_region(player_ref, argc, argv, 2, &id, &plen) != XY_OK) {
		nd_printf(player_ref, "Invalid world\n");
		nd_flush(player_ref);
		return;
	}
	if (!st_can(player_ref, id, plen)) {
		nd_printf(player_ref, "Permission denied\n");
		nd_flush(player_ref);
		return;
	}
	if (!st_row_get(id, plen, NULL)) {
		nd_printf(player_ref, "No such region\n");
		nd_flush(player_ref);
		return;
	}
	if (st_mod_loaded(id, plen, name)) {
		nd_printf(player_ref, "%s is already loaded\n", name);
		nd_flush(player_ref);
		return;
	}

	snprintf(namebuf, sizeof(namebuf), "%s", name);
	rc = st_region_load(id, plen, namebuf);
	if (rc != XY_OK) {
		nd_printf(player_ref, "%s failed to load (%s)\n", name, xy_strerror(xy_errno()));
		nd_flush(player_ref);
		return;
	}
	st_mod_add(id, plen, name);
	nd_printf(player_ref, "%s loaded into region id=0x%016llx plen=%u\n",
		namebuf, (unsigned long long)id, plen);
	nd_flush(player_ref);
}

static void
do_unloadmod(int fd, int argc, char *argv[])
{
	unsigned player_ref = fd_player(fd);
	char *name;
	uint64_t id = 0;
	uint8_t plen = ST_PLEN_ROOT;
	char namebuf[ND_ST_MOD_NAME + 1];
	int rc;

	argc = st_argc_trim(argc, argv);
	name = (argc > 1 && argv[1]) ? argv[1] : "";

	if (!*name || strchr(name, '/')) {
		nd_printf(player_ref, "Usage: unloadmod <module> [world]\n");
		nd_flush(player_ref);
		return;
	}
	if (st_cmd_region(player_ref, argc, argv, 2, &id, &plen) != XY_OK) {
		nd_printf(player_ref, "Invalid world\n");
		nd_flush(player_ref);
		return;
	}
	if (!st_can(player_ref, id, plen)) {
		nd_printf(player_ref, "Permission denied\n");
		nd_flush(player_ref);
		return;
	}
	if (!st_mod_loaded(id, plen, name)) {
		nd_printf(player_ref, "%s is not loaded\n", name);
		nd_flush(player_ref);
		return;
	}

	snprintf(namebuf, sizeof(namebuf), "%s", name);
	rc = st_region_unload(id, plen, namebuf);
	if (rc != XY_OK && rc != XY_ERR_NOTFOUND) {
		nd_printf(player_ref, "%s failed to unload (%s)\n", name, xy_strerror(xy_errno()));
		nd_flush(player_ref);
		return;
	}
	st_mod_del(id, plen, name);
	nd_printf(player_ref, "%s unloaded from region id=0x%016llx plen=%u\n",
		namebuf, (unsigned long long)id, plen);
	nd_flush(player_ref);
}

static void
do_modlist(int fd, int argc, char *argv[])
{
	unsigned player_ref = fd_player(fd);
	uint64_t id = 0;
	uint8_t plen = ST_PLEN_ROOT;
	struct st_rec rec;
	uint32_t i;

	argc = st_argc_trim(argc, argv);
	if (st_cmd_region(player_ref, argc, argv, 1, &id, &plen) != XY_OK) {
		nd_printf(player_ref, "Invalid world\n");
		nd_flush(player_ref);
		return;
	}
	if (!st_row_get(id, plen, &rec)) {
		nd_printf(player_ref, "No such region\n");
		nd_flush(player_ref);
		return;
	}
	st_row_header(player_ref, id, plen, &rec);
	for (i = 0; i < rec.nmods && i < ND_ST_MAX_MODS; i++)
		nd_printf(player_ref, "  %s\n", rec.mods[i]);
	nd_flush(player_ref);
}

static void
do_release(int fd, int argc, char *argv[])
{
	unsigned player_ref = fd_player(fd);
	char *tok = (argc > 1 && argv[1]) ? argv[1] : "";
	char *end = NULL;
	unsigned long w = 0;
	uint64_t id = 0, cell, mask;
	uint8_t plen = ST_PLEN_ROOT;
	struct st_rec rec;
	int is_world = 0;
	int consumed = 0;
	uint32_t i;
	char namebuf[ND_ST_MOD_NAME + 1];

	argc = st_argc_trim(argc, argv);
	if (argc <= 1 || !*tok) {
		nd_printf(player_ref, "Usage: release [<world> | <width> [here | world <N> | cosmos | <x> <y> <z> <w>]]\n");
		nd_flush(player_ref);
		return;
	}
	if (argc == 2) {
		w = strtoul(tok, &end, 10);
		if (!end || end == tok || *end || w > 65535) {
			nd_printf(player_ref, "Usage: release [<world> | <width> [here | world <N> | cosmos | <x> <y> <z> <w>]]\n");
			nd_flush(player_ref);
			return;
		}
		id = st_planet_id((unsigned)w);
		plen = ST_PLEN_WORLD;
		is_world = 1;
	} else {
		w = strtoul(tok, &end, 10);
		if (!end || end == tok || *end || w > ST_PLEN_CELL) {
			nd_printf(player_ref, "Usage: release [<world> | <width> [here | world <N> | cosmos | <x> <y> <z> <w>]]\n");
			nd_flush(player_ref);
			return;
		}
		if (st_cmd_point(player_ref, argc, argv, 2, &cell, &consumed) != XY_OK ||
		    consumed <= 0 || 2 + consumed != argc) {
			nd_printf(player_ref, "Usage: release [<world> | <width> [here | world <N> | cosmos | <x> <y> <z> <w>]]\n");
			nd_flush(player_ref);
			return;
		}
		plen = (uint8_t)w;
		mask = plen ? (~0ULL << (64 - plen)) : 0ULL;
		id = cell & mask;
		w = 0;
	}

	if (plen == ST_PLEN_ROOT) {
		nd_printf(player_ref, "The cosmos cannot be released (grant it instead).\n");
		nd_flush(player_ref);
		return;
	}
	if (!st_can(player_ref, id, plen)) {
		nd_printf(player_ref, "Permission denied\n");
		nd_flush(player_ref);
		return;
	}
	if (!st_row_get(id, plen, &rec)) {
		nd_printf(player_ref, "No such region\n");
		nd_flush(player_ref);
		return;
	}
	for (i = 0; i < rec.nmods && i < ND_ST_MAX_MODS; i++) {
		memcpy(namebuf, rec.mods[i], ND_ST_MOD_NAME);
		namebuf[ND_ST_MOD_NAME] = '\0';
		if (!namebuf[0])
			continue;
		st_region_unload(id, plen, namebuf);
	}
	st_row_del(id, plen);
	if (is_world)
		nd_printf(player_ref, "planet %lu released\n", w);
	else
		nd_printf(player_ref, "released: region id=0x%016llx plen=%u\n",
			(unsigned long long)id, plen);
	nd_flush(player_ref);
}

static void
do_ban(int fd, int argc, char *argv[])
{
	unsigned player_ref = fd_player(fd), victim_ref;
	uint64_t id = 0;
	uint8_t plen = ST_PLEN_ROOT;
	char *name = (argc > 1 && argv[1]) ? argv[1] : "";
	char place[64];
	OBJ victim;

	if (!*name) {
		nd_printf(player_ref, "Usage: ban <player> [world]\n");
		nd_flush(player_ref);
		return;
	}
	victim_ref = player_lookup(name);
	if (victim_ref == NOTHING) {
		nd_printf(player_ref, NOMATCH_MESSAGE);
		nd_flush(player_ref);
		return;
	}
	if (victim_ref == ROOT) {
		nd_printf(player_ref, CANTDO_MESSAGE);
		nd_flush(player_ref);
		return;
	}
	if (nd_get(HD_OBJ, &victim, &victim_ref) != 0 || victim.type != TYPE_ENTITY) {
		nd_printf(player_ref, CANTDO_MESSAGE);
		nd_flush(player_ref);
		return;
	}
	if (st_cmd_region(player_ref, argc, argv, 2, &id, &plen) != XY_OK) {
		nd_printf(player_ref, "Usage: ban <player> [world]\n");
		nd_flush(player_ref);
		return;
	}
	if (!st_can_region(player_ref, id, plen)) {
		st_refuse_region(player_ref, id, plen);
		return;
	}

	st_ban_put(victim_ref, id, plen, player_ref);
	st_ban_place(id, plen, place, sizeof(place));
	nd_printf(player_ref, "Banned %s from %s.\n", victim.name, place);
	nd_printf(victim_ref, "You have been banned from %s.\n", place);
	nd_flush(player_ref);
	nd_flush(victim_ref);
}

static void
do_driver(int fd, int argc, char *argv[])
{
	unsigned player_ref = fd_player(fd);
	if (argc < 2 || !argv[1] || !*argv[1]) {
		char stage[16] = {0};
		char map[16] = {0};
		uint8_t layout = 0;
		uint8_t flags = 0;
		st_player_resolve_presentation(player_ref, stage, sizeof(stage),
			map, sizeof(map), &layout, &flags);
		const char *lstr = (layout == ND_LAYOUT_STAGE_FULLSCREEN) ? "fullscreen" :
		                   (layout == ND_LAYOUT_STAGE_EXPANDED) ? "expanded" : "hybrid";
		const char *fstr = (flags & ND_REGION_LOCKED) ? "locked" : "choice allowed";
		nd_printf(player_ref, "Active presentation policy:\n"
		                      "  Stage driver: %s (%s)\n"
		                      "  Map driver:   %s\n"
		                      "  Layout mode:  %s\n",
		          stage[0] ? stage : "tty", fstr,
		          map[0] ? map : "ascii-bud", lstr);
		nd_flush(player_ref);
		return;
	}

	uint64_t id = 0;
	uint8_t plen = ST_PLEN_ROOT;
	struct st_rec rec;

	if (strcmp(argv[1], "clear") == 0) {
		if (st_cmd_region(player_ref, argc, argv, 2, &id, &plen) != XY_OK) {
			nd_printf(player_ref, "Invalid region\n");
			nd_flush(player_ref);
			return;
		}
		if (!st_can_region(player_ref, id, plen)) {
			st_refuse_region(player_ref, id, plen);
			return;
		}
		if (st_row_get(id, plen, &rec)) {
			rec.driver[0] = '\0';
			rec.map_driver[0] = '\0';
			rec.layout_mode = 0;
			rec.pres_flags = 0;
			rec.flags = 0;
			st_row_put(id, plen, &rec);
		}
		nd_printf(player_ref, "Cleared presentation policy for region.\n");
		nd_flush(player_ref);
		return;
	}

	if (strcmp(argv[1], "stage") == 0) {
		if (argc < 3 || !argv[2] || !*argv[2]) {
			nd_printf(player_ref, "Usage: driver stage <name|choice> [locked|choice] [region]\n");
			nd_flush(player_ref);
			return;
		}
		const char *sname = argv[2];
		int reg_arg = 3;
		uint32_t pflags = ND_REGION_ALLOW_CHOICE;

		if (strcmp(sname, "choice") == 0) {
			sname = "";
			pflags = ND_REGION_ALLOW_CHOICE;
		} else if (argc > 3 && argv[3]) {
			if (strcmp(argv[3], "locked") == 0) {
				pflags = ND_REGION_LOCKED;
				reg_arg = 4;
			} else if (strcmp(argv[3], "choice") == 0) {
				pflags = ND_REGION_ALLOW_CHOICE;
				reg_arg = 4;
			}
		}

		if (st_cmd_region(player_ref, argc, argv, reg_arg, &id, &plen) != XY_OK) {
			nd_printf(player_ref, "Invalid region\n");
			nd_flush(player_ref);
			return;
		}
		if (!st_can_region(player_ref, id, plen)) {
			st_refuse_region(player_ref, id, plen);
			return;
		}
		if (!st_row_get(id, plen, &rec)) {
			memset(&rec, 0, sizeof(rec));
			rec.owner = player_ref;
			rec.plen = plen;
		}
		snprintf(rec.driver, sizeof(rec.driver), "%s", sname);
		rec.pres_flags = pflags;
		rec.flags = pflags;
		st_row_put(id, plen, &rec);
		nd_printf(player_ref, "Set region stage driver to %s (%s)\n",
			rec.driver[0] ? rec.driver : "unconstrained",
			(pflags & ND_REGION_LOCKED) ? "locked" : "choice allowed");
		nd_flush(player_ref);
		return;
	}

	if (strcmp(argv[1], "map") == 0) {
		if (argc < 3 || !argv[2] || !*argv[2]) {
			nd_printf(player_ref, "Usage: driver map <ascii|canvas|none> [region]\n");
			nd_flush(player_ref);
			return;
		}
		const char *mname = argv[2];
		char map_drv[16] = "ascii-bud";
		if (!strcmp(mname, "canvas") || !strcmp(mname, "canvas-lamina"))
			snprintf(map_drv, sizeof(map_drv), "canvas-lamina");
		else if (!strcmp(mname, "ascii") || !strcmp(mname, "ascii-bud"))
			snprintf(map_drv, sizeof(map_drv), "ascii-bud");
		else if (!strcmp(mname, "none"))
			snprintf(map_drv, sizeof(map_drv), "none");
		else {
			nd_printf(player_ref, "Unknown map driver (use ascii, canvas, or none)\n");
			nd_flush(player_ref);
			return;
		}

		if (st_cmd_region(player_ref, argc, argv, 3, &id, &plen) != XY_OK) {
			nd_printf(player_ref, "Invalid region\n");
			nd_flush(player_ref);
			return;
		}
		if (!st_can_region(player_ref, id, plen)) {
			st_refuse_region(player_ref, id, plen);
			return;
		}
		if (!st_row_get(id, plen, &rec)) {
			memset(&rec, 0, sizeof(rec));
			rec.owner = player_ref;
			rec.plen = plen;
		}
		snprintf(rec.map_driver, sizeof(rec.map_driver), "%s", map_drv);
		st_row_put(id, plen, &rec);
		nd_printf(player_ref, "Set region map driver to %s\n", map_drv);
		nd_flush(player_ref);
		return;
	}

	if (strcmp(argv[1], "layout") == 0) {
		if (argc < 3 || !argv[2] || !*argv[2]) {
			nd_printf(player_ref, "Usage: driver layout <hybrid|expanded|fullscreen> [region]\n");
			nd_flush(player_ref);
			return;
		}
		const char *lmode = argv[2];
		uint32_t mode = ND_LAYOUT_STANDARD_HYBRID;
		if (!strcmp(lmode, "fullscreen"))
			mode = ND_LAYOUT_STAGE_FULLSCREEN;
		else if (!strcmp(lmode, "expanded"))
			mode = ND_LAYOUT_STAGE_EXPANDED;
		else if (!strcmp(lmode, "hybrid"))
			mode = ND_LAYOUT_STANDARD_HYBRID;
		else {
			nd_printf(player_ref, "Unknown layout mode (use hybrid, expanded, or fullscreen)\n");
			nd_flush(player_ref);
			return;
		}

		if (st_cmd_region(player_ref, argc, argv, 3, &id, &plen) != XY_OK) {
			nd_printf(player_ref, "Invalid region\n");
			nd_flush(player_ref);
			return;
		}
		if (!st_can_region(player_ref, id, plen)) {
			st_refuse_region(player_ref, id, plen);
			return;
		}
		if (!st_row_get(id, plen, &rec)) {
			memset(&rec, 0, sizeof(rec));
			rec.owner = player_ref;
			rec.plen = plen;
		}
		rec.layout_mode = mode;
		st_row_put(id, plen, &rec);
		nd_printf(player_ref, "Set region layout mode to %s\n", lmode);
		nd_flush(player_ref);
		return;
	}

	/* Backward compatibility: driver <name> [region] */
	const char *drv = argv[1];
	int is_choice = (strcmp(drv, "choice") == 0);

	if (st_cmd_region(player_ref, argc, argv, 2, &id, &plen) != XY_OK) {
		nd_printf(player_ref, "Invalid region\n");
		nd_flush(player_ref);
		return;
	}
	if (!st_can_region(player_ref, id, plen)) {
		st_refuse_region(player_ref, id, plen);
		return;
	}
	if (!st_row_get(id, plen, &rec)) {
		memset(&rec, 0, sizeof(rec));
		rec.owner = player_ref;
		rec.plen = plen;
	}
	if (is_choice) {
		rec.driver[0] = '\0';
		rec.pres_flags = ND_REGION_ALLOW_CHOICE;
		rec.flags = ND_REGION_ALLOW_CHOICE;
	} else {
		snprintf(rec.driver, sizeof(rec.driver), "%s", drv);
		rec.pres_flags = ND_REGION_ALLOW_CHOICE;
		rec.flags = ND_REGION_ALLOW_CHOICE;
	}
	st_row_put(id, plen, &rec);
	nd_printf(player_ref, "Set region driver to %s\n", drv);
	nd_flush(player_ref);
}

static void
do_unban(int fd, int argc, char *argv[])
{
	unsigned player_ref = fd_player(fd), victim_ref, banner = NOTHING;
	uint64_t id = 0;
	uint8_t plen = ST_PLEN_ROOT;
	char *name = (argc > 1 && argv[1]) ? argv[1] : "";
	char place[64];
	OBJ victim;

	if (!*name) {
		nd_printf(player_ref, "Usage: unban <player> [world]\n");
		nd_flush(player_ref);
		return;
	}
	victim_ref = player_lookup(name);
	if (victim_ref == NOTHING) {
		nd_printf(player_ref, NOMATCH_MESSAGE);
		nd_flush(player_ref);
		return;
	}
	if (nd_get(HD_OBJ, &victim, &victim_ref) != 0) {
		nd_printf(player_ref, NOMATCH_MESSAGE);
		nd_flush(player_ref);
		return;
	}
	if (st_cmd_region(player_ref, argc, argv, 2, &id, &plen) != XY_OK) {
		nd_printf(player_ref, "Usage: unban <player> [world]\n");
		nd_flush(player_ref);
		return;
	}
	st_ban_place(id, plen, place, sizeof(place));
	if (!st_ban_lookup(victim_ref, id, plen, &banner)) {
		nd_printf(player_ref, "%s is not banned from %s.\n", victim.name, place);
		nd_flush(player_ref);
		return;
	}
	if (banner != player_ref && !st_can_region(player_ref, id, plen)) {
		st_refuse_region(player_ref, id, plen);
		return;
	}
	st_ban_del(victim_ref, id, plen);
	nd_printf(player_ref, "Unbanned %s from %s.\n", victim.name, place);
	nd_printf(victim_ref, "You have been unbanned from %s.\n", place);
	nd_flush(player_ref);
	nd_flush(victim_ref);
}

void
st_cmd_init_xy(struct xy_ctx *ctx)
{
	if (ctx)
		xy = *ctx;
}

void
st_register_commands(void)
{
	nd_register("grant", do_grant, 0);
	nd_register("release", do_release, 0);
	nd_register("region", do_region, 0);
	nd_register("regions", do_regions, 0);
	nd_register("planet", do_planet, 0);
	nd_register("planets", do_planets, 0);
	nd_register("here", do_here, 0);
	nd_register("target", do_target, 0);
	nd_register("deny", do_deny, 0);
	nd_register("loadmod", do_loadmod, 0);
	nd_register("unloadmod", do_unloadmod, 0);
	nd_register("modlist", do_modlist, 0);
	nd_register("ban", do_ban, 0);
	nd_register("unban", do_unban, 0);
	nd_register("driver", do_driver, 0);
}
