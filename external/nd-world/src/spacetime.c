#include <nd/st.h>

#include <ctype.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <pwd.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <syslog.h>
#include <time.h>
#include <ttypt/corm.h>
#include <ttypt/qsys.h>
/* Host-side region API. NOT xy-mod.h: that header redefines xy_load and
 * friends as macros through a module-local `static struct xy_ctx xy`, and
 * this TU is engine code with no injected context -- including it here would
 * be the two-TU trap (each TU gets its own xy, only one ever injected).
 * The host globals below resolve to the axil process at dlopen time, the same
 * way axil_register already does from world.c and mods.c. */
#include <ttypt/xy.h>
#include <xxhash.h>

#include <nd/config.h>
#include <nd/noise.h>
#include <nd/params.h>
#include <nd/player.h>
#include <nd/view.h>
#include <nd/mcp.h>
#include <nd/obj-types.h>
#include <nd/map.h>
#include <nd/type.h>
#include <nd/entity.h>
#include <nd/match.h>

unsigned fd_player(unsigned fd);

/* eng_map_where has no header declaration anywhere (spacetime.c:226 and
 * friends already call it unprototyped); say it out loud so the region anchor
 * below does not add another implicit declaration. */
void eng_map_where(pos_t p, unsigned thing);

#define PRECOVERY

#define GEON_RADIUS (VIEW_AROUND + 1)
#define GEON_SIZE (GEON_RADIUS * 2 + 1)
#define GEON_M (GEON_SIZE * GEON_SIZE)
#define GEON_BDI (GEON_SIZE * (GEON_SIZE - 1))

#define ROOM_COST 80


void
nd_dowritef(unsigned player_ref, const char *fmt, va_list args)
{
	char buf[1024];
	int n = vsnprintf(buf, sizeof(buf), fmt, args);
	OBJ player;
	if (n <= 0 || nd_get(HD_OBJ, &player, &player_ref) != 0)
		return;
	nd_rwrite(player.location, player_ref, buf, (size_t)n < sizeof(buf) ? (size_t)n : sizeof(buf) - 1);
}

typedef void op_a_t(unsigned player_ref, enum exit e);
typedef int op_b_t(unsigned player_ref, struct cmd_dir cd);
typedef struct {
	union {
		op_a_t *a;
		op_b_t *b;
	} op;
	int type;
} op_t;
unsigned g_player_ref;
OBJ *g_player;
static unsigned *biome_map;

enum exit e_map[] = {
	[0 ... 254] = E_NULL,
	['h'] = E_WEST,
	['w'] = E_WEST,
	['j'] = E_SOUTH,
	['s'] = E_SOUTH,
	['k'] = E_NORTH,
	['n'] = E_NORTH,
	['l'] = E_EAST,
	['e'] = E_EAST,
	['K'] = E_UP,
	['u'] = E_UP,
	['J'] = E_DOWN,
	['d'] = E_DOWN,
};

exit_t exit_map[] = {
	[0 ... E_ALL] = {
		.simm = E_NULL,
		.name = "",
		.other = "",
		.dim = 5,
	},
	[E_EAST] = {
		.simm = E_WEST,
		.name = "east",
		.other = "wnsud",
		.dim = 1, .dis = 1,
	},
	[E_SOUTH] = {
		.simm = E_NORTH,
		.name = "south",
		.other = "ewudn",
		.dim = 0, .dis = 1,
	},
	[E_WEST] = {
		.simm = E_EAST,
		.name = "west",
		.other = "nsude",
		.dim = 1, .dis = -1,
	}, 
	[E_NORTH] = {
		.simm = E_SOUTH,
		.name = "north",
		.other = "sewud",
		.dim = 0, .dis = -1,
	},
	[E_UP] = {
		.simm = E_DOWN,
		.name = "up",
		.other = "dnsew",
		.dim = 2, .dis = 1,
	},
	[E_DOWN] = {
		.simm = E_UP,
		.name = "down",
		.other = "nsewu",
		.dim = 2, .dis = -1,
	},
};

unsigned long long day_tick = 0;
/* sub-tick remainder: nd_update() runs the world at 0.5 ticks per second, and
 * day_tick is an integer, so the halves have to be carried or day_tick would
 * truncate to 0 forever. Private, and not persisted. */
static double day_tick_frac = 0;
unsigned short day_n = 0;
double tick = 0;

static inline coord_t
sign(ucoord_t n)
{
	return (smorton_t) n - COORD_MAX;
}

/* compact_axis(): collect one out of every 3 bits from 'code',
 * starting at 'shift' (0 = x, 1 = y, 2 = z).  Returns low-order
 * 21 bits containing that coordinate.                         */
static inline uint32_t compact_axis(uint64_t code, unsigned shift)
{
    code >>= shift; /* align the desired series to LSB */
    /* first keep only 1---1---1 pattern → mask 0x1249249249249… */
    code &= 0x1249249249249249ULL;

    /* Now collapse gaps:  3→2 → 2→1 → 1→0 */
    code = (code ^ (code >> 2))  & 0x10C30C30C30C30C3ULL;
    code = (code ^ (code >> 4))  & 0x100F00F00F00F00FULL;
    code = (code ^ (code >> 8))  & 0x1F0000FF0000FFULL;
    code = (code ^ (code >> 16)) & 0x1F00000000FFFFULL;
    code = (code ^ (code >> 32)) & 0x00000000001FFFFFULL;

    return (uint32_t) code; /* low 21 bits hold the axis value */
}

static inline void decode3(uint64_t code,
                           uint32_t *x, uint32_t *y, uint32_t *z)
{
    *x = compact_axis(code, 0);   /* bits 0,3,6,…   */
    *y = compact_axis(code, 1);   /* bits 1,4,7,…   */
    *z = compact_axis(code, 2);   /* bits 2,5,8,…   */
}

void
morton_pos(pos_t p, morton_t code)
{
	static const morton_t mask_off = 0x0000FFFFFFFFFFFFULL;
	uint32_t uup[3] = { 0, 0, 0 };
	decode3(code & mask_off, &uup[0], &uup[1], &uup[2]);
	p[0] = sign(uup[0]);
	p[1] = sign(uup[1]);
	p[2] = sign(uup[2]);
	p[3] = OBITS(code);
}

void
eng_object_drop(unsigned where_ref, unsigned skel_id)
{
	pos_t pos;
        register int i;
	unsigned drop_id;
	OBJ where;
	const void *__v, *kp, *vp;
	unsigned c = corm_iter(nd_table_handle(HD_ADROP), &skel_id, CM_RANGE);

	nd_get(HD_OBJ, &(where), &where_ref);
	eng_map_where(pos, where.location);

	while (corm_next(&kp, &vp, c)) {
		DROP drop;
		skel_id = *(const unsigned *)kp;
		drop_id = *(const unsigned *)vp;
		nd_get(HD_DROP, &(drop), &drop_id);
		if (random() < (RAND_MAX >> drop.y)) {
			uint32_t v2 = XXH32((const char *) pos, sizeof(pos_t), 3);
                        int yield = drop.yield,
                            yield_v = drop.yield_v;

                        if (!yield) {
				OBJ obj;
				unsigned obj_ref = eng_object_add(&obj, drop.skel, where_ref, v2, 0);
				corm_put(obj_hd, &obj_ref, &obj);
                                continue;
                        }

                        yield += random() & yield_v;

                        for (i = 0; i < yield; i++) {
				v2 = XXH32((const char *) pos, sizeof(pos_t), 4 + i);
				OBJ obj;
                                unsigned obj_ref = eng_object_add(&obj, drop.skel, where_ref, v2, 0);
				corm_put(obj_hd, &obj_ref, &obj);
			}
                }
	}
}

int
e_exit_can(OBJ *player, enum exit e) {
	return e_ground(player->location, e);
}

int
e_ground(unsigned room, enum exit e)
{
	pos_t pos;

	if (e & (E_UP | E_DOWN))
		return 0;

	eng_map_where(pos, room);
	return pos[2] == 0;
}

enum exit
dir_e(const char dir) {
	return e_map[(int) dir];
}

char
e_dir(enum exit e) {
	return exit_map[e].name[0];
}

char *
e_other(enum exit e) {
	return (char *) exit_map[e].other;
}

morton_t
point_rel_idx(point_t p, point_t s, smorton_t w)
{
	smorton_t s0 = s[Y_COORD],
		s1 = s[X_COORD];
	if (s0 > p[Y_COORD])
		s0 -= UCOORD_MAX;
	if (s1 > p[X_COORD])
		s1 -= UCOORD_MAX;
	return (p[Y_COORD] - s0) * w + (p[X_COORD] - s1);
}

void
st_update(double dt)
{
	const char * msg = NULL;
	day_tick_frac += dt;
	unsigned long long whole = (unsigned long long) day_tick_frac;
	day_tick_frac -= (double) whole;
	day_tick += whole;
	if (day_tick > (1ULL << DAYTICK_Y))
		day_tick = 0;

	if (day_tick >= (1ULL << (DAYTICK_Y - 1))) {
		if (day_n)
			return;
		msg = "The sun sets.\n";
		day_n = 1;
	} else if (day_n) {
		msg = "The sun rises.\n";
		day_n = 0;
	} else
		return;

	unsigned c = corm_iter(obj_hd, NULL, 0);
	OBJ iobj;
	unsigned iobj_ref;
	const void *kp, *vp;

	while (corm_next(&kp, &vp, c)) {
		iobj_ref = *(const unsigned *)kp;
		iobj = *(const OBJ *)vp;

		if (iobj.type == TYPE_ENTITY) {
			view(iobj_ref);
			mcp_tod(iobj_ref, day_n);
			nd_writef(iobj_ref, msg);
		}
	}
}

static inline void
st_pos(pos_t p, unsigned loc, enum exit e)
{
	pos_t aux;
	eng_map_where(aux, loc);
	pos_move(p, aux, e);
}

static unsigned
st_there(unsigned where, enum exit e)
{
	pos_t pos;
	st_pos(pos, where, e);
	return eng_map_get(pos);
}

static inline int
fee_fail(unsigned player_ref, OBJ *player, char *desc, char *info, unsigned cost)
{
	if (player->value < cost) {
		nd_writef(player_ref, "You can't afford to %s. (%dp)\n", desc, cost);
		return 1;
	} else {
		player->value -= cost;
		nd_writef(player_ref, "%s (-%dp). %s\n",
			   desc, cost, info);
		return 0;
	}
}

static int
st_claim(unsigned player_ref, OBJ *room) {
	ROO *rroom = (ROO *) &room->data;

	if (!(rroom->flags & RF_TEMP)) {
		if (room->owner != player_ref) {
			nd_writef(player_ref, "You don't own this room\n");
			return 1;
		}

		return 0;
	}

	OBJ player;
	corm_get_copy(obj_hd, &player_ref, &(player));
	if (fee_fail(player_ref, &player, "claim a room", "", ROOM_COST))
		return 1;

	corm_put(obj_hd, &player_ref, &player);
	rroom->flags ^= RF_TEMP;
	room->owner = player_ref;

	return 0;
}

static inline void
exits_fix(unsigned there_ref, enum exit e)
{
	OBJ there;
	corm_get_copy(obj_hd, &there_ref, &(there));
	ROO *rthere = (ROO *) &there.data;
	const char *s;

	for (s = e_other(e); *s; s++) {
		enum exit e2 = dir_e(*s);
		unsigned othere_ref = st_there(there_ref, e2);

		if (othere_ref == NOTHING)
			continue;

		OBJ othere;
		corm_get_copy(obj_hd, &othere_ref, &(othere));
		ROO *rothere = (ROO *) &othere.data;

		if (rothere->flags & RF_TEMP) {
			continue;
		}

		if (!(rthere->exits & e2)) {
			if (!(rothere->exits & e_simm(e2)))
				continue;

			rthere->exits |= e2;
			continue;
		}

		if (!(rothere->exits & e_simm(e2)))
			rthere->exits &= ~e2;
	}

	corm_put(obj_hd, &there_ref, &there);
}

static void
exits_infer(unsigned here_ref, ROO *rhere)
{
	const char *s = "wsnedu";

	for (; *s; s++) {
		enum exit e = dir_e(*s);
		unsigned there_ref = st_there(here_ref, e);

		if (there_ref == NOTHING) {
                        if (e != E_UP && e != E_DOWN)
				rhere->exits |= e;
			continue;
		}

		OBJ there;
		corm_get_copy(obj_hd, &there_ref, &(there));
		ROO *rthere = (ROO *) &there.data;

		if (rthere->exits & e_simm(e)) {
			rhere->exits |= e;
			rhere->doors |= rthere->doors & e_simm(e);
		}
	}
}

void map_put(pos_t p, unsigned thing, int flags);

static unsigned
st_room_at(unsigned player_ref, pos_t pos)
{
	struct bio bio = eng_noise_point(pos);
	OBJ there;
	biome_map = biome_map_get(* (uint64_t *) pos);
	unsigned there_ref = eng_object_add(&there, biome_map[bio.bio_idx], 0, (uint64_t) &bio, 0);
	ROO *rthere = (ROO *) &there.data;
	/* Carved rooms are permanent; clear RF_TEMP so eng_room_clean
	 * does not collect the room when empty (§27.3). */
	rthere->flags &= ~RF_TEMP;
	map_put(pos, there_ref, 1);
	exits_infer(there_ref, rthere);

	fprintf(stderr, "st_room_at %u\n", player_ref);

	if (pos[2] != 0) {
		corm_put(obj_hd, &there_ref, &there);
		return there_ref;
	}

	rthere->floor = bio.bio_idx;
	corm_put(obj_hd, &there_ref, &there);
	uint32_t v = XXH32((const char *) pos, sizeof(pos_t), 1);
	nd_evt_spawn(player_ref, there_ref, bio, v);
	return there_ref;
}

void
do_bio(int fd, int argc __attribute__((unused)), char *argv[] __attribute__((unused))) {
	unsigned player_ref = fd_player(fd);
	struct bio bio;
	pos_t pos;
	OBJ player;

	nd_get(HD_OBJ, &(player), &player_ref);
	if (!eng_map_has(player.location)) {
		nd_writef(player_ref, "You are nowhere.\n");
		return;
	}
	eng_map_where(pos, player.location);
	bio = eng_noise_point(pos);
	biome_map = biome_map_get(*(uint64_t *) pos);
	SKEL biome;
	nd_get(HD_SKEL, &(biome), &biome_map[bio.bio_idx]);
	nd_writef(player_ref, "tmp %d rn %u bio %s(%d)\n",
		bio.tmp, bio.rn, biome.name, bio.bio_idx);
}

static unsigned
st_room(unsigned player_ref, unsigned location, enum exit e)
{
	pos_t pos;
	st_pos(pos, location, e);
	return st_room_at(player_ref, pos);
}

static unsigned
e_move(unsigned player_ref, enum exit e) {
	OBJ player;
	corm_get_copy(obj_hd, &player_ref, &(player));
	OBJ loc;
	corm_get_copy(obj_hd, &player.location, &(loc));
	unsigned dest_ref;
	ROO *rloc = (ROO *) &loc.data;
	char const *dwts = "door";
	int door = 0;

	int cant_move = nd_evt_move(player_ref);
	if (cant_move)
		return NOTHING;

	if (!eng_map_has(player.location) || !(rloc->exits & e)) {
		nd_writef(player_ref, "You can't go that way.\n");
		return NOTHING;
	}

	if (rloc->doors & e) {
		if (e == E_UP || e == E_DOWN) {
			dwts = "hatch";
			door = 2;
		} else
			door = 1;

		nd_writef(player_ref, "You open the %s.\n", dwts);
	}

	dest_ref = st_there(player.location, e);
	if (dest_ref == NOTHING)
		dest_ref = st_room(player_ref, player.location, e);

	eng_enter(player_ref, dest_ref, e);

	if (door)
		nd_writef(player_ref, "You close the %s.\n", dwts);

	return dest_ref;
}

unsigned
eng_room_clean(unsigned here_ref)
{
	unsigned tmp_ref;
	OBJ here;

	corm_get_copy(obj_hd, &here_ref, &(here));

	if (!(((ROO *) here.data)->flags & RF_TEMP))
		return here_ref;

	unsigned c = corm_iter(nd_table_handle(HD_CONTENTS), &here_ref, CM_RANGE);
	const void *kp, *vp;
	while (corm_next(&kp, &vp, c)) {
		OBJ tmp;
		here_ref = *(const unsigned *)kp;
		tmp_ref = *(const unsigned *)vp;
		corm_get_copy(obj_hd, &tmp_ref, &(tmp));

		if (tmp.type != TYPE_ENTITY)
			continue;

		if (tmp.flags & OF_PLAYER) {
			corm_fin(c);
			return here_ref;
		}
	}

	eng_object_move(here_ref, NOTHING);
	return NOTHING;
}

static void
walk(unsigned player_ref, enum exit e) {
	e_move(player_ref, e);
}

static void
carve(unsigned player_ref, enum exit e)
{
	OBJ player;
	corm_get_copy(obj_hd, &player_ref, &(player));
	unsigned here_ref = player.location, there_ref = here_ref;
	OBJ here;
	corm_get_copy(obj_hd, &here_ref, &(here));
	ROO *rhere = (ROO *) &here.data;
	int wall = 0;

	if (!e_ground(here_ref, e)) {
		if (st_claim(player_ref, &here))
			return;

		rhere->exits |= e;
		corm_put(obj_hd, &here_ref, &here);

		there_ref = st_there(here_ref, e);
		if (there_ref == NOTHING)
			there_ref = st_room(player_ref, here_ref, e);
		wall = 1;
	}

	there_ref = e_move(player_ref, e);
	if (there_ref == NOTHING)
		return;

	OBJ there;
	corm_get_copy(obj_hd, &there_ref, &(there));
	st_claim(player_ref, &there); // FIXME check success in advance

	if (wall) {
		ROO *rthere = (ROO *) &there.data;
		const char *s;
		for (s = e_other(e_simm(e)); *s; s++) {
			enum exit e2 = dir_e(*s);

			if ((rthere->exits & e2) &&
					st_there(there_ref, e2) != NOTHING)

				rthere->exits ^= e2;
		}
	}

	corm_put(obj_hd, &there_ref, &there);
}

static void
uncarve(unsigned player_ref, enum exit e)
{
	OBJ player;
	corm_get_copy(obj_hd, &player_ref, &(player));
	const char *s0 = "is";
	unsigned here_ref = player.location, there_ref;
	OBJ here;
	corm_get_copy(obj_hd, &here_ref, &(here));
	ROO *rhere = (ROO *) &here.data;
	int ht, cd = e_ground(here_ref, e);

	if (cd) {
		ht = rhere->flags & RF_TEMP;
		there_ref = e_move(player_ref, e);
		if (there_ref == NOTHING)
			return;
	} else {
		if (!(rhere->exits & e)) {
                        nd_writef(player_ref, "No exit there.\n");
                        return;
                }

		there_ref = st_there(here_ref, e);

		if (there_ref == NOTHING) {
			nd_writef(player_ref, "No room there.\n");
			return;
		}
		s0 = "at";
	}

	OBJ there;
	corm_get_copy(obj_hd, &there_ref, &(there));
	ROO *rthere = (ROO *) &there.data;

	if ((rthere->flags & RF_TEMP) || there.owner != player_ref) {
		nd_writef(player_ref, "You don't own th%s room.\n", s0);
		return;
	}

	rthere->flags ^= RF_TEMP;
	exits_infer(there_ref, rthere);
	if (cd) {
		if (ht && (rthere->doors & e_simm(e)))
			rthere->doors &= ~e_simm(e);
		corm_put(obj_hd, &there_ref, &there);
	} else
		eng_room_clean(there_ref);

	corm_get_copy(obj_hd, &player_ref, &(player));
	player.value += ROOM_COST;
	nd_writef(player_ref, "You collect your materials. (+%dp)\n", ROOM_COST);
	corm_put(obj_hd, &player_ref, &player);
}

static void
unwall(unsigned player_ref, enum exit e)
{
	int a, b, c, d;
	OBJ player;
	corm_get_copy(obj_hd, &player_ref, &(player));
	unsigned there_ref, here_ref = player.location;
	OBJ here;
	corm_get_copy(obj_hd, &here_ref, &(here));
	ROO *rhere = (ROO *) &here.data;

	a = here.owner == player_ref;
	b = rhere->flags & RF_TEMP;
	there_ref = st_there(here_ref, e);

	OBJ there;
	corm_get_copy(obj_hd, &there_ref, &(there));
	ROO *rthere = (ROO *) &there.data;

	if (there_ref != NOTHING) {
		c = there.owner == player_ref;
		d = rthere->flags & RF_TEMP;
	} else {
		c = 0;
		d = 1;
	}

	if (!((a && !b && (d || c))
	    || (c && !d && b))) {
		nd_writef(player_ref, "You can't do that here.\n");
		return;
	}

	if (rhere->exits & e) {
		nd_writef(player_ref, "There's an exit here already.\n");
		return;
	}

	rhere->exits |= e;
	there_ref = e_move(player_ref, e);
	if (there_ref == NOTHING)
		return;

	corm_get_copy(obj_hd, &there_ref, &(there));
	rthere = (ROO *) &there.data;
	rthere->exits |= e_simm(e);
	corm_put(obj_hd, &there_ref, &there);
	nd_writef(player_ref, "You tear down the wall.\n");
}

static inline int
gexit_claim(unsigned player_ref, enum exit e)
{
	int a, b, c, d;
	OBJ player, here, there;
	corm_get_copy(obj_hd, &player_ref, &(player));
	unsigned here_ref = player.location,
	      there_ref = st_there(player.location, e);
	corm_get_copy(obj_hd, &here_ref, &(here));
	corm_get_copy(obj_hd, &there_ref, &(there));
	ROO *rthere = (ROO *) &there.data;

	a = here_ref != NOTHING && here.owner == player_ref;
	c = rthere->flags & RF_TEMP;
	b = !c && there.owner == player_ref;
	d = e_ground(here_ref, e);

	if (a && (b || c))
		return 0;

	if (here_ref != NOTHING || (((ROO *) here.data)->flags & RF_TEMP)) {
		if (b)
			return 0;
		if (d || c) // FIXME
			return st_claim(player_ref, &there);
	}

	nd_writef(player_ref, "You can't claim that exit.\n");
	return 1;
}

static inline int
gexit_claim_walk(unsigned player_ref, enum exit e)
{
	OBJ player, here;
	corm_get_copy(obj_hd, &player_ref, &(player));
	unsigned here_ref = player.location;
	corm_get_copy(obj_hd, &here_ref, &(here));
	ROO *rhere = (ROO *) &here.data;

	if (!(rhere->exits & e)) {
		nd_writef(player_ref, "No exit here.\n");
		return 1;
	}

	unsigned there_ref = e_move(player_ref, e);
	if (there_ref == NOTHING)
		return 1;

	player.location = there_ref;
	corm_put(obj_hd, &player_ref, &player);

	return gexit_claim(player_ref, e_simm(e));
}

static void
e_wall(unsigned player_ref, enum exit e)
{
	if (gexit_claim_walk(player_ref, e))
		return;

	OBJ player, here;
	corm_get_copy(obj_hd, &player_ref, &(player));
	unsigned here_ref = player.location;
	corm_get_copy(obj_hd, &here_ref, &(here));
	ROO *rhere = (ROO *) &here.data;

	rhere->exits &= ~e_simm(e);
	corm_put(obj_hd, &here_ref, &here);

	unsigned there_ref = st_there(here_ref, e_simm(e));

	if (there_ref != NOTHING) {
		OBJ there;
		corm_get_copy(obj_hd, &there_ref, &(there));
		ROO *rthere = (ROO *) &there.data;
		rthere->exits &= ~e;
		corm_put(obj_hd, &there_ref, &there);
	}

	nd_writef(player_ref, "You build a wall.\n");
}

static void
door(unsigned player_ref, enum exit e)
{
	if (gexit_claim_walk(player_ref, e))
		return;

	OBJ player, where;
	corm_get_copy(obj_hd, &player_ref, &(player));
	unsigned where_ref = player.location;
	corm_get_copy(obj_hd, &where_ref, &(where));
	ROO *rwhere = (ROO *) &where.data;
	rwhere->doors |= e_simm(e);
	corm_put(obj_hd, &where_ref, &where);

	where_ref = st_there(where_ref, e_simm(e));

	if (where_ref == NOTHING) {
		corm_get_copy(obj_hd, &where_ref, &(where));
		rwhere = (ROO *) &where.data;
		rwhere->doors |= e;
		corm_put(obj_hd, &where_ref, &where);
	}

	nd_writef(player_ref, "You place a door.\n");
}

static void
undoor(unsigned player_ref, enum exit e)
{
	if (gexit_claim_walk(player_ref, e))
		return;

	OBJ player, where;
	corm_get_copy(obj_hd, &player_ref, &(player));
	unsigned where_ref = player.location;
	corm_get_copy(obj_hd, &where_ref, &(where));
	ROO *rwhere = (ROO *) &where.data;
	rwhere->doors &= ~e_simm(e);
	corm_put(obj_hd, &where_ref, &where);

	where_ref = st_there(where_ref, e_simm(e));

	if (where_ref != NOTHING) {
		corm_get_copy(obj_hd, &where_ref, &(where));
		rwhere = (ROO *) &where.data;
		rwhere->doors &= ~e;
		corm_put(obj_hd, &where_ref, &where);
	}

	nd_writef(player_ref, "You remove a door.\n");
}

static int
tell_pos(unsigned player_ref, struct cmd_dir cd) {
	pos_t pos;
	unsigned target_ref = cd.rep == 1 ? player_ref : cd.rep;
	OBJ target;
	corm_get_copy(obj_hd, &target_ref, &(target));
	int ret = 1;

	if (target.type != TYPE_ENTITY) {
		nd_writef(player_ref, "Invalid object type.\n");
		return 0;
	}

	eng_map_where(pos, target.location);
	nd_writef(player_ref, "0x%llx\n", MORTON_READ(pos));
	return ret;
}

void
eng_st_teleport(unsigned player_ref, uint64_t mpos) {
	pos_t pos;
	memcpy(pos, &mpos, sizeof(mpos));
	unsigned there_ref = eng_map_get(pos);
	biome_map = biome_map_get(* (uint16_t *) pos);
	if (there_ref == NOTHING)
		there_ref = st_room_at(player_ref, pos);
	eng_enter(player_ref, there_ref, E_NULL);
}

int
vim_teleport(unsigned player_ref, struct cmd_dir cd)
{
	pos_t pos;
	int ret = 0;
	if (cd.rep == 1)
		cd.rep = 0;
	if (cd.dir == '?') {
		// X6? teleport to chivas
		unsigned target_ref = cd.rep;
		OBJ target;
		corm_get_copy(obj_hd, &target_ref, &(target));
		if (target.type == TYPE_ENTITY) {
			eng_map_where(pos, target.location);
			ret = 1;
		}
		memcpy(&cd.rep, pos, sizeof(cd.rep));
	}

	eng_st_teleport(player_ref, cd.rep);
	return ret;
}

static int
pull(unsigned player_ref, struct cmd_dir cd)
{
	pos_t pos;
	enum exit e = cd.e;
	OBJ player;
	corm_get_copy(obj_hd, &player_ref, &(player));
	unsigned here_ref = player.location, there_ref;

	if (e == E_NULL)
		return 0;

	unsigned what_ref = cd.rep;

	if (what_ref == NOTHING) {
		nd_writef(player_ref, "You cannot do that.\n");
		return 1;
	}

	OBJ what, here;
	corm_get_copy(obj_hd, &what_ref, &(what));
	corm_get_copy(obj_hd, &here_ref, &(here));
	ROO *rhere = (ROO *) &here.data;

	if (!(rhere->exits & e)
	    || what.type != TYPE_ROOM
	    || what.owner != player_ref
	    || ((there_ref = st_there(here_ref, e))
		&& eng_room_clean(there_ref) == there_ref))
	{
		nd_writef(player_ref, "You cannot do that.\n");
		return 1;
	}

	st_pos(pos, player.location, e);
	map_put(pos, cd.rep, 0);
	exits_fix(cd.rep, e);
	e_move(player_ref, e);
	return 1;
}

op_t op_map[256] = {
	['r'] = { .op.a = &carve },
	['R'] = { .op.a = &uncarve },
	['d'] = { .op.a = &door } ,
	['D'] = { .op.a = &undoor },
	['w'] = { .op.a = &e_wall },
	['W'] = { .op.a = &unwall },
	['x'] = { .op.b = &tell_pos, .type = 1 },
	['X'] = { .op.b = &vim_teleport, .type = 1 },
	['#'] = { .op.b = &pull, .type = 1 },
};

static int
st_cmd_dir(struct cmd_dir *res, const char *cmd)
{
	int ofs = 0;
	morton_t rep = 1;
	char *end, sc;

	if (isdigit(cmd[0])) {
		rep = strtoull(cmd, &end, 0);
		ofs += end - &cmd[0];
	}

	res->dir = sc = cmd[ofs];
	res->e = dir_e(sc);
	res->rep = rep;

	return ofs;
}

static void may_look(unsigned player_ref, morton_t old_loc) {
	OBJ player;
	morton_t new_loc;

	corm_get_copy(obj_hd, &player_ref, &(player));
	new_loc = eng_map_mwhere(player.location);

	if (old_loc == new_loc)
		return;

	eng_look_at(player_ref, NOTHING);
	view(player_ref);
	nd_flush(player_ref);
}

int
eng_st_v(unsigned player_ref, char const *opcs)
{
	OBJ player;
	if (!player_ref || !corm_get(obj_hd, &player_ref))
		return 0;
	corm_get_copy(obj_hd, &player_ref, &(player));
	morton_t old_loc = eng_map_mwhere(player.location);
	biome_map = biome_map_get(old_loc);
	if (!biome_map)
		return 0;
	struct cmd_dir cd;
	char const *s = opcs;

	for (;*s;) {
		int ofs = 0;
		op_t op = op_map[(unsigned char) *s]; // the op
		op_a_t *aop = op.type ? NULL : op.op.a; 
		ofs += !!(aop || op.type);
		ofs += st_cmd_dir(&cd, s + ofs);

		if (!(aop || op.op.b))
			aop = &walk;

		if (aop) {
			if (cd.e == E_NULL) {
				may_look(player_ref, old_loc);
				return s > opcs ? (int)(s - opcs) : 1;
			}

			ofs ++;
			if (cd.rep > 1000)
				cd.rep = 1000;
			morton_t j;
			for (j = 0; j < cd.rep; j++)
				aop(player_ref, cd.e);
		} else if (op.op.b)
			ofs += op.op.b(player_ref, cd);

		s += ofs;
	}

	may_look(player_ref, old_loc);
	return (int)(s - opcs);
}

void
echo(char *fmt, ...) {
	char buf[1024];
	va_list va;
	va_start(va, fmt);
	int n = vsnprintf(buf, sizeof(buf), fmt, va);
	va_end(va);
	if (n > 0)
		nd_write(g_player_ref, buf, (size_t)n < sizeof(buf) ? (size_t)n : sizeof(buf) - 1);
}

void
oecho(char *format, ...) {
	char buf[1024];
	va_list args;
	va_start(args, format);
	int n = vsnprintf(buf, sizeof(buf), format, args);
	va_end(args);
	if (n > 0)
		nd_write(g_player_ref, buf, (size_t)n < sizeof(buf) ? (size_t)n : sizeof(buf) - 1);
}

/* ---------------------------------------------------------------------------
 * Phase 2: Territory governance (st_owner, st_can_region, st_init, grant,
 * release, planet, target, deny, loadmod, modlist, bans, presence)
 * has been relocated to external/nd-region (libnd-region.so).
 * Calls to governance below dispatch across the XY bus via <nd/regions.h>.
 * --------------------------------------------------------------------------- */

#define BAN_KEY_LEN 32

static void
st_ban_key(char *k, size_t len, unsigned player, uint64_t id, uint8_t plen)
{
	snprintf(k, len, "%010u:%016llx:%02u", player,
		(unsigned long long)id, plen);
}

unsigned ban_hd = (unsigned)-1;

static int
st_ban_have(void)
{
	if (ban_hd != (unsigned)-1 && ban_hd != CM_MISS)
		return 1;
	ban_hd = corm_open(world_db(), "region_ban", CM_STR, CM_U32, 0xFFFF, 0);
	return ban_hd != (unsigned)-1 && ban_hd != CM_MISS;
}

int
st_ban_at(unsigned player_ref, uint64_t id, uint8_t plen,
	uint64_t *ban_id, uint8_t *ban_plen)
{
	char k[BAN_KEY_LEN];

	if (!st_ban_have())
		return 0;
	st_ban_key(k, sizeof(k), player_ref, id, plen);
	if (corm_get(ban_hd, k) != NULL) {
		if (ban_id)
			*ban_id = id;
		if (ban_plen)
			*ban_plen = plen;
		return 1;
	}
	return 0;
}

int
eng_st_ban_check(unsigned player_ref, uint64_t code,
	uint64_t *ban_id, uint8_t *ban_plen)
{
	static const uint8_t widths[] = {
		ST_PLEN_ROOT, ST_PLEN_WORLD, 32, 48, ST_PLEN_CELL
	};
	size_t i;

	if (!st_ban_have())
		return 0;
	for (i = 0; i < sizeof(widths) / sizeof(widths[0]); i++) {
		uint64_t mask = widths[i] ? (~0ULL << (64 - widths[i])) : 0ULL;

		if (st_ban_at(player_ref, code & mask, widths[i],
				ban_id, ban_plen))
			return 1;
	}
	return 0;
}

static inline void
st_owner_name(unsigned owner_ref, char *buf, size_t len)
{
	OBJ o;
	memset(&o, 0, sizeof(o));

	if (corm_read(obj_hd, &owner_ref, &o, sizeof(o)) == 0) {
		snprintf(buf, len, "%s", o.name);
	} else {
		snprintf(buf, len, "%u", owner_ref);
	}
}

static inline int
st_region_of_pos(coord_t *pos, uint64_t *id, uint8_t *plen)
{
	uint64_t code, rid;
	uint8_t w;

	if (!pos || !id || !plen)
		return XY_ERR_INVALID;
	code = pos_morton(pos);
	rid = xy_region_at(code, ST_PLEN_CELL, &w);
	if (rid == XY_REGION_INVALID)
		return XY_ERR_NOTFOUND;
	*id = rid;
	*plen = w;
	return XY_OK;
}

void
do_room(int fd, int argc, char *argv[])
{
	unsigned player_ref = fd_player(fd);
	pos_t pos;
	unsigned there_ref;
	uint64_t rid;
	uint8_t rplen;
	char oname[64];
	char *end;
	long v;
	int i;

	if (argc < 5) {
		nd_writef(player_ref, "Usage: room <x> <y> <z> <w>\n");
		nd_flush(player_ref);
		return;
	}

	for (i = 0; i < 4; i++) {
		if (!argv[1 + i] || !*argv[1 + i]) {
			nd_writef(player_ref, "Usage: room <x> <y> <z> <w>\n");
			nd_flush(player_ref);
			return;
		}
		v = strtol(argv[1 + i], &end, 10);
		if (end == argv[1 + i] || *end || v < COORD_MIN || v > COORD_MAX) {
			nd_writef(player_ref, "Usage: room <x> <y> <z> <w>\n");
			nd_flush(player_ref);
			return;
		}
		pos[i] = (coord_t)v;
	}

	if (st_region_of_pos(pos, &rid, &rplen) != XY_OK) {
		nd_writef(player_ref, "Couldn't place a room there.\n");
		nd_flush(player_ref);
		return;
	}
	if (!st_can_region(player_ref, rid, rplen)) {
		st_owner_name(rid == 0 ? 0 : st_owner(rid, rplen), oname,
			sizeof(oname));
		nd_writef(player_ref, "Permission denied (ruled by %s)\n", oname);
		nd_flush(player_ref);
		return;
	}

	there_ref = eng_map_get(pos);
	if (there_ref != NOTHING) {
		const void *ov = corm_get(obj_hd, &there_ref);
		if (ov && ((const OBJ *)ov)->type == TYPE_ROOM) {
			eng_enter(player_ref, there_ref, E_NULL);
			nd_writef(player_ref, "room %u at %d %d %d %d (existing)\n",
				there_ref, pos[0], pos[1], pos[2], pos[3]);
			nd_flush(player_ref);
			return;
		}
	}

	there_ref = st_room_at(player_ref, pos);
	if (there_ref == NOTHING) {
		nd_writef(player_ref, "Couldn't make a room there.\n");
		nd_flush(player_ref);
		return;
	}
	eng_enter(player_ref, there_ref, E_NULL);
	nd_writef(player_ref, "room %u at %d %d %d %d\n",
		there_ref, pos[0], pos[1], pos[2], pos[3]);
	nd_flush(player_ref);
}
