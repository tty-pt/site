#include <ttypt/xy-mod.h>

#include "../auth/auth.h"

#include "common_encoding.c"
#include "common_response.c"
#include "common_storage.c"
#include "common_strlist.c"
#include "ux/site_ui.c"

#include "dict.h"

XY_MODULE_API void xy_install(void)
{
	xy_load("./mods/i18n/i18n");
	xy_load("./mods/mpfd/mpfd");

	i18n_register_dict(common_dict, COMMON_DICT_COUNT);
}
