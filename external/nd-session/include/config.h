#ifndef _CONFIG_H
#define _CONFIG_H

#include <stdlib.h>

#define BUFFER_LEN 8192
#define STD_DB "/var/nd/std.db"

extern int euid;

static inline const char *
world_db(void)
{
	const char *db = getenv("AXIL_ND_DB");
	return (db && *db) ? db : "std.db";
}

#endif
