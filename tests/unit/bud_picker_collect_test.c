#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <ttypt/qmap.h>
#include <hyle/schema.h>
#include <hyle/picker.h>
#include <hyle/source.h>
#include <hyle-source/hyle_source.h>
#include <hyle-bud/hyle-bud.h>

#define CHECK(label, condition)                                                \
	do {                                                                   \
		if (condition)                                                 \
			printf("PASS %s\n", label);                            \
		else {                                                         \
			printf("FAIL %s\n", label);                            \
			failures++;                                            \
		}                                                              \
	} while (0)

static int failures = 0;

/* Category dataset definitions */
static const hyle_source_field_t cat_fields[] = {
	{ .name = "id", .type = HYLE_FIELD_STRING, .writable = 0 },
	{ .name = "title", .type = HYLE_FIELD_STRING, .writable = 1 },
	{ 0 }
};

static const hyle_source_desc_t cat_desc[] = {
	{ .key = "id", .qm_type = BUD_QM_STR, .writable = 0 },
	{ .key = "title", .qm_type = BUD_QM_STR, .writable = 1 },
	{ 0 }
};

/* Item dataset struct and schema */
typedef struct {
	char id[64];
	char title[128];
	char category[64];
	char tags[256];
} item_record_t;

static const hyle_source_desc_t item_desc[] = {
	{ .key = "id", .offset = offsetof(item_record_t, id), .size = sizeof(((item_record_t *)0)->id), .qm_type = BUD_QM_STR, .writable = 0 },
	{ .key = "title", .offset = offsetof(item_record_t, title), .size = sizeof(((item_record_t *)0)->title), .qm_type = BUD_QM_STR, .writable = 1 },
	{ .key = "category", .offset = offsetof(item_record_t, category), .size = sizeof(((item_record_t *)0)->category), .qm_type = BUD_QM_STR, .type = HYLE_FIELD_REFERENCE, .ref_source = "test.cats", .writable = 1 },
	{ .key = "tags", .offset = offsetof(item_record_t, tags), .size = sizeof(((item_record_t *)0)->tags), .qm_type = BUD_QM_STR, .type = HYLE_FIELD_MULTI_REFERENCE, .ref_source = "test.cats", .writable = 1 },
	{ 0 }
};

static void setup_test_data(void)
{
	hyle_source_def_t def = {
		.id = "test.cats",
		.key_field = "id",
		.items_path = "test.cats",
		.fields = cat_fields,
		.field_count = 2,
		.defs = cat_desc,
		.def_count = 2,
		.store = hyle_source_store_mem()
	};
	hyle_source_register_def(&def);

	const char *names1[] = { "title" };
	const char *vals1[] = { "Rock & Roll" };
	hyle_source_put("test.cats", "rock", names1, vals1, 1);

	const char *names2[] = { "title" };
	const char *vals2[] = { "Jazz Classics" };
	hyle_source_put("test.cats", "jazz", names2, vals2, 1);

	const char *names3[] = { "title" };
	const char *vals3[] = { "Pop Hits" };
	hyle_source_put("test.cats", "pop", names3, vals3, 1);
}

int main(void)
{
	hyle_bud_picker_view_t pv;
	item_record_t rec;
	memset(&rec, 0, sizeof(rec));
	snprintf(rec.category, sizeof(rec.category), "rock");
	snprintf(rec.tags, sizeof(rec.tags), "rock,jazz");

	setup_test_data();

	/* 1. Test picker view collection from query string and schema */
	const char *qs = "pick_q_category=jaz&pick_page_category=0";
	int n = hyle_bud_picker_view_collect_schema(qs, item_desc, &rec, &pv, NULL);

	CHECK("collected 2 picker entries", n == 2 && pv.n == 2);
	if (n >= 2) {
		CHECK("entry 0 is category", strcmp(pv.entries[0].key, "category") == 0);
		CHECK("entry 0 query is jaz", strcmp(pv.entries[0].q, "jaz") == 0);
		CHECK("entry 0 filtered 1 page opt", pv.entries[0].npage == 1);
		if (pv.entries[0].npage == 1) {
			CHECK("entry 0 opt is jazz", strcmp(pv.entries[0].page_opts[0].id, "jazz") == 0);
		}
		CHECK("entry 0 selected rock", pv.entries[0].nsel == 1 && strcmp(pv.entries[0].sel[0].id, "rock") == 0);
		CHECK("entry 0 selected label resolved", strcmp(pv.entries[0].sel[0].label, "Rock & Roll") == 0);

		CHECK("entry 1 is tags", strcmp(pv.entries[1].key, "tags") == 0);
		CHECK("entry 1 multi is 1", pv.entries[1].multi == 1);
		CHECK("entry 1 selected 2 tags", pv.entries[1].nsel == 2);
	}

	/* 2. Test auto fields collection: indexed param */
	{
		int field_idx = -1, scope = -1;
		hyle_bud_picker_view_t auto_pv;
		const char *auto_qs = "pick_q_category_3=jaz&pick_page_category_3=0";
		int res = hyle_bud_picker_view_collect_auto_fields_schema(
		        auto_qs, item_desc, &auto_pv, &field_idx, &scope);
		CHECK("auto-fields matched indexed param", res == 1);
		CHECK("auto-fields field index is 2 (category)", field_idx == 2);
		CHECK("auto-fields scope is 3", scope == 3);
		CHECK("auto-pv entry key is category_3", strcmp(auto_pv.entries[0].key, "category_3") == 0);
		CHECK("auto-pv filtered 1 option", auto_pv.entries[0].npage == 1);
	}

	/* 3. Test auto fields collection: scoped double-underscore param */
	{
		int field_idx = -1, scope = -1;
		hyle_bud_picker_view_t auto_pv;
		const char *auto_qs = "pick_q_tags__2=pop";
		int res = hyle_bud_picker_view_collect_auto_fields_schema(
		        auto_qs, item_desc, &auto_pv, &field_idx, &scope);
		CHECK("auto-fields matched scoped param", res == 1);
		CHECK("auto-fields field index is 3 (tags)", field_idx == 3);
		CHECK("auto-fields scope is 2", scope == 2);
		CHECK("auto-pv entry key is tags", strcmp(auto_pv.entries[0].key, "tags") == 0);
	}

	/* 4. Test auto fields collection: ?replace=5 */
	{
		int field_idx = -1, scope = -1;
		hyle_bud_picker_view_t auto_pv;
		const char *auto_qs = "replace=5";
		int res = hyle_bud_picker_view_collect_auto_fields_schema(
		        auto_qs, item_desc, &auto_pv, &field_idx, &scope);
		CHECK("auto-fields matched replace param", res == 2);
		CHECK("auto-fields scope is 5", scope == 5);
	}

	/* 5. Test auto fields collection: no match */
	{
		int field_idx = -1, scope = -1;
		hyle_bud_picker_view_t auto_pv;
		const char *auto_qs = "foo=bar&baz=1";
		int res = hyle_bud_picker_view_collect_auto_fields_schema(
		        auto_qs, item_desc, &auto_pv, &field_idx, &scope);
		CHECK("auto-fields no match returns 0", res == 0);
		CHECK("auto-fields field_idx remains -1", field_idx == -1);
		CHECK("auto-fields scope remains -1", scope == -1);
	}

	printf("\nbud_picker_collect_test: %s\n", failures == 0 ? "ALL PASS" : "SOME FAILURES");
	return failures ? 1 : 0;
}
