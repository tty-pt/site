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

#define ND_MAP_CANVAS_W 160
#define ND_MAP_CANVAS_H 160

/*
 * Theme descriptor for liblamina room map rendering.
 */
typedef struct nd_lamina_theme {
	const char *name;
	uint32_t bg_color;
	uint32_t floor_color;
	uint32_t wall_color;
	uint32_t door_color;
	uint32_t player_color;
	uint32_t player_glow;
	uint32_t entity_color;
	uint32_t grid_border;
	uint8_t  rounded_radius;
	uint8_t  vignette_strength;
} nd_lamina_theme_t;

/* Find theme by name; falls back to default "lamina" if unknown. */
const nd_lamina_theme_t *nd_lamina_theme_find(const char *name);

/*
 * Render a beautiful, customized room map into an RGBA pixel buffer.
 * - w, h: Framebuffer dimensions (e.g. 160x160)
 * - map: 7x7 binary map data
 * - theme_name: Regional theme ("lamina", "classic", "cyber", "forest", "dungeon", etc.)
 * - tod: Time of day (0..86400) for ambient illumination tint
 * - frame: Animation frame counter for breathing/pulsing player marker
 */
void nd_lamina_render_map_themed(uint32_t *fb, uint32_t w, uint32_t h,
                                 const nd_map_data_t *map,
                                 const char *theme_name,
                                 uint32_t tod,
                                 uint32_t frame);

/*
 * Backward-compatible helper that defaults to "lamina" theme at noon.
 */
void nd_lamina_render_map(uint32_t *fb, uint32_t w, uint32_t h, const nd_map_data_t *map);

/*
 * Backward-compatible 2D scene/room renderers (retained for backward compatibility).
 */
void nd_lamina_render_scene(uint32_t *fb, uint32_t w, uint32_t h, const nd_scene_t *scene);
void nd_lamina_render_room(uint32_t *fb, uint32_t w, uint32_t h, const nd_client_state_t *st);

#ifdef __cplusplus
}
#endif

#endif /* ND_LAMINA_H */
