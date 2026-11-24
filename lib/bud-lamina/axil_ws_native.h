#ifndef AXIL_WS_NATIVE_H
#define AXIL_WS_NATIVE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	void (*on_open)(void *user);
	void (*on_close)(void *user, int code);
	void (*on_channel)(void *user, uint8_t channel,
	                   const uint8_t *data, size_t len, int is_text);
	void (*on_term_write)(void *user, const char *data, size_t len);
	void *user;
} axil_ws_native_cbs_t;

/* Configure default host/port used when ws_connect() receives a relative path like "/nd". */
void axil_ws_native_set_base(const char *host, uint16_t port);

/* Optional HTTP Cookie header value (e.g. "session=abc") for authenticated WS upgrade. */
void axil_ws_native_set_cookie(const char *cookie);

/* Register event callbacks for the singleton native WS client. */
void axil_ws_native_set_callbacks(const axil_ws_native_cbs_t *cbs);

/* Non-blocking pump: drive connect/handshake/tx/rx/ping-pong.
 * timeout_ms = 0 for non-blocking per-frame poll.
 * Returns number of messages dispatched, or -1 on error. */
int axil_ws_native_poll(int timeout_ms);

/* Direct helpers matching <ttypt/axil-ws.h> AXIL_WS_NATIVE declarations. */
void ws_connect(const char *url, size_t len);
void ws_send(const void *buf, size_t len);
void ws_send_channel(uint8_t channel, const void *buf, size_t len);
int  ws_status(void);
void ws_close(void);
void term_write(const char *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* AXIL_WS_NATIVE_H */
