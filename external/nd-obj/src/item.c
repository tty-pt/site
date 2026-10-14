#include <stdlib.h>

#include <ttypt/xy-mod.h>

#include <nd/xy.h>

/* Moved from external/axil-nd/src/item.c (Phase 2.4b): the `select` command.
 * Engine helpers replaced with law hooks (bus dispatch to the kernel
 * providers): fd_player, ent_get/ent_set. No uapi includes -- xy-types.h
 * supplies ENT (byte-identical, verified). */

void
do_select(int fd, int argc __attribute__((unused)), char *argv[] __attribute__((unused)))
{
	unsigned player_ref = fd_player(fd);
	const char *n_s = argv[1];
	unsigned n = strtoul(n_s, NULL, 0);
	ENT eplayer = ent_get(player_ref);
	eplayer.select = n;
	ent_set(player_ref, &eplayer);
}
