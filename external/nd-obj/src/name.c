#include <string.h>

#include <ttypt/xy-mod.h>

#include <nd/xy.h>

/* Moved from external/axil-nd/src/object.c (Phase 2.4b): do_name.
 * Engine helpers replaced with hooks (nd_printf, ematch_all, controls,
 * nd_get/nd_put); ok_name copied as static (NUMBER_TOKEN '#' and ESCAPE_CHAR
 * 27 from engine include/params.h; object.c keeps its copy for do_create --
 * both static, no conflict ever); message literals copied from engine
 * include/uapi/match.h. do_chown stays until player_get hook exists. */

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
do_name(int fd, int argc __attribute__((unused)), char *argv[])
{
	unsigned player_ref = fd_player(fd);
	char *name = argv[1];
	char *newname = argv[2];
	unsigned thing_ref = ematch_all(player_ref, name);

	if (thing_ref == NOTHING) {
		nd_printf(player_ref, NOMATCH_MESSAGE);
		return;
	}

	if (!controls(player_ref, thing_ref) || !*newname || !ok_name(newname)) {
		nd_printf(player_ref, CANTDO_MESSAGE);
		return;
	}

	OBJ thing;
	nd_get(HD_OBJ, &(thing), &thing_ref);
	strlcpy(thing.name, newname, sizeof(thing.name));
	nd_put(HD_OBJ, &thing_ref, &thing);
}
