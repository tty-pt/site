#ifndef ND_REGIONS_H
#define ND_REGIONS_H

/*
 * regions.h — law: public governance. Who owns which region, and what code
 * runs there. This is the treaty that makes a region a jurisdiction rather
 * than a coordinate: ownership, the authorization rule, and the position →
 * region resolution the kernel uses.
 *
 * Layering: includes <nd/law.h> (io/storage/command); nd/world.h includes
 * this. Game modules reach it through <nd/xy.h>.
 *
 * It stands on the region tree shipped by ST.md phases 0-4 — xy_claim_at(),
 * xy_region_at(), exact-region dispatch — and the `st_*` governor whose C
 * bodies live in src/spacetime.c (declared engine-internally in include/st.h,
 * with the `eng_st_*` names the provider policy requires).
 *
 * A0 established the layering/include graph; A0b exposes the CORE LAW here:
 * the authority/coordinate primitives any module uses to ask the kernel, plus
 * the shared refusal. This is deliberately a SLICE of the governor, not all
 * of it. Deferred until the command layer becomes modules, and added one hook
 * at a time when an extracted module actually calls one:
 *
 *   module set   st_mod_loaded / st_mod_add / st_mod_del,
 *                st_region_load / st_region_unload
 *   bans         st_ban_check / st_ban_put / st_ban_del / st_ban_lookup /
 *                st_ban_refuse
 *   selection    st_target_get / st_target_set / st_target_clear /
 *                st_target_or_position / st_cmd_world
 *
 * Never hooks: st_can (it is st_owner()==ref), st_ban_at (the unmasked twin of
 * st_ban_check), st_ban_place (string formatting), st_cmd_region (takes a
 * char *argv[], which XY cannot express), the st_row_* persistence layer, and
 * the boot-only st_ban_init/st_ban_migrate/st_rec_register.
 */

#include <stddef.h>
#include <stdint.h>
#include <ttypt/xy.h>

/* ------------------------------------------------------------ coordinate */

/* (id, plen) of the region a player stands in: the room's pos_t through
 * pos_morton, then the deepest covering region. XY_ERR_INVALID/NOTFOUND if
 * the out-params cannot be filled. */
XY_DECL(int, st_region_of_player, unsigned, player_ref, uint64_t *, id,
	uint8_t *, plen);

/* (id, plen) of the region containing an arbitrary object: walk containment
 * up to the first room, then take that room's morton code. */
XY_DECL(int, st_region_of_obj, unsigned, ref, uint64_t *, id,
	uint8_t *, plen);

/* ------------------------------------------------------------- authority */

/* The owner ref of exactly (id, plen), or NOTHING if the row has no owner or
 * the table is unavailable. One-field read — up to 65 regions can cover a
 * point, so this must not copy a whole row. */
XY_DECL(unsigned, st_owner, uint64_t, id, uint8_t, plen);

/* The authorization rule: may player_ref act on (id, plen)? True when they
 * rule that region or any prefix ancestor of it. */
XY_DECL(int, st_can_region, unsigned, player_ref, uint64_t, id, uint8_t, plen);

/* Who governs (id, plen)? The deepest ancestor row with an owner; NOTHING
 * means no ancestor claims it ("unclaimed", a different refusal). */
XY_DECL(unsigned, st_region_ruler, uint64_t, id, uint8_t, plen);

#define ST_PLEN_ROOT  0
#define ST_SEL_UNSET  255

/* Is (id, plen) inside the actor's authority? With sel_plen != ST_SEL_UNSET:
 * the selected region and everything under it. With no selection: the union
 * of every region the actor owns. */
XY_DECL(int, st_in_scope, unsigned, actor, uint64_t, sel_id, uint8_t, sel_plen,
	uint64_t, id, uint8_t, plen);

/* Pure prefix containment: (o_id,o_plen) covers (i_id,i_plen) iff
 * o_plen <= i_plen and masking i_id to o_plen yields o_id. */
XY_DECL(int, st_region_covers, uint64_t, o_id, uint8_t, o_plen, uint64_t, i_id,
	uint8_t, i_plen);

/* --------------------------------------------------------------- refusal */

/* The shared region refusal: names the ruler (or says the area is unclaimed)
 * and flushes. Engine body is void; the hook returns XY_OK. */
XY_DECL(int, st_refuse_region, unsigned, player_ref, uint64_t, id, uint8_t, plen);

/* -------------------------------------------------------------- presence */

XY_DECL(int, presence_refresh, unsigned, ref);
XY_DECL(int, presence_forget, unsigned, ref);

#endif /* ND_REGIONS_H */
