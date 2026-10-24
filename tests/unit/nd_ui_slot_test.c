#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <ttypt/xy-mod.h>
#include <bud/bud.h>
#include <nd/ui-slot.h>

/* Verify all newly uniformized two-header pairs in caller context */
#include <nd/level-types.h>
#include <nd/level.h>

#include <nd/mortal-types.h>
#include <nd/mortal.h>

#include <nd/plant-types.h>
#include <nd/plant.h>

#include <nd/race-types.h>
#include <nd/race.h>

#include <nd/spell-types.h>
#include <nd/spell.h>

/* Verify canonical wrappers for legacy uapi headers */
#include <nd/object.h>
#include <nd/entity.h>
#include <nd/skel.h>
#include <nd/azoth.h>
#include <nd/st.h>
#include <nd/match.h>
#include <nd/type.h>
#include <nd/map.h>

#include <nd/spell-ui.h>
#include <nd/fight-ui.h>

#define CHECK(label, condition)                                                \
	do {                                                                   \
		if (condition)                                                 \
			printf("PASS: %s\n", label);                           \
		else {                                                         \
			printf("FAIL: %s (line %d)\n", label, __LINE__);        \
			failures++;                                            \
		}                                                              \
	} while (0)

static int failures = 0;

/* Active modal state mock for unit test */
static char g_test_modal[32] = "";

const char *nd_ui_get_active_modal(void) {
	return g_test_modal;
}

void nd_ui_set_active_modal(const char *modal_id) {
	if (!modal_id) g_test_modal[0] = '\0';
	else snprintf(g_test_modal, sizeof(g_test_modal), "%s", modal_id);
}

int nd_ui_send_cmd(const char *cmd) {
	(void)cmd;
	return 1;
}

int main(void)
{
	printf("=== Testing ND Uniform Headers and UI Extensions ===\n");

	/* 1. Verify two-header constants and types */
	{
		CHECK("ND_LEVEL_DEFAULT defined", ND_LEVEL_DEFAULT == 3);
		nd_level_t lvl = 5;
		CHECK("nd_level_t typedef works", lvl == 5);

		mortal_t mort;
		mort.hp = 100;
		mort.huth[HUTH_HUNGER] = 0;
		mort.huth[HUTH_THIRST] = 0;
		CHECK("mortal_t and enum huth work", mort.hp == 100);

		plant_tile_t pt;
		pt.n = 2;
		pt.max = 5;
		CHECK("plant_tile_t works", pt.max == 5);

		race_t r;
		strncpy(r.name, "Elf", sizeof(r.name));
		r.wt = 10;
		CHECK("race_t works", r.wt == 10);

		CHECK("legacy_spell_type enum values", SPELL_FIREBALL == 4);
	}

	/* 2. Verify Spellbook modal UI rendering */
	{
		nd_client_state_t st;
		memset(&st, 0, sizeof(st));

		/* When modal is inactive, it should render an empty hidden node */
		nd_ui_set_active_modal("");
		bud_node *hidden_modal = nd_spell_render_modal(&st);
		CHECK("Inactive spell modal is not NULL", hidden_modal != NULL);

		/* When modal is 'spell', it should render the spellbook overlay card */
		nd_ui_set_active_modal("spell");
		bud_node *active_modal = nd_spell_render_modal(&st);
		CHECK("Active spell modal is not NULL", active_modal != NULL);
	}

	/* 3. Verify Combat Target HUD UI overlay rendering */
	{
		nd_client_state_t st;
		memset(&st, 0, sizeof(st));

		/* No target -> hidden empty node */
		st.target_ref = 0;
		bud_node *no_target = nd_fight_render_target_hud(&st);
		CHECK("No target HUD node is not NULL", no_target != NULL);

		/* With active target -> floating combat target card */
		st.target_ref = 42;
		st.entity_count = 1;
		st.entities[0].ref = 42;
		snprintf(st.entities[0].name, sizeof(st.entities[0].name), "Goblin Raider");

		bud_node *target_hud = nd_fight_render_target_hud(&st);
		CHECK("Active target HUD node is not NULL", target_hud != NULL);
	}

	if (failures == 0)
		printf("ALL TESTS PASSED\n");
	else
		printf("TESTS COMPLETED WITH %d FAILURE(S)\n", failures);

	return failures ? 1 : 0;
}
