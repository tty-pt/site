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

/* World convention hooks called by governance */
XY_DECL(int, map_where, coord_t *, p, unsigned, thing);
XY_DECL(morton_t, map_mwhere, unsigned, thing);
XY_DECL(ENT, ent_get, unsigned, ref);
XY_DECL(int, ent_set, unsigned, ref, ENT *, tmp);

#define NOMATCH_MESSAGE "I don't know what you mean.\n"
#define CANTDO_MESSAGE "You can't do that.\n"

static unsigned owner_hd = CM_MISS;
static unsigned ban_hd = CM_MISS;
static unsigned player_hd = CM_MISS;
static unsigned presence_hd = (unsigned)-1;
static unsigned presence_type = (unsigned)-1;

static int st_row_get(uint64_t id, uint8_t plen, struct st_rec *out);
static void st_row_put(uint64_t id, uint8_t plen, const struct st_rec *rec);

static const char *
region_db_path(void)
{
	const char *db = getenv("AXIL_ND_DB");
	return db && *db ? db : "std.db";
}

static uint32_t
st_rec_init(void)
{
	static int done = 0;
	static uint32_t rec = CM_MISS;

	if (done)
		return rec;
	done = 1;

	rec = corm_record_register("st_rec", sizeof(struct st_rec),
		(corm_record_field_t[]){
			{ .name = "owner", .type = CM_U32,
			  .offset = offsetof(struct st_rec, owner),
			  .max_size = sizeof(uint32_t) },
			{ .name = "plen", .type = CM_U32,
			  .offset = offsetof(struct st_rec, plen),
			  .max_size = sizeof(uint32_t) },
			{ .name = "nmods", .type = CM_U32,
			  .offset = offsetof(struct st_rec, nmods),
			  .max_size = sizeof(uint32_t) },
			{ .name = "flags", .type = CM_U32,
			  .offset = offsetof(struct st_rec, flags),
			  .max_size = sizeof(uint32_t) },
			{ .name = "mods", .type = CM_STR,
			  .offset = offsetof(struct st_rec, mods),
			  .max_size = sizeof(((struct st_rec *)0)->mods) },
		}, 5);
	if (rec == CM_MISS)
		WARN("st_rec_register: corm_record_register failed\n");
	return rec;
}

static void
region_tables_init(void)
{
	const char *db = region_db_path();
	uint32_t rec_id = st_rec_init();

	if (owner_hd == CM_MISS && rec_id != CM_MISS) {
		owner_hd = corm_open(db, "st", CM_STR, corm_record_type_id(rec_id),
			0xFFFF, CM_RECORD(rec_id));
		struct st_rec cosmos;
		if (!st_row_get(0, ST_PLEN_ROOT, &cosmos)) {
			memset(&cosmos, 0, sizeof(cosmos));
			cosmos.owner = 1;
			cosmos.plen = ST_PLEN_ROOT;
			st_row_put(0, ST_PLEN_ROOT, &cosmos);
		}
	}
	if (ban_hd == CM_MISS) {
		ban_hd = corm_open(db, "region_ban", CM_STR, CM_U32, 0xFFFF, 0);
	}
	if (player_hd == CM_MISS) {
		player_hd = corm_open(db, "player", CM_STR, CM_U32, 0xFF, 0);
	}
}

static int
st_have_hd(void)
{
	region_tables_init();
	return owner_hd != CM_MISS;
}

static unsigned
player_lookup(const char *name)
{
	region_tables_init();
	if (player_hd == CM_MISS || !name || !*name)
		return NOTHING;
	const unsigned *ref = corm_get(player_hd, name);
	return ref ? *ref : NOTHING;
}

/* --- Presence table (ref -> region coordinate) --------------------------- */

struct presence {
	uint64_t region_id;
	uint8_t region_plen;
};

static int
presence_have(void)
{
	if (presence_hd != (unsigned)-1 && presence_hd != CM_MISS)
		return 1;
	if (presence_type == (unsigned)-1 || presence_type == CM_MISS)
		presence_type = corm_reg(sizeof(struct presence));
	if (presence_type == (unsigned)-1 || presence_type == CM_MISS)
		return 0;
	presence_hd = corm_open(NULL, "presence", CM_U32, presence_type,
		0xFFFF, 0);
	return presence_hd != (unsigned)-1 && presence_hd != CM_MISS;
}

static inline uint64_t spread3(uint32_t x)
{
    uint64_t v = x & 0xFFFFu;
    v = (v | (v << 32)) & 0x1F00000000FFFFULL;
    v = (v | (v << 16)) & 0x1F0000FF0000FFULL;
    v = (v | (v << 8)) & 0x100F00F00F00F00FULL;
    v = (v | (v << 4)) & 0x10C30C30C30C30C3ULL;
    v = (v | (v << 2)) & 0x1249249249249249ULL;
    return v;
}

static inline uint64_t pos_morton_encode(coord_t p[4])
{
    uint16_t ux = (uint16_t)((int32_t)p[0] + 32767);
    uint16_t uy = (uint16_t)((int32_t)p[1] + 32767);
    uint16_t uz = (uint16_t)((int32_t)p[2] + 32767);
    return spread3(ux) | (spread3(uy) << 1) | (spread3(uz) << 2) | ((uint64_t)(uint16_t)p[3] << 48);
}

static int
region_of_player_compute(unsigned player_ref, uint64_t *id, uint8_t *plen)
{
	OBJ player;
	coord_t pos[4];
	uint64_t code, rid;
	uint8_t w;

	if (nd_get(HD_OBJ, &player, &player_ref) != 0)
		return XY_ERR_NOTFOUND;
	if (map_where(pos, player.location) != 0)
		return XY_ERR_NOTFOUND;
	code = pos_morton_encode(pos);
	rid = xy_region_at(code, ST_PLEN_CELL, &w);
	if (rid == XY_REGION_INVALID)
		return XY_ERR_NOTFOUND;
	*id = rid;
	*plen = w;
	return XY_OK;
}

int
eng_presence_get(unsigned ref, uint64_t *id, uint8_t *plen)
{
	const struct presence *p;

	if (!presence_have() || !id || !plen)
		return 0;
	p = corm_get(presence_hd, &ref);
	if (!p)
		return 0;
	*id = p->region_id;
	*plen = p->region_plen;
	return 1;
}

XY_IMPL(int, presence_refresh, unsigned, ref)
{
	uint64_t id;
	uint8_t plen;
	struct presence p;

	if (!presence_have())
		return 0;
	if (region_of_player_compute(ref, &id, &plen) != XY_OK) {
		corm_del(presence_hd, &ref);
		return 0;
	}
	p.region_id = id;
	p.region_plen = plen;
	corm_put(presence_hd, &ref, &p);
	return 0;
}

XY_IMPL(int, presence_forget, unsigned, ref)
{
	if (!presence_have())
		return 0;
	corm_del(presence_hd, &ref);
	return 0;
}

XY_IMPL(int, st_region_of_player, unsigned, player_ref, uint64_t *, id,
	uint8_t *, plen)
{
	uint64_t cid, pid;
	uint8_t cplen, pplen;
	int crc;

	if (!id || !plen)
		return XY_ERR_INVALID;
	if (player_ref == NOTHING || player_ref == (unsigned)-1)
		return XY_ERR_INVALID;

	if (eng_presence_get(player_ref, &pid, &pplen)) {
		crc = region_of_player_compute(player_ref, &cid, &cplen);
		if (crc != XY_OK) {
			presence_forget(player_ref);
			return crc;
		}
		if (cid != pid || cplen != pplen) {
			WARN("presence mismatch for %u: stored (%llx,%u) != computed (%llx,%u)\n",
				player_ref,
				(unsigned long long)pid, (unsigned)pplen,
				(unsigned long long)cid, (unsigned)cplen);
			*id = cid;
			*plen = cplen;
			return XY_OK;
		}
		*id = pid;
		*plen = pplen;
		return XY_OK;
	}

	crc = region_of_player_compute(player_ref, &cid, &cplen);
	if (crc != XY_OK)
		return crc;
	presence_refresh(player_ref);
	*id = cid;
	*plen = cplen;
	return XY_OK;
}

/* --- Row CRUD ------------------------------------------------------------ */

XY_IMPL(unsigned, st_owner, uint64_t, id, uint8_t, plen)
{
	char key[ST_ROW_KEY_LEN + 1];
	char q[ST_ROW_KEY_LEN + 1 + sizeof(":owner")];
	const uint32_t *o;

	if (!st_have_hd())
		return NOTHING;
	st_row_key(key, sizeof(key), id, plen);
	snprintf(q, sizeof(q), "%s:owner", key);
	o = corm_get(owner_hd, q);
	if (!o)
		return NOTHING;
	return *o;
}

static int
st_can(unsigned ref, uint64_t id, uint8_t plen)
{
	return st_owner(id, plen) == ref;
}

static int
st_row_get(uint64_t id, uint8_t plen, struct st_rec *out)
{
	char key[ST_ROW_KEY_LEN + 1];
	const struct st_rec *r;

	if (!st_have_hd())
		return 0;
	st_row_key(key, sizeof(key), id, plen);
	r = corm_get(owner_hd, key);
	if (!r)
		return 0;
	if (out)
		memcpy(out, r, sizeof(*out));
	return 1;
}

static void
st_row_put(uint64_t id, uint8_t plen, const struct st_rec *rec)
{
	char key[ST_ROW_KEY_LEN + 1];

	if (!st_have_hd() || !rec)
		return;
	st_row_key(key, sizeof(key), id, plen);
	corm_put(owner_hd, key, rec);
}

static void
st_row_del(uint64_t id, uint8_t plen)
{
	char key[ST_ROW_KEY_LEN + 1];

	if (!st_have_hd())
		return;
	st_row_key(key, sizeof(key), id, plen);
	corm_del(owner_hd, key);
}

/* --- Trampoline loaders -------------------------------------------------- */

static int
st_load_tramp(void *ud)
{
	return xy_load((char *)ud);
}

static int
st_unload_tramp(void *ud)
{
	return xy_unload((char *)ud);
}

static int
st_region_load(uint64_t id, uint8_t plen, char *name)
{
	int rc;

	if (!name || !*name || strchr(name, '/'))
		return XY_ERR_INVALID;
	rc = xy_claim_at(id, plen, NULL, NULL);
	if (rc != XY_OK)
		return rc;
	rc = xy_with_region(id, plen, st_load_tramp, name);
	xy_claim_at(0, 0, NULL, NULL);
	return rc;
}

static int
st_region_unload(uint64_t id, uint8_t plen, char *name)
{
	int rc;

	if (!name || !*name || strchr(name, '/'))
		return XY_ERR_INVALID;
	rc = xy_with_region(id, plen, st_unload_tramp, name);
	xy_claim_at(0, 0, NULL, NULL);
	return rc;
}

static int
st_mod_loaded(uint64_t id, uint8_t plen, const char *name)
{
	struct st_rec rec;
	uint32_t i;

	if (!name || !st_row_get(id, plen, &rec))
		return 0;
	for (i = 0; i < rec.nmods && i < ND_ST_MAX_MODS; i++) {
		if (strncmp(rec.mods[i], name, ND_ST_MOD_NAME) == 0)
			return 1;
	}
	return 0;
}

static int
st_mod_add(uint64_t id, uint8_t plen, const char *name)
{
	struct st_rec rec;

	if (!name || !*name || strchr(name, '/') ||
	    strlen(name) >= ND_ST_MOD_NAME)
		return XY_ERR_INVALID;
	if (!st_row_get(id, plen, &rec))
		return XY_ERR_NOTFOUND;
	if (st_mod_loaded(id, plen, name))
		return XY_OK;
	if (rec.nmods >= ND_ST_MAX_MODS)
		return XY_ERR_TOOBIG;
	memset(rec.mods[rec.nmods], 0, ND_ST_MOD_NAME);
	snprintf(rec.mods[rec.nmods], ND_ST_MOD_NAME, "%s", name);
	rec.nmods++;
	st_row_put(id, plen, &rec);
	return XY_OK;
}

static int
st_mod_del(uint64_t id, uint8_t plen, const char *name)
{
	struct st_rec rec;
	uint32_t i, j;

	if (!name || !st_row_get(id, plen, &rec))
		return XY_ERR_NOTFOUND;
	for (i = 0; i < rec.nmods && i < ND_ST_MAX_MODS; i++) {
		if (strncmp(rec.mods[i], name, ND_ST_MOD_NAME) == 0)
			break;
	}
	if (i >= rec.nmods || i >= ND_ST_MAX_MODS)
		return XY_ERR_NOTFOUND;
	for (j = i; j + 1 < rec.nmods && j + 1 < ND_ST_MAX_MODS; j++)
		memcpy(rec.mods[j], rec.mods[j + 1], ND_ST_MOD_NAME);
	rec.nmods--;
	memset(rec.mods[rec.nmods], 0, ND_ST_MOD_NAME);
	st_row_put(id, plen, &rec);
	return XY_OK;
}

/* --- Authority & Scoping ------------------------------------------------- */

XY_IMPL(int, st_can_region, unsigned, player_ref, uint64_t, id, uint8_t, plen)
{
	uint64_t mask;
	unsigned w;

	if (plen > ST_PLEN_CELL)
		return 0;

	for (w = 0; w <= plen; w++) {
		mask = w ? (~0ULL << (64 - w)) : 0ULL;
		unsigned o = st_owner(id & mask, (uint8_t)w);
		if (o != NOTHING) {
			WARN("st_can_region: player=%u check w=%u id=%llx owner=%u\n",
				player_ref, w, (unsigned long long)(id & mask), o);
		}
		if (o == player_ref)
			return 1;
	}
	return 0;
}

XY_IMPL(unsigned, st_region_ruler, uint64_t, id, uint8_t, plen)
{
	uint64_t mask;
	unsigned w;

	if (plen > ST_PLEN_CELL)
		return NOTHING;

	for (w = (unsigned)plen + 1; w-- > 0; ) {
		mask = w ? (~0ULL << (64 - w)) : 0ULL;
		unsigned owner = st_owner(id & mask, (uint8_t)w);

		if (owner != NOTHING)
			return owner;
	}
	return NOTHING;
}

XY_IMPL(int, st_region_covers, uint64_t, o_id, uint8_t, o_plen, uint64_t, i_id,
	uint8_t, i_plen)
{
	uint64_t mask;

	if (o_plen > i_plen)
		return 0;
	mask = o_plen ? (~0ULL << (64 - o_plen)) : 0ULL;
	return (i_id & mask) == o_id;
}

XY_IMPL(int, st_in_scope, unsigned, actor, uint64_t, sel_id, uint8_t, sel_plen,
	uint64_t, id, uint8_t, plen)
{
	unsigned c;
	const void *kp, *vp;

	if (sel_plen != ST_SEL_UNSET)
		return st_region_covers(sel_id, sel_plen, id, plen);

	if (!st_have_hd())
		return 0;
	c = corm_iter(owner_hd, NULL, 0);
	while (corm_next(&kp, &vp, c)) {
		const char *key = kp;
		const struct st_rec *rec = vp;
		uint64_t rid;
		uint8_t rplen;

		if (st_row_key_parse(key, &rid, &rplen) != 0)
			continue;
		if (rec->plen != rplen || rec->owner != actor)
			continue;
		if (st_region_covers(rid, rplen, id, plen)) {
			corm_fin(c);
			return 1;
		}
	}
	corm_fin(c);
	return 0;
}

XY_IMPL(int, st_region_of_obj, unsigned, ref, uint64_t *, id, uint8_t *, plen)
{
	unsigned cur = ref;
	int depth;

	if (!id || !plen)
		return XY_ERR_INVALID;

	for (depth = 0; depth < 8; depth++) {
		OBJ obj;
		if (nd_get(HD_OBJ, &obj, &cur) != 0)
			return XY_ERR_NOTFOUND;
		if (obj.type == TYPE_ROOM) {
			coord_t pos[4];
			if (map_where(pos, cur) != 0) {
				*id = 0;
				*plen = ST_PLEN_ROOT;
				return XY_OK;
			}
			uint64_t code = pos_morton_encode(pos);
			uint8_t w;
			uint64_t rid = xy_region_at(code, ST_PLEN_CELL, &w);
			if (rid == XY_REGION_INVALID) {
				*id = 0;
				*plen = ST_PLEN_ROOT;
				return XY_OK;
			}
			*id = rid;
			*plen = w;
			return XY_OK;
		}
		if (obj.location == cur || obj.location == NOTHING)
			return XY_ERR_NOTFOUND;
		cur = obj.location;
	}
	return XY_ERR_NOTFOUND;
}

/* --- Ruler Name & Refusal ------------------------------------------------ */

static void
st_owner_name(unsigned owner_ref, char *buf, size_t len)
{
	OBJ o;

	if (nd_get(HD_OBJ, &o, &owner_ref) == 0) {
		snprintf(buf, len, "%s", o.name);
	} else {
		snprintf(buf, len, "%u", owner_ref);
	}
}

XY_IMPL(int, st_refuse_region, unsigned, player_ref, uint64_t, id, uint8_t, plen)
{
	char oname[64];
	unsigned ruler = st_region_ruler(id, plen);

	if (ruler == NOTHING)
		nd_printf(player_ref, "Permission denied (this area is unclaimed)\n");
	else {
		st_owner_name(ruler, oname, sizeof(oname));
		nd_printf(player_ref, "Permission denied (ruled by %s)\n", oname);
	}
	nd_flush(player_ref);
	return 0;
}

/* --- Bans ---------------------------------------------------------------- */

#define BAN_KEY_LEN 32

static void
st_ban_key(char *k, size_t len, unsigned player, uint64_t id, uint8_t plen)
{
	snprintf(k, len, "%010u:%016llx:%02u", player,
		(unsigned long long)id, plen);
}

static int
st_ban_have(void)
{
	region_tables_init();
	return ban_hd != CM_MISS;
}

static void
st_ban_put(unsigned player_ref, uint64_t id, uint8_t plen, unsigned banner)
{
	char key[64];

	if (!st_ban_have())
		return;
	st_ban_key(key, sizeof(key), player_ref, id, plen);
	corm_put(ban_hd, key, &banner);
}

static int
st_ban_lookup(unsigned player_ref, uint64_t id, uint8_t plen, unsigned *banner)
{
	char key[64];
	const uint32_t *v;

	if (!st_ban_have())
		return 0;
	st_ban_key(key, sizeof(key), player_ref, id, plen);
	v = corm_get(ban_hd, key);
	if (!v)
		return 0;
	if (banner)
		*banner = *v;
	return 1;
}

static void
st_ban_del(unsigned player_ref, uint64_t id, uint8_t plen)
{
	char key[64];

	if (!st_ban_have())
		return;
	st_ban_key(key, sizeof(key), player_ref, id, plen);
	corm_del(ban_hd, key);
}

static void
st_ban_place(uint64_t id, uint8_t plen, char *buf, size_t len)
{
	if (plen == ST_PLEN_ROOT)
		snprintf(buf, len, "everywhere");
	else if (plen == ST_PLEN_WORLD)
		snprintf(buf, len, "world %u", st_world_of(id));
	else
		snprintf(buf, len, "this region");
}

/* --- Target Selection Helpers -------------------------------------------- */

static int
st_cmd_world(const char *tok, uint64_t *id, uint8_t *plen)
{
	char *end = NULL;
	unsigned long w;

	if (!tok || !*tok || !id || !plen)
		return XY_ERR_INVALID;
	if (strcmp(tok, "cosmos") == 0) {
		*id = 0;
		*plen = ST_PLEN_ROOT;
		return XY_OK;
	}
	w = strtoul(tok, &end, 10);
	if (!end || end == tok || *end || w > 65535)
		return XY_ERR_INVALID;
	*id = st_planet_id((unsigned)w);
	*plen = ST_PLEN_WORLD;
	return XY_OK;
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
	*cell = pos_morton_encode(pos);
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
	*cell = pos_morton_encode(pos);
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

	xy_claim_at(id, plen, NULL, NULL);
	xy_claim_at(0, 0, NULL, NULL);

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
	int claim_rc = xy_claim_at(id, plen, NULL, NULL);
	xy_claim_at(0, 0, NULL, NULL);
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

	int claim_rc = xy_claim_at(id, plen, NULL, NULL);
	xy_claim_at(0, 0, NULL, NULL);
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

	rc = xy_claim_at(id, plen, NULL, NULL);
	if (rc == XY_OK)
		rc = xy_with_region(id, plen, st_deny_tramp, &a);
	xy_claim_at(0, 0, NULL, NULL);
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
	char *name = (argc > 1 && argv[1]) ? argv[1] : "";
	uint64_t id = 0;
	uint8_t plen = ST_PLEN_ROOT;
	char namebuf[ND_ST_MOD_NAME + 1];
	int rc;

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
	char *name = (argc > 1 && argv[1]) ? argv[1] : "";
	uint64_t id = 0;
	uint8_t plen = ST_PLEN_ROOT;
	char namebuf[ND_ST_MOD_NAME + 1];
	int rc;

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

/* --- Boot restore (st_init) ---------------------------------------------- */

struct st_restore_row {
	uint64_t id;
	uint8_t plen;
	struct st_rec rec;
};

static int
st_restore_cmp(const void *a, const void *b)
{
	const struct st_restore_row *ra = a;
	const struct st_restore_row *rb = b;
	if (ra->plen != rb->plen)
		return ra->plen < rb->plen ? -1 : 1;
	if (ra->id != rb->id)
		return ra->id < rb->id ? -1 : 1;
	return 0;
}

static void
st_init(void)
{
	struct st_restore_row *rows = NULL;
	size_t n = 0, cap = 0;
	unsigned c;
	const void *kp, *vp;

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
		if (n >= cap) {
			size_t ncap = cap ? cap * 2 : 16;
			struct st_restore_row *nrows = realloc(rows, ncap * sizeof(*rows));
			if (!nrows)
				break;
			rows = nrows;
			cap = ncap;
		}
		rows[n].id = id;
		rows[n].plen = plen;
		memcpy(&rows[n].rec, rec, sizeof(rows[n].rec));
		n++;
	}
	corm_fin(c);

	if (rows) {
		qsort(rows, n, sizeof(*rows), st_restore_cmp);
		for (size_t i = 0; i < n; i++) {
			char namebuf[ND_ST_MOD_NAME + 1];
			xy_claim_at(rows[i].id, rows[i].plen, NULL, NULL);
			WARN("st_restore: region id=0x%016llx plen=%u\n",
				(unsigned long long)rows[i].id, rows[i].plen);
			for (uint32_t m = 0; m < rows[i].rec.nmods && m < ND_ST_MAX_MODS; m++) {
				memcpy(namebuf, rows[i].rec.mods[m], ND_ST_MOD_NAME);
				namebuf[ND_ST_MOD_NAME] = '\0';
				if (!namebuf[0])
					continue;
				if (st_region_load(rows[i].id, rows[i].plen, namebuf) != XY_OK)
					continue;
				WARN("st_restore: loaded %s\n", namebuf);
			}
		}
		free(rows);
	}
	xy_claim_at(0, 0, NULL, NULL);
}

XY_IMPL(int, on_axil_post_chroot, void)
{
	st_init();
	return 0;
}

/* --- Module Entry Point -------------------------------------------------- */

XY_MODULE_API void
xy_install(void)
{
	WARN("nd-region: governance module installed from '%s'\n", xy.module_path);
	region_tables_init();

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
}
