#include <ttypt/ndx-mod.h>

#include "common_dataset.c"
#include "common_encoding.c"
#include "common_form.c"
#include "common_json.c"
#include "common_post.c"
#include "common_response.c"
#include "common_storage.c"

MODULE_API void ndx_install(void)
{
	ndx_load("./mods/mpfd/mpfd");
	dataset_install_routes();
}
