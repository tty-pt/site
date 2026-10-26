#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ttypt/xy-mod.h>
#include <bud/bud.h>
#include <bud/bud_app.h>
#include <bud/bud_slot.h>
#include <bud/bud_state.h>
#include <nd/ui-slot-types.h>
#include <nd/bcp-types.h>
#include <nd/fight-ui.h>

#define ND_FIGHT_STATE_SCHEMA(F, st) \
	F(UINT, st, target_ref, 0, 0) \
	F(UINT, st, target_hp,  0, 0) \
	F(UINT, st, target_max, 0, 0)

BUD_DEFINE_STATE(nd_fight_wire_state_t, ND_FIGHT_STATE_SCHEMA)


static bud_node *
fight_render_slot(int slot, const void *state)
{
	if (slot == ND_UI_SLOT_HUD_OVERLAYS) {
		return nd_fight_render_target_hud(state);
	}
	return NULL;
}

ND_UI_GLUE(fight, .render_slot = fight_render_slot);

BUD_SLOT_CONTRIBUTE(fight_hud, .slot_id = BUD_SLOT_SIDE_PANEL, .name = "fight_hud", .render = fight_render_slot);

