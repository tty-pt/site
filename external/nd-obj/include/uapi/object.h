#ifndef UAPI_OBJECT_H
#define UAPI_OBJECT_H

#include <nd/world-types.h>

/* FIXME: not for plugins */

typedef int obj_exists_t(unsigned ref);
obj_exists_t eng_obj_exists;

typedef unsigned object_new_t(OBJ *obj);
object_new_t eng_object_new;

typedef unsigned object_copy_t(OBJ *nu, unsigned old_ref);
object_copy_t eng_object_copy;

#ifndef ND_WORLD_H
XY_DECL(int, object_move, unsigned, what_ref, unsigned, where_ref);
#endif
static inline void eng_object_move(unsigned what_ref, unsigned where_ref) { object_move(what_ref, where_ref); }

#ifndef ND_WORLD_H
XY_DECL(unsigned, object_add, OBJ *, nu, unsigned, skel_id, unsigned, where, uint64_t, v);
#endif
#ifndef ND_OBJ_PROVIDER
static inline unsigned eng_object_add(OBJ *nu, unsigned skel_id, unsigned where, uint64_t v, unsigned flags) {
    (void)flags;
    return object_add(nu, skel_id, where, v);
}
#endif

typedef void object_drop_t(unsigned where_ref, unsigned skel_id);
object_drop_t eng_object_drop;

typedef struct icon object_icon_t(unsigned player_ref, unsigned thing_ref);
object_icon_t eng_object_icon;

typedef char *object_art_t(unsigned ref);
object_art_t eng_object_art;

#ifndef ND_WORLD_H
XY_DECL(char *, unparse, unsigned, loc_ref);
#endif
#ifndef ND_OBJ_PROVIDER
static inline const char *eng_unparse(unsigned loc_ref) { return unparse(loc_ref); }
#endif

#include <ttypt/xy.h>
#ifndef ND_WORLD_H
XY_DECL(int, objects_update, double, dt);
XY_DECL(int, objects_init, unsigned, dummy);
XY_DECL(int, obj_exists, unsigned, ref);
XY_DECL(char *, object_art, unsigned, thing_ref);
XY_DECL(int, nd_rwrite, unsigned, room_ref, unsigned, exception_ref, char *, str, size_t, len);
#endif

#endif
