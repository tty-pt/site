#include <ttypt/xy-mod.h>
#include <nd/law.h>
#include <nd/world-types.h>
#include <nd/regions.h>

void do_view(int fd, int argc, char *argv[]);
void base_vtf_init(void);
void eng_view(unsigned player_ref);

unsigned eng_action_register(char *label, char *icon);
unsigned eng_vtf_register(char emp, enum color fg, unsigned flags);

XY_IMPL(unsigned, action_register, char *, label, char *, icon)
{
	return eng_action_register(label, icon);
}

XY_IMPL(unsigned, vtf_register, char, emp, int, fg, unsigned, flags)
{
	return eng_vtf_register(emp, (enum color)fg, flags);
}

XY_IMPL(int, nd_view, unsigned, player_ref)
{
	eng_view(player_ref);
	return 0;
}

XY_MODULE_API void
xy_install(void)
{
	WARN("nd-view: ANSI map viewport module installed from '%s'\n", xy.module_path);
	base_vtf_init();
	nd_register("view", do_view, 0);
}
