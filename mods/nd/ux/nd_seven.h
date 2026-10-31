#ifndef ND_SEVEN_H
#define ND_SEVEN_H

#include <stdint.h>
#include "nd/bcp.h"
#include <nd/seven-world.h>
#include "nd_seven_motion.h"
#include "nd_seven_objects.h"

/*
 * The 2D "seven" stage: the NeverDark map as one continuous, scrolling world
 * with the seven-ways lamb as the player. Pure C, dual-compiled. Every map
 * tile is a small block laid out beside its neighbours as far as the frame
 * reaches (nd/seven-world.h); its biome, plants and features come from the
 * nd_map_data_t tile, its position from nd_map_data_t.pos, the art from the
 * generated sprite atlas plus procedural pixel art (cactus, fir, sand ...).
 */

/* Feed a key event. Returns 1 if the key drives the lamb (and was consumed). */
int nd_seven_key(const char *key, int down);
void nd_seven_reset_input(void);

/*
 * Advance the lamb by dt seconds. Returns the NeverDark move key ('h','j','k',
 * 'l') when the lamb walked into a neighbouring tile, else 0.
 */
int nd_seven_tick(const nd_client_state_t *st, float dt);

/* Draw the world around the lamb into a w x h RGBA framebuffer (any size). */
void nd_seven_render(uint32_t *fb, uint32_t w, uint32_t h, const nd_client_state_t *st);

/* Lamb state, exposed for tests and the HUD. */
/* Forget remembered tile glyphs (new session or world). */
void nd_seven_forget(void);

/*
 * The objects Seven places in map tile (col, row) of md, in tile-local units
 * (NDW_TILE_W x NDW_TILE_H), back to front. Same layout the stage draws, so
 * other views (the lamina minimap) can mirror it. Returns the count.
 */
int nd_seven_tile_objects(const nd_map_data_t *md, int col, int row, ndw_obj_t out[NDW_MAX_OBJ]);

/*
 * The border warp grid of the tile at absolute pos (x, y, z, world), memoised
 * (it is a pure function of pos): Seven's ground and the lamina minimap share
 * it, so the biome borders have one shape in both. Never NULL; valid until a
 * later call may evict it.
 */
const ndw_warp_grid_t *nd_seven_warp_grid(const int16_t pos[4]);

void nd_seven_lamb(int *fx, int *fy, int *dir, int *walking);
void nd_seven_set_lamb(int fx, int fy);
void nd_seven_spot(uint32_t ref, int *sx, int *sy);

/* Interaction: detects if the lamb is near an interactable object or entity,
 * returns 1 and formats the command string (e.g. "get stick\n") into out. */
int nd_seven_action(const nd_client_state_t *st, char *out, size_t sz);
int nd_seven_action_execute(const nd_client_state_t *st, char *out, size_t sz);

/* Lamb feet relative to the map's player tile, after adopting the state's
 * current origin (so a map update and the lamb never disagree by a tile). */
void nd_seven_lamb_world(const nd_client_state_t *st, int *fx, int *fy);
void nd_seven_debug(int *pending, int *pos_x, int *pos_y, int *have_room);

#endif /* ND_SEVEN_H */
