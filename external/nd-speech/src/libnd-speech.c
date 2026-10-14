#include <ttypt/xy-mod.h>
#include <nd/law.h>
#include <nd/world-types.h>
#include <nd/regions.h>

void do_say(int fd, int argc, char *argv[]);
void do_pose(int fd, int argc, char *argv[]);
void do_wall(int fd, int argc, char *argv[]);
void do_examine(int fd, int argc, char *argv[]);
void do_inventory(int fd, int argc, char *argv[]);
void do_owned(int fd, int argc, char *argv[]);

unsigned eng_ematch_at(unsigned player_ref, unsigned where_ref, char *name);
unsigned eng_ematch_player(char *name);
unsigned eng_ematch_absolute(char *name);
unsigned eng_ematch_me(unsigned player_ref, char *str);
unsigned eng_ematch_here(unsigned player_ref, char *str);
unsigned eng_ematch_mine(unsigned player_ref, char *str);
unsigned eng_ematch_near(unsigned player_ref, char *str);
unsigned eng_ematch_all(unsigned player_ref, char *name);

XY_IMPL(unsigned, ematch_at, unsigned, player_ref, unsigned, where_ref, char *, name)
{
	return eng_ematch_at(player_ref, where_ref, name);
}

XY_IMPL(unsigned, ematch_player, char *, name)
{
	return eng_ematch_player(name);
}

XY_IMPL(unsigned, ematch_absolute, char *, name)
{
	return eng_ematch_absolute(name);
}

XY_IMPL(unsigned, ematch_me, unsigned, player_ref, char *, str)
{
	return eng_ematch_me(player_ref, str);
}

XY_IMPL(unsigned, ematch_here, unsigned, player_ref, char *, str)
{
	return eng_ematch_here(player_ref, str);
}

XY_IMPL(unsigned, ematch_mine, unsigned, player_ref, char *, str)
{
	return eng_ematch_mine(player_ref, str);
}

XY_IMPL(unsigned, ematch_near, unsigned, player_ref, char *, str)
{
	return eng_ematch_near(player_ref, str);
}

XY_IMPL(unsigned, ematch_all, unsigned, player_ref, char *, name)
{
	return eng_ematch_all(player_ref, name);
}

XY_MODULE_API void
xy_install(void)
{
	WARN("nd-speech: communication and inspection module installed from '%s'\n", xy.module_path);
	nd_register("say", do_say, 0);
	nd_register("pose", do_pose, 0);
	nd_register("wall", do_wall, 0);
	nd_register("examine", do_examine, 0);
	nd_register("inventory", do_inventory, 0);
	nd_register("owned", do_owned, 0);
}
