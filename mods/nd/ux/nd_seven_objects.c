#include "nd_seven_objects.h"
#include "nd_seven_motion.h"
#include <stdio.h>
#include <string.h>

#define SCALE 2
#define LAMB_AY 23

static const int8_t s_spots[16][2] = {
	{ -16, 26 }, { 16, 26 }, { -28, 12 }, { 28, 12 }, { -38, -6 }, { 38, -6 }, { -22, -18 }, { 22, -18 },
	{ -52, 0 }, { 52, 0 }, { -46, 24 }, { 46, 24 }, { -30, 34 }, { 30, 34 }, { -46, -22 }, { 46, -22 }
};

static room_tracker_t s_tracker = { 0 };

const room_tracker_t *nd_seven_get_tracker(void)
{
	return &s_tracker;
}

void nd_seven_objects_forget(void)
{
	memset(&s_tracker, 0, sizeof(s_tracker));
}

int str_has_ci(const char *hay, const char *needle)
{
	if (!hay || !needle || !*needle) return 0;
	size_t nlen = strlen(needle);
	for (const char *h = hay; *h; h++) {
		size_t i = 0;
		while (i < nlen && h[i]) {
			char c1 = h[i], c2 = needle[i];
			if (c1 >= 'A' && c1 <= 'Z') c1 = (char)(c1 + 32);
			if (c2 >= 'A' && c2 <= 'Z') c2 = (char)(c2 + 32);
			if (c1 != c2) break;
			i++;
		}
		if (i == nlen) return 1;
	}
	return 0;
}

int is_landscape_element(const nd_entity_desc_t *e)
{
	if (!e) return 0;
	if (e->ch == '*' || e->ch == 'o' || e->ch == 'O' ||
	    e->ch == 'x' || e->ch == 'X' || e->ch == 't' || e->ch == 'T' ||
	    e->ch == 'a' || e->ch == 'A' || e->ch == 'i' || e->ch == 'I')
		return 1;
	return str_has_ci(e->name, "stone") || str_has_ci(e->name, "rock") ||
	       str_has_ci(e->name, "boulder") || str_has_ci(e->name, "mineral") ||
	       str_has_ci(e->name, "quartz") || str_has_ci(e->name, "flint") ||
	       str_has_ci(e->name, "pebble") ||
	       str_has_ci(e->name, "tree") || str_has_ci(e->name, "pine") ||
	       str_has_ci(e->name, "fir") || str_has_ci(e->name, "birch") ||
	       str_has_ci(e->name, "cactus") || str_has_ci(e->name, "plant") ||
	       str_has_ci(e->name, "pinus") || str_has_ci(e->name, "pseudotsuga") ||
	       str_has_ci(e->name, "abies") || str_has_ci(e->name, "acacia") ||
	       str_has_ci(e->name, "betula") || str_has_ci(e->name, "linum") ||
	       str_has_ci(e->name, "arthrocereus");
}

int is_stone_or_rock(const nd_entity_desc_t *e)
{
	if (!e) return 0;
	if (e->ch == '*' || e->ch == 'o' || e->ch == 'O')
		return 1;
	return str_has_ci(e->name, "stone") || str_has_ci(e->name, "rock") ||
	       str_has_ci(e->name, "boulder") || str_has_ci(e->name, "mineral") ||
	       str_has_ci(e->name, "quartz") || str_has_ci(e->name, "flint") ||
	       str_has_ci(e->name, "pebble");
}

int is_contained_in_tree(const nd_client_state_t *st, const nd_entity_desc_t *e)
{
	if (!st || !e)
		return 0;
	for (size_t i = 0; i < st->entity_count; i++) {
		const nd_entity_desc_t *other = &st->entities[i];
		if (other->ref == e->location && is_landscape_element(other) && !is_stone_or_rock(other))
			return 1;
	}
	return 0;
}

const char *world_obj_name(const ndw_obj_t *o, const nd_client_state_t *st, int ti, int tj)
{
	if (ti == 0 && tj == 0 && st && st->room.ref) {
		if (o->kind == NDW_K_STONE || o->kind == NDW_K_ROCK || o->kind == NDW_K_BOULDER) {
			for (size_t i = 0; i < st->entity_count; i++) {
				if (st->entities[i].ref != st->me &&
				    st->entities[i].location == st->room.ref &&
				    is_stone_or_rock(&st->entities[i])) {
					if (st->entities[i].name[0])
						return st->entities[i].name;
				}
			}
		}
		if (o->kind == NDW_K_DECID || o->kind == NDW_K_TALL || o->kind == NDW_K_FIR || o->kind == NDW_K_CACTUS) {
			for (size_t i = 0; i < st->entity_count; i++) {
				if (st->entities[i].ref != st->me &&
				    st->entities[i].location == st->room.ref &&
				    is_landscape_element(&st->entities[i]) &&
				    !is_stone_or_rock(&st->entities[i])) {
					if (st->entities[i].name[0])
						return st->entities[i].name;
				}
			}
		}
	}
	switch (o->kind) {
	case NDW_K_STONE:
	case NDW_K_ROCK:
	case NDW_K_BOULDER:
		return "stone";
	case NDW_K_FIR:
		return "abies alba";
	case NDW_K_CACTUS:
		return "arthrocereus rondonianus";
	case NDW_K_TALL:
		return (o->fg == 12) ? "acacia senegal" : "pseudotsuga menziesii";
	case NDW_K_DECID:
		return (o->fg == 14 || o->fg == 6) ? "betula pendula" :
		       (o->fg == 15 || o->fg == 7) ? "betula pubescens" : "pinus sylvestris";
	case NDW_K_SHOP:        return "shop";
	case NDW_K_STAIRS_DOWN: return "stairs";
	case NDW_K_STAIRS_UP:   return "stairs";
	case NDW_K_POND:        return "pond";
	default:                return NULL;
	}
}

void sync_room_objects(const nd_client_state_t *st)
{
	if (!st || !st->room.ref) {
		s_tracker.room_ref = 0;
		s_tracker.count = 0;
		s_tracker.bound_entities = 0;
		return;
	}

	ndw_tile_t t;
	int16_t pos[4];
	int kind = nd_seven_tile_at(st, 0, 0, &t, pos);
	if (!kind)
		return;

	int need_sync = 0;
	if (s_tracker.room_ref != st->room.ref ||
	    memcmp(s_tracker.pos, pos, sizeof(pos)) != 0 ||
	    memcmp(s_tracker.glyphs, t.glyph, 3) != 0)
		need_sync = 1;
	else if (s_tracker.bound_entities == 0 && st->entity_count > 0)
		need_sync = 1;

	if (!need_sync)
		return;

	if (s_tracker.room_ref != st->room.ref ||
	    memcmp(s_tracker.pos, pos, sizeof(pos)) != 0) {
		memset(&s_tracker, 0, sizeof(s_tracker));
	}

	s_tracker.room_ref = st->room.ref;
	memcpy(s_tracker.pos, pos, sizeof(pos));
	memcpy(s_tracker.glyphs, t.glyph, 3);
	s_tracker.count = 0;
	s_tracker.bound_entities = 0;

	ndw_obj_t objs[NDW_MAX_OBJ];
	int cnt = ndw_tile_objects(&t, pos, 0, objs);
	if (cnt > NDW_MAX_OBJ)
		cnt = NDW_MAX_OBJ;
	s_tracker.count = cnt;

	uint8_t used_entity[ND_MAX_ENTITIES] = { 0 };

	for (int k = 0; k < cnt; k++) {
		s_tracker.objs[k].x = objs[k].x;
		s_tracker.objs[k].y = objs[k].y;
		s_tracker.objs[k].kind = objs[k].kind;
		s_tracker.objs[k].entity_ref = 0;
		s_tracker.objs[k].harvested = 0;
		s_tracker.objs[k].stick_harvested = 0;
		s_tracker.objs[k].name[0] = '\0';

		int is_stone = (objs[k].kind == NDW_K_STONE || objs[k].kind == NDW_K_ROCK || objs[k].kind == NDW_K_BOULDER);
		int is_plant = (objs[k].kind == NDW_K_DECID || objs[k].kind == NDW_K_TALL || objs[k].kind == NDW_K_FIR || objs[k].kind == NDW_K_CACTUS);

		if (is_stone) {
			for (size_t ei = 0; ei < st->entity_count; ei++) {
				if (used_entity[ei])
					continue;
				const nd_entity_desc_t *e = &st->entities[ei];
				if (e->location == st->room.ref && e->ref != st->me && is_stone_or_rock(e)) {
					s_tracker.objs[k].entity_ref = e->ref;
					snprintf(s_tracker.objs[k].name, sizeof(s_tracker.objs[k].name), "%s",
					         e->name[0] ? e->name : "stone");
					used_entity[ei] = 1;
					s_tracker.bound_entities++;
					break;
				}
			}
			if (!s_tracker.objs[k].name[0])
				snprintf(s_tracker.objs[k].name, sizeof(s_tracker.objs[k].name), "stone");
		} else if (is_plant) {
			for (size_t ei = 0; ei < st->entity_count; ei++) {
				if (used_entity[ei])
					continue;
				const nd_entity_desc_t *e = &st->entities[ei];
				if (e->location == st->room.ref && e->ref != st->me &&
				    is_landscape_element(e) && !is_stone_or_rock(e)) {
					s_tracker.objs[k].entity_ref = e->ref;
					snprintf(s_tracker.objs[k].name, sizeof(s_tracker.objs[k].name), "%s",
					         e->name[0] ? e->name : world_obj_name(&objs[k], st, 0, 0));
					used_entity[ei] = 1;
					s_tracker.bound_entities++;
					break;
				}
			}
			if (!s_tracker.objs[k].name[0]) {
				const char *wn = world_obj_name(&objs[k], st, 0, 0);
				if (wn)
					snprintf(s_tracker.objs[k].name, sizeof(s_tracker.objs[k].name), "%s", wn);
			}
		} else {
			const char *wn = world_obj_name(&objs[k], st, 0, 0);
			if (wn)
				snprintf(s_tracker.objs[k].name, sizeof(s_tracker.objs[k].name), "%s", wn);
		}
	}
}

int is_tracked_obj_present(const nd_client_state_t *st, int k)
{
	if (!st || !st->room.ref || k < 0 || k >= s_tracker.count)
		return 1;

	if (s_tracker.objs[k].harvested)
		return 0;

	if (s_tracker.objs[k].entity_ref != 0) {
		for (size_t ei = 0; ei < st->entity_count; ei++) {
			if (st->entities[ei].ref == s_tracker.objs[k].entity_ref &&
			    st->entities[ei].location == st->room.ref)
				return 1;
		}
		return 0;
	}

	if (st->entity_count > 0)
		return 0;

	return 1;
}

static inline void put_px(uint32_t *fb, int w, int h, int x, int y, uint32_t c)
{
	if ((unsigned)x < (unsigned)w && (unsigned)y < (unsigned)h)
		fb[y * w + x] = c;
}

static inline void fill_px(uint32_t *fb, int w, int h, int x0, int y0, int x1, int y1, uint32_t c)
{
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > w) x1 = w;
	if (y1 > h) y1 = h;
	for (int y = y0; y < y1; y++) {
		uint32_t *row = &fb[y * w + x0];
		int n = x1 - x0;
		while (n-- > 0) *row++ = c;
	}
}

static inline const uint8_t *font_rows(char ch)
{
	static const uint8_t f[][5] = {
		[' '] = { 0, 0, 0, 0, 0 },
		['*'] = { 0, 5, 2, 5, 0 },
		['!'] = { 2, 2, 2, 0, 2 },
		['$'] = { 2, 7, 2, 7, 2 },
		['<'] = { 1, 2, 4, 2, 1 },
		['>'] = { 4, 2, 1, 2, 4 },
		['~'] = { 0, 5, 7, 2, 0 },
		['a'] = { 2, 5, 7, 5, 5 },
		['b'] = { 6, 5, 6, 5, 6 },
		['c'] = { 3, 4, 4, 4, 3 },
		['d'] = { 6, 5, 5, 5, 6 },
		['e'] = { 7, 4, 6, 4, 7 },
		['f'] = { 7, 4, 6, 4, 4 },
		['g'] = { 3, 4, 5, 5, 3 },
		['h'] = { 5, 5, 7, 5, 5 },
		['i'] = { 7, 2, 2, 2, 7 },
		['j'] = { 1, 1, 1, 5, 2 },
		['k'] = { 5, 6, 4, 6, 5 },
		['l'] = { 4, 4, 4, 4, 7 },
		['m'] = { 5, 7, 5, 5, 5 },
		['n'] = { 6, 5, 5, 5, 5 },
		['o'] = { 2, 5, 5, 5, 2 },
		['p'] = { 6, 5, 6, 4, 4 },
		['q'] = { 2, 5, 5, 6, 3 },
		['r'] = { 6, 5, 6, 5, 5 },
		['s'] = { 3, 4, 2, 1, 6 },
		['t'] = { 7, 2, 2, 2, 2 },
		['u'] = { 5, 5, 5, 5, 2 },
		['v'] = { 5, 5, 5, 5, 2 },
		['w'] = { 5, 5, 5, 7, 5 },
		['x'] = { 5, 5, 2, 5, 5 },
		['y'] = { 5, 5, 2, 2, 2 },
		['z'] = { 7, 1, 2, 4, 7 },
		['O'] = { 7, 5, 5, 5, 7 },
		['#'] = { 5, 7, 5, 7, 5 },
	};
	unsigned char uc = (unsigned char)ch;
	if (uc >= 'A' && uc <= 'Z' && uc != 'O')
		uc = (unsigned char)(uc + 32);
	if (uc < sizeof(f)/sizeof(f[0]))
		return f[uc];
	return NULL;
}

void draw_object_name(uint32_t *fb, int w, int h, int cx, int top_y, const char *name)
{
	if (!name || !*name || !fb || w <= 0 || h <= 0)
		return;

	int len = (int)strlen(name);
	int tw = len > 0 ? len * 8 - 2 : 0;
	int x0 = cx - tw / 2;
	int y0 = top_y - 15;

	if (x0 < 3) x0 = 3;
	if (x0 + tw >= w - 3) x0 = w - tw - 4;
	if (y0 < 3) y0 = 3;
	if (y0 + 10 >= h - 3) y0 = h - 14;

	fill_px(fb, w, h, x0 - 3, y0 - 2, x0 + tw + 3, y0 + 12, 0xEE0E1018u);
	put_px(fb, w, h, x0 - 3, y0 - 2, 0x55FFFFFFu);
	put_px(fb, w, h, x0 + tw + 2, y0 - 2, 0x55FFFFFFu);

	int cur_x = x0;
	for (const char *p = name; *p; p++) {
		const uint8_t *rows = font_rows(*p);
		if (rows) {
			for (int r = 0; r < 5; r++)
				for (int k = 0; k < 3; k++)
					if (rows[r] & (4 >> k))
						fill_px(fb, w, h, cur_x + k * 2, y0 + r * 2,
						        cur_x + k * 2 + 2, y0 + r * 2 + 2, 0xFFFAF0E6u);
		}
		cur_x += 8;
	}
}

void draw_thinking_balloon(uint32_t *fb, int w, int h, int hx, int hy, const char *action)
{
	(void)action;
	int hover = (int)((nd_seven_get_anim() / 20) & 1);
	int bw = 28, bh = 15;
	int bx0 = hx - bw / 2;
	int by0 = hy - bh - 10 - hover;

	if (by0 < 4) by0 = 4;
	if (by0 + bh >= h - 4) by0 = h - bh - 5;
	if (bx0 < 4) bx0 = 4;
	if (bx0 + bw >= w - 4) bx0 = w - bw - 5;

	int bx1 = bx0 + bw, by1 = by0 + bh;

	uint32_t c_border = 0xFF24262E;
	uint32_t c_bg = 0xFFFDFBF7;
	uint32_t c_shadow = 0xFFE2DDD3;

	int tx = hx;
	int ty0 = by1;
	put_px(fb, w, h, tx - 2, ty0,     c_border);
	put_px(fb, w, h, tx - 1, ty0,     c_bg);
	put_px(fb, w, h, tx,     ty0,     c_bg);
	put_px(fb, w, h, tx + 1, ty0,     c_border);
	put_px(fb, w, h, tx - 1, ty0 + 1, c_border);
	put_px(fb, w, h, tx,     ty0 + 1, c_bg);
	put_px(fb, w, h, tx + 1, ty0 + 1, c_border);
	put_px(fb, w, h, tx,     ty0 + 2, c_border);

	fill_px(fb, w, h, bx0 + 2, by0, bx1 - 2, by1, c_border);
	fill_px(fb, w, h, bx0, by0 + 2, bx1, by1 - 2, c_border);
	put_px(fb, w, h, bx0 + 1, by0 + 1, c_border);
	put_px(fb, w, h, bx1 - 2, by0 + 1, c_border);
	put_px(fb, w, h, bx0 + 1, by1 - 2, c_border);
	put_px(fb, w, h, bx1 - 2, by1 - 2, c_border);

	put_px(fb, w, h, bx1 - 1, by0 + 3, c_shadow);
	fill_px(fb, w, h, bx0 + 3, by1 - 2, bx1 - 3, by1 - 1, c_shadow);

	fill_px(fb, w, h, bx0 + 2, by0 + 1, bx1 - 2, by1 - 2, c_bg);
	fill_px(fb, w, h, bx0 + 1, by0 + 2, bx1 - 1, by1 - 2, c_bg);

	int cx = (bx0 + bx1) / 2;
	int cy = (by0 + by1) / 2 - 1;

	uint32_t c_gold = 0xFFFFC820;
	uint32_t c_spark = 0xFFFFF6A0;
	put_px(fb, w, h, cx - 6, cy - 2, c_gold);
	put_px(fb, w, h, cx - 7, cy - 1, c_gold);
	put_px(fb, w, h, cx - 6, cy - 1, c_spark);
	put_px(fb, w, h, cx - 5, cy - 1, c_gold);
	put_px(fb, w, h, cx - 6, cy,     c_gold);

	uint32_t c_key = 0xFF4A4E5C;
	put_px(fb, w, h, cx - 1, cy - 1, c_key);
	put_px(fb, w, h, cx - 1, cy,     c_key);
	put_px(fb, w, h, cx - 1, cy + 1, c_key);
	fill_px(fb, w, h, cx,    cy + 1, cx + 6, cy + 2, c_key);
	put_px(fb, w, h, cx + 6, cy - 1, c_key);
	put_px(fb, w, h, cx + 6, cy,     c_key);
	put_px(fb, w, h, cx + 6, cy + 1, c_key);
}

static int seven_action_impl(const nd_client_state_t *st, char *out, size_t sz, int execute)
{
	int best_dist = 999999;
	int best_tracker_k = -1;
	char best_cmd[64] = { 0 };

	if (!st)
		return 0;
	nd_seven_sync_origin(st);
	sync_room_objects(st);

	int lx = nd_seven_get_lx();
	int ly = nd_seven_get_ly();

	for (int k = 0; k < s_tracker.count; k++) {
		if (!is_tracked_obj_present(st, k))
			continue;

		int obj_x = s_tracker.objs[k].x;
		int obj_y = s_tracker.objs[k].y;
		int dx = lx - obj_x;
		int dy = ly - obj_y;
		if (dx < 0) dx = -dx;
		if (dy < 0) dy = -dy;

		if (dx <= 40 && dy <= 32) {
			int d2 = dx * dx + dy * dy;
			if (d2 < best_dist) {
				uint8_t kind = s_tracker.objs[k].kind;
				if (kind == NDW_K_STONE || kind == NDW_K_ROCK || kind == NDW_K_BOULDER) {
					if (s_tracker.objs[k].entity_ref) {
						snprintf(best_cmd, sizeof(best_cmd), "get #%u\n", s_tracker.objs[k].entity_ref);
						best_dist = d2;
						best_tracker_k = k;
					}
				} else if (kind == NDW_K_DECID || kind == NDW_K_TALL || kind == NDW_K_FIR) {
					uint32_t tree_ref = s_tracker.objs[k].entity_ref;
					uint32_t stick_ref = 0;
					if (tree_ref && !s_tracker.objs[k].stick_harvested) {
						for (size_t si = 0; si < st->entity_count; si++) {
							if (st->entities[si].location == tree_ref &&
							    str_has_ci(st->entities[si].name, "stick")) {
								stick_ref = st->entities[si].ref;
								break;
							}
						}
					}
					if (stick_ref) {
						snprintf(best_cmd, sizeof(best_cmd), "get #%u #%u\n", tree_ref, stick_ref);
						best_dist = d2;
						best_tracker_k = k;
					}
				} else if (kind == NDW_K_STAIRS_DOWN) {
					snprintf(best_cmd, sizeof(best_cmd), ">\n");
					best_dist = d2;
				} else if (kind == NDW_K_STAIRS_UP) {
					snprintf(best_cmd, sizeof(best_cmd), "<\n");
					best_dist = d2;
				} else if (kind == NDW_K_SHOP) {
					snprintf(best_cmd, sizeof(best_cmd), "shop\n");
					best_dist = d2;
				}
			}
		}
	}

	uint32_t room = st->room.ref;
	if (room) {
		for (size_t i = 0; i < st->entity_count; i++) {
			const nd_entity_desc_t *e = &st->entities[i];
			if (st->me && e->ref == st->me)
				continue;
			if (e->location != room || is_contained_in_tree(st, e))
				continue;
			if (is_landscape_element(e))
				continue;
			uint32_t spot = ndw_mix(e->ref) % 16;
			int ex = NDW_CENTER_X + s_spots[spot][0];
			int ey = NDW_CENTER_Y + s_spots[spot][1];
			int dx = lx - ex;
			int dy = ly - ey;
			if (dx < 0) dx = -dx;
			if (dy < 0) dy = -dy;
			if (dx <= 40 && dy <= 32) {
				int d2 = dx * dx + dy * dy;
				if (d2 < best_dist) {
					char c[64];
					if ((e->actions & 4) || (e->actions == 0 && e->type != 1)) {
						snprintf(c, sizeof(c), "get #%u\n", e->ref);
					} else {
						snprintf(c, sizeof(c), "look #%u\n", e->ref);
					}
					best_dist = d2;
					best_tracker_k = -1;
					snprintf(best_cmd, sizeof(best_cmd), "%s", c);
				}
			}
		}
	}

	if (best_dist < 999999 && best_cmd[0]) {
		if (execute && best_tracker_k >= 0 && best_tracker_k < s_tracker.count) {
			uint8_t kind = s_tracker.objs[best_tracker_k].kind;
			if (kind == NDW_K_STONE || kind == NDW_K_ROCK || kind == NDW_K_BOULDER)
				s_tracker.objs[best_tracker_k].harvested = 1;
			else if (kind == NDW_K_DECID || kind == NDW_K_TALL || kind == NDW_K_FIR)
				s_tracker.objs[best_tracker_k].stick_harvested = 1;
		}
		if (out && sz > 0)
			snprintf(out, sz, "%s", best_cmd);
		return (int)strlen(best_cmd);
	}
	return 0;
}

int nd_seven_action(const nd_client_state_t *st, char *out, size_t sz)
{
	return seven_action_impl(st, out, sz, 0);
}

int nd_seven_action_execute(const nd_client_state_t *st, char *out, size_t sz)
{
	return seven_action_impl(st, out, sz, 1);
}
