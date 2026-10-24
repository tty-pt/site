#ifndef ND_VIEWPORT_H
#define ND_VIEWPORT_H

/*
 * nd_viewport.h — Pluggable Viewport Driver Interface for NeverDark Bud UI.
 * Connects client viewports (Terminal, Lamina 2D) to the Bud isomorphic DOM shell.
 */

#include <stddef.h>
#include <stdint.h>
#include <bud/bud.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nd_viewport_driver {
	const char *id;                  /* "tty", "lamina-2d" */
	const char *label;               /* "📺 Terminal", "🎮 Lamina (2D)" */
	const char *required_capability; /* "axil-tty", "lamina" */
	bud_node *(*mount)(void);        /* Renders the <pre>, <canvas>, etc. */
	int (*init)(void);               /* Client initialization hook */
	void (*on_frame)(const uint8_t *buf, size_t len);
	void (*on_resize)(int w, int h);
	void (*destroy)(void);
} nd_viewport_driver_t;

#define ND_VIEWPORT_MAX_DRIVERS 8

/* Viewport Driver Registry API */
int nd_register_viewport_driver(const nd_viewport_driver_t *drv);
const nd_viewport_driver_t *nd_get_active_viewport_driver(void);
int nd_set_active_viewport_driver(const char *id);
int nd_viewport_driver_count(void);
const nd_viewport_driver_t *nd_get_viewport_driver_at(int idx);

#ifdef __cplusplus
}
#endif

#endif /* ND_VIEWPORT_H */
