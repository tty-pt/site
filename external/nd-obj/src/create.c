#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <ttypt/xy-mod.h>

#include <nd/xy.h>

/* Moved from external/axil-nd/src/object.c (Phase 2.4b): do_create/do_recycle/
 * do_drop. Engine helpers replaced with hooks (nd_printf/nd_oprintf,
 * object_add/object_move); ok_name + message literals copied (see name.c);
 * ST_* and SKEL/TYPE_ROOM come from nd/regions.h + nd/xy-types.h.
 * do_clone/do_get stay until event firing moves; do_chown stays until the
 * player registry hooks exist. */

#define NOMATCH_MESSAGE "I don't know what you mean.\n"
#define CANTDO_MESSAGE "You can't do that.\n"
#define NUMBER_TOKEN '#'
#define ESCAPE_CHAR 27

static inline int
ok_name(const char *name)
{
	return (name
			&& *name
			&& *name != NUMBER_TOKEN
			&& !strchr(name, ' ')
			&& !strchr(name, '\r')
			&& !strchr(name, ESCAPE_CHAR)
			&& strcmp(name, "me")
			&& strcmp(name, "home")
			&& strcmp(name, "here"));
}

void
do_create(int fd, int argc, char *argv[])
{
	unsigned player_ref = fd_player(fd),
		 ref;
	unsigned long long skid;
	unsigned long long value = 0;
	uint64_t cid = 0;
	uint8_t cplen = ST_PLEN_ROOT;
	uint64_t v = 0;
	char *name = (argc > 1 && argv[1]) ? argv[1] : "";
	char *skid_text = (argc > 2 && argv[2]) ? argv[2] : "";
	char *value_text = (argc > 3 && argv[3]) ? argv[3] : "";
	char *end = NULL;
	OBJ obj;

	if (!*name || !*skid_text || !ok_name(name)) {
		nd_printf(player_ref, "Syntax: create name skel_id [v]\n");
		nd_flush(player_ref);
		return;
	}

	skid = 0;
	errno = 0;
	if (*skid_text == '-' || isspace((unsigned char)*skid_text)) {
		nd_printf(player_ref, "Syntax: create name skel_id [v]\n");
		nd_flush(player_ref);
		return;
	}
	skid = strtoull(skid_text, &end, 10);
	if (errno || !end || *end || skid > UINT_MAX) {
		nd_printf(player_ref, "Syntax: create name skel_id [v]\n");
		nd_flush(player_ref);
		return;
	}
	if (*value_text) {
		if (*value_text == '-' ||
				isspace((unsigned char)*value_text)) {
			nd_printf(player_ref, "Syntax: create name skel_id [v]\n");
			nd_flush(player_ref);
			return;
		}
		errno = 0;
		value = strtoull(value_text, &end, 10);
		if (errno || !end || *end) {
			nd_printf(player_ref, "Syntax: create name skel_id [v]\n");
			nd_flush(player_ref);
			return;
		}
		v = (uint64_t)value;
	}

	/* The new object lands in the creator's inventory, so there is no target
	 * placement to authorize. The honest scope is the region the creator is
	 * standing in, checked against the union of regions they rule. */
	if (st_region_of_player(player_ref, &cid, &cplen) != 0) {
		st_refuse_region(player_ref, 0, ST_PLEN_ROOT);
		return;
	}

	if (!st_in_scope(player_ref, 0, ST_SEL_UNSET, cid, cplen)) {
		st_refuse_region(player_ref, cid, cplen);
		return;
	}

	/* A missing skeleton must not reach object_add: it copies the row
	 * without checking, so an absent row would seed the new object from
	 * uninitialized stack memory. A room skeleton must not reach it either:
	 * the TYPE_ROOM branch reads `v` as a `struct bio *`, which a command
	 * line cannot truthfully supply. Rooms are made with `room`. */
	SKEL skel;
	unsigned skid_ref = (unsigned)skid;
	memset(&skel, 0, sizeof(skel));
	if (nd_get(HD_SKEL, &skel, &skid_ref) != 0) {
		nd_printf(player_ref, NOMATCH_MESSAGE);
		nd_flush(player_ref);
		return;
	}
	if (skel.type == TYPE_ROOM) {
		nd_printf(player_ref, "Rooms are made with `room`, not `create`.\n");
		nd_flush(player_ref);
		return;
	}

	OBJ player;
	nd_get(HD_OBJ, &(player), &player_ref);

	ref = object_add(&obj, (unsigned)skid, player_ref, v);
	obj.owner = player.owner;

	nd_put(HD_OBJ, &ref, &obj);
	nd_printf(player_ref, "Created.\n");
	nd_flush(player_ref);
}

void
do_recycle(int fd, int argc __attribute__((unused)), char *argv[])
{
	unsigned player_ref = fd_player(fd);
	char *name = argv[1];
	unsigned thing_ref;
	OBJ thing;

	if (
			(thing_ref = ematch_absolute(name)) == NOTHING
			&& (thing_ref = ematch_near(player_ref, name)) == NOTHING
			&& (thing_ref = ematch_mine(player_ref, name)) == NOTHING
	   )
	{
		nd_printf(player_ref, NOMATCH_MESSAGE);
		return;
	}

	nd_get(HD_OBJ, &(thing), &thing_ref);

	if (!controls(player_ref, thing_ref) || thing.owner != player_ref) {
		nd_printf(player_ref, CANTDO_MESSAGE);
		return;
	}

	object_move(thing_ref, NOTHING);
}

void
do_drop(int fd, int argc __attribute__((unused)), char *argv[])
{
	unsigned player_ref = fd_player(fd), thing_ref, cont_ref;
	OBJ player, cont, thing;
	nd_get(HD_OBJ, &(player), &player_ref);
	char *name = argv[1];
	char *obj = argv[2];

	if ((thing_ref = ematch_mine(player_ref, name)) == NOTHING) {
		nd_printf(player_ref, NOMATCH_MESSAGE);
		return;
	}

	cont_ref = player.location;
	if (
			obj && *obj
			&& (cont_ref = ematch_mine(player_ref, obj)) == NOTHING
			&& (cont_ref = ematch_near(player_ref, obj)) == NOTHING
	   )
	{
		nd_printf(player_ref, NOMATCH_MESSAGE);
		return;
	}

	if (thing_ref == cont_ref)
		goto error;

	nd_get(HD_OBJ, &(cont), &cont_ref);

	object_move(thing_ref, cont_ref);
	nd_get(HD_OBJ, &(thing), &thing_ref);

	if (cont.type == TYPE_ENTITY) {
		nd_printf(cont_ref, "%s hands you %s.\n", player.name, thing.name);
		return;
	}

	nd_oprintf(player_ref, "%s drops %s.\n", player.name, thing.name);
	return;
error:
	nd_printf(player_ref, CANTDO_MESSAGE);
}
