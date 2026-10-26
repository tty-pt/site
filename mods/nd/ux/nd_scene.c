#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <nd/ui-slot-types.h>
#include "nd_scene.h"

void nd_scene_init(nd_scene_t *scene)
{
	if (!scene)
		return;
	memset(scene, 0, sizeof(*scene));
	scene->room.width = 16;
	scene->room.height = 12;
	scene->room.exits = 0x0F; /* N, S, E, W */
	strncpy(scene->room.name, "The Common Hall", sizeof(scene->room.name) - 1);

	/* Generate default perimeter and floor */
	for (uint16_t r = 0; r < scene->room.height; r++) {
		for (uint16_t c = 0; c < scene->room.width; c++) {
			uint16_t idx = r * scene->room.width + c;
			if (r == 0 || r == scene->room.height - 1 || c == 0 || c == scene->room.width - 1) {
				if ((r == 0 && (c == 7 || c == 8)) ||
				    (r == scene->room.height - 1 && (c == 7 || c == 8)) ||
				    (c == 0 && (r == 5 || r == 6)) ||
				    (c == scene->room.width - 1 && (r == 5 || r == 6))) {
					scene->room.tiles[idx] = ND_SCENE_TILE_DOOR;
				} else {
					scene->room.tiles[idx] = ND_SCENE_TILE_WALL;
				}
			} else {
				scene->room.tiles[idx] = ND_SCENE_TILE_FLOOR;
			}
		}
	}

	scene->time_of_day = 0.5f; /* High noon */
	scene->camera.pitch = 45.0f;
	scene->camera.yaw = 0.0f;
	scene->camera.zoom = 10.0f;
}

void nd_scene_sync(nd_scene_t *scene, const nd_client_state_t *st)
{
	if (!scene || !st)
		return;

	/* 1. Synchronize Room Geometry */
	scene->room.ref = st->room.ref;
	scene->room.exits = st->room.exits;
	if (st->room.name[0] != '\0') {
		strncpy(scene->room.name, st->room.name, sizeof(scene->room.name) - 1);
	}

	uint16_t w = 16;
	uint16_t h = 12;
	scene->room.width = w;
	scene->room.height = h;

	for (uint16_t r = 0; r < h; r++) {
		for (uint16_t c = 0; c < w; c++) {
			uint16_t idx = r * w + c;
			if (r == 0 || r == h - 1 || c == 0 || c == w - 1) {
				/* Check exits bitmask: N=1, S=2, E=4, W=8 */
				if ((r == 0 && (c == 7 || c == 8) && (scene->room.exits & 1)) ||
				    (r == h - 1 && (c == 7 || c == 8) && (scene->room.exits & 2)) ||
				    (c == w - 1 && (r == 5 || r == 6) && (scene->room.exits & 4)) ||
				    (c == 0 && (r == 5 || r == 6) && (scene->room.exits & 8))) {
					scene->room.tiles[idx] = ND_SCENE_TILE_DOOR;
				} else {
					scene->room.tiles[idx] = ND_SCENE_TILE_WALL;
				}
			} else {
				scene->room.tiles[idx] = ND_SCENE_TILE_FLOOR;
			}
		}
	}

	/* 2. Synchronize Player & Targets */
	scene->player_ref = st->me;
	scene->target_ref = st->target_ref;

	/* 3. Synchronize Entities */
	size_t count = st->entity_count;
	if (count > ND_MAX_ENTITIES)
		count = ND_MAX_ENTITIES;
	scene->entity_count = count;

	float center_x = (float)w / 2.0f;
	float center_y = (float)h / 2.0f;

	for (size_t i = 0; i < count; i++) {
		nd_scene_entity_t *se = &scene->entities[i];
		const nd_entity_desc_t *de = &st->entities[i];

		se->ref = de->ref;
		se->attach_count = 0;
		strncpy(se->name, de->name, sizeof(se->name) - 1);
		se->ch = de->ch ? de->ch : '?';
		se->fg = de->fg ? de->fg : 0x07;
		se->is_player = (de->ref == st->me);
		se->is_target = (de->ref == st->target_ref);

		if (se->is_player) {
			se->pos.x = center_x;
			se->pos.y = center_y;
			se->pos.z = 0.0f;
			const nd_bar_state_t *hp = nd_client_gauge_get(st, "hp");
			se->hp_val = hp ? hp->val : 0;
			se->hp_max = hp ? hp->max : 0;
		} else {
			/* Distribute other entities across the floor tiles */
			float offset_x = (float)((int)(i % 5) - 2) * 2.0f;
			float offset_y = (float)((int)(i / 5) - 1) * 2.0f;
			se->pos.x = center_x + offset_x;
			se->pos.y = center_y + offset_y;
			se->pos.z = 0.0f;
			se->hp_val = 100;
			se->hp_max = 100;
		}
	}

	/* Target vitality comes from the generic target gauge when published. */
	{
		const nd_bar_state_t *tg = nd_client_gauge_get(st, ND_CLIENT_TARGET_GAUGE);
		nd_scene_entity_t *te = tg ? nd_scene_find(scene, st->target_ref) : NULL;
		if (te) {
			te->hp_val = tg->val;
			te->hp_max = tg->max;
		}
	}

	/* 3b. Let tenant modules attach equipment / effects to the scene */
	scene->fx_count = 0;
	nd_ui_glue_scene_sync(scene, st);

	/* 4. Time of Day */
	if (st->tod > 0) {
		scene->time_of_day = (float)(st->tod % 86400) / 86400.0f;
	} else {
		scene->time_of_day = 0.5f;
	}
}

nd_scene_entity_t *nd_scene_find(nd_scene_t *scene, uint32_t ref)
{
	if (!scene || !ref)
		return NULL;
	for (size_t i = 0; i < scene->entity_count; i++)
		if (scene->entities[i].ref == ref)
			return &scene->entities[i];
	return NULL;
}

void nd_scene_attach(nd_scene_entity_t *e, const char *socket, uint32_t ref)
{
	if (!e || !socket || e->attach_count >= ND_SCENE_MAX_ATTACH)
		return;
	snprintf(e->attach[e->attach_count].socket, sizeof(e->attach[0].socket), "%s", socket);
	e->attach[e->attach_count].ref = ref;
	e->attach_count++;
}

void nd_scene_fx_add(nd_scene_t *scene, const char *kind, uint32_t target_ref, uint32_t serial)
{
	if (!scene || !kind || scene->fx_count >= ND_SCENE_MAX_FX)
		return;
	nd_scene_fx_t *f = &scene->fx[scene->fx_count++];
	snprintf(f->kind, sizeof(f->kind), "%s", kind);
	f->target_ref = target_ref;
	f->serial = serial;
}
