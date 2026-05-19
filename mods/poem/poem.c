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
#include "../source/source.h"

#define POEM_ITEMS_PATH "items/poem/items"

typedef struct {
	char id[64];
	char title[256];
	char owner[32];
} poem_item_t;

static uint32_t poem_record_id;

static const source_field_t poem_items_fields[] = {
	{ "id", NULL, SOURCE_FIELD_STRING, 1, NULL, NULL },
	{ "title", "title", SOURCE_FIELD_STRING, 1, NULL, NULL },
	{ "owner", "owner", SOURCE_FIELD_STRING, 0, NULL, NULL },
	{ "body_content", "pt_PT.html", SOURCE_FIELD_STRING, 1, NULL, NULL },
};

static const qmap_record_field_t poem_record_fields[] = {
	{ "id",
	  QM_STR,
	  offsetof(poem_item_t, id),
	  sizeof(((poem_item_t *)0)->id) },
	{ "title",
	  QM_STR,
	  offsetof(poem_item_t, title),
	  sizeof(((poem_item_t *)0)->title) },
	{ "owner",
	  QM_STR,
	  offsetof(poem_item_t, owner),
	  sizeof(((poem_item_t *)0)->owner) },
	{ "body_content", QM_VSTR, 0, 0 },
};

static const source_def_t poem_items_def = {
	.id = "poem.items",
	.key_field = "id",
	.items_path = "items/poem/items",
	.access_policy = SOURCE_ACCESS_PUBLIC,
	.fields = poem_items_fields,
	.field_count = sizeof(poem_items_fields) / sizeof(poem_items_fields[0]),
};

static unsigned index_hd;

void ndx_install(void)
{
	ndx_load("./mods/auth/auth");
	ndx_load("./mods/index/index");
	ndx_load("./mods/common/common");

	poem_record_id = qmap_record_register(
	        "poem",
	        sizeof(poem_item_t),
	        poem_record_fields,
	        sizeof(poem_record_fields) / sizeof(poem_record_fields[0]));

	{
		source_def_t def = poem_items_def;
		def.record_id = poem_record_id;
		source_register(&def);
	}

	index_hd = source_get_data_hd("poem.items");
	index_open(
	        "Poem", 1, index_hd, NULL, core_get, NULL, NULL, NULL);
}
