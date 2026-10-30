#ifndef ND_SEVEN_MOTION_H
#define ND_SEVEN_MOTION_H

#include <stdint.h>
#include <stddef.h>
#include "nd/bcp.h"
#include <nd/seven-world.h>

enum { DIR_DOWN, DIR_UP, DIR_LEFT, DIR_RIGHT };

/* Lamb coordinates and animation state */
int nd_seven_get_lx(void);
int nd_seven_get_ly(void);
int nd_seven_get_dir(void);
int nd_seven_get_walking(void);
uint32_t nd_seven_get_anim(void);
void nd_seven_step_anim(uint32_t delta);

/* Lamb placement & inspection */
void nd_seven_lamb(int *fx, int *fy, int *dir, int *walking);
void nd_seven_set_lamb(int fx, int fy);
void nd_seven_lamb_world(const nd_client_state_t *st, int *fx, int *fy);
void nd_seven_debug(int *pending, int *pos_x, int *pos_y, int *have_room);
void nd_seven_forget(void);

/* Origin and tile lookup */
void nd_seven_sync_origin(const nd_client_state_t *st);
ndw_tile_t nd_seven_convert_tile(const nd_map_tile_t *m);
int nd_seven_tile_at(const nd_client_state_t *st, int ti, int tj, ndw_tile_t *t, int16_t pos[4]);

/* Movement and collision */
int nd_seven_key(const char *key, int down);
int nd_seven_move(const nd_client_state_t *st, int key, int down);
int nd_seven_tick(const nd_client_state_t *st, float dt);
int nd_seven_collision(const nd_client_state_t *st, int x, int y);

#endif /* ND_SEVEN_MOTION_H */
