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

	ssr_st.map_data.width = 7;
	ssr_st.map_data.height = 7;
	ssr_st.map_data.player_x = 3;
	ssr_st.map_data.player_y = 3;
	ssr_st.map_data.room_ref = 1234;
	for (int i = 0; i < 49; i++) {
		ssr_st.map_data.tiles[i].bio_idx = 1;
		ssr_st.map_data.tiles[i].symbol = '.';
		ssr_st.map_data.tiles[i].flags = ND_MAP_FLAG_ROOM;
		ssr_st.map_data.tiles[i].exits = 0x0F; /* N, S, E, W */
		ssr_st.map_data.tiles[i].fg = 7;
	}
	ssr_st.map_data.tiles[24].symbol = '!';
	ssr_st.map_data.tiles[24].flags |= ND_MAP_FLAG_PLAYER;
	ssr_st.map_data.tiles[24].fg = 15;

	/* 1. Execute true production nd_state_serialize() */
	char json_buf[8192] = {0};
	nd_state_serialize(&ssr_st, json_buf, sizeof(json_buf));

	assert(json_buf[0] == '{');
	assert(strstr(json_buf, "\"me\":999") != NULL);
	assert(strstr(json_buf, "\"map_hex\":\"") != NULL);
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
	assert(rehydrated->map_data.width == 7);
	assert(rehydrated->map_data.height == 7);
	assert(rehydrated->map_data.room_ref == 1234);
	assert(rehydrated->map_data.tiles[24].symbol == '!');
	assert(rehydrated->map_data.tiles[24].flags & ND_MAP_FLAG_PLAYER);
	assert(memcmp(&ssr_st.map_data, &rehydrated->map_data, sizeof(nd_map_data_t)) == 0);
	printf("PASS: binary nd_map_data_t rehydrated byte-for-byte with zero loss\n");
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

	/* 6. Viewport Governance & Composable Layout Tests */
	nd_client_state_t gov;
	memset(&gov, 0, sizeof(gov));
	gov.me = 1;
	gov.pres_flags = ND_REGION_ALLOW_CHOICE;
	strncpy(gov.preferred_driver, "tty", sizeof(gov.preferred_driver) - 1);
	strncpy(gov.preferred_map_driver, "ascii-bud", sizeof(gov.preferred_map_driver) - 1);
	gov.layout_mode = ND_LAYOUT_STANDARD_HYBRID;

	char gov_json[8192] = {0};
	nd_state_serialize(&gov, gov_json, sizeof(gov_json));
	assert(strstr(gov_json, "\"preferred_driver\":\"tty\"") != NULL);
	assert(strstr(gov_json, "\"preferred_map_driver\":\"ascii-bud\"") != NULL);
	assert(strstr(gov_json, "\"pres_flags\":1") != NULL);
	printf("PASS: presentation governance metadata serialized to JSON\n");

	/* Rehydrate governance state */
	wasm_init(gov_json, (int)strlen(gov_json));
	const nd_client_state_t *gov_rehydrated = nd_ui_get_client_state();
	assert(gov_rehydrated != NULL);
	assert(gov_rehydrated->pres_flags & ND_REGION_ALLOW_CHOICE);
	assert(strcmp(gov_rehydrated->preferred_map_driver, "ascii-bud") == 0);
	assert(gov_rehydrated->layout_mode == ND_LAYOUT_STANDARD_HYBRID);
	printf("PASS: presentation governance state rehydrated from JSON\n");

	/* In an ascii-bud region, the viewport renders the pure ASCII map (no canvas) */
	html = bud_render_html(nd_ui_render(gov_rehydrated));
	assert(html && !strstr(html, "data-bud-action=\"cycle-viewport\""));
	assert(!strstr(html, "Switch Viewport"));
	assert(strstr(html, "<nd-map id=\"room-map\""));
	assert(!strstr(html, "<canvas id=\"room-map-canvas\""));
	assert(strstr(html, "id=\"right-panel\""));
	bud_free_string(html);
	printf("PASS: ascii-bud region renders terminal stage and pure ascii map (no canvas, no switch button)\n");

	/* In a lamina region, Switch Viewport button is NOT rendered and room-map-canvas IS rendered */
	gov.pres_flags = ND_REGION_LOCKED;
	strncpy(gov.preferred_driver, "tty", sizeof(gov.preferred_driver) - 1);
	strncpy(gov.preferred_map_driver, "lamina", sizeof(gov.preferred_map_driver) - 1);
	html = bud_render_html(nd_ui_render(&gov));
	assert(html && !strstr(html, "Switch Viewport"));
	assert(!strstr(html, "data-bud-action=\"cycle-viewport\""));
	assert(strstr(html, "<canvas id=\"room-map-canvas\""));
	bud_free_string(html);
	printf("PASS: lamina region strictly omits Switch Viewport button and renders room-map-canvas\n");

	/* Test map driver 'none' */
	strncpy(gov.preferred_map_driver, "none", sizeof(gov.preferred_map_driver) - 1);
	html = bud_render_html(nd_ui_render(&gov));
	assert(html && !strstr(html, "id=\"room-map\""));
	assert(!strstr(html, "id=\"room-map-canvas\""));
	bud_free_string(html);
	printf("PASS: Map driver 'none' completely omits map container\n");

	/* Test FULLSCREEN layout mode */
	gov.layout_mode = ND_LAYOUT_STAGE_FULLSCREEN;
	html = bud_render_html(nd_ui_render(&gov));
	assert(html && strstr(html, "fullscreen-layout"));
	assert(!strstr(html, "id=\"right-panel\""));
	bud_free_string(html);
	printf("PASS: FULLSCREEN layout mode occupies full canvas and omits right panel\n");

	/* 7. Test Pure-C Binary Map Rendering */
	/* Render the state that actually carries the binary grid. The shared
	 * client state was replaced by the governance renders above, and an empty
	 * state used to pass this check only because it drew a fake "+---#" box
	 * containing an '@'. */
	html = bud_render_html(nd_ui_render(&ssr_st));
	assert(html && strstr(html, "<nd-map id=\"room-map\""));
	assert(strstr(html, "!"));
	assert(strstr(html, "id=\"room-header\""));
	assert(strstr(html, "Grand Cathedral"));
	assert(strstr(html, "#1234"));
	bud_free_string(html);
	printf("PASS: pure-C render_room_map_ascii renders binary grid with player glyph and room header\n");

	/* A state with no map yet must not show a fabricated one. */
	nd_client_state_t blank;
	memset(&blank, 0, sizeof(blank));
	html = bud_render_html(nd_ui_render(&blank));
	assert(html && strstr(html, "<nd-map id=\"room-map\""));
	assert(!strstr(html, "+---#"));
	bud_free_string(html);
	printf("PASS: empty map state renders no placeholder grid\n");

	/* A real 7x7 view buffer is ~2.2 KB of ANSI. map_buf used to be 1 KB, which
	 * cut it to its first tile row: the browser drew a single coloured cell. */
	{
		static uint8_t frame[3 + 4096];
		static nd_client_state_t big;
		size_t n = 0;
		const char *tile = "\033[42m \033[0m\033[43m  \033[0m";
		frame[n++] = ND_BCP_MAGIC_0;
		frame[n++] = ND_BCP_MAGIC_1;
		frame[n++] = ND_BCP_VIEW_BUFFER;
		for (int row = 0; row < 13; row++) {
			for (int col = 0; col < 7; col++) {
				size_t l = strlen(tile);
				memcpy(frame + n, tile, l);
				n += l;
			}
			frame[n++] = '\n';
		}
		memcpy(frame + n, "\033[42mLASTROW\033[0m", 17);
		n += 17;
		assert(n - 3 > 1023);
		memset(&big, 0, sizeof(big));
		assert(nd_bcp_decode(frame, n, &big) == 1);
		assert(strstr(big.map_buf, "LASTROW") != NULL);
		html = bud_render_html(nd_ui_render(&big));
		assert(html && strstr(html, "LASTROW"));
		bud_free_string(html);
		printf("PASS: >1 KB view buffer survives BCP decode and renders to its last row\n");
	}

	uint32_t map_fb[160 * 160] = {0};
	nd_lamina_render_map(map_fb, 160, 160, &ssr_st.map_data);
	assert(map_fb[0] != 0);
	printf("PASS: nd_lamina_render_map renders 2D mini-map pixels\n");

	/* Test: Target card with avatar image and inventory contents rendering */
	{
		nd_client_state_t inv_st;
		memset(&inv_st, 0, sizeof(inv_st));
		inv_st.me = 100;
		inv_st.target_ref = 100;
		inv_st.room.ref = 2;
		snprintf(inv_st.room.name, sizeof(inv_st.room.name), "woodland");
		inv_st.entity_count = 3;

		/* Entity 0: Player with avatar art */
		inv_st.entities[0].ref = 100;
		inv_st.entities[0].location = 2;
		inv_st.entities[0].type = 3; /* TYPE_ENTITY */
		snprintf(inv_st.entities[0].name, sizeof(inv_st.entities[0].name), "quirinpa");
		snprintf(inv_st.entities[0].art, sizeof(inv_st.entities[0].art), "entity/avatar/42.jpeg");

		/* Entity 1: Tree in room */
		inv_st.entities[1].ref = 101;
		inv_st.entities[1].location = 2;
		inv_st.entities[1].type = 5;
		inv_st.entities[1].ch = 'T';
		inv_st.entities[1].fg = 2; /* GREEN */
		snprintf(inv_st.entities[1].name, sizeof(inv_st.entities[1].name), "pinus sylvestris");

		/* Entity 2: Stick in player inventory */
		inv_st.entities[2].ref = 102;
		inv_st.entities[2].location = 100; /* carried by player */
		inv_st.entities[2].type = 4;
		inv_st.entities[2].ch = '/';
		snprintf(inv_st.entities[2].name, sizeof(inv_st.entities[2].name), "stick");

		/* When target is me (inventory): target card shows avatar and contents shows stick */
		html = bud_render_html(nd_ui_render(&inv_st));
		assert(html && strstr(html, "id=\"target-card\""));
		assert(strstr(html, "/nd/art/entity/avatar/42.jpeg"));
		assert(strstr(html, "quirinpa"));
		assert(strstr(html, "class=\"target-art\""));
		assert(strstr(html, "class=\"target-title\""));
		assert(strstr(html, "(You - Inventory)")); /* kept as the card's title tooltip */
		assert(strstr(html, "stick"));
		assert(!strstr(html, "Room Contents") && !strstr(html, "Inventory</"));
		bud_free_string(html);

		/* When target is 0 (room): target card is empty, contents shows tree in room */
		inv_st.target_ref = 0;
		html = bud_render_html(nd_ui_render(&inv_st));
		assert(html && strstr(html, "pinus sylvestris"));
		assert(!strstr(html, "Room Contents")); /* the original had no list label */
		assert(strstr(html, "<nd-avatar"));       /* rows carry an avatar thumbnail */
		bud_free_string(html);

		/* When an item is selected: renders floating action popup with look button */
		inv_st.selected_item_ref = 101; /* tree */
		html = bud_render_html(nd_ui_render(&inv_st));
		assert(html && strstr(html, "nd-item-popup"));
		assert(strstr(html, "data-action=\"look\""));
		bud_free_string(html);
		inv_st.selected_item_ref = 0;

		/* Test BCP item decoding: room item (dynflags=1, type=0) clears target and sets room */
		uint8_t room_frame[131];
		memset(room_frame, 0, sizeof(room_frame));
		room_frame[0] = ND_BCP_MAGIC_0;
		room_frame[1] = ND_BCP_MAGIC_1;
		room_frame[2] = ND_BCP_ITEM;
		nd_entity_packet_t *rpkt = (nd_entity_packet_t *)(room_frame + 3);
		rpkt->ref = 55;
		rpkt->type = 0; /* TYPE_ROOM */
		rpkt->dynflags = 1;
		snprintf(rpkt->name, sizeof(rpkt->name), "The Whispering Woods");
		inv_st.target_ref = 100;
		assert(nd_bcp_decode(room_frame, sizeof(room_frame), &inv_st) == 1);
		assert(inv_st.target_ref == 0);
		assert(inv_st.room.ref == 55);
		assert(strcmp(inv_st.room.name, "The Whispering Woods") == 0);

		/* Test BCP item decoding: player entity (dynflags=1, type=3) sets target */
		rpkt->ref = 100;
		rpkt->type = 3; /* TYPE_ENTITY */
		rpkt->dynflags = 1;
		snprintf(rpkt->name, sizeof(rpkt->name), "quirinpa");
		assert(nd_bcp_decode(room_frame, sizeof(room_frame), &inv_st) == 1);
		assert(inv_st.target_ref == 100);

		printf("PASS: target card renders avatar, room contents renders trees and inventory, BCP item updates target and room\n");
	}

	printf("ALL SSR HYDRATION TESTS PASSED\n");
	return 0;
}
