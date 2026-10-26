/* world.c — J4.8 step 4: engine boot + world state, split out of
 * x/interface.c.  The bulk of the old main() + the shared struct nd surface
 * live here.  axil is the new ndc: void transport is gone, world boot just
 * opens the store and hands control to the axil module (Phase 4 P3).
 *
 * Owns struct nd, struct nd nd, the persistent world maps, seeds, auth,
 * the command table, and the SI/SIC adapter registration.  The fd registry
 * lives in io.c, the sic/mod machinery in mods.c.
 */

#include <nd/law.h>
#include <nd/world-types.h>
#include <nd/session-types.h>
#include <nd/obj-types.h>
#include <nd/view-types.h>

#include <dirent.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include <arpa/telnet.h>
#include <ttypt/axil.h>
#include <ttypt/axil-tty.h>
#include <ttypt/corm.h>
#include <ttypt/qsys.h>

#include <nd/config.h>
#include <nd/mcp.h>
#include <nd/player.h>
#include <nd/st.h>
#include <nd/entity.h>
#include <nd/map.h>
#include <nd/match.h>
#include <nd/object.h>
#include <nd/skel.h>
#include <nd/type.h>
#include <nd/view.h>

ROO room_zero_room = {
	.flags = RF_HAVEN,
	.doors = 0,
	.exits = 0,
	.floor = 0,
};

struct object room_zero = {
	.location = NOTHING,
	.owner = 1,
	.art_id = 0,
	.type = TYPE_ROOM,
	.value = 9999999,
	.flags = 0,
};

biome_skel_t void_biome_biome = {
	.bg = BLACK,
};

SKEL void_biome = {
	.name = "void",
	.type = TYPE_ROOM,
	.max_art = 1,
};

int euid = 0;
double tick = 0;
unsigned short day_n = 0;
unsigned me = 1;

int presence_refresh(unsigned ref);
int presence_forget(unsigned ref);

void do_avatar(int fd, int argc, char *argv[]);
/* do_ban, do_unban moved to external/nd-region */
void do_chown(int fd, int argc, char *argv[]);
/* do_clone moved to external/nd-obj */
/* do_connect */
void do_connect(int fd, int argc, char *argv[]);
/* do_create, do_drop, do_name, do_recycle moved to external/nd-obj */
/* Phase 5: do_examine, do_get, do_inventory, do_look_at, do_owned, do_pose, do_say, do_view, do_wall moved to external/nd-speech and nd-view */
void do_save(int fd, int argc, char *argv[]);
/* select moved to external/nd-obj */
/* do_status moved to external/nd-obj */
/* do_teleport moved to external/nd-world */
/* do_unban moved to external/nd-region */
/* do_room moved to external/nd-world */
void do_deny(int fd, int argc, char *argv[]);
void do_grant(int fd, int argc, char *argv[]);
void do_toad(int fd, int argc, char *argv[]);
void do_wall(int fd, int argc, char *argv[]);
void do_target(int fd, int argc, char *argv[]);

static unsigned nd_player_login(int fd, char *user);

void map_init(void);
int map_close(unsigned flags);
int map_sync(void);

/* The store lives in the axil data root: axil chdir()s into
 * axil_config.chroot before modules load, so a cwd-relative default is also
 * chroot-relative (-C /var/nd -> /var/nd/std.db; plain dev host -> ./std.db,
 * writable). AXIL_ND_DB still overrides. Non-static: map.c + spacetime.c +
 * the nd_open XY provider share this path. */
void
close_all(int i)
{
	corm_save();

	closelog();
	sync();
	if (i)
		exit(i);
}

/* Module value-type registry. nd_len_reg("equipper", 40) registers that the
 * value type named "equipper" is 40 bytes; hd_mod_open() consults this when a
 * module opens a table with that value kind, so corm stores full structs
 * instead of truncating to CM_U32 (4 bytes). Without this, every module table
 * holding a struct silently lost all but the first 4 bytes on put, and nd_get
 * copied 4 bytes into the caller's (larger) buffer, leaving stack garbage --
 * the "stack smashing" crash that killed the all-19 boot.
 *
 * Fixed cap: modules register on the order of ten types. Names are not copied;
 * modules pass string literals. */
#define MOD_TYPE_MAX 64
static const char *mod_type_name[MOD_TYPE_MAX];
static unsigned mod_type_tid[MOD_TYPE_MAX];
static unsigned mod_type_n;

/* Register a module value type by name. Called by the nd_len_reg XY hook
 * (nd_api.c). */
void
mod_type_register(char *name, unsigned tid)
{
	if (mod_type_n >= MOD_TYPE_MAX)
		return;
	mod_type_name[mod_type_n] = name;
	mod_type_tid[mod_type_n] = tid;
	mod_type_n++;
}

/* Look up a registered value-type ID by name. Returns 0 if not found, in
 * which case the caller falls back to shared_kind(). */
static unsigned
mod_type_lookup(char *name)
{
	for (unsigned i = 0; i < mod_type_n; i++)
		if (!strcmp(mod_type_name[i], name))
			return mod_type_tid[i];
	return 0;
}

static uint32_t
shared_kind(char *t)
{
	if (!strcmp(t, "s"))
		return CM_STR;
	if (!strcmp(t, "p"))
		return CM_PTR;
	return CM_U32;
}

/* The engine's `enum hd` -> corm table map (MODS.md §0.2). Defined here
 * because this is where the tables are opened; uapi/io.h declares it and
 * nd_api.c's providers resolve through hd_resolve(). */
extern unsigned nd_hds[HD_MAX];

/* Tables modules opened with nd_open(), indexed by nd_hd_mod_idx(). The
 * corm handle never leaves this table: a module holds the tag from
 * hd_mod_open() and gets the handle back through hd_resolve(). Fixed cap
 * because the whole module set opens on the order of ten. */
#define HD_MOD_MAX 32
static unsigned hd_mod[HD_MOD_MAX];
static unsigned hd_mod_n;

unsigned
hd_mod_open(char *type, char *iden, char *anon, unsigned flags)
{
	if (hd_mod_n >= HD_MOD_MAX)
		return 0;
	/* flags used to be dropped ((void) flags; ... corm_open(..., 0)), which
	 * silently broke every module table opened with ND_AINDEX (nd-race's
	 * `nd_open("race", "u", "race", ND_AINDEX)`, whose rows are inserted
	 * with a NULL key -- that only works on an auto-indexed map) and every
	 * secondary opened ND_SEC|ND_PGET (nd-race's race_rhd, whose nd_get must
	 * return the PRIMARY key). The ND_* values are deliberately the corm
	 * CM_* values (nd/xy-types.h mirrors them), so this is a plain forward.
	 *
	 * The value type likewise used to be shared_kind(anon) unconditionally,
	 * i.e. CM_U32 for every module table, truncating struct values to 4
	 * bytes. If the module registered a size for this value kind with
	 * nd_len_reg(), use the registered custom type instead. */
	unsigned vtype = mod_type_lookup(anon);
	if (!vtype)
		vtype = shared_kind(anon);
	unsigned h = corm_open(world_db(), type, shared_kind(iden),
		vtype, 0xFF, flags);
	/* corm_open returns 0 on failure; do not burn a registry slot on it. */
	if (!h)
		return 0;
	hd_mod[hd_mod_n] = h;
	return nd_hd_mod(hd_mod_n++);
}



void
shared_init(void)
{
	nd_table_handle_set(HD_FD, hd_resolve(HD_FD));
	nd_table_handle_set(HD_RTYPE, type_hd + 2);
	nd_table_handle_set(HD_RWTS, wts_hd + 2);
}

int
test_handler(socket_t fd, char *body __attribute__((unused)))
{
	axil_respond(fd, 200, "Test ok\r\n");
	return 0;
}

int
nd_world_init(int argc __attribute__((unused)), char **argv __attribute__((unused)))
{
	openlog("nd", LOG_PID | LOG_CONS | LOG_NDELAY, LOG_DAEMON);

	unsigned obj_type = corm_reg(sizeof(OBJ));
	unsigned ent_type = corm_reg(sizeof(ENT));
	unsigned skel_type = corm_reg(sizeof(SKEL));
	unsigned drop_type = corm_reg(sizeof(DROP));
	unsigned element_type = corm_reg(sizeof(element_t));
	unsigned biome_type = corm_reg(sizeof(unsigned) * BIOME_MAX);
	unsigned ai_type = corm_reg(sizeof(action_t));
	unsigned vtf_type = corm_reg(sizeof(vtf_t));

	const char *db = world_db();

	nd_io_init();
	signal(SIGSEGV, close_all);

	/* The region table is record-aware with a string key (ST.md §22.1): one
	 * row per region holding owner + width + module set. The record layout
	 * must be registered before the open, and both halves of the open --
	 * vtype and CM_RECORD(id) -- must name the same registration, or the
	 * open refuses with CM_MISS and every region call below degrades to a
	 * miss instead of indexing corm_heads[CM_MISS]. */
	/* Phase 2: st table initialization moved to external/nd-region */
	nd_table_handle_set(HD_VTF, corm_open(NULL, "vtf", CM_U32, vtf_type, 0xFF, CM_AINDEX));
	nd_table_handle_set(HD_BCP, corm_open(NULL, "bcp", CM_U32, CM_STR, 0xFF, CM_AINDEX));
	nd_table_handle_set(HD_HD, corm_open(NULL, NULL, CM_STR, CM_U32, 0xFF, 0));

	/* action_hd/type_hd are boot-registration maps, not state: every
	 * consumer either iterates the whole map (mcp_actions) or is handed
	 * the offsets below fresh each boot. Persisting them only let a
	 * per-boot re-registration append duplicates. Same family as
	 * vtf_hd/bcp_hd just above: transient, and repopulated
	 * unconditionally below. */
	nd_table_handle_set(HD_ACTION, corm_open(NULL, "action", CM_U32, ai_type, 0xFFFF, CM_AINDEX));
	nd_table_handle_set(HD_TYPE, corm_open(NULL, "ndt", CM_U32, CM_STR, 0xFFFF, CM_AINDEX));
	nd_table_handle_set(HD_ENTITY, corm_open(db, "entity", CM_U32, ent_type, 0xFFFF, 0));
	nd_table_handle_set(HD_PLAYER, corm_open(db, "player", CM_STR, CM_U32, 0xFF, 0));
	nd_table_handle_set(HD_OBJ, corm_open(db, "obj", CM_U32, obj_type, 0xFFFF, CM_AINDEX));
	/* contents/obs are DERIVED indices over persisted sources, not state
	 * in their own right, so they stay transient: contents is a pure
	 * function of obj_hd (one pair per object with location != NOTHING),
	 * and obs is rebuilt by eng_look_at as players move. objects_init
	 * runs unconditionally below and rebuilds contents from obj_hd
	 * before any read, so persisting them could only duplicate pairs and
	 * strand dead-session observers. The persisted state is obj_hd plus
	 * ent_hd.last_observed, which is what survives a reboot. */
	nd_table_handle_set(HD_CONTENTS, corm_open(NULL, "contents", CM_U32, CM_U32, 0xFFFF,
				CM_SORTED | CM_MULTIVALUE));
	nd_table_handle_set(HD_OBS, corm_open(NULL, "obs", CM_U32, CM_U32, 0xFFFF,
			   CM_SORTED | CM_MULTIVALUE));
	/* Phase 3: map_init moved to external/nd-world */

	nd_table_handle_set(HD_SKEL, corm_open(db, "skel", CM_U32, skel_type, 0xFFFF, CM_AINDEX));
	nd_table_handle_set(HD_DROP, corm_open(db, "drop", CM_U32, drop_type, 0xFFFF, CM_AINDEX));
	nd_table_handle_set(HD_ADROP, corm_open(db, "adrop", CM_U32, CM_U32, 0xFFFF,
			     CM_SORTED | CM_MULTIVALUE));
	nd_table_handle_set(HD_ELEMENT, corm_open(db, "element", CM_U32, element_type, 0xFFFF,
			       CM_AINDEX));
	nd_table_handle_set(HD_WTS, corm_open(db, "wts", CM_U32, CM_STR, 0xFFFF, CM_AINDEX));
	nd_table_handle_set(HD_RWTS, corm_open(db, "awts", CM_U32, CM_U32, 0xFFFF,
			    CM_SORTED | CM_MULTIVALUE));
	nd_table_handle_set(HD_BIOME, corm_open(db, "biome", CM_U32, biome_type, 0xFFFF, 0));

	shared_init();
	objects_init(0);
	/* Phase 2: ban table init moved to external/nd-region */

	/* obj_hd persists, so room 0 distinguishes fresh from existing. Decided
	 * once here, before any seeding puts below; every !existed guard in
	 * this function reads it. */
	unsigned zero = 0;
	unsigned existed = (corm_get(obj_hd, &zero) != NULL);
	if (!existed) {
		memcpy(room_zero.data, &room_zero_room, sizeof(room_zero_room));
		corm_put(obj_hd, NULL, &room_zero);
	}

	/* The 20 SIC_AREG() calls that used to stand here are gone. SIC_AREG
	 * was world.c-local and registered every adapter explicitly at boot,
	 * redundantly with the .sic_auto_init sections the SIC_DEFs emitted.
	 * Registration is now a single path: each XY_DEF's AUTO_INIT
	 * constructor in src/nd_events.c (folded into libaxil-nd.c) registers
	 * its own hook. */

	/* type_hd is transient: these two are its only entries, re-registered
	 * every boot in this fixed order. The order is load-bearing, not
	 * incidental — object.c:186 reads type_hd + 1 and world.c sets
	 * nd.hds[HD_RTYPE] = type_hd + 2, both assuming "room" is id 1 and
	 * "entity" is id 2. These seeds run before any module loads below, so
	 * the host claims those ids first on every boot. If a module-facing
	 * type registration API is ever added, whoever seeds here still has to
	 * go first, or those two offsets must become by-name lookups. */
	corm_put(type_hd, NULL, "room");
	corm_put(type_hd, NULL, "entity");

	corm_put(bcp_hd, NULL, "item");
	corm_put(bcp_hd, NULL, "view");
	corm_put(bcp_hd, NULL, "view_buffer");
	corm_put(bcp_hd, NULL, "room");
	corm_put(bcp_hd, NULL, "entity");
	corm_put(bcp_hd, NULL, "auth_failure");
	corm_put(bcp_hd, NULL, "auth_success");
	corm_put(bcp_hd, NULL, "out");
	corm_put(bcp_hd, NULL, "tod");
	corm_put(bcp_hd, NULL, "action");

	/* action_hd is transient, so these are re-registered every boot —
	 * unconditionally, like the bcp_hd puts above. They must precede any
	 * module load below so the built-in actions keep the low ids;
	 * module-registered actions append after. */
	{
		action_t ai;
		strlcpy(ai.label, "look", sizeof(ai.label));
		strlcpy(ai.icon, "\xf0\x9f\x94\x8d", sizeof(ai.icon));
		corm_put(action_hd, NULL, &ai);
		strlcpy(ai.label, "get", sizeof(ai.label));
		strlcpy(ai.icon, "\xf0\x9f\x96\x90\xef\xb8\x8f", sizeof(ai.icon));
		corm_put(action_hd, NULL, &ai);
		strlcpy(ai.label, "drop", sizeof(ai.label));
		strlcpy(ai.icon, "\xf0\x9f\xaa\xa3", sizeof(ai.icon));
		corm_put(action_hd, NULL, &ai);
	}

	if (!existed) {
		element_t spirit = {
			.color = MAGENTA,
			.weakness = ELM_SPIRIT,
		}, fire = {
			.color = RED,
			.weakness = ELM_WATER,
		}, water = {
			.color = BLUE,
			.weakness = ELM_FIRE,
		}, air = {
			.color = WHITE,
			.weakness = ELM_EARTH,
		}, earth = {
			.color = YELLOW,
			.weakness = ELM_AIR,
		}, physical = {
			.color = GREEN,
			.weakness = ELM_SPIRIT,
		};

		corm_put(element_hd, NULL, &spirit);
		corm_put(element_hd, NULL, &fire);
		corm_put(element_hd, NULL, &water);
		corm_put(element_hd, NULL, &air);
		corm_put(element_hd, NULL, &earth);
		corm_put(element_hd, NULL, &physical); /* 5 */

		SENT adam_sent = {
			.y = 255,
		};

		SKEL adam = {
			.name = "human",
			.type = TYPE_ENTITY,
			.max_art = 300,
		};

		memcpy(adam.data, &adam_sent, sizeof(adam_sent));
		corm_put(skel_hd, NULL, &adam);     /* 0 */
		corm_put(wts_hd, NULL, "punch");    /* 0 */

		unsigned biome_map[BIOME_MAX];
		memcpy(void_biome.data, &void_biome_biome, sizeof(void_biome_biome));
		unsigned void_ref = corm_put(skel_hd, NULL, &void_biome);
		for (unsigned i = 0; i < BIOME_MAX; i++)
			biome_map[i] = void_ref;
		void_ref = 16;
		corm_put(biome_hd, &void_ref, biome_map);
	}

	/* objects_init() moved to libnd-obj xy_install */

	/* Nothing to do here for modules on any boot: game modules live in
	 * region rows and restore from on_axil_post_chroot(), i.e. after -C
	 * has done its chroot, so everything they load resolves inside the
	 * jail. */

	srand(getpid());

	setenv("TERM", "xterm-256color", 1);

	axil_register_handler("/test", &test_handler);

	WARN("Done.\n");

	return 0;
}

void
do_sh(int fd, int argc __attribute__((unused)), char *argv[] __attribute__((unused)))
{
	/* Refusal happens in axil-tty; a non-zero return means the gate bit, and
	 * the log line is the only trace of a probe that never allocated. */
	if (axil_tty_shell(fd) != 0)
		WARN("sh refused on %d\n", fd);
}

static int
help_topic_cmp(const void *a, const void *b)
{
	return strcmp(*(char *const *)a, *(char *const *)b);
}

/* List every installed help topic, one per line.  Read from man/ rather than a
 * compiled-in list, so `help` can never advertise a page that was not built or
 * hide one that was; man/ is generated from man-src/ by `make man`.  A bare
 * `help`/`man` lands here. */
static void
nd_help_index(unsigned player_ref)
{
	char man_dir[BUFSIZ];
	DIR *dir = NULL;
	struct dirent *de;
	char **topics = NULL;
	size_t n = 0, cap = 0;

	if (nd_asset_dir(ND_ASSET_MAN, man_dir, sizeof(man_dir)))
		dir = opendir(man_dir);
	if (!dir)
		dir = opendir("man");

	if (dir) {
		while ((de = readdir(dir)) != NULL) {
			size_t len = strlen(de->d_name);

			if (len <= 3 || strcmp(de->d_name + len - 3, ".10") != 0)
				continue;
			if (n == cap) {
				char **grown;

				cap = cap ? cap * 2 : 64;
				grown = realloc(topics, cap * sizeof(*topics));
				if (!grown)
					break;
				topics = grown;
			}
			topics[n] = strndup(de->d_name, len - 3);
			if (topics[n])
				n++;
		}
		closedir(dir);
	}

	if (n > 0) {
		qsort(topics, n, sizeof(*topics), help_topic_cmp);
		nd_writef(player_ref, "%zu help topics:\n", n);
		for (size_t i = 0; i < n; i++) {
			nd_writef(player_ref, "  %s\n", topics[i]);
			free(topics[i]);
		}
		free(topics);
	} else {
		nd_writef(player_ref, "No help topics installed.\n");
	}
	nd_writef(player_ref, "Type `help <topic>` for a single topic.\n");
	eng_nd_flush(player_ref);
}

void
do_man(int fd, int argc, char *argv[])
{
	const char *topic;
	char path[BUFSIZ];
	WARN("do_man called: fd=%d argc=%d argv0=%s argv1=%s\n", fd, argc, argv[0], argc > 1 ? argv[1] : "");

	/* No topic: the index of everything available. `help begin` still opens
	 * the tutorial, and the browser client's help button sends exactly that. */
	if (argc <= 1 || !argv[1] || !*argv[1]) {
		nd_help_index(fd_player(fd));
		return;
	}
	topic = argv[1];

	/* The topic reaches an execve argv below. A slash escapes "man/<topic>.10"
	 * into an arbitrary-file read (man -l preprocesses through cat), and a
	 * leading dash turns the fallback branch's positional into a man flag
	 * (-K, --pager=). Legitimate topics are bare command names, so refuse all
	 * three outright. */
	if (strchr(topic, '/') || strstr(topic, "..") || *topic == '-') {
		WARN("man: refused topic '%s' on %d\n", topic, fd);
		axil_write(fd, "No manual entry.\r\n", strlen("No manual entry.\r\n"));
		return;
	}

	char man_dir[BUFSIZ / 2];
	if (nd_asset_dir(ND_ASSET_MAN, man_dir, sizeof(man_dir)))
		snprintf(path, sizeof(path), "%s/%s.10", man_dir, topic);
	else
		snprintf(path, sizeof(path), "man/%s.10", topic);

	if (access(path, R_OK) == 0) {
		char *rargv[] = { "/usr/bin/man", "-P", "cat", "-l", path, NULL };
		axil_tty_exec(fd, rargv);
	} else {
		char *rargv[] = { "/usr/bin/man", "-P", "cat", "-s", "10", (char *)topic, NULL };
		axil_tty_exec(fd, rargv);
	}
}

struct cmd_slot cmds[] = {
	{
		.name = "sh",
		.cb = &do_sh,
	}, {
		.name = "GET",
		.cb = &do_GET,
		.flags = CF_NOAUTH | CF_NOTRIM,
	}, {
		.name = "POST",
		.cb = &do_POST,
		.flags = CF_NOAUTH | CF_NOTRIM,
	}, {
		.name = "PRI",
		.cb = &do_GET,
		.flags = CF_NOAUTH | CF_NOTRIM,
	}, {
		.name = "connect",
		.cb = &do_connect,
		.flags = CF_NOAUTH,
	}, {
		.name = "avatar",
		.cb = &do_avatar,
		/* Phase 5: speech/look/match/view commands moved to modules */
	}, {
		.name = "man",
		.cb = &do_man,
	}, {
		.name = "help",
		.cb = &do_man,
	}, {
		.name = "save",
		.cb = &do_save,
		/* Phase 4: status moved to external/nd-obj */
		/* Phase 3: room moved to external/nd-world */
	}, {
		/* Phase 2: planet, planets, here, loadmod, unloadmod, modlist,
		 * release, target, deny, grant moved to external/nd-region */
		.name = NULL,
		.cb = NULL,
	},
};

/* Expose the nd command table to axil's dispatcher.  GET/POST/PRI are axil
 * core HTTP commands (do_GET/do_POST in libaxil.so) — registering them here
 * would shadow axil's request_handle routing, so they stay reserved. */
void
nd_session_register_commands(void)
{
	for (size_t i = 0; cmds[i].cb; i++) {
		if (strcmp(cmds[i].name, "GET") == 0 ||
		    strcmp(cmds[i].name, "POST") == 0 ||
		    strcmp(cmds[i].name, "PRI") == 0)
			continue;

		nd_register(cmds[i].name, cmds[i].cb, cmds[i].flags);
		WARN("session: registered command '%s'\n", cmds[i].name);
	}
}

void
do_save(int fd, int argc __attribute__((unused)), char *argv[] __attribute__((unused)))
{
	unsigned player_ref = fd_player(fd);

	if (player_ref != 1) {
		nd_writef(player_ref, "Only root can save\n");
		return;
	}

	corm_save();
}

static inline void
avatar(OBJ *player)
{
	const SKEL *skel = corm_get(skel_hd, &player->skid);

	player->art_id = 1 + (random() % (skel && skel->max_art ? skel->max_art : 1));
}

void
do_avatar(int fd, int argc __attribute__((unused)), char *argv[] __attribute__((unused)))
{
	unsigned player_ref = fd_player(fd);
	OBJ player;
	memset(&player, 0, sizeof(player));
	if (corm_read(obj_hd, &player_ref, &player, sizeof(player)) != 0)
		return;
	avatar(&player);
	corm_put(obj_hd, &player_ref, &player);
	mcp_content_out(player.location, player_ref);
	mcp_content_in(player.location, player_ref);
}

static inline void mcp_actions(unsigned player_ref) {
	unsigned c = corm_iter(action_hd, NULL, 0);
	const void *kp, *vp;

	while (corm_next(&kp, &vp, c)) {
		const action_t *ai = vp;

		mcp_action(player_ref, *(const unsigned *)kp, (char *)ai->label,
			   (char *)ai->icon);
	}
	corm_fin(c);
}

unsigned
auth(unsigned fd)
{
	/* Identity source: REMOTE_USER, set by axil_auth() (WS-phase handler
	 * or -A auto-auth).  cmd_proc's auth gate (libaxil.c) already dropped
	 * non-CF_NOAUTH commands pre-auth, so raw guests only boot via
	 * do_connect — the dead QSESSION branch is gone. */
	char env_user[BUFSIZ];

	if (axil_env_get(fd, env_user, sizeof(env_user), "REMOTE_USER") == 0 &&
	    *env_user)
		return nd_player_login(fd, env_user);

	mcp_auth_fail(fd, 3);
	return 0;
}

/* The editable login notice, shown on every login AFTER the room and map.
 * Read from `motd` relative to cwd (like man/ and art/), so an operator can
 * replace the default without a rebuild; the built-in fallback keeps a fresh
 * checkout or stripped install working.  Shipped by install-data to
 * share/axil-nd/motd. */
static const char nd_motd_default[] =
	"Welcome to Neverdark.\n"
	"\n"
	"Type `help` for the topic list, or `help begin` for a tour.\n"
	"To shape the world, load a game module or write your own:\n"
	"  help modules   installing and writing game modules\n"
	"  help loadmod   load a module into a region you rule\n"
	"  grant          hand a region to another player\n"
	"  sh             open a shell to edit and build code\n";

static void
nd_motd_show(unsigned player_ref)
{
	char dir[BUFSIZ - 32], path[BUFSIZ], buf[BUFSIZ];
	FILE *f = NULL;
	const char *text = nd_motd_default;

	if (nd_asset_dir(ND_ASSET_MOTD, dir, sizeof(dir))) {
		snprintf(path, sizeof(path), "%s/motd", dir);
		f = fopen(path, "r");
	}
	if (!f)
		f = fopen("motd", "r");

	if (f) {
		size_t len = fread(buf, 1, sizeof(buf) - 2, f);

		fclose(f);
		if (len > 0) {
			if (buf[len - 1] != '\n')
				buf[len++] = '\n';
			buf[len] = '\0';
			text = buf;
		}
	}

	nd_writef(player_ref, "%s", text);
	eng_nd_flush(player_ref);
}

/* Re-anchor a player to a new socket on (re)login: writes must go to the
 * socket the player is logging in on, not to a descriptor that may already
 * be gone.  Traced with gdb: a raw guest re-running `connect <name>` for a
 * name already held by a WebSocket player left the old fd in the write set,
 * eng_nd_wwrite() sent on stale fd 5, and the process died.  nd_io_attach()
 * (io.c) replaces rather than appends, so a relogin re-points the player. */
void nd_io_attach(unsigned fd, unsigned player_ref);

/* Shared create/re-login + landing for WS (auth via REMOTE_USER) and raw
 * guests (do_connect).  axil_auth() here is what flips DF_AUTHENTICATED so
 * subsequent commands pass cmd_proc's auth gate. */
static unsigned
nd_player_login(int fd, char *user)
{
	OBJ player;
	unsigned player_ref = player_get(user);

	WARN("nd_player_login: '%s' (%u/%u)\n", user, fd, player_ref);

	if (player_ref == NOTHING) {
		player_ref = eng_object_add(&player, 0, NOTHING, 0, OF_PLAYER);
		strlcpy(player.name, user, sizeof(player.name));
		player.value = 150;
		player_put(user, player_ref);
		avatar(&player);

		nd_io_attach(fd, player_ref);
		corm_put(obj_hd, &player_ref, &player);

		nd_evt_new_player(player_ref);
	} else {
		/* Entry-only enforcement (ST.md §27.6(1) §7): login is always
		 * allowed, and a ban bites on arrival instead. "Excluded from
		 * region R" is meaningless for a login that spawns into the
		 * void, and a login refusal would turn a cosmos-wide ban into a
		 * permanent lockout. */
		nd_io_attach(fd, player_ref);
	}

	/* axil_auth() is called for what it DOES, not for what it returns: it
	 * records the passwd entry and flips DF_AUTHENTICATED, which cmd_proc's
	 * auth gate requires before any command runs. Its return is ADVISORY and
	 * means "this name has no passwd entry", not "not authenticated" --
	 * axil.h: "Returns 0 on success, 1 if the name is unknown to the system
	 * (the connection is still marked authenticated)", and "The 0/1 return is
	 * ADVISORY ... A caller that wants to reject an unknown name must check
	 * for itself".
	 *
	 * Reading that 1 as a rejection silently degraded every site login: every
	 * axil-auth-registered account has no passwd entry -- getpwnam() fails
	 * for all of them -- so the login bailed out here, BEFORE
	 * mcp_auth_success/mcp_actions/do_view. A site user got a working
	 * connection but never received AUTH_SUCCESS, their room view, or their
	 * action table at login. An account with a session cookie is
	 * authenticated -- auth() only gets here with REMOTE_USER set, which
	 * axil's platform auth populates from that session or from -A -- and the
	 * privilege fallback for a name with no passwd entry runs as the server's
	 * own identity, never uid 0 (axil.h), so completing the login grants
	 * nothing extra. Keep it visible, though: the name is not a system user. */
	if (axil_auth(fd, user))
		WARN("'%s': no passwd entry; authenticated by session only\n", user);

	mcp_auth_success(player_ref);
	mcp_actions(player_ref);
	eng_look_at(player_ref, NOTHING);
	presence_refresh(player_ref);
	nd_view(player_ref ? player_ref : fd_player(fd));
	if (day_n)
		mcp_tod(player_ref, 1);
	else
		mcp_tod(player_ref, 0);

	nd_motd_show(player_ref);

	nd_evt_auth(player_ref);
	return player_ref;
}

void nd_event_announce(unsigned player_ref, unsigned loc_ref);

/* Credential check owned by axil-auth, reached through the xy bus. Declared
 * (not implemented) here, exactly like axil_tty_exec above: XY_DECL expands to
 * a dispatcher. Non-zero means valid BY CONVENTION -- the bus zero-fills the
 * result when no module implements the hook, so a "0 means valid" predicate
 * would authenticate everyone the moment axil-auth is absent. */
XY_DECL(int, auth_password_matches,
	const char *, username,
	const char *, password);

/* Whether xy_install() managed to load libaxil-auth. The bus convention above
 * already fails closed without it; this flag exists so the prompt can say so
 * instead of hanging at "Password: ". */
static int nd_auth_ready = 0;

void
nd_set_auth_ready(int ready)
{
	nd_auth_ready = ready ? 1 : 0;
}

void
do_connect(int fd, int argc, char *argv[])
{
	char pending[64];

	/* Passworded login in two phases: `connect <name>` arms a one-shot
	 * password prompt on this descriptor, and the next line is the password.
	 * A name alone never authenticates -- the old passwordless form is gone.
	 * Pending state lives in the descriptor environment, which axil frees on
	 * close, so a recycled fd can never inherit someone else's prompt. */
	if (argc < 2 || !argv[1] || !*argv[1]) {
		axil_write(fd, "Usage: connect <name>\r\n", strlen("Usage: connect <name>\r\n"));
		return;
	}

	/* The pending slot is 63 chars; a longer name can never verify (auth
	 * usernames are bounded well below that), so refuse it here rather than
	 * storing a truncated name that fails confusingly later. */
	if (strlen(argv[1]) >= sizeof(pending)) {
		axil_write(fd, "Usage: connect <name>\r\n", strlen("Usage: connect <name>\r\n"));
		return;
	}

	if (axil_env_get(fd, pending, sizeof(pending), "ND_PWPEND") == 0 && *pending) {
		axil_write(fd, "Answer the pending password prompt first.\r\n",
		    strlen("Answer the pending password prompt first.\r\n"));
		return;
	}

	if (!nd_auth_ready) {
		axil_write(fd, "Password authentication is unavailable.\r\n",
		    strlen("Password authentication is unavailable.\r\n"));
		return;
	}

	axil_env_put(fd, "ND_PWPEND", argv[1]);
	/* IAC WILL ECHO: "I will echo, you will not." The server then sends
	 * nothing back for the password line, so it never appears on screen or
	 * in scrollback. Restored to WONT ECHO once the line is read. The prompt
	 * ends with a newline: line-oriented readers (and this engine's own
	 * transcript drains) can otherwise lose a newline-less prompt when
	 * their read times out mid-line. */
	static const unsigned char will_echo[] = { 255, 251, 1 };
	axil_write(fd, (void *)will_echo, sizeof(will_echo));
	axil_write(fd, "Password: \r\n", strlen("Password: \r\n"));
}

/* Second phase of connect: the line that arrived while ND_PWPEND was armed.
 * Called from on_axil_parse (libaxil-nd.c), which already stripped telnet
 * negotiation and cut the line. Clears the prompt before verifying, fails
 * closed and uniformly, and closes on failure so each guess costs a reconnect. */
void
nd_password_line(int fd, const char *name, const char *password)
{
	static const unsigned char wont_echo[] = { 255, 252, 1 };

	axil_env_put(fd, "ND_PWPEND", "");
	axil_write(fd, (void *)wont_echo, sizeof(wont_echo));
	axil_write(fd, "\r\n", 2);

	if (!nd_auth_ready || !auth_password_matches(name, password)) {
		/* Uniform on purpose: unknown user, wrong password, and an
		 * unavailable service are indistinguishable, so the prompt cannot
		 * be used to enumerate accounts. */
		WARN("connect: failed password login for '%s' on %d\n", name, fd);
		axil_write(fd, "Invalid name or password.\r\n",
		    strlen("Invalid name or password.\r\n"));
		axil_close(fd);
		return;
	}

	unsigned player_ref = nd_player_login(fd, (char *)name);

	if (player_ref && player_ref != NOTHING) {
		nd_io_attach(fd, player_ref);
		nd_event_announce(player_ref, player_ref);
		eng_nd_flush(player_ref);
	}
}

int
nd_connect(int fd)
{
	return auth(fd);
}

void
nd_disconnect(int fd)
{
	if (!(axil_flags(fd) & DF_AUTHENTICATED))
		return;

	unsigned player_ref = fd_player(fd);
	const OBJ *player = corm_get(obj_hd, &player_ref);

	WARN("%s(%u) on fd %d\n", player ? player->name : "?",
	     player_ref, fd);
	nd_io_detach(fd);
	/* Purge transient presence on disconnect to prevent phantom claims */
	presence_forget(player_ref);
}

void
nd_update(unsigned long long dt)
{
	/* dt is microseconds, so fdt is real seconds and the gate below fires
	 * once per second. mul is day ticks per update: 0.5 stretches the
	 * 1 << DAYTICK_Y (1024) cycle to 2048 s, ~34 min, sunset at ~17 min. */
	double mul = 0.5;
	double fdt = dt / 1000000.0;

	tick += fdt;
	if (tick > 1.0) {
		tick -= 1.0;
		objects_update(1.0 * mul);
	}
}

void
nd_vim(int fd, int argc __attribute__((unused)), char *argv[]) {
	if (!(axil_flags(fd) & DF_AUTHENTICATED))
		return;

	if (!argv || !argv[0] || !*argv[0])
		return;

	unsigned player_ref = fd_player(fd);
	if (!player_ref)
		return;

	char const *s = argv[0];
	unsigned pos = 0;
	sic_str_t ss = { .str = "", .pos = 0 };

	strlcpy(ss.str, argv[0], sizeof(ss.str));

	for (; s[pos]; ) {
		unsigned old_pos = pos;
		int ret = st_v(player_ref, s + pos);

		pos += ret < 0 ? - ret : ret;
		ss.pos = pos;
		ret = nd_evt_vim(player_ref, ss);
		pos += ret < 0 ? - ret : ret;

		if (pos == old_pos && s[pos])
			pos++;
	}
}

void
nd_command(int fd, int argc __attribute__((unused)), char *argv[] __attribute__((unused)))
{
	me = fd_player(fd);
	nd_io_reset(fd);
}


