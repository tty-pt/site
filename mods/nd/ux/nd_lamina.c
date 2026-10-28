#include <string.h>
#include <stdint.h>
#include "nd_lamina.h"
#include "nd_scene.h"
#include "nd_seven.h"

/* Helper to blend / write 32-bit RGBA pixel (0xAABBGGRR in LE format) */
static inline void set_pixel(uint32_t *fb, uint32_t w, uint32_t h, uint32_t x, uint32_t y, uint32_t color)
{
	if (x < w && y < h)
		fb[y * w + x] = color;
}

/* Alpha-blending of source color over destination pixel */
static inline uint32_t blend_pixel(uint32_t dst, uint32_t src)
{
	uint32_t sa = (src >> 24) & 0xFF;
	if (sa == 0) return dst;
	if (sa >= 255) return src;

	uint32_t sr = src & 0xFF;
	uint32_t sg = (src >> 8) & 0xFF;
	uint32_t sb = (src >> 16) & 0xFF;

	uint32_t dr = dst & 0xFF;
	uint32_t dg = (dst >> 8) & 0xFF;
	uint32_t db = (dst >> 16) & 0xFF;

	uint32_t inv = 255 - sa;
	uint32_t r = (sr * sa + dr * inv) / 255;
	uint32_t g = (sg * sa + dg * inv) / 255;
	uint32_t b = (sb * sa + db * inv) / 255;

	return 0xFF000000 | (b << 16) | (g << 8) | r;
}

static inline void blend_pixel_at(uint32_t *fb, uint32_t w, uint32_t h, uint32_t x, uint32_t y, uint32_t color)
{
	if (x < w && y < h) {
		fb[y * w + x] = blend_pixel(fb[y * w + x], color);
	}
}

static void fill_rect(uint32_t *fb, uint32_t w, uint32_t h, int rx, int ry, int rw, int rh, uint32_t color)
{
	int x0 = rx < 0 ? 0 : rx;
	int y0 = ry < 0 ? 0 : ry;
	int x1 = rx + rw;
	int y1 = ry + rh;
	if (x1 > (int)w) x1 = (int)w;
	if (y1 > (int)h) y1 = (int)h;
	for (int y = y0; y < y1; y++) {
		for (int x = x0; x < x1; x++) {
			fb[y * w + x] = color;
		}
	}
}

static void fill_rect_blend(uint32_t *fb, uint32_t w, uint32_t h, int rx, int ry, int rw, int rh, uint32_t color)
{
	int x0 = rx < 0 ? 0 : rx;
	int y0 = ry < 0 ? 0 : ry;
	int x1 = rx + rw;
	int y1 = ry + rh;
	if (x1 > (int)w) x1 = (int)w;
	if (y1 > (int)h) y1 = (int)h;
	for (int y = y0; y < y1; y++) {
		for (int x = x0; x < x1; x++) {
			fb[y * w + x] = blend_pixel(fb[y * w + x], color);
		}
	}
}

/* Rounded rectangle with antialiased soft corners */
static void fill_rounded_rect(uint32_t *fb, uint32_t w, uint32_t h,
                              int rx, int ry, int rw, int rh, int radius, uint32_t color)
{
	if (radius <= 0) {
		fill_rect_blend(fb, w, h, (uint32_t)rx, (uint32_t)ry, (uint32_t)rw, (uint32_t)rh, color);
		return;
	}
	int r2 = radius * radius;
	for (int y = ry; y < ry + rh; y++) {
		if (y < 0 || (uint32_t)y >= h) continue;
		for (int x = rx; x < rx + rw; x++) {
			if (x < 0 || (uint32_t)x >= w) continue;

			int dx = 0;
			int dy = 0;
			if (x < rx + radius) dx = rx + radius - x;
			else if (x >= rx + rw - radius) dx = x - (rx + rw - radius - 1);

			if (y < ry + radius) dy = ry + radius - y;
			else if (y >= ry + rh - radius) dy = y - (ry + rh - radius - 1);

			if (dx > 0 && dy > 0) {
				int dist2 = dx * dx + dy * dy;
				if (dist2 > r2 + radius) {
					continue; /* Outside corner */
				} else if (dist2 > r2 - radius) {
					/* Soft AA edge */
					uint32_t a = (color >> 24) & 0xFF;
					uint32_t soft_a = (a * (r2 + radius - dist2)) / (radius * 2);
					uint32_t soft_col = (soft_a << 24) | (color & 0x00FFFFFF);
					blend_pixel_at(fb, w, h, (uint32_t)x, (uint32_t)y, soft_col);
					continue;
				}
			}
			blend_pixel_at(fb, w, h, (uint32_t)x, (uint32_t)y, color);
		}
	}
}

/* Radial glow circle */
static void draw_radial_glow(uint32_t *fb, uint32_t w, uint32_t h,
                             int cx, int cy, int max_r, uint32_t color)
{
	if (max_r <= 0) return;
	uint32_t base_a = (color >> 24) & 0xFF;
	uint32_t rgb = color & 0x00FFFFFF;

	for (int dy = -max_r; dy <= max_r; dy++) {
		int y = cy + dy;
		if (y < 0 || (uint32_t)y >= h) continue;
		for (int dx = -max_r; dx <= max_r; dx++) {
			int x = cx + dx;
			if (x < 0 || (uint32_t)x >= w) continue;

			int dist2 = dx * dx + dy * dy;
			if (dist2 <= max_r * max_r) {
				int dist = 0;
				while ((dist + 1) * (dist + 1) <= dist2) dist++;
				uint32_t a = (base_a * (max_r - dist)) / max_r;
				blend_pixel_at(fb, w, h, (uint32_t)x, (uint32_t)y, (a << 24) | rgb);
			}
		}
	}
}

/* The ASCII map's colours. nd.css colours the ASCII map's spans with
 * cfN / cN classes keyed by ANSI colour index (0..15, bold folded into 8..15);
 * this is that same palette (--c0..--c15) so a tile reads the same colour here
 * as it does there. 0xAABBGGRR. */
static const uint32_t g_ansi_rgb[16] = {
	0xFF2C2C2C, 0xFF6A4ABD, 0xFF465E3A, 0xFF5A97A3,
	0xFFBD8973, 0xFFAC778A, 0xFF5F5C41, 0xFFA5858B,
	0xFFC1AAB4, 0xFF874ABD, 0xFF8AD353, 0xFF2ECDB8,
	0xFFBD9C73, 0xFFC58995, 0xFFBAD016, 0xFFF5F5F5
};

static inline uint32_t ansi_rgb(uint8_t idx)
{
	return g_ansi_rgb[idx & 15];
}

/* Scale an opaque colour's brightness by num/den. */
static inline uint32_t shade(uint32_t c, uint32_t num, uint32_t den)
{
	uint32_t r = ((c & 0xFF) * num) / den;
	uint32_t g = (((c >> 8) & 0xFF) * num) / den;
	uint32_t b = (((c >> 16) & 0xFF) * num) / den;
	if (r > 255) r = 255;
	if (g > 255) g = 255;
	if (b > 255) b = 255;
	return 0xFF000000 | (b << 16) | (g << 8) | r;
}

/* 3x5 bitmap font for the non-plant cell glyphs the ASCII map uses:
 * '!' entity/player, '$' shop, '~' pond, '<' '>' stairs, plus a few more. */
static const uint16_t g_font_3x5[128] = {
	['!'] = 0x2482, /* 010 010 010 000 010 */
	['$'] = 0x3C9E, /* 011 110 010 011 110 */
	['~'] = 0x0780, /* 000 011 110 000 000 */
	['*'] = 0x55D5, /* 101 010 111 010 101 */
	['>'] = 0x4212, /* arrow right */
	['<'] = 0x1242, /* arrow left */
	['.'] = 0x0002, /* dot */
	['#'] = 0x5757, /* hatch */
	['+'] = 0x2720, /* cross */
	['@'] = 0x7572, /* at */
	['?'] = 0x7120  /* question */
};

static void draw_glyph_scaled(uint32_t *fb, uint32_t w, uint32_t h,
                              int x, int y, int ch, uint32_t color, int scale)
{
	if (ch <= 0 || ch >= 128) return;
	uint16_t pattern = g_font_3x5[ch];
	if (!pattern) {
		/* A glyph we have no bitmap for still has to show up: a solid pip. */
		fill_rounded_rect(fb, w, h, x, y + scale, 3 * scale, 3 * scale, scale, color);
		return;
	}

	for (int row = 0; row < 5; row++) {
		for (int col = 0; col < 3; col++) {
			int bit = 14 - (row * 3 + col);
			if (pattern & (1 << bit))
				fill_rect_blend(fb, w, h, (uint32_t)(x + col * scale),
				                (uint32_t)(y + row * scale), (uint32_t)scale,
				                (uint32_t)scale, color);
		}
	}
}

/* Plants. The ASCII map shows one letter per plant cell: the letter family says
 * what it is (x/X deciduous or crop, t/T tall tree, a/A fir, i/I cactus), the
 * case says how grown it is (lower small, upper big), and the colour is the
 * species' own. Draw a sprite per family, in that colour, in a cell box
 * (cx,cy,cw,ch). */
static int is_plant_glyph(int g)
{
	return g == 'x' || g == 'X' || g == 't' || g == 'T' ||
	       g == 'a' || g == 'A' || g == 'i' || g == 'I';
}

static int is_mineral_glyph(int g)
{
	return g == '*' || g == 'o' || g == 'O';
}

static void draw_crown(uint32_t *fb, uint32_t w, uint32_t h, int mx, int top,
                       int cw_, int ch_, uint32_t col)
{
	if (cw_ < 2) cw_ = 2;
	if (ch_ < 2) ch_ = 2;
	fill_rounded_rect(fb, w, h, mx - cw_ / 2, top, cw_, ch_, cw_ / 2, shade(col, 70, 100));
	fill_rounded_rect(fb, w, h, mx - cw_ / 2, top, cw_ - 1, ch_ - 1, (cw_ - 1) / 2, col);
	if (cw_ >= 6)
		fill_rounded_rect(fb, w, h, mx - cw_ / 4 - 1, top + 1, cw_ / 2, ch_ / 3, 1,
		                  shade(col, 130, 100));
}

static void draw_plant(uint32_t *fb, uint32_t w, uint32_t h,
                       int cx, int cy, int cw, int ch, int glyph, uint32_t col)
{
	const uint32_t bark = 0xFF2A4A6B;
	int big = (glyph >= 'A' && glyph <= 'Z');
	int mx = cx + cw / 2;
	int my = cy + ch / 2;
	int fam = glyph | 0x20; /* lower-case */

	if (fam == 'x') {
		/* Deciduous tree / crop: round crown on a short trunk. */
		int crown_w = big ? cw - 1 : (cw * 6) / 10;
		int crown_h = (crown_w * 9) / 10 + 1;
		int trunk_h = big ? 5 : 3;
		int total = crown_h + trunk_h - 1;
		int top = my - total / 2;
		fill_rect_blend(fb, w, h, (uint32_t)(mx - (big ? 1 : 0)), (uint32_t)(top + crown_h - 1),
		                big ? 2u : 1u, (uint32_t)trunk_h, bark);
		draw_crown(fb, w, h, mx, top, crown_w, crown_h, col);
	} else if (fam == 't') {
		/* Tall tree: narrower, taller crown on a long trunk. */
		int crown_w = big ? (cw * 8) / 10 : (cw * 5) / 10;
		int crown_h = (crown_w * 14) / 10;
		int trunk_h = big ? 7 : 5;
		int total = crown_h + trunk_h - 1;
		int top = my - total / 2;
		fill_rect_blend(fb, w, h, (uint32_t)(mx - (big ? 1 : 0)), (uint32_t)(top + crown_h - 1),
		                big ? 2u : 1u, (uint32_t)trunk_h, bark);
		draw_crown(fb, w, h, mx, top, crown_w, crown_h, col);
	} else if (fam == 'a') {
		/* Fir: stacked, widening tiers on a short trunk. */
		int tiers = big ? 3 : 2;
		int tier_h = big ? 4 : 3;
		int total = tiers * tier_h + 3;
		int top = my - total / 2;
		for (int i = 0; i < tiers; i++) {
			int tw = ((i + 1) * (cw - 1)) / tiers;
			if (tw < 2) tw = 2;
			uint32_t tc = shade(col, 100 - (uint32_t)(tiers - 1 - i) * 14, 100); /* top tier darkest, bottom exact */
			fill_rounded_rect(fb, w, h, mx - tw / 2, top + i * (tier_h - 1), tw, tier_h, 1, tc);
		}
		fill_rect_blend(fb, w, h, (uint32_t)(mx - (big ? 1 : 0)),
		                (uint32_t)(top + tiers * (tier_h - 1) + 1), big ? 2u : 1u, 3u, bark);
	} else {
		/* Cactus: a column with two arms. */
		int body_h = big ? (ch * 5) / 10 : (ch * 4) / 10;
		int bw = big ? 3 : 2;
		int top = my - body_h / 2;
		fill_rounded_rect(fb, w, h, mx - bw / 2, top, bw, body_h, 1, col);
		fill_rect_blend(fb, w, h, (uint32_t)(mx - bw / 2 - 2), (uint32_t)(top + body_h / 3), 2u, 2u, col);
		fill_rect_blend(fb, w, h, (uint32_t)(mx - bw / 2 - 2), (uint32_t)(top + body_h / 6), 1u,
		                (uint32_t)(body_h / 4), col);
		fill_rect_blend(fb, w, h, (uint32_t)(mx + bw - bw / 2), (uint32_t)(top + body_h / 2), 2u, 2u, col);
		fill_rect_blend(fb, w, h, (uint32_t)(mx + bw - bw / 2 + 1), (uint32_t)(top + body_h / 4), 1u,
		                (uint32_t)(body_h / 4), col);
	}
}

/* Pixels for n/30 of a tile, at least 1. */
static int tile_px(uint32_t tile_sz, int n)
{
	int v = (int)tile_sz * n / 30;
	return v < 1 ? 1 : v;
}

/* One stone, rock or boulder with its foot at (fx, fy): stones are small grey
 * dots, rocks bigger shaded tan/grey blobs, boulders bigger still. */
static void draw_mineral(uint32_t *fb, uint32_t w, uint32_t h, uint32_t tile_sz,
                         int fx, int fy, int kind, int variant)
{
	if (kind == NDW_K_STONE) {
		int d = tile_px(tile_sz, 2);
		uint32_t g = 0xFF8E8E8E - (uint32_t)(variant & 3) * 0x00080808u;
		fill_rect_blend(fb, w, h, (uint32_t)(fx - d / 2), (uint32_t)(fy - d), (uint32_t)d, (uint32_t)d, g);
		return;
	}
	{
		int boulder = kind == NDW_K_BOULDER;
		int rw = tile_px(tile_sz, boulder ? 7 : 4) + 1;
		int rh = rw * 3 / 4 + 1;
		uint32_t base = (variant & 1) ? 0xFF6E8CB0 : 0xFF959AA4;   /* tan / cold grey */
		int x = fx - rw / 2, y = fy - rh;

		fill_rect_blend(fb, w, h, (uint32_t)(x - 1), (uint32_t)(fy - 1), (uint32_t)(rw + 2), 2u, 0x55000000);
		fill_rounded_rect(fb, w, h, x, y, rw, rh, rh / 2 + 1, shade(base, 62, 100));
		fill_rounded_rect(fb, w, h, x, y, rw - 1, rh - 1, (rh - 1) / 2 + 1, base);
		fill_rect_blend(fb, w, h, (uint32_t)(x + 1), (uint32_t)(y + 1), (uint32_t)(rw / 3 > 0 ? rw / 3 : 1), 1u,
		                shade(base, 140, 100));
	}
}

/* The plants and minerals of tile (col,row), exactly where Seven puts them,
 * scaled from the 160x128 world tile to the map tile. */
static void draw_tile_objects(uint32_t *fb, uint32_t w, uint32_t h, const nd_map_data_t *map,
                              int col, int row, uint32_t tx, uint32_t ty, uint32_t tile_sz)
{
	ndw_obj_t objs[NDW_MAX_OBJ];
	int n = nd_seven_tile_objects(map, col, row, objs);

	for (int i = 0; i < n; i++) {
		const ndw_obj_t *o = &objs[i];
		int fx = (int)tx + o->x * (int)tile_sz / NDW_TILE_W;
		int fy = (int)ty + o->y * (int)tile_sz / NDW_TILE_H;

		if (o->kind >= NDW_K_DECID && o->kind <= NDW_K_CACTUS) {
			int cw = tile_px(tile_sz, o->big ? 12 : 10);
			int ch = cw * 14 / 10 + 2;
			if (cw < 5) cw = 5;
			draw_plant(fb, w, h, fx - cw / 2, fy - ch, cw, ch, o->glyph, ansi_rgb(o->fg));
		} else if (o->kind >= NDW_K_STONE && o->kind <= NDW_K_BOULDER) {
			draw_mineral(fb, w, h, tile_sz, fx, fy, o->kind, o->variant);
		}
	}
}

/* One map cell (left, centre or right third of a tile): a plant sprite or the
 * glyph the ASCII map shows there, in the colour the ASCII map shows it. */
static void draw_cell(uint32_t *fb, uint32_t w, uint32_t h,
                      int cx, int cy, int cw, int ch, int glyph, uint8_t fg)
{
	if (glyph <= 0 || glyph == ' ')
		return;
	uint32_t col = ansi_rgb(fg);
	/* Flora and minerals are laid out by draw_tile_objects, at the positions
	 * Seven places them; every other glyph keeps its cell. */
	if (is_plant_glyph(glyph) || is_mineral_glyph(glyph))
		return;
	int scale = cw >= 9 ? 2 : 1;
	draw_glyph_scaled(fb, w, h, cx + (cw - 3 * scale) / 2, cy + (ch - 5 * scale) / 2,
	                  glyph, col, scale);
}

/* Thematic profiles */
static const nd_lamina_theme_t g_themes[] = {
	{
		.name = "lamina",
		.bg_color = 0xFF08090C,
		.floor_color = 0xFF262A36,
		.wall_color = 0xFF4C5468,
		.door_color = 0xFFE08833,
		.player_color = 0xFF00E5FF,
		.player_glow = 0x6600C8FF,
		.entity_color = 0xFFFFD700,
		.grid_border = 0x22FFFFFF,
		.rounded_radius = 3,
		.vignette_strength = 0 /* tiles keep the ASCII map's exact colours */
	},
	{
		.name = "classic",
		.bg_color = 0xFF000000,
		.floor_color = 0xFF181818,
		.wall_color = 0xFF666666,
		.door_color = 0xFFAAAA00,
		.player_color = 0xFF00FF00,
		.player_glow = 0x4400FF00,
		.entity_color = 0xFFFFFF00,
		.grid_border = 0x11FFFFFF,
		.rounded_radius = 1,
		.vignette_strength = 0
	},
	{
		.name = "cyber",
		.bg_color = 0xFF050510,
		.floor_color = 0xFF11172E,
		.wall_color = 0xFFFF0055,
		.door_color = 0xFF00F0FF,
		.player_color = 0xFF00FFFF,
		.player_glow = 0x8800FFFF,
		.entity_color = 0xFFFF2299,
		.grid_border = 0x3300FFFF,
		.rounded_radius = 2,
		.vignette_strength = 75
	},
	{
		.name = "dungeon",
		.bg_color = 0xFF0A0705,
		.floor_color = 0xFF221A14,
		.wall_color = 0xFF554433,
		.door_color = 0xFFAA6622,
		.player_color = 0xFFFFCC33,
		.player_glow = 0x55FF9900,
		.entity_color = 0xFFFF4422,
		.grid_border = 0x22886644,
		.rounded_radius = 4,
		.vignette_strength = 80
	}
};

const nd_lamina_theme_t *nd_lamina_theme_find(const char *name)
{
	if (name && name[0]) {
		for (size_t i = 0; i < sizeof(g_themes) / sizeof(g_themes[0]); i++) {
			if (strcmp(g_themes[i].name, name) == 0)
				return &g_themes[i];
		}
	}
	return &g_themes[0]; /* Default "lamina" */
}

/* Time of day ambient multiplier (0..255) */
static inline uint32_t tod_brightness(uint32_t tod)
{
	if (tod == 0) return 240; /* Noon default */
	uint32_t sec = tod % 86400;
	/* Noon = 43200 (brightest: 255), Midnight = 0 (dimmest: 140) */
	int diff = (int)sec - 43200;
	if (diff < 0) diff = -diff;
	return 255 - (uint32_t)((diff * 115) / 43200);
}

/*
 * Organic biome borders. The tile at grid (gx, gy) was filled with its own
 * biome colour; where a neighbouring tile has another one, the pixels near
 * that edge are repainted with the neighbour's colour wherever the shared
 * ndw_ground_weights (the very contour Seven's ground uses, evaluated at the
 * pixel's world position) hands the point to that neighbour: crisp, no mixed
 * colours. Rooms and the grid's outside never blend.
 */
static void blend_tile_border(uint32_t *fb, uint32_t w, uint32_t h, const nd_map_data_t *map,
                              uint32_t cols, uint32_t rows, int gx, int gy,
                              int tx, int ty, int tile_sz, uint32_t amb)
{
	const nd_map_tile_t *own = &map->tiles[gy * (int)cols + gx];
	uint32_t col[9];
	unsigned no_blend = 0;
	uint8_t grp[9];
	int differs = 0;
	int16_t pos[4];
	int band = (NDW_BLEND_BAND * tile_sz + NDW_TILE_W - 1) / NDW_TILE_W + 1;
	const ndw_warp_grid_t *grid;

	if (own->flags & ND_MAP_FLAG_ROOM)
		return;
	for (int dj = -1; dj <= 1; dj++)
		for (int di = -1; di <= 1; di++) {
			int nx = gx + di, ny = gy + dj, k = (dj + 1) * 3 + (di + 1);
			const nd_map_tile_t *nt = own;

			if (di || dj) {
				if (nx < 0 || ny < 0 || nx >= (int)cols || ny >= (int)rows) {
					no_blend |= NDW_OWN_BIT(di, dj);
				} else {
					nt = &map->tiles[ny * (int)cols + nx];
					if (nt->flags & ND_MAP_FLAG_ROOM)
						no_blend |= NDW_OWN_BIT(di, dj);
				}
			}
			col[k] = shade(ansi_rgb(nt->bg & 7), amb, 255);
			differs |= ((nt->bg & 7) != (own->bg & 7));
			grp[k] = (uint8_t)(nt->bg & 7);
		}
	if (!differs)
		return;
	pos[0] = (int16_t)(map->pos[0] + gx - map->player_x);
	pos[1] = (int16_t)(map->pos[1] - (gy - map->player_y));
	pos[2] = map->pos[2];
	pos[3] = map->pos[3];
	grid = nd_seven_warp_grid(pos);

	for (int py = 0; py < tile_sz; py++) {
		int qy = ((py * 2 + 1) * (NDW_TILE_H * NDW_SUB)) / (2 * tile_sz);
		int row_edge = py < band || py >= tile_sz - band;

		for (int px = 0; px < tile_sz; px++) {
			int qx;
			uint16_t wt[9];

			if (!row_edge && px >= band && px < tile_sz - band)
				continue;
			qx = ((px * 2 + 1) * (NDW_TILE_W * NDW_SUB)) / (2 * tile_sz);
			if (ndw_ground_weights(grid, qx, qy, grp, no_blend, wt)) {
				int own_k = 4;

				for (int k = 0; k < 9; k++)
					if (wt[k])
						own_k = k;
				fill_rect(fb, w, h, tx + px, ty + py, 1, 1, col[own_k]);
			}
		}
	}
}

void nd_lamina_render_map_themed(uint32_t *fb, uint32_t w, uint32_t h,
                                 const nd_map_data_t *map,
                                 const char *theme_name,
                                 uint32_t tod,
                                 uint32_t frame)
{
	nd_lamina_render_map_live(fb, w, h, map, theme_name, tod, frame, 0, 0, 0);
}

void nd_lamina_render_map_live(uint32_t *fb, uint32_t w, uint32_t h,
                               const nd_map_data_t *map,
                               const char *theme_name,
                               uint32_t tod,
                               uint32_t frame,
                               int live, int lamb_x, int lamb_y)
{
	if (!fb || w == 0 || h == 0)
		return;

	const nd_lamina_theme_t *theme = nd_lamina_theme_find(theme_name);

	/* 1. Base dark background */
	fill_rect(fb, w, h, 0, 0, w, h, theme->bg_color);

	if (!map || map->width == 0 || map->height == 0)
		return;

	uint32_t cols = map->width;
	uint32_t rows = map->height;
	if (cols > 7) cols = 7;
	if (rows > 7) rows = 7;

	uint32_t pad = 4;
	uint32_t avail_w = (w > pad * 2) ? (w - pad * 2) : w;
	uint32_t avail_h = (h > pad * 2) ? (h - pad * 2) : h;
	uint32_t tile_w = avail_w / cols;
	uint32_t tile_h = avail_h / rows;
	uint32_t tile_sz = (tile_w < tile_h) ? tile_w : tile_h;
	if (tile_sz < 8) tile_sz = 8;

	uint32_t start_x = (w - (cols * tile_sz)) / 2;
	uint32_t start_y = (h - (rows * tile_sz)) / 2;

	uint32_t amb = tod_brightness(tod);

	/* Check if player tile exists in map */
	int have_player = 0, ptx = 0, pty = 0;
	for (uint32_t y = 0; y < rows; y++) {
		for (uint32_t x = 0; x < cols; x++) {
			if (map->tiles[y * cols + x].flags & ND_MAP_FLAG_PLAYER) {
				have_player = 1;
				ptx = (int)x;
				pty = (int)y;
				break;
			}
		}
		if (have_player) break;
	}

	/* Live continuous mode is only active when live is requested AND player exists */
	int is_live_centered = live && have_player;
	int pcx = 0, pcy = 0;

	/* Live mode: ONE pixel origin for the whole grid. The player's continuous
	 * world position is rounded once; every tile then sits at an exact
	 * multiple of tile_sz from it, so neighbours abut with no 1px gaps and
	 * never shift relative to each other (no jitter, no seams). */
	int live_ox = 0, live_oy = 0;

	if (is_live_centered) {
		pcx = (int)w / 2;
		pcy = (int)h / 2;
		int wx = ptx * NDW_TILE_W + lamb_x;
		int wy = pty * NDW_TILE_H + lamb_y;
		int px = (wx * (int)tile_sz + (wx >= 0 ? NDW_TILE_W / 2 : -NDW_TILE_W / 2)) / NDW_TILE_W;
		int py = (wy * (int)tile_sz + (wy >= 0 ? NDW_TILE_H / 2 : -NDW_TILE_H / 2)) / NDW_TILE_H;
		live_ox = pcx - px;
		live_oy = pcy - py;
	} else {
		pcx = (int)(start_x + (uint32_t)ptx * tile_sz + tile_sz / 2);
		pcy = (int)(start_y + (uint32_t)pty * tile_sz + tile_sz / 2);
	}

	/* 2. Render each tile the way the ASCII map does: the biome's background
	 * colour on EVERY tile (wilderness as well as rooms), three cells per tile
	 * (left / centre / right) showing plants, stairs, shops, ponds and entities
	 * in their own colours, and walls in the wall-paint colour around rooms. */
	for (uint32_t y = 0; y < rows; y++) {
		for (uint32_t x = 0; x < cols; x++) {
			const nd_map_tile_t *tile = &map->tiles[y * cols + x];
			int tx, ty;
			if (is_live_centered) {
				tx = live_ox + (int)x * (int)tile_sz;
				ty = live_oy + (int)y * (int)tile_sz;
			} else {
				tx = (int)(start_x + x * tile_sz);
				ty = (int)(start_y + y * tile_sz);
			}

			int is_room = (tile->flags & ND_MAP_FLAG_ROOM);
			int is_player = (tile->flags & ND_MAP_FLAG_PLAYER);

			/* Biome background (ANSI 0..7), dimmed by time of day. */
			uint32_t tile_col = shade(ansi_rgb(tile->bg & 7), amb, 255);
			fill_rect(fb, w, h, tx, ty, (int)tile_sz, (int)tile_sz, tile_col);
			blend_tile_border(fb, w, h, map, cols, rows, (int)x, (int)y, tx, ty, (int)tile_sz, amb);

			/* Rooms get a soft outline; wilderness stays seamless. */
			if (is_room && theme->grid_border && !is_live_centered) {
				fill_rect_blend(fb, w, h, tx, ty, (int)tile_sz, 1, theme->grid_border);
				fill_rect_blend(fb, w, h, tx, ty, 1, (int)tile_sz, theme->grid_border);
			}

			/* Walls / doors, drawn only around room tiles (as the ASCII map does). */
			if (is_room) {
				uint32_t wall_col = tile->wall_fg ? ansi_rgb(tile->wall_fg) : theme->wall_color;
				uint32_t door_col = theme->door_color;
				int t3 = (int)tile_sz / 3;

				/* North */
				if (!(tile->exits & 2))
					fill_rect_blend(fb, w, h, tx, ty, (int)tile_sz, 2, wall_col);
				else if (tile->doors & 2)
					fill_rect_blend(fb, w, h, tx + t3, ty, t3, 2, door_col);
				/* South */
				if (!(tile->exits & 16))
					fill_rect_blend(fb, w, h, tx, ty + (int)tile_sz - 2, (int)tile_sz, 2, wall_col);
				else if (tile->doors & 16)
					fill_rect_blend(fb, w, h, tx + t3, ty + (int)tile_sz - 2, t3, 2, door_col);
				/* West */
				if (!(tile->exits & 1))
					fill_rect_blend(fb, w, h, tx, ty, 2, (int)tile_sz, wall_col);
				else if (tile->doors & 1)
					fill_rect_blend(fb, w, h, tx, ty + t3, 2, t3, door_col);
				/* East */
				if (!(tile->exits & 8))
					fill_rect_blend(fb, w, h, tx + (int)tile_sz - 2, ty, 2, (int)tile_sz, wall_col);
				else if (tile->doors & 8)
					fill_rect_blend(fb, w, h, tx + (int)tile_sz - 2, ty + t3, 2, t3, door_col);
			}

			/* The three cells: left, centre, right. */
			int cw = (int)(tile_sz / 3);
			int ch = (int)tile_sz;
			int cx0 = tx + ((int)tile_sz - 3 * cw) / 2;
			draw_cell(fb, w, h, cx0, ty, cw, ch, tile->side_l, tile->fg_l);
			if (!is_player)
				draw_cell(fb, w, h, cx0 + cw, ty, cw, ch, tile->symbol, tile->fg);
			draw_cell(fb, w, h, cx0 + 2 * cw, ty, cw, ch, tile->side_r, tile->fg_r);
		}
	}

	/* Objects in a second pass: a crown or rock that overhangs its tile is
	 * never overpainted by a neighbour's ground drawn later. */
	for (uint32_t y = 0; y < rows; y++) {
		for (uint32_t x = 0; x < cols; x++) {
			int tx, ty;
			if (is_live_centered) {
				tx = live_ox + (int)x * (int)tile_sz;
				ty = live_oy + (int)y * (int)tile_sz;
			} else {
				tx = (int)(start_x + x * tile_sz);
				ty = (int)(start_y + y * tile_sz);
			}
			draw_tile_objects(fb, w, h, map, (int)x, (int)y, (uint32_t)tx, (uint32_t)ty, tile_sz);
		}
	}

	/* Player avatar with breathing radial glow. Drawn last so no tree hides it. */
	if (have_player) {
		int cx = pcx, cy = pcy;

		/* Breathing radius based on frame counter (0..60 cycle) */
		int pulse = (int)(frame % 60);
		if (pulse > 30) pulse = 60 - pulse;
		int glow_radius = (int)(tile_sz / 2) + (pulse / 6);

		draw_radial_glow(fb, w, h, cx, cy, glow_radius, theme->player_glow);

		/* Inner player marker */
		fill_rounded_rect(fb, w, h, cx - 3, cy - 3, 7, 7, 3, theme->player_color);
		fill_rounded_rect(fb, w, h, cx - 1, cy - 1, 3, 3, 1, 0xFFFFFFFF);
	}

	/* 3. Soft atmospheric vignette around map edges */
	if (theme->vignette_strength > 0) {
		int cx = (int)w / 2;
		int cy = (int)h / 2;
		int max_dist2 = cx * cx + cy * cy;
		for (uint32_t y = 0; y < h; y++) {
			int dy = (int)y - cy;
			for (uint32_t x = 0; x < w; x++) {
				int dx = (int)x - cx;
				int dist2 = dx * dx + dy * dy;
				if (dist2 > max_dist2 / 3) {
					uint32_t vig = (uint32_t)(((dist2 - max_dist2 / 3) * theme->vignette_strength) / (max_dist2 - max_dist2 / 3));
					if (vig > 180) vig = 180;
					blend_pixel_at(fb, w, h, x, y, (vig << 24) | 0x000000);
				}
			}
		}
	}

	/* 4. Circular / rounded masking when live smooth movement is active.
	 * Radius is one room/tile smaller than the viewport half-dimension so edge
	 * rooms never pop in/out as the player moves continuously. */
	if (is_live_centered) {
		int cx = (int)w / 2;
		int cy = (int)h / 2;
		int max_dim = (int)(w < h ? w : h);
		int r = (max_dim / 2) - (int)tile_sz - 2;
		if (r < (int)tile_sz) r = (int)tile_sz;
		int r2 = r * r;
		int rim_r2 = (r - 1) * (r - 1);
		uint32_t border_col = theme->grid_border ? theme->grid_border : 0x55FFFFFF;

		for (int y = 0; y < (int)h; y++) {
			int dy = y - cy;
			for (int x = 0; x < (int)w; x++) {
				int dx = x - cx;
				int dist2 = dx * dx + dy * dy;
				if (dist2 > r2 + r) {
					/* Fully outside circular mask: transparent */
					fb[y * w + x] = 0x00000000u;
				} else if (dist2 > r2 - r) {
					/* Antialiased rim: fade to transparent. Colour is
					 * premultiplied (the canvas composites premultiplied). */
					int factor = dist2 - (r2 - r);
					uint32_t pixel = fb[y * w + x];
					uint32_t fade = (uint32_t)((factor * 255) / (2 * r));
					if (fade > 255) fade = 255;
					uint32_t keep = 255 - fade;
					uint32_t cr = (((pixel >> 16) & 0xFF) * keep) / 255;
					uint32_t cg = (((pixel >> 8) & 0xFF) * keep) / 255;
					uint32_t cb = ((pixel & 0xFF) * keep) / 255;
					fb[y * w + x] = (keep << 24) | (cr << 16) | (cg << 8) | cb;
				} else if (dist2 >= rim_r2) {
					/* Subtle aesthetic outer ring border */
					blend_pixel_at(fb, w, h, (uint32_t)x, (uint32_t)y, border_col);
				}
			}
		}
	}
}

void nd_lamina_render_map(uint32_t *fb, uint32_t w, uint32_t h, const nd_map_data_t *map)
{
	nd_lamina_render_map_themed(fb, w, h, map, "lamina", 43200, 0);
}

void nd_lamina_render_scene(uint32_t *fb, uint32_t w, uint32_t h, const nd_scene_t *scene)
{
	if (!fb || w == 0 || h == 0 || !scene)
		return;

	fill_rect(fb, w, h, 0, 0, w, h, 0xFF08080C);
}

void nd_lamina_render_room(uint32_t *fb, uint32_t w, uint32_t h, const nd_client_state_t *st)
{
	(void)st;
	if (!fb || w == 0 || h == 0) return;
	fill_rect(fb, w, h, 0, 0, w, h, 0xFF08080C);
}
