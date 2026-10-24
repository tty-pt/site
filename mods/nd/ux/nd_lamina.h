#ifndef ND_LAMINA_H
#define ND_LAMINA_H

#include <stdint.h>
#include <stddef.h>
#include "nd/bcp.h"
#include "nd_scene.h"
#include <ttypt/lam.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ND_CANVAS_W 640
#define ND_CANVAS_H 480

/*
 * Render a 2D tilemap room view from the synchronized scene graph into an RGBA pixel buffer.
 */
void nd_lamina_render_scene(uint32_t *fb, uint32_t w, uint32_t h, const nd_scene_t *scene);

/*
 * Backward-compatible helper that synchronizes and renders from client state.
 */
void nd_lamina_render_room(uint32_t *fb, uint32_t w, uint32_t h, const nd_client_state_t *st);

#ifdef __cplusplus
}
#endif

#endif /* ND_LAMINA_H */
