#include <ttypt/xy-mod.h>

#include "../auth/auth.h"

#include "common_encoding.c"
#include "common_response.c"
#include "common_storage.c"
#include "common_strlist.c"
#include "bud_adapter.c"
#include "ux/site_ui.c"

XY_MODULE_API void xy_install(void)
{
	xy_load("./mods/mpfd/mpfd");
}
