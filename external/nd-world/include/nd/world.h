#ifndef ND_WORLD_H
#define ND_WORLD_H

/*
 * world.h — convention: the reference universe. The Neverdark physics a
 * module opts into: object model, terrain/map, positions and teleport, wts
 * noun inflection, entity, matching (ematch), vtf/action registration,
 * procedural noise, and the MCP client protocol.
 *
 * This is NOT law. A region may replace any of it with its own module set;
 * these hooks are simply what the first-party modules agree on, kept in one
 * header for their convenience and for interop between modules that share
 * this convention.
 *
 * Layering: includes <nd/regions.h> (law + governance). Game modules reach
 * it through <nd/xy.h>.
 */

#include <stdarg.h>
#include <stdio.h>
#include <nd/law.h>
#include <nd/world-types.h>
#include <nd/regions.h>

/* broadcast to every entity in room_ref except exception_ref */
XY_DECL(int, nd_rwrite, unsigned, room_ref, unsigned, exception_ref,
	char *, str, size_t, len);

/* ----------------------------------------------------------------- map */

XY_DECL(int, map_has, unsigned, thing);
XY_DECL(morton_t, map_mwhere, unsigned, thing);
/* XY has no array-typed args (pos_t decays): positions are coord_t * to 4 */
XY_DECL(int, map_where, coord_t *, p, unsigned, thing);
XY_DECL(int, map_delete, unsigned, what);
XY_DECL(unsigned, map_get, coord_t *, p);
XY_DECL(int, map_search, unsigned *, mat, coord_t *, pos, unsigned, radius);
XY_DECL(int, nd_noise_view, struct bio *, to, coord_t *, pos);

/* ------------------------------------------------------------------- st */

XY_DECL(int, st_teleport, unsigned, player_ref, uint64_t, pos);

/* ------------------------------------------------------------------ wts */

XY_DECL(char *, wts_plural, char *, singular);

/* --------------------------------------------------------------- object */

XY_DECL(int, obj_exists, unsigned, ref);
XY_DECL(unsigned, object_new, OBJ *, obj);
XY_DECL(unsigned, object_copy, OBJ *, nu, unsigned, old_ref);
XY_DECL(int, object_move, unsigned, what_ref, unsigned, where_ref);
XY_DECL(unsigned, object_add, OBJ *, nu, unsigned, skel_id, unsigned, where,
	uint64_t, v);
XY_DECL(int, object_drop, unsigned, where_ref, unsigned, skel_id);
/* engine needs the player for per-player icon dispatch (SESSION-9m). */
XY_DECL(struct icon, object_icon, unsigned, player_ref, unsigned, thing_ref);
XY_DECL(char *, object_art, unsigned, ref);
XY_DECL(const char *, unparse, unsigned, loc_ref);
XY_DECL(int, objects_init, unsigned, dummy);

/* --------------------------------------------------------------- entity */

/* XY needs >=1 arg pair: dummy is ignored, pass 0 */
XY_DECL(unsigned, me_get, unsigned, dummy);
XY_DECL(ENT, ent_get, unsigned, ref);
XY_DECL(int, ent_set, unsigned, ref, ENT *, tmp);
XY_DECL(int, ent_del, unsigned, ref);
XY_DECL(int, ent_reset, ENT *, ent);
XY_DECL(int, controls, unsigned, who_ref, unsigned, what_ref);
XY_DECL(int, payfor, unsigned, who_ref, OBJ *, who, unsigned, cost);
XY_DECL(int, look_around, unsigned, player_ref);
XY_DECL(int, enter, unsigned, player_ref, unsigned, loc_ref, int, e);
XY_DECL(int, look_at, unsigned, player_ref, unsigned, loc_ref);
XY_DECL(unsigned, room_clean, unsigned, loc_ref);

/* --------------------------------------------------------------- ematch */

XY_DECL(unsigned, ematch_at, unsigned, player_ref, unsigned, where_ref, char *, name);
XY_DECL(unsigned, ematch_player, char *, name);
XY_DECL(unsigned, ematch_absolute, char *, name);
XY_DECL(unsigned, ematch_me, unsigned, player_ref, char *, str);
XY_DECL(unsigned, ematch_here, unsigned, player_ref, char *, str);
XY_DECL(unsigned, ematch_mine, unsigned, player_ref, char *, str);
XY_DECL(unsigned, ematch_near, unsigned, player_ref, char *, str);
XY_DECL(unsigned, ematch_all, unsigned, player_ref, char *, name);

/* ------------------------------------------------------ register / vtf */

XY_DECL(unsigned, action_register, char *, label, char *, icon);
XY_DECL(unsigned, vtf_register, char, emp, int, fg, unsigned, flags);

/* ---------------------------------------------------------------- noise */

XY_DECL(struct bio, noise_point, coord_t *, p);

/* ------------------------------------------------------------------ mcp */

XY_DECL(int, fbcp_item, unsigned, player_ref, unsigned, obj_ref, unsigned char, dynflags);
XY_DECL(int, fbcp, unsigned, player_ref, size_t, len, unsigned char, iden, void *, data);
XY_DECL(int, mcp_content_out, unsigned, loc_ref, unsigned, thing_ref);
XY_DECL(int, mcp_content_in, unsigned, loc_ref, unsigned, thing_ref);
XY_DECL(int, mcp_bar, unsigned char, iden, unsigned, player_ref, unsigned short, val, unsigned short, max);

static inline int
nd_oprintf(unsigned player_ref, char *fmt, ...)
{
	OBJ player;
	char buf[1024];
	va_list va;
	int n;

	if (!nd_get(HD_OBJ, &player, &player_ref))
		return -1;
	va_start(va, fmt);
	n = vsnprintf(buf, sizeof(buf), fmt, va);
	va_end(va);
	if (n < 0)
		return n;
	return nd_rwrite(player.location, player_ref, buf,
			 (size_t) n < sizeof(buf) ? (size_t) n : sizeof(buf) - 1);
}

#endif /* ND_WORLD_H */
