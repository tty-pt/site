#ifndef ND_SEVEN_H
#define ND_SEVEN_H

#include <stdint.h>
#include "nd/bcp.h"

/*
 * The 2D "seven" stage: a deterministic Zelda-like room per NeverDark cell,
 * with the seven-ways lamb as the player. Pure C, dual-compiled; the room is
 * generated from the cell coordinates carried in nd_map_data_t.pos (see
 * nd/seven-room.h), the art comes from the generated sprite atlas.
 */

/* Feed a key event. Returns 1 if the key drives the lamb (and was consumed). */
int nd_seven_key(const char *key, int down);

/*
 * Advance the lamb by dt seconds. Returns the NeverDark move key ('h','j','k',
 * 'l') when the lamb walked out through a gate, else 0.
 */
int nd_seven_tick(const nd_client_state_t *st, float dt);

/* Draw the current room and the lamb into a w x h RGBA framebuffer. */
void nd_seven_render(uint32_t *fb, uint32_t w, uint32_t h, const nd_client_state_t *st);

/* Lamb state, exposed for tests and the HUD. */
void nd_seven_lamb(int *fx, int *fy, int *dir, int *walking);
void nd_seven_debug(int *pending, int *pos_x, int *pos_y, int *have_room);

#endif /* ND_SEVEN_H */
