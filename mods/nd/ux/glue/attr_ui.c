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
#include <nd/attr-ui.h>

#define ND_ATTR_SCHEMA(F, st) \
	F(UINT, st, str,   0, 10) \
	F(UINT, st, con,   0, 10) \
	F(UINT, st, dex,   0, 10) \
	F(UINT, st, int_,  0, 10) \
	F(UINT, st, wiz,   0, 10) \
	F(UINT, st, cha,   0, 10) \
	F(UINT, st, dodge, 0, 5)  \
	F(UINT, st, dmg,   0, 12) \
	F(UINT, st, mdmg,  0, 8)  \
	F(UINT, st, def,   0, 10) \
	F(UINT, st, mdef,  0, 6)

BUD_DEFINE_STATE(nd_attr_wire_state_t, ND_ATTR_SCHEMA)


XY_LISTENER_TAG(int, on_nd_ui_action, attr, const char *, action_id)
{
	if (action_id && strcmp(action_id, "stats") == 0) {
		nd_ui_toggle_modal("stats");
		return 1;
	}
	int ret = 0;
	return xy_last(&ret) == XY_OK ? ret : 0;
}

static void
attr_stat_row_enhanced(bud_node *col, const char *label, const char *value)
{
	bud_node *row = bud_element("div");
	bud_set_attr(row, "class", "rpg-stat-row flex items-center justify-between gap-3 text-xs");
	bud_node *l = bud_element("span");
	bud_set_attr(l, "class", "tbbold rpg-stat-label font-bold text-neutral-300 uppercase tracking-wider");
	bud_append(l, bud_text(label));
	bud_node *v = bud_element("span");
	bud_set_attr(v, "class", "rpg-stat-val font-mono font-bold text-neutral-100");
	bud_append(v, bud_text(value));
	bud_append(row, l);
	bud_append(row, v);
	bud_append(col, row);
}

static bud_node *
attr_render_stats_modal_enhanced(const void *state)
{
	const nd_client_state_t *st = (const nd_client_state_t *)state;
	const char *active = nd_ui_get_active_modal();
	int is_open = (active && strcmp(active, "stats") == 0);

	bud_node *overlay = bud_element("div");
	bud_set_attr(overlay, "id", "stats-modal");
	bud_set_attr(overlay, "class", is_open
		? "modal fixed inset-0 z-50 flex items-center justify-center bg-black/60"
		: "hidden");
	bud_bind(overlay, "click", 1, on_attr_modal_close);

	const nd_stats_state_t *ss = (const nd_stats_state_t *)
		nd_client_ext_get(st, ND_ATTR_EXT_KEY);

	static const struct { const char *label; int idx; } k_attrs[] = {
		{ "str", ND_ATTR_STR }, { "con", ND_ATTR_CON }, { "dex", ND_ATTR_DEX },
		{ "int", ND_ATTR_INT }, { "wiz", ND_ATTR_WIZ }, { "cha", ND_ATTR_CHA },
	};
	enum { EF_DODGE, EF_DMG, EF_MDMG, EF_DEF, EF_MDEF };
	static const struct { const char *label; int which; } k_effects[] = {
		{ "dodge", EF_DODGE }, { "dmg", EF_DMG }, { "mdmg", EF_MDMG },
		{ "def", EF_DEF }, { "mdef", EF_MDEF },
	};

	bud_node *card = bud_element("div");
	bud_set_attr(card, "id", "stats");
	bud_set_attr(card, "class", "rpg-modal-card stats-card flex flex-col gap-3 p-4 border border-[#9589c5] bg-[#2c2c2c]/90 shadow-2xl min-w-[300px] select-none");

	bud_node *hdr = bud_element("div");
	bud_set_attr(hdr, "class", "rpg-modal-header flex items-center justify-between gap-2 pb-2 border-b border-[#9589c5]/50");
	bud_node *title = bud_element("span");
	bud_set_attr(title, "class", "text-xs font-bold text-neutral-100 tracking-wider uppercase");
	bud_append(title, bud_text("📋 Character Sheet"));
	bud_append(hdr, title);
	bud_append(card, hdr);

	bud_node *body = bud_element("div");
	bud_set_attr(body, "class", "stats-cols-wrap flex gap-5 w-full");

	bud_node *cols[2];
	static const char *const col_titles[2] = { "Attributes", "Combat" };
	for (int c = 0; c < 2; c++) {
		cols[c] = bud_element("div");
		bud_set_attr(cols[c], "class", "rpg-stat-col flex flex-col gap-1.5 flex-1");
		bud_node *ct = bud_element("div");
		bud_set_attr(ct, "class", "rpg-stat-col-title text-[10px] font-bold text-[#c2b3ff] uppercase tracking-wider pb-1 border-b border-neutral-700/50");
		bud_append(ct, bud_text(col_titles[c]));
		bud_append(cols[c], ct);
		bud_append(body, cols[c]);
	}

	for (size_t i = 0; i < sizeof(k_attrs) / sizeof(k_attrs[0]); i++) {
		char v[16];
		snprintf(v, sizeof(v), "%u", ss ? (unsigned)ss->attr[k_attrs[i].idx] : 0u);
		attr_stat_row_enhanced(cols[0], k_attrs[i].label, v);
	}
	for (size_t i = 0; i < sizeof(k_effects) / sizeof(k_effects[0]); i++) {
		unsigned val = 0;
		if (ss) switch (k_effects[i].which) {
		case EF_DODGE: val = ss->dodge; break;
		case EF_DMG:   val = ss->dmg;   break;
		case EF_MDMG:  val = ss->mdmg;  break;
		case EF_DEF:   val = ss->def;   break;
		case EF_MDEF:  val = ss->mdef;  break;
		}
		char v[16];
		snprintf(v, sizeof(v), "%u", val);
		attr_stat_row_enhanced(cols[1], k_effects[i].label, v);
	}

	bud_append(card, body);
	bud_bind(card, "click", 1, on_attr_card_click);
	bud_append(overlay, card);
	return overlay;
}

static int
attr_contribute(int slot, void *parent, const void *state)
{
	if (slot == ND_UI_SLOT_ACTIONS)
		bud_append((bud_node *)parent, nd_ui_dock_button("📋", "Character Sheet", "stats"));
	else if (slot == ND_UI_SLOT_MODALS)
		bud_append((bud_node *)parent, attr_render_stats_modal_enhanced(state));
	return 0;
}

static bud_node *
attr_render_slot(int slot, const void *state)
{
	if (slot == ND_UI_SLOT_GAUGES) {
		return nd_attr_render_gauges(state);
	}
	return NULL;
}

XY_LISTENER_TAG(int, on_bcp_channel, attr, uint8_t, opcode, const uint8_t *, payload, size_t, len, void *, state)
{
	if (opcode == ND_BCP_STATS) {
		/* The channel hook receives the payload after the 3-byte header;
		 * nd_attr_bcp_unpack expects the full frame, so rebuild the view. */
		uint8_t frame[3 + 64];
		if (len > sizeof(frame) - 3)
			return -1;
		frame[0] = ND_BCP_MAGIC_0; frame[1] = ND_BCP_MAGIC_1; frame[2] = opcode;
		memcpy(frame + 3, payload, len);
		return nd_attr_bcp_unpack(frame, 3 + len, state);
	}
	int ret = 0;
	return xy_last(&ret) == XY_OK ? ret : 0;
}

static int
attr_state_default(void *state)
{
	nd_stats_state_t *s = nd_client_ext((nd_client_state_t *)state,
			ND_ATTR_EXT_KEY, sizeof(nd_stats_state_t));
	if (s) {
		for (int i = 0; i < ND_ATTR_MAX; i++)
			s->attr[i] = 10;
		s->dodge = 5; s->dmg = 12; s->mdmg = 8; s->def = 10; s->mdef = 6;
	}
	return 0;
}

static int
attr_state_serialize(const void *state, char *buf, size_t cap)
{
	const nd_stats_state_t *s = (const nd_stats_state_t *)
		nd_client_ext_get((const nd_client_state_t *)state, ND_ATTR_EXT_KEY);
	if (s) {
		nd_attr_wire_state_t w;
		w.str   = s->attr[ND_ATTR_STR];
		w.con   = s->attr[ND_ATTR_CON];
		w.dex   = s->attr[ND_ATTR_DEX];
		w.int_  = s->attr[ND_ATTR_INT];
		w.wiz   = s->attr[ND_ATTR_WIZ];
		w.cha   = s->attr[ND_ATTR_CHA];
		w.dodge = s->dodge;
		w.dmg   = s->dmg;
		w.mdmg  = s->mdmg;
		w.def   = s->def;
		w.mdef  = s->mdef;
		char wire_json[256];
		if (nd_attr_wire_state_t_serialize(&w, wire_json, sizeof(wire_json)) > 0) {
			size_t cur = strlen(buf);
			snprintf(buf + cur, cap - cur, "%s\"attr\":%s", cur ? "," : "", wire_json);
		}
	}
	return 0;
}

XY_LISTENER_TAG(int, on_nd_state_deserialize, attr, const char *, json, void *, state)
{
	const char *at = json ? strstr(json, "\"attr\":{") : NULL;
	nd_stats_state_t *s = at ? nd_client_ext((nd_client_state_t *)state,
			ND_ATTR_EXT_KEY, sizeof(nd_stats_state_t)) : NULL;
	if (s) {
		const char *o = at + 7;
		size_t n = strcspn(o, "}") + 1;
		nd_attr_wire_state_t w;
		memset(&w, 0, sizeof(w));
		if (nd_attr_wire_state_t_deserialize(&w, o, n) == 0) {
			s->attr[ND_ATTR_STR] = (uint16_t)w.str;
			s->attr[ND_ATTR_CON] = (uint16_t)w.con;
			s->attr[ND_ATTR_DEX] = (uint16_t)w.dex;
			s->attr[ND_ATTR_INT] = (uint16_t)w.int_;
			s->attr[ND_ATTR_WIZ] = (uint16_t)w.wiz;
			s->attr[ND_ATTR_CHA] = (uint16_t)w.cha;
			s->dodge = w.dodge;
			s->dmg   = w.dmg;
			s->mdmg  = w.mdmg;
			s->def   = w.def;
			s->mdef  = w.mdef;
		}
	}
	int ret = 0;
	return xy_last(&ret) == XY_OK ? ret : 0;
}

static int attr_fill_action(int slot_id, bud_node *parent, const void *state)
{
	(void)state;
	if (slot_id == BUD_SLOT_ACTIONS_DOCK) {
		bud_append(parent, nd_ui_dock_button("📋", "Character Sheet", "stats"));
		return 0;
	}
	return -1;
}

static int attr_fill_modal(int slot_id, bud_node *parent, const void *state)
{
	if (slot_id == BUD_SLOT_MODALS) {
		bud_append(parent, attr_render_stats_modal_enhanced(state));
		return 0;
	}
	return -1;
}

BUD_SLOT_CONTRIBUTE(attr_actions, .slot_id = BUD_SLOT_ACTIONS_DOCK, .name = "stats", .fill = attr_fill_action);
BUD_SLOT_CONTRIBUTE(attr_modals, .slot_id = BUD_SLOT_MODALS, .name = "stats", .fill = attr_fill_modal);
BUD_SLOT_CONTRIBUTE(attr_gauges, .slot_id = BUD_SLOT_STATUS_GAUGES, .name = "stats", .render = attr_render_slot);


ND_UI_GLUE(attr, .contribute = attr_contribute, .render_slot = attr_render_slot, .state_default = attr_state_default, .state_serialize = attr_state_serialize);
