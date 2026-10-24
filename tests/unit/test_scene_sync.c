#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include "nd_scene.h"
#include "nd_lamina.h"

/* Stand-in for the host glue registry: behaves like the equip + spell
 * modules would, so the scene extension points are exercised without them. */
static int g_glue_calls;
void nd_ui_glue_scene_sync(void *scene, const void *state)
{
	nd_scene_t *sc = scene;
	const nd_client_state_t *st = state;
	g_glue_calls++;
	nd_scene_entity_t *me = nd_scene_find(sc, st->me);
	if (me) {
		nd_scene_attach(me, "head", 7);
		nd_scene_attach(me, "rhand", 8);
	}
	nd_scene_fx_add(sc, "fire", st->target_ref, 3);
}

int main(void)
{
	printf("=== Testing ND Reactive Scene Graph Synchronizer ===\n");

	nd_scene_t scene;
	nd_scene_init(&scene);

	assert(scene.room.width == 16);
	assert(scene.room.height == 12);
	assert(scene.time_of_day == 0.5f);
	printf("PASS: nd_scene_init defaults verified\n");

	/* Test scene synchronization with client state */
	nd_client_state_t st;
	memset(&st, 0, sizeof(st));
	st.me = 42;
	nd_client_gauge_set(&st, "hp", 80, 100);
	st.target_ref = 101;
	st.tod = 43200; /* Noon (12:00) */
	st.room.ref = 777;
	st.room.exits = 0x05; /* North (1) + East (4) */
	strncpy(st.room.name, "Ancient Vault", sizeof(st.room.name) - 1);

	/* Add player entity */
	st.entities[0].ref = 42;
	strncpy(st.entities[0].name, "Hero", sizeof(st.entities[0].name) - 1);
	st.entities[0].ch = '@';
	st.entities[0].fg = 0x0B;

	/* Add mob target entity */
	st.entities[1].ref = 101;
	strncpy(st.entities[1].name, "Skeleton Guard", sizeof(st.entities[1].name) - 1);
	st.entities[1].ch = 'S';
	st.entities[1].fg = 0x0C;
	st.entity_count = 2;

	nd_scene_sync(&scene, &st);

	assert(scene.room.ref == 777);
	assert(strcmp(scene.room.name, "Ancient Vault") == 0);
	assert(scene.room.exits == 0x05);
	assert(scene.entity_count == 2);
	assert(scene.player_ref == 42);
	assert(scene.target_ref == 101);

	/* Check North door (r=0, c=7) is ND_SCENE_TILE_DOOR */
	assert(scene.room.tiles[0 * 16 + 7] == ND_SCENE_TILE_DOOR);
	/* Check South door is NOT set because exits does not have S (bit 2) */
	assert(scene.room.tiles[(12 - 1) * 16 + 7] == ND_SCENE_TILE_WALL);
	/* Check East door (c=15, r=5) is ND_SCENE_TILE_DOOR */
	assert(scene.room.tiles[5 * 16 + 15] == ND_SCENE_TILE_DOOR);

	/* Verify player entity */
	assert(scene.entities[0].ref == 42);
	assert(scene.entities[0].is_player == 1);
	assert(scene.entities[0].pos.x == 8.0f);
	assert(scene.entities[0].pos.y == 6.0f);
	assert(scene.entities[0].hp_val == 80);

	/* Verify target mob entity */
	assert(scene.entities[1].ref == 101);
	assert(scene.entities[1].is_player == 0);
	assert(scene.entities[1].is_target == 1);

	printf("PASS: nd_scene_sync layout, exits, player and target verified\n");

	/* Module scene contributions: attachments rebuilt per sync, fx recorded */
	assert(g_glue_calls == 1);
	assert(scene.entities[0].attach_count == 2);
	assert(!strcmp(scene.entities[0].attach[0].socket, "head") && scene.entities[0].attach[0].ref == 7);
	assert(scene.entities[1].attach_count == 0);
	assert(scene.fx_count == 1 && !strcmp(scene.fx[0].kind, "fire") && scene.fx[0].target_ref == 101);
	nd_scene_sync(&scene, &st);
	assert(scene.entities[0].attach_count == 2); /* not accumulated across syncs */
	assert(scene.fx_count == 1);
	printf("PASS: scene attachments + fx contributed by modules, no accumulation\n");

	/* Target vitality gauge drives the target entity's bar */
	nd_client_gauge_set(&st, ND_CLIENT_TARGET_GAUGE, 25, 50);
	nd_scene_sync(&scene, &st);
	assert(scene.entities[1].hp_val == 25 && scene.entities[1].hp_max == 50);
	printf("PASS: target gauge sets target entity vitality\n");

	/* Attach/fx helpers are bounded */
	for (int i = 0; i < ND_SCENE_MAX_ATTACH + 5; i++)
		nd_scene_attach(&scene.entities[1], "x", (uint32_t)i);
	assert(scene.entities[1].attach_count == ND_SCENE_MAX_ATTACH);
	for (int i = 0; i < ND_SCENE_MAX_FX + 5; i++)
		nd_scene_fx_add(&scene, "air", 1, (uint32_t)i);
	assert(scene.fx_count == ND_SCENE_MAX_FX);
	assert(nd_scene_find(&scene, 0) == NULL && nd_scene_find(&scene, 99999) == NULL);
	printf("PASS: scene helper bounds and lookups\n");
	nd_scene_sync(&scene, &st); /* restore a clean fx list */

	/* Test Lamina render with scene graph */
	uint32_t fb[640 * 480];
	memset(fb, 0, sizeof(fb));
	nd_lamina_render_scene(fb, 640, 480, &scene);

	/* Verify pixels were rendered (non-zero) */
	int nonzero = 0;
	for (int i = 0; i < 640 * 480; i++) {
		if (fb[i] != 0) nonzero++;
	}
	assert(nonzero > 1000);
	printf("PASS: nd_lamina_render_scene framebuffer output verified (%d non-zero pixels)\n", nonzero);

	printf("ALL SCENE SYNCHRONIZER TESTS PASSED\n");
	return 0;
}
