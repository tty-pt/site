#ifndef ND_VIEW_TYPES_H
#define ND_VIEW_TYPES_H

#include "biome.h"
#include "st.h"
#include <nd/law.h>

#define ROOM_MAX 32

#define vtf_hd nd_table_handle(HD_VTF)
#define action_hd nd_table_handle(HD_ACTION)
#define type_hd nd_table_handle(HD_TYPE)
#define bcp_hd nd_table_handle(HD_BCP)

typedef view_tile_t view_t[VIEW_M];

#endif /* ND_VIEW_TYPES_H */
