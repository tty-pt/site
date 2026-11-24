#define _POSIX_C_SOURCE 200809L

#ifndef AXIL_WS_NATIVE
#define AXIL_WS_NATIVE 1
#endif

#include "axil_ws_native.h"
#include <ttypt/axil-ws.h>

#include <arpa/inet.h>
#include <assert.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

typedef struct {
	int      opened;
	int      closed;
	int      close_code;
	int      n_bcp;
	int      n_text;
	uint8_t  last_bcp[64];
	size_t   last_bcp_len;
	char     last_term[128];
} ws_test_state_t;

static void on_open(void *u)
{
	((ws_test_state_t *)u)->opened++;
}

static void on_close(void *u, int code)
{
	ws_test_state_t *st = (ws_test_state_t *)u;
	st->closed++;
	st->close_code = code;
}

static void on_channel(void *u, uint8_t ch, const uint8_t *data, size_t len, int is_text)
{
	(void)is_text;
	ws_test_state_t *st = (ws_test_state_t *)u;
	if (ch == AXIL_WS_CH_BCP) {
		st->n_bcp++;
		size_t n = len < sizeof(st->last_bcp) ? len : sizeof(st->last_bcp);
		memcpy(st->last_bcp, data, n);
		st->last_bcp_len = n;
	} else if (ch == AXIL_WS_CH_TEXT) {
		st->n_text++;
	}
}

static void on_term_write(void *u, const char *data, size_t len)
{
	ws_test_state_t *st = (ws_test_state_t *)u;
	size_t n = len < sizeof(st->last_term) - 1 ? len : sizeof(st->last_term) - 1;
	memcpy(st->last_term, data, n);
	st->last_term[n] = '\0';
}

static void server_send_frame(int cfd, uint8_t opcode, const void *data, size_t len)
{
	uint8_t hdr[4];
	size_t hlen = 0;
	hdr[hlen++] = (uint8_t)(0x80u | opcode);
	if (len <= 125) {
		hdr[hlen++] = (uint8_t)len;
	} else {
		hdr[hlen++] = 126;
		hdr[hlen++] = (uint8_t)((len >> 8) & 0xFFu);
		hdr[hlen++] = (uint8_t)(len & 0xFFu);
	}
	assert(send(cfd, hdr, hlen, 0) == (ssize_t)hlen);
	if (len > 0)
		assert(send(cfd, data, len, 0) == (ssize_t)len);
}

static void server_read_masked(int cfd, uint8_t *opcode_out, uint8_t *out, size_t *len_out)
{
	uint8_t hdr[6];
	size_t got = 0;
	while (got < 6) {
		ssize_t r = recv(cfd, hdr + got, 6 - got, 0);
		assert(r > 0);
		got += (size_t)r;
	}
	*opcode_out = hdr[0] & 0x0Fu;
	assert((hdr[1] & 0x80u) != 0); /* Client frames must be masked */
	size_t plen = hdr[1] & 0x7Fu;
	assert(plen < 126);
	uint8_t mask[4];
	memcpy(mask, hdr + 2, 4);

	got = 0;
	while (got < plen) {
		ssize_t r = recv(cfd, out + got, plen - got, 0);
		assert(r > 0);
		got += (size_t)r;
	}
	for (size_t i = 0; i < plen; i++)
		out[i] ^= mask[i & 3];
	out[plen] = '\0';
	*len_out = plen;
}

int main(void)
{
	printf("test_ws_native:\n");

	/* Bind ephemeral loopback server */
	int lfd = socket(AF_INET, SOCK_STREAM, 0);
	assert(lfd >= 0);
	int one = 1;
	setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port = 0;
	assert(bind(lfd, (struct sockaddr *)&addr, sizeof(addr)) == 0);
	assert(listen(lfd, 1) == 0);

	socklen_t alen = sizeof(addr);
	assert(getsockname(lfd, (struct sockaddr *)&addr, &alen) == 0);
	uint16_t port = ntohs(addr.sin_port);

	ws_test_state_t st;
	memset(&st, 0, sizeof(st));
	axil_ws_native_cbs_t cbs = {
		.on_open = on_open,
		.on_close = on_close,
		.on_channel = on_channel,
		.on_term_write = on_term_write,
		.user = &st
	};
	axil_ws_native_set_callbacks(&cbs);
	axil_ws_native_set_base("127.0.0.1", port);

	/* Initiate non-blocking connection with relative path "/nd" */
	ws_connect("/nd", 3);
	assert(ws_status() == AXIL_WS_CONNECTING);

	int cfd = accept(lfd, NULL, NULL);
	assert(cfd >= 0);

	/* Pump client to send HTTP Upgrade request */
	for (int i = 0; i < 10; i++) axil_ws_native_poll(5);

	char req[1024];
	ssize_t rn = recv(cfd, req, sizeof(req) - 1, 0);
	assert(rn > 0);
	req[rn] = '\0';
	assert(strstr(req, "GET /nd HTTP/1.1") != NULL);
	assert(strstr(req, "Upgrade: websocket") != NULL);

	/* Send 101 Switching Protocols + initial text + BCP binary frames */
	const char *resp =
		"HTTP/1.1 101 Switching Protocols\r\n"
		"Upgrade: websocket\r\n"
		"Connection: Upgrade\r\n"
		"Sec-WebSocket-Accept: dummy\r\n\r\n";
	assert(send(cfd, resp, strlen(resp), 0) == (ssize_t)strlen(resp));

	const char *greeting = "\033[32mWelcome!\033[0m\n";
	server_send_frame(cfd, 0x01, greeting, strlen(greeting));

	const uint8_t bcp_pkt[] = { 0x23, 0x62, 0x01, 0xAA, 0xBB };
	server_send_frame(cfd, 0x02, bcp_pkt, sizeof(bcp_pkt));

	/* Poll until both messages are dispatched */
	for (int i = 0; i < 20 && (st.n_bcp == 0 || st.n_text == 0); i++)
		axil_ws_native_poll(10);

	assert(st.opened == 1);
	assert(ws_status() == AXIL_WS_CONNECTED);
	assert(st.n_text == 1);
	assert(strcmp(st.last_term, greeting) == 0);
	assert(st.n_bcp == 1);
	assert(st.last_bcp_len == sizeof(bcp_pkt));
	assert(memcmp(st.last_bcp, bcp_pkt, sizeof(bcp_pkt)) == 0);

	/* Client -> Server masked text frame */
	ws_send("look\n", 5);
	uint8_t op = 0, sbuf[64];
	size_t slen = 0;
	server_read_masked(cfd, &op, sbuf, &slen);
	assert(op == 0x01);
	assert(slen == 5 && memcmp(sbuf, "look\n", 5) == 0);

	/* Server Ping -> Client auto-Pong */
	server_send_frame(cfd, 0x09, "hi", 2);
	for (int i = 0; i < 5; i++) axil_ws_native_poll(10);
	server_read_masked(cfd, &op, sbuf, &slen);
	assert(op == 0x0A);
	assert(slen == 2 && memcmp(sbuf, "hi", 2) == 0);

	/* Clean close */
	ws_close();
	assert(ws_status() == AXIL_WS_DISCONNECTED);
	assert(st.closed == 1 && st.close_code == 1000);

	close(cfd);
	close(lfd);
	printf("  test_handshake_channels_and_ping: PASS\n");
	printf("test_ws_native: ALL TESTS PASSED\n");
	return 0;
}
