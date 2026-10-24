#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

#include "nd/bcp.h"
#include "nd/bcp-meta.h"
#include <bud/bud.h>
#include <nd/equip-bcp.h>
#include <nd/spell-bcp.h>
#include <nd/mortal-bcp.h>
#include "nd_scene.h"
#include "nd_ui.h"

int respond_html(int fd, const char *html)
{
	(void)fd;
	(void)html;
	return 0;
}

int main(void)
{
	printf("=== Testing Full-Fidelity SSR-to-WASM State Hydration ===\n");

	nd_client_state_t ssr_st;
	memset(&ssr_st, 0, sizeof(ssr_st));
	ssr_st.me = 999;
	ssr_st.auth_failed = 0;
	nd_client_gauge_set(&ssr_st, "hp", 88, 110);
	nd_client_gauge_set(&ssr_st, "mp", 42, 60);
	ssr_st.tod = 43200;
	strncpy(ssr_st.active_modal, "spell", sizeof(ssr_st.active_modal) - 1);

	ssr_st.room.ref = 1234;
	ssr_st.room.exits = 0x07; /* N, S, E */
	strncpy(ssr_st.room.name, "Grand Cathedral", sizeof(ssr_st.room.name) - 1);
	strncpy(ssr_st.room.desc, "Sunlight streams through the high stained-glass windows.", sizeof(ssr_st.room.desc) - 1);

	ssr_st.entities[0].ref = 999;
	strncpy(ssr_st.entities[0].name, "Paladin", sizeof(ssr_st.entities[0].name) - 1);
	ssr_st.entities[0].ch = '@';
	ssr_st.entities[0].fg = 0x0F;
	ssr_st.entities[0].location = 1234;

	ssr_st.entities[1].ref = 2002;
	strncpy(ssr_st.entities[1].name, "Cathedral Abbot", sizeof(ssr_st.entities[1].name) - 1);
	ssr_st.entities[1].ch = 'A';
	ssr_st.entities[1].fg = 0x0E;
	ssr_st.entities[1].location = 1234;
	ssr_st.entity_count = 2;

	/* 1. Execute true production nd_state_serialize() */
	char json_buf[4096] = {0};
	nd_state_serialize(&ssr_st, json_buf, sizeof(json_buf));

	assert(json_buf[0] == '{');
	assert(strstr(json_buf, "\"me\":999") != NULL);
	assert(strstr(json_buf, "\"Grand Cathedral\"") != NULL);
	assert(strstr(json_buf, "\"Cathedral Abbot\"") != NULL);
	assert(strstr(json_buf, "\"active_modal\":\"spell\"") != NULL);
	printf("PASS: production nd_state_serialize() formatted complete snapshot\n");

	/* 2. Execute true production wasm_init() */
	wasm_init(json_buf, (int)strlen(json_buf));

	/* 3. Verify rehydrated client_state via nd_ui_get_client_state() */
	const nd_client_state_t *rehydrated = nd_ui_get_client_state();
	assert(rehydrated != NULL);
	assert(rehydrated->me == 999);
	assert(nd_client_gauge_get(rehydrated, "hp") && nd_client_gauge_get(rehydrated, "hp")->val == 88);
	assert(nd_client_gauge_get(rehydrated, "hp")->max == 110);
	assert(nd_client_gauge_get(rehydrated, "mp") && nd_client_gauge_get(rehydrated, "mp")->val == 42);
	assert(nd_client_gauge_get(rehydrated, "mp")->max == 60);
	assert(rehydrated->tod == 43200);
	assert(strcmp(rehydrated->active_modal, "spell") == 0);

	/* Verify room rehydration */
	assert(rehydrated->room.ref == 1234);
	assert(rehydrated->room.exits == 0x07);
	assert(strcmp(rehydrated->room.name, "Grand Cathedral") == 0);
	assert(strcmp(rehydrated->room.desc, "Sunlight streams through the high stained-glass windows.") == 0);

	/* Verify entity table rehydration */
	assert(rehydrated->entity_count == 2);
	assert(rehydrated->entities[0].ref == 999);
	assert(strcmp(rehydrated->entities[0].name, "Paladin") == 0);
	assert(rehydrated->entities[0].ch == '@');
	assert(rehydrated->entities[1].ref == 2002);
	assert(strcmp(rehydrated->entities[1].name, "Cathedral Abbot") == 0);
	assert(rehydrated->entities[1].ch == 'A');
	printf("PASS: production wasm_init() rehydrated 2 entities and full room descriptor\n");

	/* 4. Verify reactive Scene Graph synchronization */
	nd_scene_t scene;
	nd_scene_init(&scene);
	nd_scene_sync(&scene, rehydrated);

	assert(scene.room.ref == 1234);
	assert(scene.room.exits == 0x07);
	assert(scene.entity_count == 2);
	assert(scene.player_ref == 999);
	assert(scene.entities[0].is_player == 1);
	assert(scene.entities[1].is_player == 0);
	printf("PASS: scene graph reconciled immediately from production rehydrated state\n");

	/* 5. Real module glue through the registry: presentation built from state */
	nd_client_state_t rich;
	memset(&rich, 0, sizeof(rich));
	rich.me = 999;
	rich.entity_count = 2;
	rich.entities[0] = ssr_st.entities[0];
	rich.entities[1] = ssr_st.entities[1];
	strncpy(rich.active_modal, "equip", sizeof(rich.active_modal) - 1);
	nd_ui_glue_state_default(&rich);
	assert(nd_client_gauge_get(&rich, "hp") && nd_client_gauge_get(&rich, "mp"));

	nd_equipment_state_t *eq = nd_client_ext(&rich, ND_EQUIP_EXT_KEY, sizeof(*eq));
	eq->slots[ND_BP_HEAD] = 11;
	eq->slots[ND_BP_RHAND] = 12;

	nd_spellbook_state_t *bk = nd_client_ext(&rich, ND_SPELL_EXT_KEY, sizeof(*bk));
	bk->count = 2;
	strcpy(bk->slots[0].name, "Fireball"); bk->slots[0].cost = 3; bk->slots[0].spell_skel = 5;
	strcpy(bk->slots[1].name, "Heal");     bk->slots[1].cost = 99; bk->slots[1].spell_skel = 1; bk->slots[1].id = 1;
	bk->last_cast.serial = 1; bk->last_cast.element = 2; bk->last_cast.target_ref = 2002;

	rich.target_ref = 2002;
	nd_client_gauge_set(&rich, ND_CLIENT_TARGET_GAUGE, 30, 100);

	bud_node *tree = nd_ui_render(&rich);
	char *html = bud_render_html(tree);
	assert(html);

	/* equipment: one cell per descriptor, grid dimensions from the table */
	int slots = 0;
	for (const char *q = html; (q = strstr(q, "data-slot=\"")); q++) slots++;
	assert(slots == (int)ND_EQUIP_SLOT_COUNT);
	assert(strstr(html, "data-slot=\"head\""));
	assert(strstr(html, "unequip c") && strstr(html, "unequip g"));
	assert(strstr(html, "#11") && strstr(html, "#12"));
	printf("PASS: equipment paper doll rendered from descriptor table (%d slots)\n", slots);

	/* fight HUD: width computed from the target gauge, never hard-coded */
	assert(strstr(html, "id=\"combat-target-hud\""));
	assert(strstr(html, "width: 30%;"));
	assert(strstr(html, "data-target-pct=\"30\""));
	assert(strstr(html, "data-billboard-target=\"2002\""));
	printf("PASS: fight HUD vitality bar width computed from target gauge (30%%)\n");

	/* grimoire rows come from the spellbook */
	assert(strstr(html, "Fireball") && strstr(html, "Heal"));
	assert(strstr(html, "data-cmd=\"c0\""));
	/* Heal costs 99 MP but the player has 50: row is disabled, no cast command */
	assert(!strstr(html, "data-cmd=\"c1\"") && strstr(html, "data-disabled=\"1\""));
	assert(strstr(html, "data-spell-slot=\"0\"") && strstr(html, "data-spell-slot=\"1\""));
	printf("PASS: grimoire rows rendered from spellbook state\n");
	bud_free_string(html);

	/* a zero-max target must not divide by zero; full target clamps to 100% */
	nd_client_gauge_set(&rich, ND_CLIENT_TARGET_GAUGE, 5, 0);
	html = bud_render_html(nd_ui_render(&rich));
	assert(html && strstr(html, "width: 100%;"));
	bud_free_string(html);
	nd_client_gauge_set(&rich, ND_CLIENT_TARGET_GAUGE, 500, 100);
	html = bud_render_html(nd_ui_render(&rich));
	assert(html && strstr(html, "width: 100%;"));
	bud_free_string(html);
	printf("PASS: fight HUD handles zero max and overfull gauge\n");

	/* scene: module glue attaches equipment and the cast effect */
	nd_scene_t scene2;
	nd_scene_init(&scene2);
	nd_scene_sync(&scene2, &rich);
	nd_scene_entity_t *me = nd_scene_find(&scene2, 999);
	assert(me && me->attach_count == 2);
	assert(scene2.fx_count == 1 && !strcmp(scene2.fx[0].kind, "fire") && scene2.fx[0].target_ref == 2002);
	printf("PASS: scene graph carries equipment sockets and spell fx from module glue\n");

	printf("ALL SSR HYDRATION TESTS PASSED\n");
	return 0;
}
