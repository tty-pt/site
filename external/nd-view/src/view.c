#include <nd/view.h>

#include <stdio.h>
#include <string.h>

#include <nd/mcp.h>
#include <nd/regions.h>
#include <nd/map-cell.h>
#include <nd/noise.h>
#include <nd/params.h>
#include <nd/entity.h>
#include <nd/law.h>
#include <nd/view-types.h>
#include <nd/map.h>
#include <nd/world-types.h>
unsigned nd_get(unsigned hd, void *value, void *key);
ENT ent_get(unsigned ref);
#include <nd/type.h>

static unsigned *biome_map;
unsigned short day_n = 0;

unsigned vtf_entity, vtf_shop, vtf_max;

/* The biome's background colour as an enum, independent of time of day. */
static enum color
biome_bg_color(unsigned i)
{
	if (i >= BIOME_MAX) /* unmapped/limbo tiles: render void, never OOB */
		i = 0;

	/* Read the row rather than trusting it: a biome with no skel row leaves
	 * skel zeroed by the copy's miss, and a row from an older build can carry
	 * a colour outside the eight ansi_bg[] slots. */
	SKEL skel = { 0 };
	biome_skel_t *biome_skel = (biome_skel_t *) skel.data;
	enum color bg = BLACK;

	if (nd_get(HD_SKEL, &(skel), &biome_map[i]) == 0 &&
	    biome_skel->bg <= WHITE)
		bg = biome_skel->bg;

	return bg;
}

static inline char * biome_bg(unsigned i) {
	static char ret[16];
	if (NIGHT_IS)
		return ANSI_RESET;

	/* The copy is bounded by the source string, not by sizeof(ret): ansi_bg
	 * entries are 6-byte literals like "\033[40m" and memcpy-ing 16 bytes out
	 * of one read 10 bytes past the literal (ASan, src/view.c:29). */
	const char *src = ansi_bg[biome_bg_color(i)];
	size_t n = strlen(src);
	if (n >= sizeof(ret))
		n = sizeof(ret) - 1;
	memcpy(ret, src, n);
	ret[n] = 0;
	return ret;
}

static const char * v = "|";
static const char * l = ANSI_FG_WHITE "+";
static const char *h_open	= "   ";
static const char *h_closed	= "---";

void base_vtf_init(void) {
	if (!vtf_hd) return;
	vtf_shop = eng_vtf_register('$', GREEN, BOLD);
	vtf_entity = eng_vtf_register('!', YELLOW, BOLD);
}

static inline char *
empty_tile(char *b, view_tile_t *t, unsigned side) {
	sic_str_t ss = { .str = "" };
	ss = nd_evt_empty_tile(*t, side, ss);
	b += sprintf(b, "%s", ss.str);
	return b;
}

/* The first registered view-tile flag set on `t`, if any: what the centre of a
 * room tile shows ('!' entity, '$' shop, '~' pond, ...). */
static int
vtf_find(const view_tile_t *t, vtf_t *vtf)
{
	unsigned i;

	memset(vtf, 0, sizeof(*vtf));
	for (i = 0; i < vtf_max; i++) {
		unsigned ref = (1 << i);

		if (!(t->flags & ref))
			continue;

		const void *__v = corm_get(vtf_hd, &i);
		if (__v) {
			memcpy(vtf, __v, sizeof(*vtf));
			return 1;
		}
	}
	return 0;
}

static inline char *
dr_room(char *buf, view_tile_t *t, const char *bg, int is_player)
{
	register char *b = buf;
	vtf_t vtf;

	if (t->exits & E_DOWN)
		b = stpcpy(b, ANSI_FG_WHITE "<");
	else
		b = empty_tile(b, t, 0);

	int got = vtf_find(t, &vtf);

	b = stpcpy(b, ANSI_RESET);
	b = stpcpy(b, bg);

	if (is_player) {
		/* The player is an entity like any other: the original game
		 * drew its tile as the yellow bold entity glyph '!'. */
		b = stpcpy(b, ANSI_BOLD);
		b = stpcpy(b, ansi_fg[YELLOW]);
		*b++ = '!';
		b = stpcpy(b, ANSI_RESET_BOLD);
	} else if (got) {
		if (vtf.pi.flags & BOLD)
			b = stpcpy(b, ANSI_BOLD);
		b = stpcpy(b, ansi_fg[vtf.pi.fg]);
		*b++ = vtf.emp;
		if (vtf.pi.flags & BOLD)
			b = stpcpy(b, ANSI_RESET_BOLD);
	} else
		b = empty_tile(b, t, 1);


	if (t->exits & E_UP)
		b = stpcpy(b, ANSI_FG_WHITE ">");
	else
		b = empty_tile(b, t, 2);

	b = stpcpy(b, ANSI_RESET);
	b = stpcpy(b, bg);

	return b;
}

static inline unsigned
floor_get(unsigned what_ref) {
	OBJ what;
	nd_get(HD_OBJ, &(what), &what_ref);
	ROO *rwhat = (ROO *) &what.data;
	unsigned char floor = rwhat->floor;

	if (floor > BIOME_VOLCANIC)
		return BIOME_VOLCANIC;
	else
		return floor;
}

static inline const char *
wall_paint(int floor)
{
	switch (floor) {
	case 11: /* desert */
	case -8:
		return ANSI_FG_RED;
	default:
		return ANSI_FG_YELLOW;
	}
}

/* wall_paint() as an ANSI colour index, for the graphical map. */
static inline uint8_t
wall_paint_ansi(int floor)
{
	switch (floor) {
	case 11: /* desert */
	case -8:
		return 1; /* red */
	default:
		return 3; /* yellow */
	}
}

/* One cell (side 0 left, 1 centre, 2 right) of a tile's on_empty_tile output
 * (plants), as glyph + ANSI colour; blank cells give glyph 0. */
static void
view_empty_cell(const view_tile_t *t, unsigned side, uint8_t *glyph, uint8_t *fg)
{
	sic_str_t ss = { .str = "" };

	ss = nd_evt_empty_tile(*t, side, ss);
	nd_map_cell_parse(ss.str, glyph, fg);
}

static inline char *
dr_v(char *b, view_tile_t *t, enum exit exit,
		const char *wp) {
	if (t->doors & exit)
		return stpcpy(b, l);
	else if (t->exits & exit)
		return stpcpy(b, " ");
	else {
		b = stpcpy(b, wp);
		return stpcpy(b, v);
	}
}

static char *
dr_vs(char *b, view_tile_t *t, const view_tile_t *player_tile)
{
	view_tile_t * const t_max = &t[VIEW_SIZE];
	const char *bg, *wp;
	view_tile_t *tp;
	unsigned floor;
	enum exit exit; // loop cache

	if (t->room == NOTHING) {
		floor = t->bio_idx;
		wp = "";
	} else {
		floor = t->bio_idx;
		wp = wall_paint(floor);
	}

	bg = biome_bg(floor);
	b = stpcpy(b, " ");
	for (;;) {
		b = stpcpy(b, bg);
		b = dr_room(b, t, bg, t == player_tile);

		tp = t;
		t++;

		if (t >= t_max)
			break;

		if (t->room == NOTHING) {
			floor = t->bio_idx;
			bg = biome_bg(floor);
		} else {
			floor = t->bio_idx;
			wp = wall_paint(floor);
			bg = biome_bg(floor);
			b = stpcpy(b, bg);
		}

		if (t->room != NOTHING) {
			exit = E_WEST;
			tp = t;
		} else if (tp->room != NOTHING) {
			exit = E_EAST;
		} else {
			*b++ = ' ';
			continue;
		}

		b = dr_v(b, tp, exit, wp);
	}

        b = stpcpy(b, ANSI_RESET"\r\n");
        return b;
}

static inline char *
dr_h(char *b, char const *bg, char const *wp, char *w, int toggle)
{
	if (toggle) {
		b = stpcpy(b, " ");
		b = stpcpy(b, bg);
	} else {
		b = stpcpy(b, bg);
		b = stpcpy(b, " ");
	}
	b = stpcpy(b, wp);
	b = stpcpy(b, w);
	return b;
}

static inline char *
dr_hs_n(char *b, view_tile_t *t)
{
	view_tile_t * const t_max = &t[VIEW_SIZE];
	const char *wp, *bg;
	char wb[32], *w;
	unsigned floor;
	int toggle;

	for (wp = "",
	     bg = biome_bg(
		     t->room == NOTHING
		     ? t->bio_idx
		     : floor_get(t->room)) ;
	     t < t_max ;
	     bg = biome_bg(floor),
	     b = dr_h(b, bg, wp, w, toggle),
	     t++)
	{
		view_tile_t *tn = &t[VIEW_SIZE];
		unsigned curr = t->room,
		      next = tn->room;

		floor = t->bio_idx;
		if (next != NOTHING) {
			floor = tn->bio_idx;
			toggle = 0;
		} else if (curr != NOTHING) {
			floor = t->bio_idx;
			toggle = 0;
		} else {
			floor = t->bio_idx;
			w = (char *) h_open;
			toggle = 1;
			continue;
		}

		wp = wall_paint(floor);

		if (!(t->exits & E_SOUTH) || !(tn->exits & E_NORTH))
			w = (char *) h_closed;
		else if ((t->doors & E_SOUTH) || (tn->doors & E_NORTH)) {
			snprintf(wb, sizeof(wb), "-"ANSI_FG_WHITE"+%s-", wp);
			w = wb;
		} else
			w = (char *) h_open;
	}

        b = stpcpy(b, ANSI_RESET"\r\n");
        return b;
}

static char *
view_draw(view_t view) {
	static char view_buf[8 * BUFSIZ];
	view_tile_t * const t_max = &view[VIEW_BDI];
	view_tile_t * const player_tile = &view[VIEW_AROUND * VIEW_SIZE + VIEW_AROUND];
	view_tile_t *t = view;
	char *p = view_buf;
        memset(view_buf, 0, sizeof(view_buf));

	p = dr_vs(p, t, player_tile);

	for (; t < t_max;) {
		p = dr_hs_n(p, t);
		t += VIEW_SIZE;
		p = dr_vs(p, t, player_tile);
	}

	return view_buf;
}

static inline void
view_build_exit(view_tile_t *t, ROO *rloc, enum exit e) {
	if (rloc->exits & e) {
		t->exits |= e;
		t->doors |= rloc->doors & e;
	}
}

static inline void
view_build_exit_z(view_tile_t *t, ROO *rloc, enum exit e) {
	if (rloc->exits & e)
		t->exits |= e;
}

static inline void
view_build_exit_s(view_tile_t *t, pos_t p, enum exit e) {
	pos_t pa;
	pos_move(pa, p, e);
	unsigned tmp_ref = map_get(pa);

	if (tmp_ref == NOTHING) {
		t->exits |= e;
		return;
	}

	OBJ tmp;
	nd_get(HD_OBJ, &(tmp), &tmp_ref);
	ROO *rtmp = (ROO *) &tmp.data;

	if (rtmp->exits & e_simm(e)) {
		t->exits |= e;
		if (rtmp->doors & e_simm(e))
			t->doors |= e;
	}
}

static inline ucoord_t
view_build_flags(unsigned loc_ref) {
	unsigned c = corm_iter(nd_table_handle(HD_CONTENTS), &loc_ref, CM_RANGE);
	unsigned tmp_ref;
	ucoord_t flags = 0;
	const void *kp, *vp;

	while (corm_next(&kp, &vp, c)) {
		loc_ref = *(const unsigned *)kp;
		tmp_ref = *(const unsigned *)vp;
		OBJ tmp;
		nd_get(HD_OBJ, &(tmp), &tmp_ref);
		switch (tmp.type) {
		case TYPE_ENTITY:
			flags |= vtf_entity;

			if (ent_get(tmp_ref).flags & EF_SHOP)
				flags |= vtf_shop;

			break;
		}

		nd_evt_view_flags(flags, tmp_ref);
	}

	return flags;
}

static void
view_build_tile(struct bio *n, unsigned loc_ref, view_tile_t *t, pos_t p)
{
	t->room = loc_ref;
	if (loc_ref != NOTHING) {
		OBJ loc;
		nd_get(HD_OBJ, &(loc), &loc_ref);
		ROO *rloc = (ROO *) &loc.data;
		view_build_exit(t, rloc, E_EAST);
		view_build_exit(t, rloc, E_NORTH);
		view_build_exit(t, rloc, E_WEST);
		view_build_exit(t, rloc, E_SOUTH);
		view_build_exit_z(t, rloc, E_UP);
		view_build_exit_z(t, rloc, E_DOWN);
		t->flags = view_build_flags(loc_ref);
		t->bio_idx = floor_get(loc_ref);
		if (rloc->flags & RF_TEMP)
			t->room = NOTHING;
	} else {
		view_build_exit_s(t, p, E_EAST);
		view_build_exit_s(t, p, E_NORTH);
		view_build_exit_s(t, p, E_WEST);
		view_build_exit_s(t, p, E_SOUTH);
		/* view_build_exit_sz(t, descr, player, loc, E_UP); */
		/* view_build_exit_sz(t, descr, player, loc, E_DOWN); */
		t->bio_idx = n->bio_idx;
	}

	memcpy(&t->raw, &n->raw, sizeof(n->raw));
}

static inline view_tile_t *
_view_build(struct bio *n, unsigned *g, view_tile_t *b, pos_t p)
{
	unsigned *g_max = g + VIEW_SIZE;

	// TODO vary p vertically and horizontally
	for (; g < g_max ; n++, g++, b++, p[1]++)
		view_build_tile(n, *g, b, p);

        return b;
}

void
eng_view(unsigned player_ref)
{
	struct bio bd[VIEW_M], *n_p = bd,
		   *n_max = &bd[VIEW_BDI + 1];
	pos_t pos, opos;
	OBJ player;
	nd_get(HD_OBJ, &(player), &player_ref);
	map_where(opos, player.location);
	view_t view;
        view_tile_t *p = view;
	unsigned o[VIEW_M], *o_p = o;
	uint64_t position = map_mwhere(player.location);
	biome_map = biome_map_get(position);

	memcpy(pos, opos, sizeof(pos));
	nd_noise_view(bd, pos);
	map_search(o, pos, VIEW_AROUND);
	memset(view, 0, sizeof(view));

	for (; n_p < n_max;) {
		_view_build(n_p, o_p, p, pos);
		p += VIEW_SIZE;
		o_p += VIEW_SIZE;
		n_p += VIEW_SIZE;
		pos[0]++;
		pos[1] = opos[1];
	}

	char *view_buf = view_draw(view);

	nd_map_data_t map_data;
	memset(&map_data, 0, sizeof(map_data));
	map_data.width = VIEW_SIZE;
	map_data.height = VIEW_SIZE;
	map_data.player_x = VIEW_AROUND;
	map_data.player_y = VIEW_AROUND;
	map_data.room_ref = player.location;

	/* Fill each tile with what dr_room()/dr_vs() paint for it, so the graphical
	 * map shows the same information as the ASCII one: three cells (left,
	 * centre, right), the biome background, and the wall paint colour. */
	for (int i = 0; i < VIEW_M; i++) {
		const view_tile_t *vt = &view[i];
		nd_map_tile_t *mt = &map_data.tiles[i];
		int is_player = (i == (VIEW_AROUND * VIEW_SIZE + VIEW_AROUND));

		mt->bio_idx = (uint8_t)(vt->bio_idx & 0xFF);
		mt->exits = (uint8_t)(vt->exits & 0xFF);
		mt->doors = (uint8_t)(vt->doors & 0xFF);
		mt->bg = nd_color_to_ansi(biome_bg_color(vt->bio_idx));
		mt->wall_fg = wall_paint_ansi(vt->bio_idx);

		uint8_t flags = 0;
		if (vt->room != NOTHING && vt->room != 0)
			flags |= ND_MAP_FLAG_ROOM;
		if (is_player)
			flags |= ND_MAP_FLAG_PLAYER;
		if (vt->flags & vtf_entity)
			flags |= ND_MAP_FLAG_ENTITY;
		mt->flags = flags;

		/* left cell: stairs down, else plants */
		if (vt->exits & E_DOWN) {
			mt->side_l = '<';
			mt->fg_l = 7;
		} else {
			view_empty_cell(vt, 0, &mt->side_l, &mt->fg_l);
		}

		/* centre cell: the player, then a registered flag glyph, else plants */
		vtf_t vtf;
		if (is_player) {
			mt->symbol = '!';
			mt->fg = 11; /* bold yellow */
		} else if (vtf_find(vt, &vtf)) {
			mt->symbol = (uint8_t)vtf.emp;
			mt->fg = (uint8_t)(nd_color_to_ansi(vtf.pi.fg) +
				((vtf.pi.flags & BOLD) ? 8 : 0));
		} else {
			view_empty_cell(vt, 1, &mt->symbol, &mt->fg);
		}

		/* right cell: stairs up, else plants */
		if (vt->exits & E_UP) {
			mt->side_r = '>';
			mt->fg_r = 7;
		} else {
			view_empty_cell(vt, 2, &mt->side_r, &mt->fg_r);
		}
	}

	char stage[16] = {0};
	char map_drv[16] = {0};
	uint8_t layout = 0;
	uint8_t flags = 0;
	if (st_player_resolve_presentation(player_ref, stage, sizeof(stage),
	                                   map_drv, sizeof(map_drv), &layout, &flags) == 0) {
		fbcp_region(player_ref, stage, map_drv, layout, flags);
	}

	fbcp_view(player_ref, &map_data);
	fbcp_view_buf(player_ref, view_buf);
}

void
do_view(int fd, int argc __attribute__((unused)), char *argv[] __attribute__((unused)))
{
        eng_view(fd_player(fd));
}
