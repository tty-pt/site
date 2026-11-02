#ifndef ND_WORLD_TYPES_H
#define ND_WORLD_TYPES_H

/*
 * world-types.h — shared value types for the NeverDark reference universe.
 *
 * Defines the GAME data types that nd/world.h (service hooks) and
 * nd/world-events.h (events) pass by value/pointer: OBJ, ENT, SKEL, room,
 * exit, bio, icon, color, etc.
 *
 * Convention, not law.
 */

#include <stddef.h>
#include <stdint.h>

#include <ttypt/xy.h>

#include <nd/law-types.h>
#include <nd/law.h>

/* --- game universe table indices ------------------------------------------ */

enum hd {
	HD_FD = ND_HD_FD,
	HD_SKEL,
	HD_DROP,
	HD_ADROP,
	HD_BIOME,
	HD_WTS,
	HD_RWTS,
	HD_OBJ,
	HD_OBS,
	HD_CONTENTS,
	HD_TYPE,
	HD_RTYPE,
	HD_BCP,
	HD_ELEMENT,
	HD_HD,
	HD_ENTITY,
	HD_MAP_W,
	HD_PLAYER,
	HD_OWNER,
	HD_VTF,
	HD_ACTION,
	HD_MAX,
};

#define biome_hd nd_table_handle(HD_BIOME)
#define map_w_hd nd_table_handle(HD_MAP_W)
#define element_hd nd_table_handle(HD_ELEMENT)
#define wts_hd nd_table_handle(HD_WTS)
#define awts_hd nd_table_handle(HD_RWTS)
#define hd_hd nd_table_handle(HD_HD)

/* --- azoth.h ------------------------------------------------------------- */

#ifndef AZOTH_H
#define AZOTH_H

#define ANSI_BOLD	"\033[1m"
#define ANSI_DIM      	"\033[2m"
#define ANSI_ITALIC   	"\033[3m"
#define ANSI_UNDERLINE	"\033[4m"
#define ANSI_FLASH	"\033[5m"
#define ANSI_REVERSE	"\033[7m"
#define ANSI_OSTRIKE	"\033[9m"

#define ANSI_RESET_BOLD	ANSI_DIM

#define ANSI_FG_BLACK	"\033[30m"
#define ANSI_FG_RED	"\033[31m"
#define ANSI_FG_YELLOW	"\033[33m"
#define ANSI_FG_GREEN	"\033[32m"
#define ANSI_FG_CYAN	"\033[36m"
#define ANSI_FG_BLUE	"\033[34m"
#define ANSI_FG_MAGENTA	"\033[35m"
#define ANSI_FG_WHITE	"\033[37m"

#define ANSI_BG_BLACK	"\033[40m"
#define ANSI_BG_RED	"\033[41m"
#define ANSI_BG_YELLOW	"\033[43m"
#define ANSI_BG_GREEN	"\033[42m"
#define ANSI_BG_CYAN	"\033[46m"
#define ANSI_BG_BLUE	"\033[44m"
#define ANSI_BG_MAGENTA	"\033[45m"
#define ANSI_BG_WHITE	"\033[47m"

#define ANSI_RESET	"\033[0m"

enum color {
	BLACK,
	RED,
	YELLOW,
	GREEN,
	CYAN,
	BLUE,
	MAGENTA,
	WHITE
};

enum pi_flags {
	BOLD = 1,
};

struct print_info {
	enum color fg;
	unsigned flags;
};

static char *ansi_bg[] __attribute__((unused)) = {
	[BLACK] = ANSI_BG_BLACK,
	[RED] = ANSI_BG_RED,
	[YELLOW] = ANSI_BG_YELLOW,
	[GREEN] = ANSI_BG_GREEN,
	[CYAN] = ANSI_BG_CYAN,
	[BLUE] = ANSI_BG_BLUE,
	[MAGENTA] = ANSI_BG_MAGENTA,
	[WHITE] = ANSI_BG_WHITE,
};

static char *ansi_fg[] __attribute__((unused)) = {
	[BLACK] = ANSI_FG_BLACK,
	[RED] = ANSI_FG_RED,
	[YELLOW] = ANSI_FG_YELLOW,
	[GREEN] = ANSI_FG_GREEN,
	[CYAN] = ANSI_FG_CYAN,
	[BLUE] = ANSI_FG_BLUE,
	[MAGENTA] = ANSI_FG_MAGENTA,
	[WHITE] = ANSI_FG_WHITE,
};

#endif /* AZOTH_H */

/* --- object.h ------------------------------------------------------------ */

#define ROOT ((unsigned) 1)
#define NOTHING ((unsigned) -1)

enum object_flags {
	OF_PLAYER = 1,
	OF_INF = 1,
};

enum room_flags {
	RF_TEMP = 1,
	RF_HAVEN = 2,
	RF_MODIFIED = 4,
};

enum exit {
	E_NULL = 0,
	E_WEST = 1,
	E_NORTH = 2,
	E_UP = 4,
	E_EAST = 8,
	E_SOUTH = 16,
	E_DOWN = 32,
	E_ALL = 63,
};

typedef struct {
	unsigned flags;
	unsigned char exits;
	unsigned char doors;
	unsigned char floor;
} ROO;

enum entity_flags {
	EF_SHOP = 4,
};

enum base_actions {
	ACT_LOOK = 1,
	ACT_OPEN = 2,
	ACT_GET = 4,
	ACT_DROP = 8,
};

typedef struct entity {
	unsigned home;
	unsigned flags;

	unsigned last_observed;
	unsigned char select;

	unsigned char target_plen;
	uint64_t target_id;
} ENT;

typedef struct object {
	unsigned location, owner;

	unsigned skid;
	unsigned art_id;
	unsigned char type;
	unsigned value;
	unsigned flags;
	char name[32];
	unsigned data[8];
} OBJ;

struct icon {
	int actions;
	struct print_info pi;
	char ch;
};

/* --- st.h ---------------------------------------------------------------- */

#define DIM 2
#define NOISE_MAX ((uint32_t) -1)
#define DAYTICK_Y	10

typedef int16_t coord_t;
typedef uint16_t ucoord_t;

typedef coord_t point_t[DIM];

typedef coord_t point4D_t[4];

typedef point4D_t pos_t;

typedef uint64_t morton_t;

struct cmd_dir {
	char dir;
	enum exit e;
	morton_t rep;
};

struct bio {
	coord_t tmp;
	ucoord_t rn;
	uint32_t ty;
	unsigned bio_idx;
	unsigned raw[16];
};

typedef struct view_tile {
	unsigned bio_idx;
	unsigned room;
	ucoord_t flags, exits, doors;
	unsigned raw[16];
} view_tile_t;

struct spawn_arg {
	unsigned where_ref;
	void *arg;
	coord_t *pos;
};

/* --- skel.h -------------------------------------------------------------- */

enum base_element {
	ELM_SPIRIT = 1,
	ELM_FIRE = 2,
	ELM_WATER = 4,
	ELM_AIR = 8,
	ELM_EARTH = 16,
	ELM_PHYSICAL = 32,
};

enum biome {
	BIOME_WATER = 0,
	BIOME_PERMANENT_ICE = 1,

	BIOME_TUNDRA = 2,
	BIOME_TUNDRA2,
	BIOME_TUNDRA3,
	BIOME_TUNDRA4,

	BIOME_COLD_DESERT,
	BIOME_SHRUBLAND,
	BIOME_CONIFEROUS_FOREST,
	BIOME_BOREAL_FOREST,

	BIOME_TEMPERATE_GRASSLAND,
	BIOME_WOODLAND,
	BIOME_TEMPERATE_SEASONAL_FOREST,
	BIOME_TEMPERATE_RAINFOREST,

	BIOME_DESERT,
	BIOME_SAVANNAH,
	BIOME_TROPICAL_SEASONAL_FOREST,
	BIOME_TROPICAL_RAINFOREST,
	BIOME_VOLCANIC = 18,
	BIOME_MAX = 19,
};

typedef struct drop {
	unsigned skel;
	unsigned char y, yield, yield_v;
} DROP;

typedef struct entity_skel {
	unsigned char y, flags;
	unsigned element;
	unsigned biomes;
} SENT;

enum type {
	TYPE_ROOM,
	TYPE_ENTITY,
};

typedef struct {
	const enum color bg;
} biome_skel_t;

typedef struct object_skel {
	char const name[32];
        enum type type;
	unsigned max_art;
	unsigned data[8];
} SKEL;

typedef struct {
	enum color color;
	unsigned weakness;
} element_t;

typedef struct {
	char str[256];
	int pos;
} sic_str_t;

typedef char small_buf_t[64];

typedef struct {
	struct print_info pi;
	char emp;
} vtf_t;

#endif /* ND_WORLD_TYPES_H */
