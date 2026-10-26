#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ttypt/xy-mod.h>
#include <bud/bud.h>
#include <bud/bud_slot.h>
#include <bud/bud_state.h>
#include <nd/bcp-types.h>
#include <nd/bcp-meta.h>
#include <nd/mortal-bcp.h>
#include <nd/ui-slot-types.h>

#define ND_MORTAL_STATE_SCHEMA(F, st) \
	F(UINT, st, hp,     0, 100) \
	F(UINT, st, max_hp, 0, 100)

BUD_DEFINE_STATE(nd_mortal_wire_state_t, ND_MORTAL_STATE_SCHEMA)


XY_LISTENER_TAG(int, on_bcp_channel, mortal, uint8_t, opcode, const uint8_t *, payload, size_t, len, void *, state)
{
	if (opcode == ND_BCP_TARGET_HP)
		return nd_mortal_target_unpack((nd_client_state_t *)state, payload, len);
	if (opcode == ND_BCP_HP) {
		uint8_t frame[3 + 8];
		if (len > sizeof(frame) - 3)
			return -1;
		frame[0] = ND_BCP_MAGIC_0; frame[1] = ND_BCP_MAGIC_1; frame[2] = opcode;
		memcpy(frame + 3, payload, len);
		return nd_bcp_decode_gauge((nd_client_state_t *)state, ND_MORTAL_HP_GAUGE, frame, 3 + len);
	}
	int ret = 0;
	return xy_last(&ret) == XY_OK ? ret : 0;
}

static int
mortal_state_default(void *state)
{
	nd_client_state_t *st = (nd_client_state_t *)state;
	if (!nd_client_gauge_get(st, ND_MORTAL_HP_GAUGE))
		nd_client_gauge_set(st, ND_MORTAL_HP_GAUGE, 100, 100);
	return 0;
}

ND_UI_GLUE(mortal, .state_default = mortal_state_default);
