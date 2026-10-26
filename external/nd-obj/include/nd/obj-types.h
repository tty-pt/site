#ifndef ND_OBJ_TYPES_H
#define ND_OBJ_TYPES_H

/*
 * obj-types.h — shared types for NeverDark object and entity model (libnd-obj).
 */

#include <nd/world-types.h>
#include <nd/law.h>

#define obj_hd nd_table_handle(HD_OBJ)
#define skel_hd nd_table_handle(HD_SKEL)
#define contents_hd nd_table_handle(HD_CONTENTS)
#define obs_hd nd_table_handle(HD_OBS)
#define ent_hd nd_table_handle(HD_ENTITY)
#define drop_hd nd_table_handle(HD_DROP)
#define adrop_hd nd_table_handle(HD_ADROP)

#endif /* ND_OBJ_TYPES_H */
