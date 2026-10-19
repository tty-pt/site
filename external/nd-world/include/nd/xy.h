#ifndef ND_XY_H
#define ND_XY_H

/*
 * xy.h — umbrella over the NeverDark module API exposed through libxylem
 * (XY). This is the single include game modules (and the 19 external nd-*
 * modules) have always used; it is now just the union of the three layers:
 *
 *     nd/law.h      law:       transport (io), storage, command registration
 *     nd/regions.h  law:       public governance (region ownership)
 *     nd/world.h    convention: the reference universe (object model, map,
 *                              entity, ematch, vtf, noise, mcp)
 *
 * Include the narrowest layer you need: a kernel-side object should include
 * <nd/law.h> (or <nd/regions.h> if it governs), a game module may keep
 * including <nd/xy.h> unchanged.
 *
 * A module includes this header as `<nd/xy.h>`. `make install` puts this
 * tree in $(PREFIX)/include/nd/, so `-I$(PREFIX)/include` resolves it — the
 * same -I that mk/portable.mk and nd-mod.mk's XY_INC already put on every
 * module compile line, and the one that carries <ttypt/xy.h> too:
 *
 *     #include <ttypt/xy-mod.h>   // must PRECEDE this header; see nd_last
 *     #include <nd/xy.h>
 *
 * Replaces the engine's dlopen mod-load / `struct nd` handoff / SIC macros
 * (interface.c mod_load + `*ind = nd`, uapi/type.h SIC_DECL/SIC_DEF/SIC_CALL)
 * with the XY host/module protocol:
 *
 *   - The ENGINE (host) provides every hook via XY_IMPL in ONE TU that
 *     must NOT include this header (site mods/common/common.h rule). The hook
 *     name with no module override dispatches to the engine's implementation
 *     within the caller's region subtree.
 *   - GAME MODULES include this header freely, then call the hooks as plain C
 *     (XY_DECL forwards through xy_call to the engine provider). Modules
 *     implement their own game events (see nd/hooks.h) with XY_IMPL.
 *   - `get_xy_ptr()` in xy-mod.h supplies the injected xy context
 *     (module_path, region_id, load/unload, ...) — the replacement for
 *     `struct nd *ind = dlsym(sl, "nd"); *ind = nd;`.
 *
 * See nd/law.h for the HOOK SHAPE CONSTRAINT and the self-contained-header
 * rationale; it applies to every layer.
 */

#include <nd/law.h>
#include <nd/regions.h>
#include <nd/world.h>

#endif /* ND_XY_H */