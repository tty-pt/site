#include <ttypt/xy-mod.h>
#include <nd/law.h>
#include <nd/world-types.h>
#include <nd/regions.h>

void eng_fbcp_item(unsigned player_ref, unsigned obj_ref, unsigned char dynflags);
void eng_fbcp(unsigned player_ref, size_t len, unsigned char iden, void *data);
void eng_mcp_content_out(unsigned loc_ref, unsigned thing_ref);
void eng_mcp_content_in(unsigned loc_ref, unsigned thing_ref);
void eng_mcp_bar(unsigned char iden, unsigned player_ref, unsigned short val, unsigned short max);
void eng_mcp_auth_fail(int descr, int reason);
void eng_mcp_auth_success(unsigned player_ref);
void eng_mcp_tod(unsigned player_ref, unsigned tod);
void eng_fbcp_view_buf(unsigned player_ref, char *view);
void eng_mcp_action(unsigned player_ref, unsigned id, char *label, char *icon);

XY_IMPL(int, fbcp_item, unsigned, player_ref, unsigned, obj_ref, unsigned char, dynflags)
{
	eng_fbcp_item(player_ref, obj_ref, dynflags);
	return 0;
}

XY_IMPL(int, fbcp, unsigned, player_ref, size_t, len, unsigned char, iden, void *, data)
{
	eng_fbcp(player_ref, len, iden, data);
	return 0;
}

XY_IMPL(int, mcp_content_out, unsigned, loc_ref, unsigned, thing_ref)
{
	eng_mcp_content_out(loc_ref, thing_ref);
	return 0;
}

XY_IMPL(int, mcp_content_in, unsigned, loc_ref, unsigned, thing_ref)
{
	eng_mcp_content_in(loc_ref, thing_ref);
	return 0;
}

XY_IMPL(int, mcp_bar, unsigned char, iden, unsigned, player_ref, unsigned short, val, unsigned short, max)
{
	eng_mcp_bar(iden, player_ref, val, max);
	return 0;
}

XY_IMPL(int, mcp_auth_fail, int, descr, int, reason)
{
	eng_mcp_auth_fail(descr, reason);
	return 0;
}

XY_IMPL(int, mcp_auth_success, unsigned, player_ref)
{
	eng_mcp_auth_success(player_ref);
	return 0;
}

XY_IMPL(int, mcp_tod, unsigned, player_ref, unsigned, tod)
{
	eng_mcp_tod(player_ref, tod);
	return 0;
}

XY_IMPL(int, fbcp_view_buf, unsigned, player_ref, char *, view)
{
	eng_fbcp_view_buf(player_ref, view);
	return 0;
}

XY_IMPL(int, mcp_action, unsigned, player_ref, unsigned, id, char *, label, char *, icon)
{
	eng_mcp_action(player_ref, id, label, icon);
	return 0;
}

XY_MODULE_API void
xy_install(void)
{
	WARN("nd-mcp: client binary protocol module installed from '%s'\n", xy.module_path);
}
