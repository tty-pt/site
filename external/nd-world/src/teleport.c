#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <nd/xy.h>

#define NOMATCH_MESSAGE "I don't know what you mean.\n"
#define CANTDO_MESSAGE "You can't do that.\n"

void
do_teleport(int fd, int argc __attribute__((unused)), char *argv[])
{
	unsigned player_ref = fd_player(fd), victim_ref, destination_ref;
	char *arg1, *arg2, *to;

	if (!argv[1] || !*argv[1]) {
		nd_printf(player_ref, "Usage: teleport [<victim>] <destination>\n");
		nd_flush(player_ref);
		return;
	}
	arg1 = argv[1];
	arg2 = (argv[2] && *argv[2]) ? argv[2] : "";

	if (*arg2 == '\0') {
		victim_ref = player_ref;
		to = arg1;
	} else if ((victim_ref = ematch_all(player_ref, arg1)) == NOTHING) {
		nd_printf(player_ref, NOMATCH_MESSAGE);
		nd_flush(player_ref);
		return;
	} else
		to = arg2;

	OBJ victim;
	nd_get(HD_OBJ, &victim, &victim_ref);

	destination_ref = ematch_all(player_ref, to);
	if (destination_ref == NOTHING) {
		nd_printf(player_ref, NOMATCH_MESSAGE);
		nd_flush(player_ref);
		return;
	}

	OBJ destination;
	nd_get(HD_OBJ, &destination, &destination_ref);

	if (victim_ref == destination_ref || destination.location == victim_ref)
		goto error;

	switch (victim.type) {
	case TYPE_ENTITY:
		if (!controls(player_ref, victim_ref) ||
				!controls(player_ref, destination_ref) ||
				!controls(player_ref, victim.location) ||
				destination.type != TYPE_ROOM)
			goto error;
		nd_printf(victim_ref, "You feel a wrenching sensation...\n");
		enter(victim_ref, destination_ref, E_NULL);
		nd_flush(victim_ref);
		nd_flush(player_ref);
		return;
	case TYPE_ROOM:
		goto error;
	default:
		if (!(
			controls(player_ref, destination_ref) &&
			(controls(player_ref, victim_ref) || controls(player_ref, victim.location)) )
		   )
			goto error;
		object_move(victim_ref, destination_ref);
		nd_flush(player_ref);
		return;
	}
error:
	nd_printf(player_ref, CANTDO_MESSAGE);
	nd_flush(player_ref);
}
