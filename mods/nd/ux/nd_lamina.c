#include <string.h>
#include <stdint.h>
#include "nd_lamina.h"
#include "nd_scene.h"

/* Helper to blend / write 32-bit RGBA pixel (0xAABBGGRR in LE or standard RGBA) */
static inline void set_pixel(uint32_t *fb, uint32_t w, uint32_t h, uint32_t x, uint32_t y, uint32_t color)
{
	if (x < w && y < h)
		fb[y * w + x] = color;
}

static void fill_rect(uint32_t *fb, uint32_t w, uint32_t h, uint32_t rx, uint32_t ry, uint32_t rw, uint32_t rh, uint32_t color)
{
	for (uint32_t y = ry; y < ry + rh && y < h; y++) {
		for (uint32_t x = rx; x < rx + rw && x < w; x++) {
			fb[y * w + x] = color;
		}
	}
}

static void draw_ring(uint32_t *fb, uint32_t w, uint32_t h, uint32_t cx, uint32_t cy, uint32_t r, uint32_t color)
{
	for (int dy = -(int)r; dy <= (int)r; dy++) {
		for (int dx = -(int)r; dx <= (int)r; dx++) {
			int dist2 = dx * dx + dy * dy;
			if (dist2 >= (int)((r - 1) * (r - 1)) && dist2 <= (int)(r * r)) {
				uint32_t px = cx + dx;
				uint32_t py = cy + dy;
				set_pixel(fb, w, h, px, py, color);
			}
		}
	}
}

void nd_lamina_render_scene(uint32_t *fb, uint32_t w, uint32_t h, const nd_scene_t *scene)
{
	if (!fb || w == 0 || h == 0 || !scene)
		return;

	/* 1. Background clear - dark slate tinted by time of day */
	uint32_t bg_col = (scene->time_of_day < 0.25f || scene->time_of_day > 0.75f)
		? 0xFF08080C   /* Night */
		: 0xFF101018;  /* Day */
	fill_rect(fb, w, h, 0, 0, w, h, bg_col);

	/* 2. Room floor & walls from scene grid */
	uint32_t tile_size = 32;
	uint32_t grid_cols = scene->room.width ? scene->room.width : 16;
	uint32_t grid_rows = scene->room.height ? scene->room.height : 12;
	uint32_t origin_x = (w > grid_cols * tile_size) ? (w - grid_cols * tile_size) / 2 : 0;
	uint32_t origin_y = (h > grid_rows * tile_size) ? (h - grid_rows * tile_size) / 2 : 0;

	for (uint32_t r = 0; r < grid_rows; r++) {
		for (uint32_t c = 0; c < grid_cols; c++) {
			uint32_t tx = origin_x + c * tile_size;
			uint32_t ty = origin_y + r * tile_size;
			uint8_t tile = scene->room.tiles[r * grid_cols + c];

			if (tile == ND_SCENE_TILE_WALL) {
				fill_rect(fb, w, h, tx, ty, tile_size, tile_size, 0xFF2B2B38); /* Solid wall */
			} else if (tile == ND_SCENE_TILE_DOOR) {
				fill_rect(fb, w, h, tx, ty, tile_size, tile_size, 0xFF553322); /* Door portal */
			} else {
				/* Floor checkerboard */
				uint32_t floor_col = ((r + c) % 2 == 0) ? 0xFF1E1E26 : 0xFF24242E;
				fill_rect(fb, w, h, tx, ty, tile_size, tile_size, floor_col);
			}
		}
	}

	/* 3. Render all active entities from scene graph */
	for (size_t i = 0; i < scene->entity_count; i++) {
		const nd_scene_entity_t *se = &scene->entities[i];
		uint32_t ex = origin_x + (uint32_t)(se->pos.x * (float)tile_size);
		uint32_t ey = origin_y + (uint32_t)(se->pos.y * (float)tile_size);

		/* Entity color */
		uint32_t ent_col = 0xFFEEAA33; /* Default amber */
		if (se->is_player) {
			ent_col = 0xFF44AAFF; /* Player cyan/blue */
		} else if (se->is_target) {
			ent_col = 0xFF3344DD; /* Target crimson */
		}

		/* Target highlight ring */
		if (se->is_target) {
			draw_ring(fb, w, h, ex + 8, ey + 8, 14, 0xFFFFCC00);
		}

		/* Base avatar body box */
		fill_rect(fb, w, h, ex + 2, ey + 2, 12, 12, ent_col);

		/* 2D Equipment Layering: layer gear sprites/plates onto the avatar */
		for (size_t a = 0; a < se->attach_count; a++) {
			const char *sock = se->attach[a].socket;
			uint32_t ref = se->attach[a].ref;
			if (!ref)
				continue;

			if (!strcmp(sock, "head")) {
				/* 2D Helm / Visor crest */
				fill_rect(fb, w, h, ex + 1, ey, 14, 3, 0xFFCCCCCC);
				fill_rect(fb, w, h, ex + 5, ey - 2, 6, 2, 0xFF888888);
			} else if (!strcmp(sock, "chest") || !strcmp(sock, "body")) {
				/* 2D Armor chestplate */
				fill_rect(fb, w, h, ex + 4, ey + 5, 8, 7, 0xFFAAAAAA);
				fill_rect(fb, w, h, ex + 6, ey + 7, 4, 3, 0xFF666666);
			} else if (!strcmp(sock, "main_hand") || !strcmp(sock, "weapon")) {
				/* 2D Weapon blade and hilt */
				fill_rect(fb, w, h, ex + 13, ey + 6, 2, 4, 0xFF553311);
				fill_rect(fb, w, h, ex + 15, ey + 3, 2, 9, 0xFFEEEEEE);
			} else if (!strcmp(sock, "off_hand") || !strcmp(sock, "shield")) {
				/* 2D Shield */
				fill_rect(fb, w, h, ex - 2, ey + 5, 4, 8, 0xFF3B5998);
				fill_rect(fb, w, h, ex - 1, ey + 7, 2, 4, 0xFFDDAA33);
			} else if (!strcmp(sock, "legs") || !strcmp(sock, "feet")) {
				/* 2D Greaves / Boots */
				fill_rect(fb, w, h, ex + 3, ey + 13, 4, 3, 0xFF554433);
				fill_rect(fb, w, h, ex + 9, ey + 13, 4, 3, 0xFF554433);
			} else {
				/* Generic equipment socket pip */
				uint32_t pc = 0xFF000000u | ((ref * 2654435761u) & 0x00FFFFFFu) | 0x00404040u;
				fill_rect(fb, w, h, ex + 18, ey + (uint32_t)a * 4, 3, 3, pc);
			}
		}

		/* Health bar */
		if (se->hp_max > 0) {
			uint32_t bar_w = 24;
			uint32_t hp_w = (se->hp_val * bar_w) / se->hp_max;
			if (hp_w > bar_w) hp_w = bar_w;
			fill_rect(fb, w, h, ex - 4, ey - 6, bar_w, 3, 0xFF333333);
			fill_rect(fb, w, h, ex - 4, ey - 6, hp_w, 3, se->is_player ? 0xFF33CC44 : 0xFFEE3333);
		}
	}

	/* 4. Spell effects: rings around the target, shown for a short burst */
	static uint32_t seen_serial;
	static int frames_left;
	for (size_t i = 0; i < scene->fx_count; i++) {
		const nd_scene_fx_t *fx = &scene->fx[i];
		if (fx->serial != seen_serial) {
			seen_serial = fx->serial;
			frames_left = 45;
		}
		if (frames_left <= 0)
			continue;
		uint32_t col = !strcmp(fx->kind, "fire") ? 0xFF2244FF
			: !strcmp(fx->kind, "water") ? 0xFFFF8822
			: !strcmp(fx->kind, "air") ? 0xFFDDDDDD
			: !strcmp(fx->kind, "earth") ? 0xFF2299AA : 0xFFFFFFFF;
		for (size_t j = 0; j < scene->entity_count; j++) {
			const nd_scene_entity_t *se = &scene->entities[j];
			if (se->ref != fx->target_ref && !(se->is_player && !fx->target_ref))
				continue;
			uint32_t ex = origin_x + (uint32_t)(se->pos.x * (float)tile_size);
			uint32_t ey = origin_y + (uint32_t)(se->pos.y * (float)tile_size);
			draw_ring(fb, w, h, ex + 8, ey + 8, (uint32_t)(24 - frames_left / 3), col);
		}
	}
	if (frames_left > 0)
		frames_left--;
}

void nd_lamina_render_room(uint32_t *fb, uint32_t w, uint32_t h, const nd_client_state_t *st)
{
	nd_scene_t scene;
	nd_scene_init(&scene);
	if (st) {
		nd_scene_sync(&scene, st);
	}
	nd_lamina_render_scene(fb, w, h, &scene);
}
