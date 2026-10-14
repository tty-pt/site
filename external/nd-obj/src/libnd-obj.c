#include <ttypt/xy-mod.h>

#include <nd/law.h>
#include <nd/world-types.h>
#include <nd/regions.h>

void do_select(int fd, int argc, char *argv[]);
void do_name(int fd, int argc, char *argv[]);
void do_create(int fd, int argc, char *argv[]);
void do_recycle(int fd, int argc, char *argv[]);
void do_drop(int fd, int argc, char *argv[]);
void do_clone(int fd, int argc, char *argv[]);
void do_chown(int fd, int argc, char *argv[]);
void do_status(int fd, int argc, char *argv[]);
void do_look_at(int fd, int argc, char *argv[]);

int eng_obj_exists(unsigned ref);
unsigned eng_object_new(OBJ *obj);
unsigned eng_object_copy(OBJ *nu, unsigned old_ref);
void eng_object_move(unsigned what_ref, unsigned where_ref);
unsigned eng_object_add(OBJ *nu, unsigned skel_id, unsigned where,
	uint64_t v, unsigned flags);
struct icon eng_object_icon(unsigned player_ref, unsigned thing_ref);
char *eng_object_art(unsigned ref);
const char *eng_unparse(unsigned loc_ref);

unsigned eng_me_get(void);
ENT eng_ent_get(unsigned ref);
void eng_ent_set(unsigned ref, ENT *tmp);
void eng_ent_del(unsigned ref);
int eng_controls(unsigned who_ref, unsigned what_ref);
int eng_payfor(unsigned who_ref, OBJ *who, unsigned cost);
void eng_enter(unsigned player_ref, unsigned loc_ref, int e);
void eng_look_at(unsigned player_ref, unsigned loc_ref);

/* Object hooks */
XY_IMPL(int, obj_exists, unsigned, ref)
{
	return eng_obj_exists(ref);
}

XY_IMPL(unsigned, object_new, OBJ *, obj)
{
	return eng_object_new(obj);
}

XY_IMPL(unsigned, object_copy, OBJ *, nu, unsigned, old_ref)
{
	return eng_object_copy(nu, old_ref);
}

XY_IMPL(int, object_move, unsigned, what_ref, unsigned, where_ref)
{
	eng_object_move(what_ref, where_ref);
	return 0;
}

XY_IMPL(unsigned, object_add, OBJ *, nu, unsigned, skel_id, unsigned, where,
	uint64_t, v)
{
	return eng_object_add(nu, skel_id, where, v, 0);
}

XY_IMPL(struct icon, object_icon, unsigned, player_ref, unsigned, thing_ref)
{
	return eng_object_icon(player_ref, thing_ref);
}

XY_IMPL(char *, object_art, unsigned, ref)
{
	return eng_object_art(ref);
}

XY_IMPL(const char *, unparse, unsigned, loc_ref)
{
	return eng_unparse(loc_ref);
}

/* Entity hooks */
XY_IMPL(unsigned, me_get, unsigned, dummy)
{
	(void)dummy;
	return eng_me_get();
}

XY_IMPL(ENT, ent_get, unsigned, ref)
{
	return eng_ent_get(ref);
}

XY_IMPL(int, ent_set, unsigned, ref, ENT *, tmp)
{
	eng_ent_set(ref, tmp);
	return 0;
}

XY_IMPL(int, ent_del, unsigned, ref)
{
	eng_ent_del(ref);
	return 0;
}

void eng_nd_rwrite(unsigned room_ref, unsigned exception_ref, char *str, size_t len);

XY_IMPL(int, nd_rwrite, unsigned, room_ref, unsigned, exception_ref, char *, str, size_t, len)
{
	eng_nd_rwrite(room_ref, exception_ref, str, len);
	return 0;
}

XY_IMPL(int, ent_reset, ENT *, ent)
{
	(void)ent;
	return 0;
}

XY_IMPL(int, controls, unsigned, who_ref, unsigned, what_ref)
{
	return eng_controls(who_ref, what_ref);
}

XY_IMPL(int, payfor, unsigned, who_ref, OBJ *, who, unsigned, cost)
{
	return eng_payfor(who_ref, who, cost);
}

XY_IMPL(int, look_around, unsigned, player_ref)
{
	(void)player_ref;
	return 0;
}

XY_IMPL(int, enter, unsigned, player_ref, unsigned, loc_ref, int, e)
{
	eng_enter(player_ref, loc_ref, (enum exit)e);
	return 0;
}

XY_IMPL(int, look_at, unsigned, player_ref, unsigned, loc_ref)
{
	eng_look_at(player_ref, loc_ref);
	return 0;
}

void eng_objects_init(void);

XY_IMPL(int, objects_init, unsigned, dummy)
{
	(void)dummy;
	eng_objects_init();
	return 0;
}

XY_MODULE_API void
xy_install(void)
{
	WARN("nd-obj: bootstrap object module installed from '%s'\n",
	     xy.module_path);
	nd_register("select", do_select, 0);
	nd_register("name", do_name, 0);
	nd_register("create", do_create, 0);
	nd_register("recycle", do_recycle, 0);
	nd_register("drop", do_drop, 0);
	nd_register("clone", do_clone, 0);
	nd_register("chown", do_chown, 0);
	nd_register("status", do_status, 0);
	nd_register("look", do_look_at, 0);
}
