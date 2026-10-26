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
#include "region_cmd.h"

/* World convention hooks called by governance */
XY_DECL(int, map_where, coord_t *, p, unsigned, thing);
XY_DECL(morton_t, map_mwhere, unsigned, thing);
XY_DECL(ENT, ent_get, unsigned, ref);
XY_DECL(int, ent_set, unsigned, ref, ENT *, tmp);

#define NOMATCH_MESSAGE "I don't know what you mean.\n"
#define CANTDO_MESSAGE "You can't do that.\n"

unsigned owner_hd = CM_MISS;
unsigned ban_hd = CM_MISS;
unsigned player_hd = CM_MISS;
unsigned presence_hd = (unsigned)-1;
static unsigned presence_type = (unsigned)-1;

int st_row_get(uint64_t id, uint8_t plen, struct st_rec *out);
void st_row_put(uint64_t id, uint8_t plen, const struct st_rec *rec);

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
			{ .name = "layout_mode", .type = CM_U32,
			  .offset = offsetof(struct st_rec, layout_mode),
			  .max_size = sizeof(uint32_t) },
			{ .name = "pres_flags", .type = CM_U32,
			  .offset = offsetof(struct st_rec, pres_flags),
			  .max_size = sizeof(uint32_t) },
			{ .name = "driver", .type = CM_STR,
			  .offset = offsetof(struct st_rec, driver),
			  .max_size = sizeof(((struct st_rec *)0)->driver) },
			{ .name = "map_driver", .type = CM_STR,
			  .offset = offsetof(struct st_rec, map_driver),
			  .max_size = sizeof(((struct st_rec *)0)->map_driver) },
			{ .name = "mods", .type = CM_STR,
			  .offset = offsetof(struct st_rec, mods),
			  .max_size = sizeof(((struct st_rec *)0)->mods) },
		}, 9);
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

int
st_have_hd(void)
{
	region_tables_init();
	return owner_hd != CM_MISS;
}

unsigned
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
	struct st_rec rec;
	if (!st_row_get(id, plen, &rec))
		return NOTHING;
	return rec.owner;
}

int
st_can(unsigned ref, uint64_t id, uint8_t plen)
{
	return st_owner(id, plen) == ref;
}

int
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

void
st_row_put(uint64_t id, uint8_t plen, const struct st_rec *rec)
{
	char key[ST_ROW_KEY_LEN + 1];

	if (!st_have_hd() || !rec)
		return;
	st_row_key(key, sizeof(key), id, plen);
	corm_put(owner_hd, key, rec);
}

void
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

int
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

int
st_region_unload(uint64_t id, uint8_t plen, char *name)
{
	int rc;

	if (!name || !*name || strchr(name, '/'))
		return XY_ERR_INVALID;
	rc = xy_with_region(id, plen, st_unload_tramp, name);
	xy_claim_at(0, 0, NULL, NULL);
	return rc;
}

int
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

int
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

int
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

void
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

XY_IMPL(int, st_resolve_presentation_policy, uint64_t, id, uint8_t, plen,
	char *, out_stage, size_t, stage_len,
	char *, out_map, size_t, map_len,
	uint8_t *, out_layout,
	uint8_t *, out_flags)
{
	uint64_t mask;
	unsigned w;
	char res_stage[16] = "";
	char res_map[16] = "ascii-bud";
	uint8_t res_layout = ND_LAYOUT_STANDARD_HYBRID;
	uint8_t res_flags = ND_REGION_ALLOW_CHOICE;
	int stage_locked = 0;
	int map_locked = 0;
	int layout_locked = 0;

	if (plen > ST_PLEN_CELL)
		plen = ST_PLEN_CELL;

	/* Walk prefix lengths from cosmos (0) down to cell (plen).
	 * If a higher authority locks an attribute (ND_REGION_LOCKED),
	 * lower levels cannot override it.
	 * If not locked, subordinate regions can refine or override. */
	for (w = 0; w <= plen; w++) {
		struct st_rec rec;
		mask = w ? (~0ULL << (64 - w)) : 0ULL;
		if (!st_row_get(id & mask, (uint8_t)w, &rec))
			continue;

		uint32_t eff_flags = rec.pres_flags | rec.flags;

		/* Stage driver resolution */
		if (!stage_locked) {
			if (rec.driver[0]) {
				snprintf(res_stage, sizeof(res_stage), "%s", rec.driver);
				if (eff_flags & ND_REGION_LOCKED) {
					stage_locked = 1;
					res_flags |= ND_REGION_LOCKED;
					res_flags &= ~ND_REGION_ALLOW_CHOICE;
				} else if (eff_flags & ND_REGION_ALLOW_CHOICE) {
					res_flags |= ND_REGION_ALLOW_CHOICE;
				}
			} else if (eff_flags & ND_REGION_ALLOW_CHOICE) {
				res_flags |= ND_REGION_ALLOW_CHOICE;
			}
		}

		/* Map driver resolution */
		if (!map_locked) {
			if (rec.map_driver[0]) {
				snprintf(res_map, sizeof(res_map), "%s", rec.map_driver);
				if (eff_flags & ND_REGION_LOCKED)
					map_locked = 1;
			}
		}

		/* Layout mode resolution */
		if (!layout_locked) {
			if (rec.layout_mode != 0) {
				res_layout = (uint8_t)rec.layout_mode;
				if (eff_flags & ND_REGION_LOCKED)
					layout_locked = 1;
			}
		}
	}

	if (out_stage && stage_len > 0)
		snprintf(out_stage, stage_len, "%s", res_stage[0] ? res_stage : "tty");
	if (out_map && map_len > 0)
		snprintf(out_map, map_len, "%s", res_map[0] ? res_map : "ascii-bud");
	if (out_layout)
		*out_layout = res_layout;
	if (out_flags)
		*out_flags = res_flags;

	return XY_OK;
}

XY_IMPL(int, st_player_resolve_presentation, unsigned, player_ref,
	char *, out_stage, size_t, stage_len,
	char *, out_map, size_t, map_len,
	uint8_t *, out_layout,
	uint8_t *, out_flags)
{
	uint64_t id;
	uint8_t plen;
	if (st_region_of_player(player_ref, &id, &plen) != XY_OK) {
		if (out_stage && stage_len > 0)
			snprintf(out_stage, stage_len, "tty");
		if (out_map && map_len > 0)
			snprintf(out_map, map_len, "ascii-bud");
		if (out_layout)
			*out_layout = ND_LAYOUT_STANDARD_HYBRID;
		if (out_flags)
			*out_flags = ND_REGION_ALLOW_CHOICE;
		return XY_OK;
	}
	return st_resolve_presentation_policy(id, plen, out_stage, stage_len,
		out_map, map_len, out_layout, out_flags);
}

XY_IMPL(int, st_region_get_driver, uint64_t, id, uint8_t, plen, char *, out_driver, size_t, maxlen)
{
	if (!out_driver || maxlen == 0)
		return XY_ERR_INVALID;
	return st_resolve_presentation_policy(id, plen, out_driver, maxlen, NULL, 0, NULL, NULL);
}

XY_IMPL(int, st_region_set_driver, uint64_t, id, uint8_t, plen, const char *, driver)
{
	struct st_rec rec;
	if (!st_row_get(id, plen, &rec)) {
		memset(&rec, 0, sizeof(rec));
		rec.owner = 1;
		rec.plen = plen;
	}
	if (!driver || !*driver || strcmp(driver, "choice") == 0) {
		rec.driver[0] = '\0';
		rec.pres_flags |= ND_REGION_ALLOW_CHOICE;
		rec.pres_flags &= ~ND_REGION_LOCKED;
	} else {
		snprintf(rec.driver, sizeof(rec.driver), "%s", driver);
	}
	st_row_put(id, plen, &rec);
	return XY_OK;
}

XY_IMPL(int, st_player_get_driver, unsigned, player_ref, char *, out_driver, size_t, maxlen)
{
	if (!out_driver || maxlen == 0)
		return XY_ERR_INVALID;
	return st_player_resolve_presentation(player_ref, out_driver, maxlen, NULL, 0, NULL, NULL);
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

void
st_ban_put(unsigned player_ref, uint64_t id, uint8_t plen, unsigned banner)
{
	char key[64];

	if (!st_ban_have())
		return;
	st_ban_key(key, sizeof(key), player_ref, id, plen);
	corm_put(ban_hd, key, &banner);
}

int
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

void
st_ban_del(unsigned player_ref, uint64_t id, uint8_t plen)
{
	char key[64];

	if (!st_ban_have())
		return;
	st_ban_key(key, sizeof(key), player_ref, id, plen);
	corm_del(ban_hd, key);
}

void
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

int
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

int
st_xy_claim_at(uint64_t id, uint8_t plen)
{
	return xy_claim_at(id, plen, NULL, NULL);
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
	st_cmd_init_xy(&xy);
	st_register_commands();
}
