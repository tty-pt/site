#ifndef UAPI_ENTITY_H
#define UAPI_ENTITY_H

#include <math.h>
#include "./skel.h"
#include "./object.h"

typedef unsigned me_get_t(void);
me_get_t eng_me_get;

#ifndef ND_WORLD_H
XY_DECL(ENT, ent_get, unsigned, ref);
XY_DECL(int, ent_set, unsigned, ref, ENT *, tmp);
#endif

typedef void ent_del_t(unsigned ref);
ent_del_t eng_ent_del;

typedef void ent_reset_t(ENT *ent);
ent_reset_t ent_reset;

#ifndef ND_WORLD_H
XY_DECL(int, controls, unsigned, who_ref, unsigned, what_ref);
#endif
#ifndef ND_OBJ_PROVIDER
static inline int eng_controls(unsigned who_ref, unsigned what_ref) {
    return controls(who_ref, what_ref);
}
#endif

typedef int payfor_t(unsigned who_ref, OBJ *who, unsigned cost);
payfor_t eng_payfor;

typedef void look_around_t(unsigned player_ref);
look_around_t look_around;

#ifndef ND_WORLD_H
XY_DECL(int, enter, unsigned, player_ref, unsigned, loc_ref, int, e);
#endif
#ifndef ND_OBJ_PROVIDER
static inline void eng_enter(unsigned player_ref, unsigned loc_ref, int e) { enter(player_ref, loc_ref, e); }
#endif

#ifndef ND_WORLD_H
XY_DECL(int, look_at, unsigned, player_ref, unsigned, loc_ref);
#endif
#ifndef ND_OBJ_PROVIDER
static inline void eng_look_at(unsigned player_ref, unsigned loc_ref) { look_at(player_ref, loc_ref); }
#endif

extern unsigned me;

#include <ttypt/xy.h>
#ifndef ND_WORLD_H
XY_DECL(unsigned, room_clean, unsigned, loc_ref);
#endif

#endif
