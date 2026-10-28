#ifndef ND_SEVEN_H
#define ND_SEVEN_H

#include <stdint.h>
#include "nd/bcp.h"

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

void nd_seven_lamb(int *fx, int *fy, int *dir, int *walking);
void nd_seven_debug(int *pending, int *pos_x, int *pos_y, int *have_room);

#endif /* ND_SEVEN_H */
