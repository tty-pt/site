#include <ttypt/ndx-mod.h>

#include "../auth/auth.h"

#include "common_encoding.c"
#include "common_json.c"
#include "common_response.c"
#include "common_storage.c"
#include "common_strlist.c"

MODULE_API void ndx_install(void)
{
	ndx_load("./mods/mpfd/mpfd");
}
