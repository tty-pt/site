#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ttypt/xy-mod.h>
#include <bud/bud.h>
#include <bud/bud_app.h>
#include <bud/bud_slot.h>
#include <bud/bud_state.h>
#include <nd/ui-slot-types.h>
#include <nd/scene.h>
#include <nd/bcp-types.h>
#include <nd/bcp-meta.h>
#include <nd/spell-bcp.h>
#include <nd/spell-ui.h>

#define ND_SPELL_STATE_SCHEMA(F, st) \
	F(UINT, st, mp,     0, 50) \
	F(UINT, st, max_mp, 0, 50)

BUD_DEFINE_STATE(nd_spell_wire_state_t, ND_SPELL_STATE_SCHEMA)


XY_LISTENER_TAG(int, on_bcp_channel, spell, uint8_t, opcode, const uint8_t *, payload, size_t, len, void *, state)
{
	if (opcode == ND_BCP_SPELL_LIST || opcode == ND_BCP_SPELL_CAST) {
		return nd_spell_bcp_unpack(opcode, payload, len, state);
	}
	if (opcode == ND_BCP_MP) {
		/* nd_bcp_decode_gauge wants the full frame; rebuild the 3-byte header. */
		uint8_t frame[3 + 8];
		if (len > sizeof(frame) - 3)
			return -1;
		frame[0] = ND_BCP_MAGIC_0; frame[1] = ND_BCP_MAGIC_1; frame[2] = opcode;
		memcpy(frame + 3, payload, len);
		return nd_bcp_decode_gauge((nd_client_state_t *)state, ND_SPELL_MP_GAUGE, frame, 3 + len);
	}
	int ret = 0;
	return xy_last(&ret) == XY_OK ? ret : 0;
}

XY_LISTENER_TAG(int, on_nd_ui_action, spell, const char *, action_id)
{
	if (action_id && strcmp(action_id, "spell") == 0) {
		nd_ui_toggle_modal("spell");
		return 1;
	}
	int ret = 0;
	return xy_last(&ret) == XY_OK ? ret : 0;
}

static int
spell_contribute(int slot, void *parent, const void *state)
{
	if (slot == ND_UI_SLOT_ACTIONS)
		bud_append((bud_node *)parent, nd_ui_dock_button("📜", "Spellbook", "spell"));
	else if (slot == ND_UI_SLOT_MODALS)
		bud_append((bud_node *)parent, nd_spell_render_modal(state));
	return 0;
}

static int
spell_state_default(void *state)
{
	nd_client_state_t *st = (nd_client_state_t *)state;
	if (!nd_client_gauge_get(st, ND_SPELL_MP_GAUGE))
		nd_client_gauge_set(st, ND_SPELL_MP_GAUGE, 50, 50);
	return 0;
}

static const char *
spell_element_kind(unsigned element)
{
	switch (element) {
	case 1:  return "spirit";
	case 2:  return "fire";
	case 4:  return "water";
	case 8:  return "air";
	case 16: return "earth";
	default: return "physical";
	}
}

static void
spell_scene_sync(void *scene, const void *state)
{
	const nd_spellbook_state_t *book = (const nd_spellbook_state_t *)
		nd_client_ext_get((const nd_client_state_t *)state, ND_SPELL_EXT_KEY);
	if (book && book->last_cast.serial)
		nd_scene_fx_add((nd_scene_t *)scene, spell_element_kind(book->last_cast.element),
				book->last_cast.target_ref, book->last_cast.serial);
}

ND_UI_GLUE(spell, .contribute = spell_contribute, .state_default = spell_state_default,
	.scene_sync = spell_scene_sync);

static int spell_fill_action(int slot_id, bud_node *parent, const void *state)
{
	(void)state;
	if (slot_id == BUD_SLOT_ACTIONS_DOCK) {
		bud_append(parent, nd_ui_dock_button("📜", "Spellbook", "spell"));
		return 0;
	}
	return -1;
}

static int spell_fill_modal(int slot_id, bud_node *parent, const void *state)
{
	if (slot_id == BUD_SLOT_MODALS) {
		bud_append(parent, nd_spell_render_modal(state));
		return 0;
	}
	return -1;
}

BUD_SLOT_CONTRIBUTE(spell_actions, .slot_id = BUD_SLOT_ACTIONS_DOCK, .name = "spell", .fill = spell_fill_action);
BUD_SLOT_CONTRIBUTE(spell_modals, .slot_id = BUD_SLOT_MODALS, .name = "spell", .fill = spell_fill_modal);

