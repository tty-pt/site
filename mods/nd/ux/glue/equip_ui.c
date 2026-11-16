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
#include <nd/equip-bcp.h>
#include <nd/equip-ui.h>

#define ND_EQUIP_STATE_SCHEMA(F, st) \
	F(UINT, st, head, 0, 0) \
	F(UINT, st, neck, 0, 0) \
	F(UINT, st, body, 0, 0) \
	F(UINT, st, legs, 0, 0) \
	F(UINT, st, feet, 0, 0) \
	F(UINT, st, main, 0, 0) \
	F(UINT, st, off,  0, 0)

BUD_DEFINE_STATE(nd_equip_wire_state_t, ND_EQUIP_STATE_SCHEMA)


XY_LISTENER_TAG(int, on_bcp_channel, equip, uint8_t, opcode, const uint8_t *, payload, size_t, len, void *, state)
{
	if (opcode == ND_BCP_EQUIPMENT) {
		return nd_equip_bcp_unpack(opcode, payload, len, state);
	}
	int ret = 0;
	return xy_last(&ret) == XY_OK ? ret : 0;
}

XY_LISTENER_TAG(int, on_nd_ui_action, equip, const char *, action_id)
{
	if (action_id && strcmp(action_id, "equip") == 0) {
		nd_ui_toggle_modal("equip");
		return 1;
	}
	int ret = 0;
	return xy_last(&ret) == XY_OK ? ret : 0;
}

static const char *
equip_slot_hint(const char *key)
{
	if (!key) return "·";
	if (strcmp(key, "head") == 0)    return "🪖";
	if (strcmp(key, "neck") == 0)    return "📿";
	if (strcmp(key, "chest") == 0)   return "🦺";
	if (strcmp(key, "back") == 0)    return "🧥";
	if (strcmp(key, "pants") == 0)   return "👖";
	if (strcmp(key, "rhand") == 0)   return "⚔";
	if (strcmp(key, "lhand") == 0)   return "🛡";
	if (strcmp(key, "rfinger") == 0) return "💍";
	if (strcmp(key, "lfinger") == 0) return "💍";
	return "·";
}

static bud_node *
equip_make_socket(const nd_equip_slot_desc_t *d, uint32_t item_ref)
{
	bud_node *cell = bud_element("div");
	if (!d) {
		bud_set_attr(cell, "class", "equip-socket-spacer");
		return cell;
	}

	char class_buf[200];
	snprintf(class_buf, sizeof(class_buf),
		"btn equip-socket %s cursor-pointer select-none",
		(item_ref > 0) ? "equip-socket-filled" : "equip-socket-empty");
	bud_set_attr(cell, "class", class_buf);
	bud_set_attr(cell, "data-slot", d->key);
	bud_set_attr(cell, "title", d->label);

	char cmd_buf[32];
	snprintf(cmd_buf, sizeof(cmd_buf), "unequip %c", d->cmd);
	bud_set_attr(cell, "data-cmd", cmd_buf);

	if (item_ref > 0) {
		char ref_buf[16];
		snprintf(ref_buf, sizeof(ref_buf), "#%u", item_ref);
		bud_node *badge = bud_element("span");
		bud_set_attr(badge, "class", "equip-socket-ref pointer-events-none");
		bud_append(badge, bud_text(ref_buf));
		bud_append(cell, badge);
	} else {
		bud_node *hint = bud_element("span");
		bud_set_attr(hint, "class", "equip-socket-hint pointer-events-none");
		bud_append(hint, bud_text(equip_slot_hint(d->key)));
		bud_append(cell, hint);
	}
	return cell;
}

static bud_node *
equip_render_modal_enhanced(const void *st)
{
	static const uint32_t k_no_slots[ND_BP_MAX] = {0};
	const nd_equipment_state_t *eq = (const nd_equipment_state_t *)
		nd_client_ext_get((const nd_client_state_t *)st, ND_EQUIP_EXT_KEY);
	const uint32_t *slots = eq ? eq->slots : k_no_slots;
	const char *active = nd_ui_get_active_modal();
	int is_open = (active && strcmp(active, "equip") == 0);

	bud_node *overlay = bud_element("div");
	bud_set_attr(overlay, "id", "equipment-modal");
	bud_set_attr(overlay, "class", is_open
		? "modal fixed inset-0 z-50 flex items-center justify-center bg-black/60"
		: "hidden");
	bud_bind(overlay, "click", 1, on_equip_modal_close);

	bud_node *doll = bud_element("div");
	bud_set_attr(doll, "id", "equipment");
	bud_set_attr(doll, "class", "rpg-modal-card equip-card flex flex-col gap-3 p-4 border border-[#9589c5] bg-[#2c2c2c]/90 shadow-2xl select-none");
	bud_bind(doll, "click", 1, on_equip_card_click);

	bud_node *hdr = bud_element("div");
	bud_set_attr(hdr, "class", "rpg-modal-header flex items-center justify-between gap-2 pb-2 border-b border-[#9589c5]/50");
	bud_node *title = bud_element("span");
	bud_set_attr(title, "class", "text-xs font-bold text-neutral-100 tracking-wider uppercase");
	bud_append(title, bud_text("👕 Equipment Sockets"));
	bud_append(hdr, title);
	bud_append(doll, hdr);

	bud_node *grid = bud_element("div");
	bud_set_attr(grid, "class", "equip-doll-grid flex flex-col gap-2.5 items-center py-1");

	unsigned rows, cols;
	nd_equip_grid_dims(&rows, &cols);
	for (unsigned r = 0; r < rows; r++) {
		bud_node *row = bud_element("div");
		bud_set_attr(row, "class", "equip-doll-row flex gap-2.5 items-center justify-center");
		for (unsigned c = 0; c < cols; c++) {
			const nd_equip_slot_desc_t *d = NULL;
			for (size_t i = 0; i < ND_EQUIP_SLOT_COUNT; i++)
				if (nd_equip_slots[i].row == r && nd_equip_slots[i].col == c)
					d = &nd_equip_slots[i];
			bud_append(row, equip_make_socket(d, d ? slots[d->slot_id] : 0));
		}
		bud_append(grid, row);
	}

	bud_append(doll, grid);
	bud_append(overlay, doll);
	return overlay;
}

static int
equip_contribute(int slot, void *parent, const void *state)
{
	if (slot == ND_UI_SLOT_ACTIONS)
		bud_append((bud_node *)parent, nd_ui_dock_button("👕", "Equipment", "equip"));
	else if (slot == ND_UI_SLOT_MODALS)
		bud_append((bud_node *)parent, equip_render_modal_enhanced(state));
	return 0;
}

static void
equip_scene_sync(void *scene, const void *state)
{
	nd_scene_t *sc = (nd_scene_t *)scene;
	const nd_client_state_t *st = (const nd_client_state_t *)state;
	const nd_equipment_state_t *eq = (const nd_equipment_state_t *)
		nd_client_ext_get(st, ND_EQUIP_EXT_KEY);
	nd_scene_entity_t *me = eq ? nd_scene_find(sc, st->me) : NULL;
	if (!me)
		return;
	for (size_t i = 0; i < ND_EQUIP_SLOT_COUNT; i++) {
		uint32_t ref = eq->slots[nd_equip_slots[i].slot_id];
		if (ref)
			nd_scene_attach(me, nd_equip_slots[i].key, ref);
	}
}

ND_UI_GLUE(equip, .contribute = equip_contribute, .scene_sync = equip_scene_sync);

static int equip_fill_action(int slot_id, bud_node *parent, const void *state)
{
	(void)state;
	if (slot_id == BUD_SLOT_ACTIONS_DOCK) {
		bud_append(parent, nd_ui_dock_button("👕", "Equipment", "equip"));
		return 0;
	}
	return -1;
}

static int equip_fill_modal(int slot_id, bud_node *parent, const void *state)
{
	if (slot_id == BUD_SLOT_MODALS) {
		bud_append(parent, equip_render_modal_enhanced(state));
		return 0;
	}
	return -1;
}

BUD_SLOT_CONTRIBUTE(equip_actions, .slot_id = BUD_SLOT_ACTIONS_DOCK, .name = "equip", .fill = equip_fill_action);
BUD_SLOT_CONTRIBUTE(equip_modals, .slot_id = BUD_SLOT_MODALS, .name = "equip", .fill = equip_fill_modal);

