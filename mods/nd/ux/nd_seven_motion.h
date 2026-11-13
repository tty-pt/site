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
int nd_seven_get_world(void);
int nd_seven_get_walking(void);
uint32_t nd_seven_get_anim(void);
float nd_seven_get_anim_time(void);
void nd_seven_step_anim(uint32_t delta);

/* Lamb placement & inspection */
void nd_seven_lamb(int *fx, int *fy, int *dir, int *walking);
void nd_seven_set_lamb(int fx, int fy);
void nd_seven_lamb_world(const nd_client_state_t *st, int *fx, int *fy);
void nd_seven_world_pos(const nd_client_state_t *st, int64_t *wx, int64_t *wy);
void nd_seven_debug(int *pending, int *pos_x, int *pos_y, int *have_room);
void nd_seven_forget(void);

/* Origin and tile lookup */
void nd_seven_sync_origin(const nd_client_state_t *st);
ndw_tile_t nd_seven_convert_tile(const nd_map_tile_t *m);
int nd_seven_tile_at(const nd_client_state_t *st, int ti, int tj, ndw_tile_t *t, int16_t pos[4]);
int nd_seven_resolve_tile_at_pos(const nd_client_state_t *st, const int16_t pos[4], ndw_tile_t *out);
int nd_seven_resolve_room_objects(const nd_client_state_t *st, const int16_t pos[4],
                                  ndw_tile_t *out_tile, ndw_obj_t out_objs[NDW_MAX_OBJ]);
const ndw_obj_t *nd_seven_resolve_room_objects_fast(const nd_client_state_t *st, const int16_t pos[4],
                                                    ndw_obj_t scratch_objs[NDW_MAX_OBJ], int *out_count);
void nd_seven_set_scenery_synth_budget(uint32_t budget);
int nd_seven_lookup_known_tile(const int16_t pos[4], ndw_tile_t *out);
int nd_seven_centre_restore(const int16_t pos[4], uint8_t *glyph, uint8_t *fg);

/* Movement and collision */
int nd_seven_key(const char *key, int down);
int nd_seven_move(const nd_client_state_t *st, int key, int down);
void nd_seven_reset_input(void);
int nd_seven_tick(const nd_client_state_t *st, float dt);
int nd_seven_collision(const nd_client_state_t *st, int x, int y);
void nd_seven_collision_invalidate(void);

/* Elastic footstep / actor wake ring buffer for reactive grass */
#define ND_SEVEN_WAKE_MAX 16

typedef struct {
	float wx, wy;
	float vx, vy;
	float t_sec;
	uint8_t valid;
} nd_grass_wake_step_t;

void nd_seven_record_wake_step(float wx, float wy, float vx, float vy, float t_sec);
void nd_seven_clear_wake(void);
const nd_grass_wake_step_t *nd_seven_get_wake_steps(int *out_count);

#endif /* ND_SEVEN_MOTION_H */
