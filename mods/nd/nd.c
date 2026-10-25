#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ttypt/xy-mod.h>
#include <ttypt/axil.h>
#include <bud/bud.h>
#include <nd/bcp.h>
#include <nd/ui-slot.h>

#include "ux/nd_ui.h"
#include "../common/common.h"

static int
seed_row(const char *const *f, size_t n, void *user)
{
	nd_client_state_t *st = (nd_client_state_t *)user;
	if (n < 2)
		return 0;

	if (!strcmp(f[0], "room") && n >= 5) {
		st->room.ref = (uint32_t)strtoul(f[1], NULL, 0);
		st->room.exits = (uint32_t)strtoul(f[2], NULL, 0);
		strncpy(st->room.name, f[3], sizeof(st->room.name) - 1);
		strncpy(st->room.desc, f[4], sizeof(st->room.desc) - 1);
	} else if (!strcmp(f[0], "map") && n >= 2) {
		/* Unescape \n into real newlines */
		size_t m = 0;
		for (const char *p = f[1]; *p && m + 1 < sizeof(st->map_buf); p++) {
			if (*p == '\\' && p[1] == 'n') {
				st->map_buf[m++] = '\n';
				p++;
			} else {
				st->map_buf[m++] = *p;
			}
		}
		st->map_buf[m] = '\0';
	} else if (!strcmp(f[0], "action") && n >= 4) {
		if (st->action_count < ND_MAX_ACTIONS) {
			size_t idx = st->action_count++;
			st->actions[idx].id = (uint32_t)strtoul(f[1], NULL, 0);
			strncpy(st->actions[idx].label, f[2], sizeof(st->actions[idx].label) - 1);
			strncpy(st->actions[idx].icon, f[3], sizeof(st->actions[idx].icon) - 1);
		}
	}
	return 0;
}

static void
nd_seed_state_from_data(nd_client_state_t *st)
{
	/* Open universe_seed.dsv from AXIL_ND_DATA, dirname(AXIL_ND_DB), var/nd, or mods/nd/data */
	FILE *fp = NULL;
	const char *ev;
	char path[512];

	if ((ev = getenv("AXIL_ND_DATA")) && *ev) {
		snprintf(path, sizeof(path), "%s/universe_seed.dsv", ev);
		fp = fopen(path, "r");
	}
	if (!fp && (ev = getenv("AXIL_ND_DB")) && *ev) {
		char dbdir[400], *slash;
		snprintf(dbdir, sizeof(dbdir), "%s", ev);
		if ((slash = strrchr(dbdir, '/')))
			*slash = '\0';
		else
			snprintf(dbdir, sizeof(dbdir), ".");
		snprintf(path, sizeof(path), "%s/universe_seed.dsv", dbdir);
		fp = fopen(path, "r");
	}
	if (!fp)
		fp = fopen("var/nd/universe_seed.dsv", "r");
	if (!fp)
		fp = fopen("mods/nd/data/universe_seed.dsv", "r");

	if (fp) {
		char line[1024];
		while (fgets(line, sizeof(line), fp)) {
			const char *fields[16];
			size_t n = 0;
			char *p = line, *nl;
			if ((nl = strpbrk(line, "\r\n")))
				*nl = '\0';
			while (*p == ' ' || *p == '\t')
				p++;
			if (!*p || *p == '#')
				continue;
			if (!strncmp(p, "map|", 4)) {
				fields[0] = "map";
				fields[1] = p + 4;
				n = 2;
			} else {
				for (;;) {
					char *bar = strchr(p, '|');
					if (n < 16)
						fields[n++] = p;
					if (!bar)
						break;
					*bar = '\0';
					p = bar + 1;
				}
			}
			seed_row(fields, n, st);
		}
		fclose(fp);
	}

	/* Fallback defaults if no seed table was found or fields were missing */
	if (!st->room.ref) {
		st->room.ref = 1001;
		st->room.exits = 15;
		strncpy(st->room.name, "The Citadel Common Hall", sizeof(st->room.name) - 1);
		strncpy(st->room.desc, "A grand hall with towering stone arches and banners.", sizeof(st->room.desc) - 1);
	}
	if (!st->map_buf[0]) {
		strncpy(st->map_buf,
			"+---+---+---+\n"
			"|   |   |   |\n"
			"+---#   #---+\n"
			"|   # @ #   |\n"
			"+---#   #---+\n"
			"|   |   |   |\n"
			"+---+---+---+",
			sizeof(st->map_buf) - 1);
	}
	if (st->map_data.width == 0) {
		st->map_data.width = 7;
		st->map_data.height = 7;
		st->map_data.player_x = 3;
		st->map_data.player_y = 3;
		st->map_data.room_ref = st->room.ref;
		for (int i = 0; i < 49; i++) {
			st->map_data.tiles[i].symbol = '.';
			st->map_data.tiles[i].flags = ND_MAP_FLAG_ROOM;
			st->map_data.tiles[i].fg = 7;
		}
		st->map_data.tiles[24].symbol = '@';
		st->map_data.tiles[24].flags |= ND_MAP_FLAG_PLAYER;
		st->map_data.tiles[24].fg = 15;
	}
	if (st->action_count == 0) {
		st->action_count = 3;
		st->actions[0].id = 0;
		strncpy(st->actions[0].label, "look", sizeof(st->actions[0].label) - 1);
		strncpy(st->actions[0].icon, "\xf0\x9f\x94\x8d", sizeof(st->actions[0].icon) - 1);
		st->actions[1].id = 1;
		strncpy(st->actions[1].label, "get", sizeof(st->actions[1].label) - 1);
		strncpy(st->actions[1].icon, "\xf0\x9f\x96\x90\xef\xb8\x8f", sizeof(st->actions[1].icon) - 1);
		st->actions[2].id = 2;
		strncpy(st->actions[2].label, "drop", sizeof(st->actions[2].label) - 1);
		strncpy(st->actions[2].icon, "\xf0\x9f\xaa\xa3", sizeof(st->actions[2].icon) - 1);
	}
}

XY_IMPL(int, nd_ssr_respond, socket_t, fd)
{
	char user[ENV_VALUE_LEN] = {0};
	axil_env_get(fd, user, sizeof(user), "REMOTE_USER");

	nd_client_state_t st;
	memset(&st, 0, sizeof(st));
	/* Modules seed their own defaults (vitals, attributes, ...). */
	nd_ui_glue_state_default(&st);
	if (user[0]) {
		strncpy(st.user, user, sizeof(st.user) - 1);
	}
	nd_seed_state_from_data(&st);

	char state_json[8192];
	nd_state_serialize(&st, state_json, sizeof(state_json));

	char extra_head[9216];
	snprintf(
		extra_head, sizeof(extra_head),
		"<link rel=\"stylesheet\" href=\"/nd.css\">\n"
		"<script type=\"application/json\" id=\"bud-state\">%s</script>\n",
		state_json);

	bud_node *body = nd_ui_render(&st);
	return site_ui_respond_page(
		fd, "NeverDark", "/nd", "🕹️", user, extra_head, "nd", body);
}

XY_MODULE_API void
xy_install(void)
{
	xy_load("./mods/common/common");
}
