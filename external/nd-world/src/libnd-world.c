#include <ttypt/xy-mod.h>
#include <nd/law.h>
#include <nd/world-types.h>
#include <nd/regions.h>

void do_room(int fd, int argc, char *argv[]);
void do_teleport(int fd, int argc, char *argv[]);
void map_init(void);

int eng_map_has(unsigned thing);
morton_t eng_map_mwhere(unsigned thing);
void eng_map_where(pos_t p, unsigned thing);
int eng_map_delete(unsigned what);
unsigned eng_map_get(pos_t p);
void eng_map_search(unsigned *mat, pos_t pos, unsigned radius);
void noise_view(struct bio *to, pos_t pos);
void eng_st_teleport(unsigned player_ref, uint64_t pos);
struct bio eng_noise_point(coord_t *p);
unsigned eng_room_clean(unsigned loc_ref);
int eng_st_v(unsigned player_ref, const char *dir);
void eng_object_drop(unsigned where_ref, unsigned skel_id);
int eng_st_ban_check(unsigned player_ref, uint64_t code, uint64_t *ban_id, uint8_t *ban_plen);

XY_IMPL(int, map_has, unsigned, thing)
{
	return eng_map_has(thing);
}

XY_IMPL(morton_t, map_mwhere, unsigned, thing)
{
	return eng_map_mwhere(thing);
}

XY_IMPL(int, map_where, coord_t *, p, unsigned, thing)
{
	eng_map_where(p, thing);
	return 0;
}

XY_IMPL(int, map_delete, unsigned, what)
{
	return eng_map_delete(what);
}

XY_IMPL(unsigned, map_get, coord_t *, p)
{
	return eng_map_get(p);
}

XY_IMPL(int, map_search, unsigned *, mat, coord_t *, pos, unsigned, radius)
{
	eng_map_search(mat, pos, radius);
	return 0;
}

XY_IMPL(int, nd_noise_view, struct bio *, to, coord_t *, pos)
{
	noise_view(to, pos);
	return 0;
}

XY_IMPL(int, st_teleport, unsigned, player_ref, uint64_t, pos)
{
	eng_st_teleport(player_ref, pos);
	return 0;
}

XY_IMPL(struct bio, noise_point, coord_t *, p)
{
	return eng_noise_point(p);
}

XY_IMPL(unsigned, room_clean, unsigned, loc_ref)
{
	return eng_room_clean(loc_ref);
}

XY_IMPL(int, st_v, unsigned, player_ref, const char *, dir)
{
	return eng_st_v(player_ref, dir);
}

XY_IMPL(int, object_drop, unsigned, where_ref, unsigned, skel_id)
{
	eng_object_drop(where_ref, skel_id);
	return 0;
}

XY_IMPL(int, st_ban_check, unsigned, player_ref, uint64_t, code, uint64_t *, ban_id, uint8_t *, ban_plen)
{
	return eng_st_ban_check(player_ref, code, ban_id, ban_plen);
}

XY_MODULE_API void
xy_install(void)
{
	WARN("nd-world: 4D world and physics module installed from '%s'\n", xy.module_path);
	map_init();
	nd_register("room", do_room, 0);
	nd_register("teleport", do_teleport, 0);
}
