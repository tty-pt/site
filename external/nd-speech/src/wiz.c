#include <nd/law.h>
#include <nd/world-types.h>
#include <nd/obj-types.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <ttypt/xy.h>

#include "config.h"
#include "st.h"
#include "uapi/entity.h"
#include "uapi/match.h"
#include "player.h"

unsigned fd_player(unsigned fd);

void
do_teleport(int fd, int argc __attribute__((unused)), char *argv[]) {
	unsigned player_ref = fd_player(fd), victim_ref, destination_ref;
	char *arg1, *arg2, *to;

	/* Both argv[1] and argv[2] are read before anything proves they are
	 * there. axil delivers a bare verb as argc == 2 with an empty argv[1],
	 * so `argc < 2` does not catch it (measured: bare `create` reached its
	 * gate, not its Usage line) -- the guard has to be on the string, which
	 * is also what makes argv[2] safe to dereference below. axil additionally
	 * sets argv[argc] = "" (libaxil.c cmd_proc), but relying on that for
	 * argv[2] means relying on an off-by-one nobody has measured. */
	if (!argv[1] || !*argv[1]) {
		nd_writef(player_ref, "Usage: teleport [<victim>] <destination>\n");
		eng_nd_flush(player_ref);
		return;
	}
	arg1 = argv[1];
	arg2 = (argv[2] && *argv[2]) ? argv[2] : "";

	/* get victim, destination */
	if (*arg2 == '\0') {
		victim_ref = player_ref;
		to = arg1;
	} else if ((victim_ref = ematch_all(player_ref, arg1)) == NOTHING) {
		nd_writef(player_ref, NOMATCH_MESSAGE);
		eng_nd_flush(player_ref);
		return;
	} else
		to = arg2;

	OBJ victim;
	nd_get(HD_OBJ, &(victim), &victim_ref);

	destination_ref = ematch_all(player_ref, to);

	if (destination_ref == NOTHING) {
		nd_writef(player_ref, NOMATCH_MESSAGE);
		eng_nd_flush(player_ref);
		return;
	}

	OBJ destination;
	nd_get(HD_OBJ, &(destination), &destination_ref);

	if (victim_ref == destination_ref || destination.location == victim_ref)
		goto error;

	switch (victim.type) {
	case TYPE_ENTITY:
		if (!eng_controls(player_ref, victim_ref) ||
				!eng_controls(player_ref, destination_ref) ||
				!eng_controls(player_ref, victim.location) ||
				destination.type != TYPE_ROOM)
			goto error;
		nd_writef(victim_ref, "You feel a wrenching sensation...\n");
		eng_enter(victim_ref, destination_ref, E_NULL);
		eng_nd_flush(victim_ref);
		eng_nd_flush(player_ref);
		return;
	case TYPE_ROOM:
		goto error;
	default:
		if (!(
			eng_controls(player_ref, destination_ref) &&
			(eng_controls(player_ref, victim_ref) || eng_controls(player_ref, victim.location)) )
		   )
			goto error;
		eng_object_move(victim_ref, destination_ref);
		eng_nd_flush(player_ref);
		return;
	}
error:
	nd_writef(player_ref, CANTDO_MESSAGE);
	eng_nd_flush(player_ref);
}

/* Phase 2: do_ban and do_unban moved to external/nd-region */
