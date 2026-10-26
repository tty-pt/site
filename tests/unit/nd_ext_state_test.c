/* nd_ext_state_test.c — generic client-state extension slots plus the
 * spell and equipment BCP unpackers that live in their own modules. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <nd/bcp-types.h>
#include <nd/spell-bcp.h>
#include <nd/equip-bcp.h>
#include <nd/mortal-bcp.h>

static int failures;
#define CHECK(name, cond) do { \
	if (cond) printf("PASS: %s\n", name); \
	else { printf("FAIL: %s\n", name); failures++; } \
} while (0)

int main(void)
{
	static nd_client_state_t a, b, full;
	printf("=== Testing client-state extension slots & module BCP ===\n");

	/* Generic slots */
	CHECK("get on empty state is NULL", nd_client_ext_get(&a, "x") == NULL);
	void *p = nd_client_ext(&a, "x", 16);
	CHECK("claim returns slot", p != NULL);
	CHECK("claim is stable", nd_client_ext(&a, "x", 16) == p);
	CHECK("get finds claimed slot", nd_client_ext_get(&a, "x") == p);
	CHECK("oversized claim rejected", nd_client_ext(&a, "big", sizeof(a.ext[0].data) + 1) == NULL);
	CHECK("overlong key rejected", nd_client_ext(&a, "0123456789abcdefX", 8) == NULL);
	CHECK("NULL state rejected", nd_client_ext(NULL, "x", 8) == NULL);
	CHECK("slot table fills up", ({ int ok = 1; char k[8];
		for (int i = 0; i < ND_EXT_SLOTS * 2; i++) { snprintf(k, sizeof k, "k%d", i);
			void *q = nd_client_ext(&full, k, 8);
			if (i < ND_EXT_SLOTS && !q) ok = 0;
			if (i >= ND_EXT_SLOTS && q) ok = 0; }
		ok; }));
	CHECK("claim fails when slot table is full, unpack reports error", nd_equip_bcp_unpack(ND_BCP_EQUIPMENT, (const uint8_t *)"\1\0\0\0", 4, &full) == -1);

	/* Equipment: per-session isolation, no global */
	uint8_t eqf[3 + 4 * ND_BP_MAX];
	memset(eqf, 0, sizeof eqf);
	eqf[0] = '#'; eqf[1] = 'b'; eqf[2] = ND_BCP_EQUIPMENT;
	/* raw array form: slot[ND_BP_HEAD] = 42 (little-endian) */
	eqf[3] = 42;
	/* the declarative decoder claims '#b' frames of its own schema; feed the
	 * raw-array form without the magic prefix to hit the cursor fallback */
	int rc = nd_equip_bcp_unpack(ND_BCP_EQUIPMENT, eqf + 3, 4 * ND_BP_MAX, &a);
	const nd_equipment_state_t *ea = nd_client_ext_get(&a, ND_EQUIP_EXT_KEY);
	CHECK("equip unpack ok", rc == 1);
	CHECK("equip stored in session A", ea && ea->slots[ND_BP_HEAD] == 42);
	CHECK("session B untouched", nd_client_ext_get(&b, ND_EQUIP_EXT_KEY) == NULL);

	/* Spell list */
	uint8_t sp[1 + sizeof(nd_bcp_spell_slot_wire_t) * 2];
	memset(sp, 0, sizeof sp);
	sp[0] = 2;
	nd_bcp_spell_slot_wire_t w[2];
	memset(w, 0, sizeof w);
	w[0].slot_id = 0; w[0].spell_skel = 7; w[0].cost = 3; strcpy(w[0].name, "Heal");
	w[1].slot_id = 1; w[1].spell_skel = 9; w[1].cost = 15;
	memset(w[1].name, 'Z', sizeof w[1].name); /* unterminated on the wire */
	memcpy(sp + 1, w, sizeof w);
	rc = nd_spell_bcp_unpack(ND_BCP_SPELL_LIST, sp, sizeof sp, &b);
	const nd_spellbook_state_t *bk = nd_client_ext_get(&b, ND_SPELL_EXT_KEY);
	CHECK("spell unpack ok", rc == 1 && bk && bk->count == 2);
	CHECK("spell fields", bk && bk->slots[0].cost == 3 && !strcmp(bk->slots[0].name, "Heal"));
	CHECK("spell name bounded + terminated", bk && bk->slots[1].name[sizeof bk->slots[1].name - 1] == '\0');
	CHECK("spell state isolated from A", nd_client_ext_get(&a, ND_SPELL_EXT_KEY) == NULL);
	CHECK("truncated spell frame rejected",
		nd_spell_bcp_unpack(ND_BCP_SPELL_LIST, sp, 1 + sizeof(nd_bcp_spell_slot_wire_t), &b) == -1);
	CHECK("empty frame rejected", nd_spell_bcp_unpack(ND_BCP_SPELL_LIST, sp, 0, &b) == -1);

	/* --- Malformed / hostile SPELL_LIST packets (Phase 5) --- */
	static nd_client_state_t m;
	uint8_t big[1 + sizeof(nd_bcp_spell_slot_wire_t) * ND_MAX_SPELLS];
	memset(big, 0, sizeof big);
	big[0] = 200; /* count far above the 8-slot cap */
	CHECK("oversized count with short body rejected",
		nd_spell_bcp_unpack(ND_BCP_SPELL_LIST, big, 1 + sizeof(nd_bcp_spell_slot_wire_t) * 3, &m) == -1);
	CHECK("oversized count clamps to 8 with full body",
		nd_spell_bcp_unpack(ND_BCP_SPELL_LIST, big, sizeof big, &m) == 1 &&
		((const nd_spellbook_state_t *)nd_client_ext_get(&m, ND_SPELL_EXT_KEY))->count == ND_MAX_SPELLS);
	CHECK("NULL payload rejected", nd_spell_bcp_unpack(ND_BCP_SPELL_LIST, NULL, 8, &m) == -1);
	CHECK("NULL state rejected", nd_spell_bcp_unpack(ND_BCP_SPELL_LIST, sp, sizeof sp, NULL) == -1);
	CHECK("unknown opcode rejected", nd_spell_bcp_unpack(99, sp, sizeof sp, &m) == -1);

	uint8_t oor[1 + sizeof(nd_bcp_spell_slot_wire_t)];
	nd_bcp_spell_slot_wire_t ow;
	memset(oor, 0, sizeof oor); memset(&ow, 0, sizeof ow);
	oor[0] = 1; ow.slot_id = 250; ow.cost = 5; strcpy(ow.name, "Odd");
	memcpy(oor + 1, &ow, sizeof ow);
	static nd_client_state_t o;
	CHECK("out-of-range slot id falls back to row index",
		nd_spell_bcp_unpack(ND_BCP_SPELL_LIST, oor, sizeof oor, &o) == 1 &&
		!strcmp(((const nd_spellbook_state_t *)nd_client_ext_get(&o, ND_SPELL_EXT_KEY))->slots[0].name, "Odd"));

	uint8_t framed[3 + sizeof oor];
	framed[0] = '#'; framed[1] = 'b'; framed[2] = ND_BCP_SPELL_LIST;
	memcpy(framed + 3, oor, sizeof oor);
	static nd_client_state_t fr;
	CHECK("full '#b' framed packet accepted",
		nd_spell_bcp_unpack(ND_BCP_SPELL_LIST, framed, sizeof framed, &fr) == 1);
	CHECK("header-only frame rejected",
		nd_spell_bcp_unpack(ND_BCP_SPELL_LIST, framed, 3, &fr) == -1);

	/* --- SPELL_CAST acknowledgement --- */
	uint8_t ack[6] = { 2 /* slot */, 2 /* fire */, 0x2a, 0, 0, 0 };
	static nd_client_state_t c1, c2;
	CHECK("cast ack ok", nd_spell_bcp_unpack(ND_BCP_SPELL_CAST, ack, sizeof ack, &c1) == 1);
	CHECK("cast ack truncated rejected", nd_spell_bcp_unpack(ND_BCP_SPELL_CAST, ack, 5, &c1) == -1);
	const nd_spellbook_state_t *k1 = nd_client_ext_get(&c1, ND_SPELL_EXT_KEY);
	CHECK("cast ack fields", k1 && k1->last_cast.slot == 2 && k1->last_cast.element == 2 &&
		k1->last_cast.target_ref == 42 && k1->last_cast.serial == 1);
	nd_spell_bcp_unpack(ND_BCP_SPELL_CAST, ack, sizeof ack, &c1);
	CHECK("cast ack serial increments", ((const nd_spellbook_state_t *)nd_client_ext_get(&c1, ND_SPELL_EXT_KEY))->last_cast.serial == 2);
	CHECK("cast ack isolated per session", nd_client_ext_get(&c2, ND_SPELL_EXT_KEY) == NULL);

	/* --- Equipment hardening + descriptor table --- */
	static nd_client_state_t e1, e2;
	uint8_t oor_slot[4 + 0];
	(void)oor_slot;
	nd_bcp_msg_equip_slot_t em = { .slot_id = ND_BP_MAX + 3, .item_ref = 5 };
	uint8_t ebuf[128];
	int el = nd_bcp_pack_msg_equip_slot(ebuf, sizeof ebuf, &em);
	CHECK("equip slot id out of range ignored (no crash)",
		el > 0 && nd_equip_bcp_unpack(ND_BCP_EQUIPMENT, ebuf, (size_t)el, &e1) != 1);
	em.slot_id = ND_BP_NECK;
	el = nd_bcp_pack_msg_equip_slot(ebuf, sizeof ebuf, &em);
	CHECK("equip slot message stored", nd_equip_bcp_unpack(ND_BCP_EQUIPMENT, ebuf, (size_t)el, &e1) == 1);
	em.slot_id = ND_BP_NECK; em.item_ref = 77;
	el = nd_bcp_pack_msg_equip_slot(ebuf, sizeof ebuf, &em);
	nd_equip_bcp_unpack(ND_BCP_EQUIPMENT, ebuf, (size_t)el, &e2);
	CHECK("two sessions hold different equipment",
		((const nd_equipment_state_t *)nd_client_ext_get(&e1, ND_EQUIP_EXT_KEY))->slots[ND_BP_NECK] == 5 &&
		((const nd_equipment_state_t *)nd_client_ext_get(&e2, ND_EQUIP_EXT_KEY))->slots[ND_BP_NECK] == 77);

	int seen[ND_BP_MAX] = {0}, dup_cell = 0;
	for (size_t i = 0; i < ND_EQUIP_SLOT_COUNT; i++) {
		seen[nd_equip_slots[i].slot_id]++;
		for (size_t j = i + 1; j < ND_EQUIP_SLOT_COUNT; j++)
			if (nd_equip_slots[i].row == nd_equip_slots[j].row && nd_equip_slots[i].col == nd_equip_slots[j].col)
				dup_cell = 1;
	}
	int all_once = 1;
	for (int i = 0; i < ND_BP_MAX; i++) if (seen[i] != 1) all_once = 0;
	CHECK("every body part has exactly one slot descriptor", all_once);
	CHECK("no two slots share a layout cell", !dup_cell);
	CHECK("slot lookup by id", nd_equip_slot_by_id(ND_BP_HEAD) && !strcmp(nd_equip_slot_by_id(ND_BP_HEAD)->key, "head"));
	CHECK("slot lookup rejects unknown", nd_equip_slot_by_id(ND_BP_MAX) == NULL);
	char txt[512];
	nd_equipment_state_t demo = {{0}};
	demo.slots[ND_BP_RHAND] = 9;
	size_t tl = nd_equip_format_text(&demo, txt, sizeof txt);
	CHECK("text listing names every slot", tl > 0 && strstr(txt, "Right Hand") && strstr(txt, "Pants") && strstr(txt, "#9"));
	char tiny[16];
	CHECK("text listing never overruns a small buffer", nd_equip_format_text(&demo, tiny, sizeof tiny) < sizeof tiny);

	/* --- Target vitality channel --- */
	static nd_client_state_t t1, t2;
	uint8_t tv[8] = { 0x0b, 0, 0, 0, 30, 0, 100, 0 }; /* ref 11, 30/100 */
	CHECK("target frame ok", nd_mortal_target_unpack(&t1, tv, sizeof tv) == 1);
	const nd_bar_state_t *tg = nd_client_gauge_get(&t1, ND_CLIENT_TARGET_GAUGE);
	CHECK("target gauge + ref set", tg && tg->val == 30 && tg->max == 100 && t1.target_ref == 11);
	CHECK("target frame short rejected", nd_mortal_target_unpack(&t1, tv, 7) == -1);
	uint8_t clear[8] = {0};
	nd_mortal_target_unpack(&t1, clear, sizeof clear);
	CHECK("target ref 0 clears target + gauge", t1.target_ref == 0 &&
		nd_client_gauge_get(&t1, ND_CLIENT_TARGET_GAUGE)->val == 0);
	CHECK("target isolated per session", t2.target_ref == 0 && nd_client_gauge_get(&t2, ND_CLIENT_TARGET_GAUGE) == NULL);

	printf(failures ? "FAILED: %d\n" : "ALL ND EXT STATE TESTS PASSED\n", failures);
	return failures ? 1 : 0;
}
