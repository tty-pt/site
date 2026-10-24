#ifndef UAPI_MAP_H
#define UAPI_MAP_H

#include "./st.h"

#ifndef ND_WORLD_H
typedef int map_has_t(unsigned thing);
map_has_t eng_map_has;

XY_DECL(morton_t, map_mwhere, unsigned, thing);
XY_DECL(int, map_where, coord_t *, p, unsigned, thing);
XY_DECL(unsigned, map_get, coord_t *, p);
XY_DECL(int, map_search, unsigned *, mat, coord_t *, pos, unsigned, radius);
XY_DECL(int, nd_noise_view, struct bio *, to, coord_t *, pos);

#ifndef ND_WORLD_PROVIDER
static inline void eng_map_where(coord_t *p, unsigned thing) { map_where(p, thing); }
static inline morton_t eng_map_mwhere(unsigned thing) { return map_mwhere(thing); }
static inline unsigned eng_map_get(pos_t at) { return map_get(at); }
#else
unsigned eng_map_get(pos_t at);
morton_t eng_map_mwhere(unsigned thing);
void eng_map_where(coord_t *p, unsigned thing);
#endif

typedef int map_delete_t(unsigned what);
map_delete_t eng_map_delete;
#endif

#endif
