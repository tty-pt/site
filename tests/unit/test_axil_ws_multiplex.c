#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include "ttypt/axil-ws.h"
#include "nd/bcp.h"
#include "nd/bcp-meta.h"

/* Mock sinks to verify channel routing */
static char g_term_sink[1024] = {0};
static size_t g_term_sink_len = 0;

static nd_client_state_t g_mock_bcp_state;
static int g_bcp_dispatches = 0;

static char g_rpc_sink[512] = {0};
static int g_rpc_dispatches = 0;

/* Frame encoder mimicking axil-bridge.js multiplexed channel transport */
static size_t encode_channel_frame(uint8_t channel, const uint8_t *payload, size_t len, uint8_t *out, size_t cap)
{
	if (!payload || len == 0 || !out || cap < len + 2)
		return 0;

	if (channel == AXIL_WS_CH_TEXT) {
		/* Stream 0: Raw ANSI text stream without prefix */
		memcpy(out, payload, len);
		return len;
	} else if (channel == AXIL_WS_CH_BCP) {
		/* Stream 1: BCP protocol frame with magic '#b' */
		if (len >= 2 && payload[0] == ND_BCP_MAGIC_0 && payload[1] == ND_BCP_MAGIC_1) {
			memcpy(out, payload, len);
			return len;
		}
		out[0] = ND_BCP_MAGIC_0;
		out[1] = ND_BCP_MAGIC_1;
		memcpy(out + 2, payload, len);
		return len + 2;
	} else if (channel == AXIL_WS_CH_RPC) {
		/* Stream 2: Framed RPC message prefixed with channel id */
		out[0] = 0xAA; /* RPC Magic */
		out[1] = channel;
		memcpy(out + 2, payload, len);
		return len + 2;
	}
	return 0;
}

/* Dispatcher verifying simulated socket multiplexing */
static void mock_ws_receive_frame(const uint8_t *frame, size_t len)
{
	if (!frame || len == 0)
		return;

	/* Channel inspection matching axil-bridge.js */
	if (len >= 2 && frame[0] == ND_BCP_MAGIC_0 && frame[1] == ND_BCP_MAGIC_1) {
		/* Channel 1: BCP */
		int rc = nd_bcp_decode(frame, len, &g_mock_bcp_state);
		if (rc > 0) {
			g_bcp_dispatches++;
		}
	} else if (len >= 2 && frame[0] == 0xAA && frame[1] == AXIL_WS_CH_RPC) {
		/* Channel 2: RPC */
		size_t rpc_len = len - 2;
		if (rpc_len >= sizeof(g_rpc_sink)) rpc_len = sizeof(g_rpc_sink) - 1;
		memcpy(g_rpc_sink, frame + 2, rpc_len);
		g_rpc_sink[rpc_len] = '\0';
		g_rpc_dispatches++;
	} else {
		/* Channel 0: Text stream to terminal */
		size_t to_copy = (len < sizeof(g_term_sink) - g_term_sink_len - 1)
			? len : (sizeof(g_term_sink) - g_term_sink_len - 1);
		memcpy(g_term_sink + g_term_sink_len, frame, to_copy);
		g_term_sink_len += to_copy;
		g_term_sink[g_term_sink_len] = '\0';
	}
}

int main(void)
{
	printf("=== Testing axil-ws Channel Multiplexing & Framing ===\n");

	memset(&g_mock_bcp_state, 0, sizeof(g_mock_bcp_state));

	/* 1. Test Channel 0: Plain text ANSI TTY stream */
	const char text_payload[] = "\033[32mWelcome to NeverDark\033[0m\r\n";
	uint8_t text_frame[256];
	size_t text_frame_len = encode_channel_frame(AXIL_WS_CH_TEXT, (const uint8_t *)text_payload, strlen(text_payload), text_frame, sizeof(text_frame));
	assert(text_frame_len == strlen(text_payload));

	mock_ws_receive_frame(text_frame, text_frame_len);
	assert(g_term_sink_len == strlen(text_payload));
	assert(strcmp(g_term_sink, text_payload) == 0);
	assert(g_bcp_dispatches == 0);
	assert(g_rpc_dispatches == 0);
	printf("PASS: Channel 0 (Text) routed cleanly to terminal sink\n");

	/* 2. Test Channel 1: Binary BCP packet (#b) */
	uint8_t room_buf[128];
	nd_room_desc_t rm;
	memset(&rm, 0, sizeof(rm));
	rm.ref = 101;
	rm.exits = 5;
	strncpy(rm.name, "Hall", sizeof(rm.name) - 1);
	strncpy(rm.desc, "A hall", sizeof(rm.desc) - 1);
	int rlen = nd_bcp_pack_room_msg(room_buf, sizeof(room_buf), &rm);
	assert(rlen > 0);

	mock_ws_receive_frame(room_buf, (size_t)rlen);
	assert(g_bcp_dispatches == 1);
	assert(g_mock_bcp_state.room.ref == 101);
	assert(strcmp(g_mock_bcp_state.room.name, "Hall") == 0);
	printf("PASS: Channel 1 (BCP) decoded and updated client state room\n");

	/* 3. Test Channel 2: RPC message */
	const char rpc_payload[] = "{\"action\":\"spell:cast\",\"spell_id\":7}";
	uint8_t rpc_frame[256];
	size_t rpc_frame_len = encode_channel_frame(AXIL_WS_CH_RPC, (const uint8_t *)rpc_payload, strlen(rpc_payload), rpc_frame, sizeof(rpc_frame));
	assert(rpc_frame_len == strlen(rpc_payload) + 2);

	mock_ws_receive_frame(rpc_frame, rpc_frame_len);
	assert(g_rpc_dispatches == 1);
	assert(strcmp(g_rpc_sink, rpc_payload) == 0);
	printf("PASS: Channel 2 (RPC) dispatched to RPC receiver\n");

	/* 4. Sequential multiplexing over single socket connection */
	for (int i = 0; i < 5; i++) {
		mock_ws_receive_frame(text_frame, text_frame_len);
		mock_ws_receive_frame(room_buf, (size_t)rlen);
		mock_ws_receive_frame(rpc_frame, rpc_frame_len);
	}
	assert(g_bcp_dispatches == 6);
	assert(g_rpc_dispatches == 6);
	printf("PASS: multiplexed stream interleaved without frame corruption\n");

	printf("ALL AXIL WS MULTIPLEX TESTS PASSED\n");
	return 0;
}
