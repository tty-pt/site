#ifndef ND_SEVEN_H
#define ND_SEVEN_H

/*
 * nd-seven: the region module that marks an area as the 2D continuous
 * "seven" stage. Loading it into a region (`loadmod libnd-seven <region>`)
 * is how a modder claims the area; the stage, layout and map presentation
 * are chosen with `driver stage lamina-seven`, `driver layout game-2d`.
 *
 * The world generator and collision shared with the browser UX live in
 * <nd/seven-world.h> (pure C, header-only).
 */

#include <nd/seven-world.h>

#ifndef XY_MODULE_API
#define XY_MODULE_API __attribute__((visibility("default")))
#endif

#if !defined(__wasm__)
XY_MODULE_API void xy_install(void);
XY_MODULE_API void xy_uninstall(void);
#endif

#endif /* ND_SEVEN_H */
