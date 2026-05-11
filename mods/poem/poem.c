#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

#include <ttypt/ndx-mod.h>
#include <ttypt/ndc.h>
#include <ttypt/qmap.h>

#include "../index/index.h"
#include "../common/common.h"

#define POEM_ITEMS_PATH "items/poem/items"

static unsigned index_hd;

void ndx_install(void)
{
	ndx_load("./mods/auth/auth");
	ndx_load("./mods/index/index");
	ndx_load("./mods/common/common");

	index_hd = index_open("Poem", 0, 1, NULL, NULL, NULL, NULL, NULL);

	{
		static const dataset_field_t fields[] = {
			{ "id", NULL, DATASET_FIELD_STRING, 0 },
			{ "title", "title", DATASET_FIELD_STRING, 1 },
			{ "owner", "owner", DATASET_FIELD_STRING, 0 },
			{ "body_content",
			  "pt_PT.html",
			  DATASET_FIELD_STRING,
			  1 }
		};

		dataset_def_t def = { .id = "poem.items",
			              .key_field = "id",
			              .items_path = "items/poem/items",
			              .access_policy = DATASET_ACCESS_PUBLIC,
			              .fields = fields,
			              .field_count = sizeof(fields) /
			                             sizeof(fields[0]),
			              .source_hd = index_hd };

		dataset_register(&def);
	}
}
