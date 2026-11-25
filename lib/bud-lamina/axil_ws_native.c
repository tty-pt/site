#define _POSIX_C_SOURCE 200809L

#ifndef AXIL_WS_NATIVE
#define AXIL_WS_NATIVE 1
#endif

#include "axil_ws_native.h"
#include <ttypt/axil-ws.h>

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#ifdef AXIL_WS_TLS
#include <openssl/ssl.h>
#include <openssl/err.h>
#endif

typedef enum {
	WS_ST_IDLE = 0,
	WS_ST_TCP_CONNECTING,
	WS_ST_TLS_HANDSHAKE,
	WS_ST_HANDSHAKE,
	WS_ST_OPEN,
	WS_ST_CLOSING
} ws_state_t;

typedef struct {
	int        fd;
	ws_state_t state;
	int        use_tls;
#ifdef AXIL_WS_TLS
	SSL_CTX   *ssl_ctx;
	SSL       *ssl;
#endif

	char     base_host[256];
	uint16_t base_port;
	char     cookie[512];

	char     req_host[256];
	uint16_t req_port;
	char     req_path[512];

	uint8_t *tx_buf;
	size_t   tx_len;
	size_t   tx_cap;

	uint8_t *rx_buf;
	size_t   rx_len;
	size_t   rx_cap;

	/* Fragmentation reassembly */
	uint8_t *frag_buf;
	size_t   frag_len;
	size_t   frag_cap;
	uint8_t  frag_opcode;
	int      frag_active;

	axil_ws_native_cbs_t cbs;
	uint32_t rng_state;
} ws_client_t;

static ws_client_t g_ws = {
	.fd = -1,
	.state = WS_ST_IDLE,
	.base_host = "127.0.0.1",
	.base_port = 8080,
	.rng_state = 0x51A7F00Du
};

static uint32_t next_rand32(void)
{
	uint32_t x = g_ws.rng_state;
	if (x == 0) x = (uint32_t)time(NULL) ^ 0x9E3779B9u;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	g_ws.rng_state = x;
	return x;
}

static void base64_encode(const uint8_t *in, size_t len, char *out)
{
	static const char tbl[] =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	size_t o = 0;
	for (size_t i = 0; i < len; i += 3) {
		uint32_t v = ((uint32_t)in[i]) << 16;
		if (i + 1 < len) v |= ((uint32_t)in[i + 1]) << 8;
		if (i + 2 < len) v |= (uint32_t)in[i + 2];
		out[o++] = tbl[(v >> 18) & 0x3F];
		out[o++] = tbl[(v >> 12) & 0x3F];
		out[o++] = (i + 1 < len) ? tbl[(v >> 6) & 0x3F] : '=';
		out[o++] = (i + 2 < len) ? tbl[v & 0x3F] : '=';
	}
	out[o] = '\0';
}

static int buf_append(uint8_t **buf, size_t *len, size_t *cap,
                      const void *data, size_t n)
{
	if (n == 0) return 0;
	if (*len + n > *cap) {
		size_t nc = *cap ? *cap * 2 : 4096;
		while (nc < *len + n) nc *= 2;
		uint8_t *nb = realloc(*buf, nc);
		if (!nb) return -1;
		*buf = nb;
		*cap = nc;
	}
	memcpy(*buf + *len, data, n);
	*len += n;
	return 0;
}

static void ws_reset_conn(int notify_close, int close_code)
{
#ifdef AXIL_WS_TLS
	if (g_ws.ssl) {
		SSL_shutdown(g_ws.ssl);
		SSL_free(g_ws.ssl);
		g_ws.ssl = NULL;
	}
#endif
	if (g_ws.fd >= 0) {
		close(g_ws.fd);
		g_ws.fd = -1;
	}
	ws_state_t prev = g_ws.state;
	g_ws.state = WS_ST_IDLE;
	g_ws.use_tls = 0;
	g_ws.tx_len = 0;
	g_ws.rx_len = 0;
	g_ws.frag_len = 0;
	g_ws.frag_active = 0;

	if (notify_close && prev != WS_ST_IDLE && g_ws.cbs.on_close)
		g_ws.cbs.on_close(g_ws.cbs.user, close_code);
}

static void ws_queue_frame(uint8_t opcode, const void *payload, size_t len)
{
	uint8_t hdr[14];
	size_t hlen = 0;
	hdr[hlen++] = (uint8_t)(0x80u | (opcode & 0x0Fu)); /* FIN=1 */

	if (len <= 125) {
		hdr[hlen++] = (uint8_t)(0x80u | len); /* MASK=1 */
	} else if (len <= 65535) {
		hdr[hlen++] = (uint8_t)(0x80u | 126u);
		hdr[hlen++] = (uint8_t)((len >> 8) & 0xFFu);
		hdr[hlen++] = (uint8_t)(len & 0xFFu);
	} else {
		hdr[hlen++] = (uint8_t)(0x80u | 127u);
		uint64_t l64 = (uint64_t)len;
		for (int i = 7; i >= 0; i--)
			hdr[hlen++] = (uint8_t)((l64 >> (i * 8)) & 0xFFu);
	}

	uint32_t m = next_rand32();
	uint8_t mask[4] = {
		(uint8_t)(m & 0xFFu),
		(uint8_t)((m >> 8) & 0xFFu),
		(uint8_t)((m >> 16) & 0xFFu),
		(uint8_t)((m >> 24) & 0xFFu)
	};
	memcpy(hdr + hlen, mask, 4);
	hlen += 4;

	if (buf_append(&g_ws.tx_buf, &g_ws.tx_len, &g_ws.tx_cap, hdr, hlen) < 0)
		return;

	if (len > 0 && payload) {
		size_t start = g_ws.tx_len;
		if (buf_append(&g_ws.tx_buf, &g_ws.tx_len, &g_ws.tx_cap, payload, len) < 0)
			return;
		for (size_t i = 0; i < len; i++)
			g_ws.tx_buf[start + i] ^= mask[i & 3];
	}
}

static void ws_send_handshake(void)
{
	uint8_t nonce[16];
	for (int i = 0; i < 4; i++) {
		uint32_t r = next_rand32();
		memcpy(nonce + i * 4, &r, 4);
	}
	char key_b64[32];
	base64_encode(nonce, sizeof(nonce), key_b64);

	char host_hdr[300];
	if ((g_ws.use_tls && g_ws.req_port == 443) ||
	    (!g_ws.use_tls && g_ws.req_port == 80)) {
		snprintf(host_hdr, sizeof(host_hdr), "%s", g_ws.req_host);
	} else {
		snprintf(host_hdr, sizeof(host_hdr), "%s:%u",
		         g_ws.req_host, (unsigned)g_ws.req_port);
	}

	char req[1536];
	int n;
	if (g_ws.cookie[0]) {
		n = snprintf(req, sizeof(req),
			"GET %s HTTP/1.1\r\n"
			"Host: %s\r\n"
			"Upgrade: websocket\r\n"
			"Connection: Upgrade\r\n"
			"Sec-WebSocket-Key: %s\r\n"
			"Sec-WebSocket-Version: 13\r\n"
			"Cookie: %s\r\n"
			"\r\n",
			g_ws.req_path, host_hdr, key_b64, g_ws.cookie);
	} else {
		n = snprintf(req, sizeof(req),
			"GET %s HTTP/1.1\r\n"
			"Host: %s\r\n"
			"Upgrade: websocket\r\n"
			"Connection: Upgrade\r\n"
			"Sec-WebSocket-Key: %s\r\n"
			"Sec-WebSocket-Version: 13\r\n"
			"\r\n",
			g_ws.req_path, host_hdr, key_b64);
	}
	if (n > 0) {
		buf_append(&g_ws.tx_buf, &g_ws.tx_len, &g_ws.tx_cap, req, (size_t)n);
	}
	g_ws.state = WS_ST_HANDSHAKE;
}

static void ws_flush_tx(void)
{
	while (g_ws.fd >= 0 && g_ws.tx_len > 0) {
		ssize_t w;
#ifdef AXIL_WS_TLS
		if (g_ws.use_tls) {
			if (!g_ws.ssl) { ws_reset_conn(1, 1006); break; }
			int r = SSL_write(g_ws.ssl, g_ws.tx_buf, (int)g_ws.tx_len);
			if (r > 0) {
				w = (ssize_t)r;
			} else {
				int err = SSL_get_error(g_ws.ssl, r);
				if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE)
					break;
				ws_reset_conn(1, 1006);
				break;
			}
		} else
#endif
		{
			w = send(g_ws.fd, g_ws.tx_buf, g_ws.tx_len, MSG_NOSIGNAL);
		}
		if (w > 0) {
			size_t uw = (size_t)w;
			if (uw < g_ws.tx_len)
				memmove(g_ws.tx_buf, g_ws.tx_buf + uw, g_ws.tx_len - uw);
			g_ws.tx_len -= uw;
		} else if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
			break;
		} else {
			ws_reset_conn(1, 1006);
			break;
		}
	}
}

static void ws_dispatch_message(uint8_t opcode, const uint8_t *data, size_t len)
{
	int is_text = (opcode == 0x01);
	uint8_t channel = AXIL_WS_CH_TEXT;

	if (!is_text && len >= 2 && data[0] == 0x23 && data[1] == 0x62) {
		channel = AXIL_WS_CH_BCP;
	} else if (!is_text && len >= 2 && data[0] == 0x23 && data[1] == 0x72) {
		channel = AXIL_WS_CH_RPC;
	}

	if (channel == AXIL_WS_CH_TEXT && g_ws.cbs.on_term_write && len > 0)
		g_ws.cbs.on_term_write(g_ws.cbs.user, (const char *)data, len);

	if (g_ws.cbs.on_channel)
		g_ws.cbs.on_channel(g_ws.cbs.user, channel, data, len, is_text);
}

static int ws_process_rx(void)
{
	int dispatched = 0;

	if (g_ws.state == WS_ST_HANDSHAKE) {
		/* Scan for \r\n\r\n */
		if (g_ws.rx_len < 4) return 0;
		size_t hdr_end = 0;
		for (size_t i = 0; i + 3 < g_ws.rx_len; i++) {
			if (g_ws.rx_buf[i] == '\r' && g_ws.rx_buf[i + 1] == '\n' &&
			    g_ws.rx_buf[i + 2] == '\r' && g_ws.rx_buf[i + 3] == '\n') {
				hdr_end = i + 4;
				break;
			}
		}
		if (hdr_end == 0) {
			if (g_ws.rx_len > 16384)
				ws_reset_conn(1, 1002);
			return 0;
		}

		/* Verify HTTP/1.1 101 */
		int ok = 0;
		if (hdr_end >= 12 && memcmp(g_ws.rx_buf, "HTTP/1.", 7) == 0) {
			const char *sp = memchr(g_ws.rx_buf, ' ', hdr_end);
			if (sp && (size_t)(sp - (const char *)g_ws.rx_buf) + 4 <= hdr_end &&
			    memcmp(sp + 1, "101", 3) == 0) {
				ok = 1;
			}
		}
		if (!ok) {
			ws_reset_conn(1, 1002);
			return -1;
		}

		size_t rem = g_ws.rx_len - hdr_end;
		if (rem > 0)
			memmove(g_ws.rx_buf, g_ws.rx_buf + hdr_end, rem);
		g_ws.rx_len = rem;
		g_ws.state = WS_ST_OPEN;

		if (g_ws.cbs.on_open)
			g_ws.cbs.on_open(g_ws.cbs.user);
	}

	while (g_ws.state == WS_ST_OPEN && g_ws.rx_len >= 2) {
		uint8_t b0 = g_ws.rx_buf[0];
		uint8_t b1 = g_ws.rx_buf[1];
		int fin = (b0 & 0x80u) != 0;
		uint8_t opcode = b0 & 0x0Fu;
		int masked = (b1 & 0x80u) != 0;
		uint64_t plen = b1 & 0x7Fu;
		size_t pos = 2;

		if (plen == 126) {
			if (g_ws.rx_len < pos + 2) break;
			plen = ((uint64_t)g_ws.rx_buf[pos] << 8) | g_ws.rx_buf[pos + 1];
			pos += 2;
		} else if (plen == 127) {
			if (g_ws.rx_len < pos + 8) break;
			plen = 0;
			for (int i = 0; i < 8; i++)
				plen = (plen << 8) | g_ws.rx_buf[pos + (size_t)i];
			pos += 8;
		}

		uint8_t mask[4] = {0, 0, 0, 0};
		if (masked) {
			if (g_ws.rx_len < pos + 4) break;
			memcpy(mask, g_ws.rx_buf + pos, 4);
			pos += 4;
		}

		if (plen > 32ULL * 1024ULL * 1024ULL) {
			ws_reset_conn(1, 1009);
			return -1;
		}
		if (g_ws.rx_len < pos + (size_t)plen)
			break;

		uint8_t *payload = g_ws.rx_buf + pos;
		size_t ulen = (size_t)plen;
		if (masked) {
			for (size_t i = 0; i < ulen; i++)
				payload[i] ^= mask[i & 3];
		}

		/* Copy payload before shifting rx_buf so re-entrant callbacks are safe */
		uint8_t *copy = NULL;
		if (ulen > 0) {
			copy = malloc(ulen);
			if (copy) memcpy(copy, payload, ulen);
		}

		size_t frame_sz = pos + ulen;
		size_t rem = g_ws.rx_len - frame_sz;
		if (rem > 0)
			memmove(g_ws.rx_buf, g_ws.rx_buf + frame_sz, rem);
		g_ws.rx_len = rem;

		if (opcode == 0x08) {
			/* Close frame */
			int code = 1000;
			if (copy && ulen >= 2)
				code = ((int)copy[0] << 8) | (int)copy[1];
			free(copy);
			ws_queue_frame(0x08, NULL, 0);
			ws_flush_tx();
			ws_reset_conn(1, code);
			break;
		} else if (opcode == 0x09) {
			/* Ping -> reply Pong */
			ws_queue_frame(0x0A, copy, ulen);
			ws_flush_tx();
		} else if (opcode == 0x0A) {
			/* Pong: ignore */
		} else if (opcode == 0x01 || opcode == 0x02) {
			if (fin && !g_ws.frag_active) {
				ws_dispatch_message(opcode, copy ? copy : (const uint8_t *)"", ulen);
				dispatched++;
			} else {
				g_ws.frag_len = 0;
				g_ws.frag_opcode = opcode;
				g_ws.frag_active = 1;
				if (copy && ulen > 0)
					buf_append(&g_ws.frag_buf, &g_ws.frag_len, &g_ws.frag_cap, copy, ulen);
				if (fin) {
					ws_dispatch_message(g_ws.frag_opcode,
					                    g_ws.frag_buf ? g_ws.frag_buf : (const uint8_t *)"",
					                    g_ws.frag_len);
					g_ws.frag_len = 0;
					g_ws.frag_active = 0;
					dispatched++;
				}
			}
		} else if (opcode == 0x00 && g_ws.frag_active) {
			if (copy && ulen > 0)
				buf_append(&g_ws.frag_buf, &g_ws.frag_len, &g_ws.frag_cap, copy, ulen);
			if (fin) {
				ws_dispatch_message(g_ws.frag_opcode,
				                    g_ws.frag_buf ? g_ws.frag_buf : (const uint8_t *)"",
				                    g_ws.frag_len);
				g_ws.frag_len = 0;
				g_ws.frag_active = 0;
				dispatched++;
			}
		}

		free(copy);
	}

	return dispatched;
}

/* ── Public API ─────────────────────────────────────────────────────────── */

void axil_ws_native_set_base(const char *host, uint16_t port)
{
	if (host && host[0])
		snprintf(g_ws.base_host, sizeof(g_ws.base_host), "%s", host);
	if (port > 0)
		g_ws.base_port = port;
}

void axil_ws_native_set_cookie(const char *cookie)
{
	if (cookie && cookie[0])
		snprintf(g_ws.cookie, sizeof(g_ws.cookie), "%s", cookie);
	else
		g_ws.cookie[0] = '\0';
}

void axil_ws_native_set_callbacks(const axil_ws_native_cbs_t *cbs)
{
	if (cbs) g_ws.cbs = *cbs;
	else memset(&g_ws.cbs, 0, sizeof(g_ws.cbs));
}

static int ws_start_tls(void)
{
#ifdef AXIL_WS_TLS
	if (!g_ws.ssl_ctx) {
		g_ws.ssl_ctx = SSL_CTX_new(TLS_client_method());
		if (!g_ws.ssl_ctx) return -1;
		SSL_CTX_set_default_verify_paths(g_ws.ssl_ctx);
	}
	g_ws.ssl = SSL_new(g_ws.ssl_ctx);
	if (!g_ws.ssl) return -1;
	SSL_set_fd(g_ws.ssl, g_ws.fd);
	SSL_set_tlsext_host_name(g_ws.ssl, g_ws.req_host);
	SSL_set_connect_state(g_ws.ssl);
	g_ws.state = WS_ST_TLS_HANDSHAKE;
	int r = SSL_connect(g_ws.ssl);
	if (r == 1) {
		ws_send_handshake();
		ws_flush_tx();
		return 0;
	}
	int err = SSL_get_error(g_ws.ssl, r);
	if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE)
		return 0;
	return -1;
#else
	return -1;
#endif
}

static void parse_ws_url(const char *url, size_t len)
{
	char tmp[768];
	if (len >= sizeof(tmp)) len = sizeof(tmp) - 1;
	memcpy(tmp, url, len);
	tmp[len] = '\0';

	snprintf(g_ws.req_host, sizeof(g_ws.req_host), "%s", g_ws.base_host);
	g_ws.req_port = g_ws.base_port;
	g_ws.use_tls = 0;
	snprintf(g_ws.req_path, sizeof(g_ws.req_path), "/");

	const char *p = tmp;
	int is_wss = (strncmp(p, "wss://", 6) == 0);
	int is_ws  = (strncmp(p, "ws://", 5) == 0);
	if (is_wss || is_ws) {
		g_ws.use_tls = is_wss ? 1 : 0;
		p += is_wss ? 6 : 5;
		const char *slash = strchr(p, '/');
		const char *host_end = slash ? slash : (p + strlen(p));
		const char *colon = memchr(p, ':', (size_t)(host_end - p));
		if (colon) {
			size_t hl = (size_t)(colon - p);
			if (hl >= sizeof(g_ws.req_host)) hl = sizeof(g_ws.req_host) - 1;
			memcpy(g_ws.req_host, p, hl);
			g_ws.req_host[hl] = '\0';
			int port = atoi(colon + 1);
			if (port > 0 && port <= 65535) g_ws.req_port = (uint16_t)port;
		} else {
			size_t hl = (size_t)(host_end - p);
			if (hl >= sizeof(g_ws.req_host)) hl = sizeof(g_ws.req_host) - 1;
			memcpy(g_ws.req_host, p, hl);
			g_ws.req_host[hl] = '\0';
			g_ws.req_port = is_wss ? 443 : 80;
		}
		if (slash)
			snprintf(g_ws.req_path, sizeof(g_ws.req_path), "%.511s", slash);
	} else if (p[0] == '/') {
		snprintf(g_ws.req_path, sizeof(g_ws.req_path), "%.511s", p);
	} else {
		snprintf(g_ws.req_path, sizeof(g_ws.req_path), "/%.510s", p);
	}
}

void ws_connect(const char *url, size_t len)
{
	if (!url || len == 0) return;
	ws_reset_conn(0, 0);
	parse_ws_url(url, len);

	char port_str[16];
	snprintf(port_str, sizeof(port_str), "%u", (unsigned)g_ws.req_port);

	struct addrinfo hints, *res = NULL;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	if (getaddrinfo(g_ws.req_host, port_str, &hints, &res) != 0 || !res) {
		if (g_ws.cbs.on_close)
			g_ws.cbs.on_close(g_ws.cbs.user, 1006);
		return;
	}

	int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (fd < 0) {
		freeaddrinfo(res);
		if (g_ws.cbs.on_close)
			g_ws.cbs.on_close(g_ws.cbs.user, 1006);
		return;
	}

	int flags = fcntl(fd, F_GETFL, 0);
	if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);

	int rc = connect(fd, res->ai_addr, res->ai_addrlen);
	int err = errno;
	freeaddrinfo(res);

	if (rc == 0) {
		g_ws.fd = fd;
		if (g_ws.use_tls) {
			if (ws_start_tls() < 0)
				ws_reset_conn(1, 1006);
		} else {
			ws_send_handshake();
			ws_flush_tx();
		}
	} else if (err == EINPROGRESS) {
		g_ws.fd = fd;
		g_ws.state = WS_ST_TCP_CONNECTING;
	} else {
		close(fd);
		if (g_ws.cbs.on_close)
			g_ws.cbs.on_close(g_ws.cbs.user, 1006);
	}
}

void ws_send(const void *buf, size_t len)
{
	if (!buf || len == 0) return;
	if (g_ws.state != WS_ST_OPEN) return;
	/* Text opcode for UTF-8 commands unless binary BCP/Telnet IAC */
	const uint8_t *b = (const uint8_t *)buf;
	uint8_t op = (b[0] == 0xFF || (len >= 2 && b[0] == 0x23 && (b[1] == 0x62 || b[1] == 0x72)))
		? 0x02 : 0x01;
	ws_queue_frame(op, buf, len);
	ws_flush_tx();
}

void ws_send_channel(uint8_t channel, const void *buf, size_t len)
{
	if (!buf || len == 0) return;
	if (g_ws.state != WS_ST_OPEN) return;
	uint8_t op = (channel == AXIL_WS_CH_TEXT) ? 0x01 : 0x02;
	ws_queue_frame(op, buf, len);
	ws_flush_tx();
}

int ws_status(void)
{
	switch (g_ws.state) {
	case WS_ST_TCP_CONNECTING:
	case WS_ST_TLS_HANDSHAKE:
	case WS_ST_HANDSHAKE: return AXIL_WS_CONNECTING;
	case WS_ST_OPEN:      return AXIL_WS_CONNECTED;
	case WS_ST_CLOSING:   return AXIL_WS_CLOSING;
	default:              return AXIL_WS_DISCONNECTED;
	}
}

void ws_close(void)
{
	if (g_ws.state == WS_ST_OPEN) {
		ws_queue_frame(0x08, NULL, 0);
		ws_flush_tx();
	}
	ws_reset_conn(1, 1000);
}

void term_write(const char *buf, size_t len)
{
	if (buf && len > 0 && g_ws.cbs.on_term_write)
		g_ws.cbs.on_term_write(g_ws.cbs.user, buf, len);
}

int axil_ws_native_poll(int timeout_ms)
{
	if (g_ws.fd < 0 || g_ws.state == WS_ST_IDLE)
		return 0;

	struct pollfd pfd;
	pfd.fd = g_ws.fd;
	pfd.events = POLLIN;
	if (g_ws.state == WS_ST_TCP_CONNECTING || g_ws.state == WS_ST_TLS_HANDSHAKE || g_ws.tx_len > 0)
		pfd.events |= POLLOUT;
	pfd.revents = 0;

	int pr = poll(&pfd, 1, timeout_ms);
	if (pr < 0) {
		if (errno == EINTR) return 0;
		ws_reset_conn(1, 1006);
		return -1;
	}
	if (pr == 0)
		return 0;

	if (g_ws.state == WS_ST_TCP_CONNECTING) {
		if (pfd.revents & (POLLERR | POLLHUP)) {
			ws_reset_conn(1, 1006);
			return -1;
		}
		if (pfd.revents & POLLOUT) {
			int so_err = 0;
			socklen_t slen = sizeof(so_err);
			if (getsockopt(g_ws.fd, SOL_SOCKET, SO_ERROR, &so_err, &slen) < 0 || so_err != 0) {
				ws_reset_conn(1, 1006);
				return -1;
			}
			if (g_ws.use_tls) {
				if (ws_start_tls() < 0) {
					ws_reset_conn(1, 1006);
					return -1;
				}
			} else {
				ws_send_handshake();
				ws_flush_tx();
			}
		}
		return 0;
	}

#ifdef AXIL_WS_TLS
	if (g_ws.state == WS_ST_TLS_HANDSHAKE) {
		if (!g_ws.ssl) { ws_reset_conn(1, 1006); return -1; }
		int r = SSL_connect(g_ws.ssl);
		if (r == 1) {
			ws_send_handshake();
			ws_flush_tx();
			return 0;
		}
		int err = SSL_get_error(g_ws.ssl, r);
		if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE)
			return 0;
		ws_reset_conn(1, 1006);
		return -1;
	}
#endif

	if (pfd.revents & POLLOUT)
		ws_flush_tx();

	int dispatched = 0;
	if (g_ws.fd >= 0 && (pfd.revents & (POLLIN | POLLHUP | POLLERR))) {
		uint8_t tmp[8192];
		for (;;) {
			ssize_t r;
#ifdef AXIL_WS_TLS
			if (g_ws.use_tls) {
				if (!g_ws.ssl) { ws_reset_conn(1, 1006); return -1; }
				int sr = SSL_read(g_ws.ssl, tmp, (int)sizeof(tmp));
				if (sr > 0) {
					r = (ssize_t)sr;
				} else {
					int err = SSL_get_error(g_ws.ssl, sr);
					if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE)
						break;
					if (err == SSL_ERROR_ZERO_RETURN || sr == 0) {
						r = 0;
					} else {
						ws_reset_conn(1, 1006);
						return -1;
					}
				}
			} else
#endif
			{
				r = recv(g_ws.fd, tmp, sizeof(tmp), 0);
			}
			if (r > 0) {
				if (buf_append(&g_ws.rx_buf, &g_ws.rx_len, &g_ws.rx_cap,
				               tmp, (size_t)r) < 0) {
					ws_reset_conn(1, 1011);
					return -1;
				}
			} else if (r == 0) {
				int d = ws_process_rx();
				if (d > 0) dispatched += d;
				ws_reset_conn(1, 1000);
				return dispatched;
			} else {
				if (errno == EAGAIN || errno == EWOULDBLOCK)
					break;
				if (errno == EINTR)
					continue;
				ws_reset_conn(1, 1006);
				return -1;
			}
		}
		int d = ws_process_rx();
		if (d < 0) return -1;
		dispatched += d;
	}

	return dispatched;
}
