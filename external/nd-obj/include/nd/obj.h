#ifndef ND_OBJ_H
#define ND_OBJ_H

/*
 * obj.h — caller XY interface for NeverDark object and entity model (libnd-obj).
 */

#include "obj-types.h"
#include <ttypt/xy.h>

XY_DECL(int, object_add, unsigned, ref, unsigned, type, uint64_t, v);
XY_DECL(int, object_del, unsigned, ref, unsigned, type);
XY_DECL(int, object_move, unsigned, ref, unsigned, loc);
XY_DECL(unsigned, object_copy, unsigned, old_ref, unsigned, new_owner);
XY_DECL(unsigned, object_new, OBJ *, obj);
XY_DECL(int, obj_exists, unsigned, ref);
XY_DECL(int, look_at, unsigned, player_ref, unsigned, loc_ref);
XY_DECL(int, controls, unsigned, who, unsigned, what);
XY_DECL(const char *, unparse, unsigned, loc_ref);
XY_DECL(struct icon, object_icon, unsigned, player_ref, unsigned, what_ref);
XY_DECL(const char *, object_art, unsigned, thing_ref);
XY_DECL(unsigned, art_id, unsigned, ref);

#endif /* ND_OBJ_H */
