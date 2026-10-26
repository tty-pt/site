#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include "nd/bcp-meta.h"
#include "nd/bcp.h"
#include "nd/equip-bcp.h"

int main(void)
{
	printf("=== Testing Declarative BCP Meta Framework ===\n");

	/* 1. Gauge pack & unpack */
	uint8_t gbuf[32];
	int glen = nd_bcp_pack_gauge(gbuf, sizeof(gbuf), 10 /* mock tenant bar channel */, 85, 120);
	assert(glen == sizeof(nd_bcp_gauge_wire_t));

	uint16_t cur = 0, max = 0;
	int grc = nd_bcp_unpack_gauge(gbuf, glen, &cur, &max);
	assert(grc == 1);
	assert(cur == 85);
	assert(max == 120);
	printf("PASS: gauge pack & unpack\n");

	/* 2. Malformed / truncated gauge */
	assert(nd_bcp_unpack_gauge(gbuf, glen - 1, &cur, &max) == -1);
	printf("PASS: truncated gauge bounds check\n");

	/* 3. Room message pack & unpack */
	nd_room_desc_t rm_in = {
		.ref = 1042,
		.name = "The Obsidian Spire",
		.desc = "A towering monolith overlooking the desolate plains.",
		.exits = 0x0A
	};
	uint8_t rbuf[1024];
	int rlen = nd_bcp_pack_room_msg(rbuf, sizeof(rbuf), &rm_in);
	assert(rlen > 0);

	nd_room_desc_t rm_out;
	memset(&rm_out, 0, sizeof(rm_out));
	int rrc = nd_bcp_unpack_room_msg(rbuf, rlen, &rm_out);
	assert(rrc == 1);
	assert(rm_out.ref == 1042);
	assert(strcmp(rm_out.name, "The Obsidian Spire") == 0);
	assert(strcmp(rm_out.desc, "A towering monolith overlooking the desolate plains.") == 0);
	assert(rm_out.exits == 0x0A);
	printf("PASS: room message pack & unpack\n");

	/* 4. Entity message pack & unpack */
	nd_entity_desc_t ent_in = {
		.ref = 501,
		.location = 1042,
		.dynflags = 1,
		.type = 2,
		.actions = 4,
		.name = "Ancient Golem",
		.unparse = "Ancient Golem (#501)",
		.fg = 0x03,
		.flags = 0,
		.ch = 'G',
		.art = "golem_stone"
	};
	uint8_t ebuf[1024];
	nd_bcp_writer_t ew;
	nd_bcp_writer_init(&ew, ebuf, sizeof(ebuf));
	nd_bcp_write_hdr(&ew, ND_BCP_ITEM);
	nd_bcp_write_u32(&ew, ent_in.ref);
	nd_bcp_write_u32(&ew, ent_in.location);
	nd_bcp_write_u8(&ew, ent_in.dynflags);
	nd_bcp_write_u32(&ew, ent_in.type);
	nd_bcp_write_u32(&ew, ent_in.actions);
	nd_bcp_write_str(&ew, ent_in.name);
	nd_bcp_write_str(&ew, ent_in.unparse);
	nd_bcp_write_u16(&ew, ent_in.fg);
	nd_bcp_write_u16(&ew, ent_in.flags);
	nd_bcp_write_u8(&ew, (uint8_t)ent_in.ch);
	nd_bcp_write_str(&ew, ent_in.art);
	assert(nd_bcp_writer_ok(&ew));
	size_t elen = nd_bcp_writer_len(&ew, ebuf);

	nd_entity_desc_t ent_out;
	memset(&ent_out, 0, sizeof(ent_out));
	int erc = nd_bcp_unpack_entity_msg(ebuf, elen, &ent_out);
	assert(erc == 1);
	assert(ent_out.ref == 501);
	assert(ent_out.location == 1042);
	assert(strcmp(ent_out.name, "Ancient Golem") == 0);
	assert(ent_out.ch == 'G');
	assert(strcmp(ent_out.art, "golem_stone") == 0);
	printf("PASS: entity message pack & unpack\n");

	/* 5. Full client state decode via nd_bcp_decode */
	nd_client_state_t st;
	memset(&st, 0, sizeof(st));

	assert(nd_bcp_decode_gauge(&st, "hp", gbuf, glen) == 1);
	assert(nd_client_gauge_get(&st, "hp") && nd_client_gauge_get(&st, "hp")->val == 85);
	assert(nd_client_gauge_get(&st, "hp")->max == 120);

	assert(nd_bcp_decode(rbuf, rlen, &st) == 1);
	assert(st.room.ref == 1042);
	assert(strcmp(st.room.name, "The Obsidian Spire") == 0);

	assert(nd_bcp_decode(ebuf, elen, &st) == 1);
	assert(st.entity_count == 1);
	assert(st.entities[0].ref == 501);
	assert(strcmp(st.entities[0].name, "Ancient Golem") == 0);

	printf("PASS: full nd_bcp_decode integration\n");

	/* 6. Tier 2 Macro-Generated Schema DSL tests */
	nd_bcp_msg_room_wire_t wire_in = {
		.ref = 9001,
		.exits = 0x03,
		.name = "Celestial Observatory",
		.desc = "Glass domes open to the swirling cosmos."
	};
	uint8_t wbuf[1024];
	int wlen = nd_bcp_pack_msg_room_wire(wbuf, sizeof(wbuf), &wire_in);
	assert(wlen > 0);

	nd_bcp_msg_room_wire_t wire_out;
	memset(&wire_out, 0, sizeof(wire_out));
	assert(nd_bcp_unpack_msg_room_wire(wbuf, wlen, &wire_out) == 1);
	assert(wire_out.ref == 9001);
	assert(wire_out.exits == 0x03);
	assert(strcmp(wire_out.name, "Celestial Observatory") == 0);
	assert(strcmp(wire_out.desc, "Glass domes open to the swirling cosmos.") == 0);
	printf("PASS: Tier 2 macro-generated schema pack & unpack (room_wire)\n");

	nd_bcp_msg_equip_slot_t eq_in = {
		.slot_id = 5, /* Right Hand */
		.item_ref = 888,
		.name = "Excalibur"
	};
	uint8_t eqbuf[128];
	int eqlen = nd_bcp_pack_msg_equip_slot(eqbuf, sizeof(eqbuf), &eq_in);
	assert(eqlen > 0);

	nd_bcp_msg_equip_slot_t eq_out;
	memset(&eq_out, 0, sizeof(eq_out));
	assert(nd_bcp_unpack_msg_equip_slot(eqbuf, eqlen, &eq_out) == 1);
	assert(eq_out.slot_id == 5);
	assert(eq_out.item_ref == 888);
	assert(strcmp(eq_out.name, "Excalibur") == 0);
	printf("PASS: Tier 2 macro-generated schema pack & unpack (equip_slot)\n");

	printf("ALL BCP META TESTS PASSED\n");
	return 0;
}
