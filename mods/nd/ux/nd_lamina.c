#include <string.h>
#include <stdint.h>
#include "nd_lamina.h"
#include "nd_scene.h"

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

static void fill_rect(uint32_t *fb, uint32_t w, uint32_t h, uint32_t rx, uint32_t ry, uint32_t rw, uint32_t rh, uint32_t color)
{
	for (uint32_t y = ry; y < ry + rh && y < h; y++) {
		for (uint32_t x = rx; x < rx + rw && x < w; x++) {
			fb[y * w + x] = color;
		}
	}
}

static void fill_rect_blend(uint32_t *fb, uint32_t w, uint32_t h, uint32_t rx, uint32_t ry, uint32_t rw, uint32_t rh, uint32_t color)
{
	for (uint32_t y = ry; y < ry + rh && y < h; y++) {
		for (uint32_t x = rx; x < rx + rw && x < w; x++) {
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

/* 3x5 compact bitmap font for map tile glyphs ('!', 'A', '>', '<', '.', '#', '+') */
static const uint16_t g_font_3x5[] = {
	['!'] = 0x5040, /* 101, 000, 100 -> binary packed */
	['A'] = 0x7577, /* 111, 101, 111, 101, 101 */
	['>'] = 0x4212, /* > arrow */
	['<'] = 0x1242, /* < arrow */
	['.'] = 0x0002, /* dot at bottom center */
	['#'] = 0x5757, /* grid hatch */
	['+'] = 0x2720, /* cross */
	['@'] = 0x7572, /* player glyph */
	['?'] = 0x7120, /* question */
	['t'] = 0x2722, /* small tree: trunk + branch */
	['T'] = 0x7222, /* big tree: canopy bar + trunk */
	['x'] = 0x5250, /* small shrub / plant */
	['X'] = 0x5750, /* big dense bush / foliage */
	['a'] = 0x2575, /* small pine */
	['i'] = 0x2022, /* small cactus */
	['I'] = 0x5722  /* saguaro cactus */
};

static void draw_glyph_3x5(uint32_t *fb, uint32_t w, uint32_t h,
                           int x, int y, char ch, uint32_t color)
{
	if (ch < 0 || (size_t)ch >= sizeof(g_font_3x5) / sizeof(g_font_3x5[0])) return;
	uint16_t pattern = g_font_3x5[(int)ch];
	if (!pattern) return;

	for (int row = 0; row < 5; row++) {
		for (int col = 0; col < 3; col++) {
			int bit = 14 - (row * 3 + col);
			if (bit >= 0 && (pattern & (1 << bit))) {
				blend_pixel_at(fb, w, h, (uint32_t)(x + col), (uint32_t)(y + row), color);
			}
		}
	}
}

/* Biome palette table (0..15) */
static const uint32_t g_biome_palette[16] = {
	[0]  = 0xFF14181E, /* 0: Void / Limbo / Space (deep cosmic obsidian) */
	[1]  = 0xFF243B28, /* 1: Verdant Meadow / Forest (rich moss green) */
	[2]  = 0xFF1E3836, /* 2: Swamp / Wetlands (deep murky teal) */
	[3]  = 0xFF3D3222, /* 3: Plains / Steppe (dry golden umber) */
	[4]  = 0xFF2A2A38, /* 4: Mountain / Stone (cool granite) */
	[5]  = 0xFF18304C, /* 5: Ocean / Lake (midnight azure) */
	[6]  = 0xFF422818, /* 6: Volcanic / Badlands (smoldering rust) */
	[7]  = 0xFF4A4A58, /* 7: Snow / Tundra (permafrost slate) */
	[8]  = 0xFF1E1E28, /* 8: Subterranean / Deep Cavern (shadow basalt) */
	[9]  = 0xFF443820, /* 9: Desert / Dunes (warm sandstone) */
	[10] = 0xFF341C34, /* 10: Arcane Sanctum (mystic violet) */
	[11] = 0xFF202830, /* 11: Urban / Town Settlement (cobblestone) */
	[12] = 0xFF383824, /* 12: Farmland / Orchard (olive grove) */
	[13] = 0xFF2C241E, /* 13: Ruins / Wastes (ancient debris) */
	[14] = 0xFF1C2C28, /* 14: Jungle / Canopy (dense pine) */
	[15] = 0xFF2A2434  /* 15: Celestial / Citadel (aetherium) */
};

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
		.vignette_strength = 60
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
		.vignette_strength = 20
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

void nd_lamina_render_map_themed(uint32_t *fb, uint32_t w, uint32_t h,
                                 const nd_map_data_t *map,
                                 const char *theme_name,
                                 uint32_t tod,
                                 uint32_t frame)
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

	/* 2. Render each room tile */
	for (uint32_t y = 0; y < rows; y++) {
		for (uint32_t x = 0; x < cols; x++) {
			const nd_map_tile_t *tile = &map->tiles[y * cols + x];
			uint32_t tx = start_x + x * tile_sz;
			uint32_t ty = start_y + y * tile_sz;

			int is_room = (tile->flags & ND_MAP_FLAG_ROOM);
			int is_player = (tile->flags & ND_MAP_FLAG_PLAYER);
			int is_entity = (tile->flags & ND_MAP_FLAG_ENTITY);

			/* Biome floor tint */
			uint8_t bio = tile->bio_idx % 16;
			uint32_t bio_col = g_biome_palette[bio];

			uint32_t tile_col;
			if (is_room) {
				/* Blend theme floor color with biome tint */
				uint32_t r = (((theme->floor_color & 0xFF) * 3) + ((bio_col & 0xFF) * 5)) / 8;
				uint32_t g = ((((theme->floor_color >> 8) & 0xFF) * 3) + (((bio_col >> 8) & 0xFF) * 5)) / 8;
				uint32_t b = ((((theme->floor_color >> 16) & 0xFF) * 3) + (((bio_col >> 16) & 0xFF) * 5)) / 8;

				/* Apply time-of-day ambient multiplier */
				r = (r * amb) / 255;
				g = (g * amb) / 255;
				b = (b * amb) / 255;
				tile_col = 0xFF000000 | (b << 16) | (g << 8) | r;
			} else {
				/* Unexplored / cosmic void */
				tile_col = 0xFF0A0C10;
			}

			/* Draw rounded room tile */
			fill_rounded_rect(fb, w, h, (int)tx + 1, (int)ty + 1,
			                  (int)tile_sz - 2, (int)tile_sz - 2,
			                  is_room ? theme->rounded_radius : 1,
			                  tile_col);

			/* Subtle cell border */
			if (is_room && theme->grid_border) {
				fill_rect_blend(fb, w, h, tx + 1, ty + 1, tile_sz - 2, 1, theme->grid_border);
				fill_rect_blend(fb, w, h, tx + 1, ty + 1, 1, tile_sz - 2, theme->grid_border);
			}

			/* Walls / Door Portals */
			if (is_room) {
				uint32_t wall_col = theme->wall_color;
				uint32_t door_col = theme->door_color;

				/* North Wall / Door */
				if (!(tile->exits & 2)) {
					fill_rect_blend(fb, w, h, tx + 1, ty + 1, tile_sz - 2, 2, wall_col);
				} else if (tile->doors & 2) {
					fill_rect_blend(fb, w, h, tx + tile_sz / 3, ty + 1, tile_sz / 3, 2, door_col);
				}

				/* South Wall / Door */
				if (!(tile->exits & 16)) {
					fill_rect_blend(fb, w, h, tx + 1, ty + tile_sz - 3, tile_sz - 2, 2, wall_col);
				} else if (tile->doors & 16) {
					fill_rect_blend(fb, w, h, tx + tile_sz / 3, ty + tile_sz - 3, tile_sz / 3, 2, door_col);
				}

				/* West Wall / Door */
				if (!(tile->exits & 1)) {
					fill_rect_blend(fb, w, h, tx + 1, ty + 1, 2, tile_sz - 2, wall_col);
				} else if (tile->doors & 1) {
					fill_rect_blend(fb, w, h, tx + 1, ty + tile_sz / 3, 2, tile_sz / 3, door_col);
				}

				/* East Wall / Door */
				if (!(tile->exits & 8)) {
					fill_rect_blend(fb, w, h, tx + tile_sz - 3, ty + 1, 2, tile_sz - 2, wall_col);
				} else if (tile->doors & 8) {
					fill_rect_blend(fb, w, h, tx + tile_sz - 3, ty + tile_sz / 3, 2, tile_sz / 3, door_col);
				}
			}

			/* Up / Down stairs glyphs */
			if (tile->exits & 4) {
				draw_glyph_3x5(fb, w, h, (int)(tx + tile_sz / 2 - 1), (int)(ty + tile_sz / 2 - 2), '>', 0xFFFFFFFF);
			} else if (tile->exits & 32) {
				draw_glyph_3x5(fb, w, h, (int)(tx + tile_sz / 2 - 1), (int)(ty + tile_sz / 2 - 2), '<', 0xFFAAAAAA);
			}

			/* Entity pip */
			if (is_entity && !is_player) {
				uint32_t ec = theme->entity_color;
				fill_rounded_rect(fb, w, h, (int)(tx + tile_sz / 2 - 2), (int)(ty + tile_sz / 2 - 2), 5, 5, 2, ec);
			}

			/* Tree / Plant nature rendering matching ASCII map information */
			if (is_room && !is_player && !is_entity && tile->symbol) {
				char s = (char)tile->symbol;
				if (s == 't' || s == 'T' || s == 'x' || s == 'X' || s == 'a' || s == 'A' || s == 'i' || s == 'I') {
					int cx = (int)(tx + tile_sz / 2);
					int cy = (int)(ty + tile_sz / 2);

					if (s == 't' || s == 'T') {
						/* Tree: lush green canopy circle + trunk */
						fill_rect_blend(fb, w, h, (uint32_t)(cx - 1), (uint32_t)(cy + 1), 2, 4, 0xFF4A3525); /* Bark */
						fill_rounded_rect(fb, w, h, cx - 4, cy - 5, 8, 7, 3, (s == 'T') ? 0xFF2D8A3E : 0xFF3DA652); /* Canopy */
						fill_rounded_rect(fb, w, h, cx - 2, cy - 4, 4, 3, 1, 0xFF54C46B); /* Highlight */
					} else if (s == 'a' || s == 'A') {
						/* Pine / Fir: conical tiered pine */
						fill_rect_blend(fb, w, h, (uint32_t)(cx - 1), (uint32_t)(cy + 2), 2, 3, 0xFF3A2818);
						fill_rounded_rect(fb, w, h, cx - 2, cy - 4, 4, 3, 1, 0xFF1D6632);
						fill_rounded_rect(fb, w, h, cx - 4, cy - 1, 8, 4, 2, 0xFF287A40);
					} else if (s == 'i' || s == 'I') {
						/* Cactus: desert saguaro arms */
						fill_rect_blend(fb, w, h, (uint32_t)(cx - 1), (uint32_t)(cy - 4), 2, 9, 0xFF2E854B);
						fill_rect_blend(fb, w, h, (uint32_t)(cx - 4), (uint32_t)(cy - 2), 2, 4, 0xFF2E854B);
						fill_rect_blend(fb, w, h, (uint32_t)(cx + 2), (uint32_t)(cy - 1), 2, 4, 0xFF2E854B);
						fill_rect_blend(fb, w, h, (uint32_t)(cx - 3), (uint32_t)(cy + 1), 6, 2, 0xFF2E854B);
					} else {
						/* Shrub / crop / bush */
						fill_rounded_rect(fb, w, h, cx - 3, cy - 3, 6, 5, 2, 0xFF4E9E3B);
						fill_rounded_rect(fb, w, h, cx - 1, cy - 2, 3, 2, 1, 0xFF72C758);
					}
				} else if (s != '.' && s != ' ') {
					/* Other feature glyph (e.g. shrine, altar) */
					draw_glyph_3x5(fb, w, h, (int)(tx + tile_sz / 2 - 1), (int)(ty + tile_sz / 2 - 2), s, 0xFFE0E0E0);
				}
			}

			/* Player avatar with breathing radial glow */
			if (is_player) {
				int cx = (int)(tx + tile_sz / 2);
				int cy = (int)(ty + tile_sz / 2);

				/* Breathing radius based on frame counter (e.g. 0..60 cycle) */
				int pulse = (int)(frame % 60);
				if (pulse > 30) pulse = 60 - pulse;
				int glow_radius = (int)(tile_sz / 2) + (pulse / 6);

				draw_radial_glow(fb, w, h, cx, cy, glow_radius, theme->player_glow);

				/* Inner diamond/circle player marker */
				fill_rounded_rect(fb, w, h, cx - 3, cy - 3, 7, 7, 3, theme->player_color);
				fill_rounded_rect(fb, w, h, cx - 1, cy - 1, 3, 3, 1, 0xFFFFFFFF);
			}
		}
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
