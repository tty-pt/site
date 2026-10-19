#include <string.h>
#include <ttypt/xy-mod.h>
#include <nd/xy.h>

XY_MODULE_API void xy_install(void)
{
	WARN("testprobe module installed from '%s'\n", xy.module_path);
}

XY_IMPL(int, on_enter, unsigned, player_ref, unsigned, loc_ref)
{
	unsigned h = nd_open("testprobe_ent", "u", "u", 0);
	OBJ obj;
	int rc;

	(void)loc_ref;
	memset(&obj, 0, sizeof(obj));
	rc = nd_get(HD_OBJ, &obj, &player_ref);
	nd_printf(player_ref, "[testprobe] on_enter %s hd=%s\n",
		(rc == 0 && obj.name[0]) ? obj.name : "(unresolved)",
		nd_hd_is_mod(h) ? "tagged" : "UNTAGGED");
	WARN("testprobe on_enter: HD_OBJ=%s hd=%s\n",
		(rc == 0 && obj.name[0]) ? "resolved (ok)" : "NOT RESOLVED (bug)",
		nd_hd_is_mod(h) ? "tagged" : "UNTAGGED");
	return 0;
}
