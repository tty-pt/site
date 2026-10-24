#include <stdint.h>
#include <stddef.h>

#include <ttypt/xy-mod.h>
#include <nd/bcp-types.h>
#include <nd/bcp-meta.h>

int g_custom_called = 0;

XY_LISTENER_TAG(int, on_bcp_channel, test_equip, uint8_t, opcode, const uint8_t *, payload, size_t, len, void *, state)
{
	(void)payload;
	(void)len;
	/* Bar channels owned by (mock) tenant modules: 10 -> "hp", 13 -> "mp".
	 * The hook gets the payload after the 3-byte header; rebuild the frame. */
	if ((opcode == 10 || opcode == 13) && len == 4) {
		uint8_t frame[7] = { ND_BCP_MAGIC_0, ND_BCP_MAGIC_1, opcode };
		for (size_t i = 0; i < 4; i++) frame[3 + i] = payload[i];
		return nd_bcp_decode_gauge((nd_client_state_t *)state, opcode == 10 ? "hp" : "mp", frame, sizeof(frame));
	}
	if (opcode == 12) {
		g_custom_called++;
		return 1;
	}
	int ret = 0;
	return xy_last(&ret) == XY_OK ? ret : 0;
}
