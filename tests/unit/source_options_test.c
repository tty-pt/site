#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <ttypt/qmap.h>
#include <hyle/schema.h>
#include <hyle/picker.h>
#include <hyle/source.h>
#include <hyle-source/hyle_source.h>

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

/* Test data definitions */
static const hyle_source_field_t tag_fields[] = {
	{ .name = "id", .type = HYLE_FIELD_STRING, .writable = 0 },
	{ .name = "title", .type = HYLE_FIELD_STRING, .writable = 1 },
	{ .name = "color", .type = HYLE_FIELD_STRING, .writable = 1 },
	{ 0 }
};

static const hyle_source_desc_t tag_desc[] = {
	{ .key = "id", .qm_type = BUD_QM_STR, .writable = 0 },
	{ .key = "title", .qm_type = BUD_QM_STR, .writable = 1 },
	{ .key = "color", .qm_type = BUD_QM_STR, .writable = 1 },
	{ 0 }
};

static void setup_test_data(void)
{
	hyle_source_def_t def = {
		.id = "test.tags",
		.key_field = "id",
		.items_path = "test.tags",
		.fields = tag_fields,
		.field_count = 3,
		.defs = tag_desc,
		.def_count = 3,
		.store = hyle_source_store_mem()
	};
	hyle_source_register_def(&def);

	/* Populate some records */
	const char *names1[] = { "title", "color" };
	const char *vals1[] = { "Rock & Roll", "red" };
	hyle_source_put("test.tags", "rock", names1, vals1, 2);

	const char *names2[] = { "title", "color" };
	const char *vals2[] = { "Jazz Classics", "blue" };
	hyle_source_put("test.tags", "jazz", names2, vals2, 2);

	const char *names3[] = { "title", "color" };
	const char *vals3[] = { "Pop Hits", "yellow" };
	hyle_source_put("test.tags", "pop", names3, vals3, 2);
}

int main(void)
{
	char display_field[64];
	char id_buf[16][64];
	char label_buf[16][256];
	hyle_option_t opts[16];
	int total = 0;
	int count = 0;

	setup_test_data();

	/* 1. Test display field discovery */
	display_field[0] = '\0';
	int rc = hyle_source_get_display_field("test.tags", display_field, sizeof(display_field));
	CHECK("display field found", rc == 0);
	CHECK("display field is title", strcmp(display_field, "title") == 0);

	/* 2. Test get item label */
	char label[128] = { 0 };
	const char *lbl = hyle_source_get_item_label("test.tags", "rock", display_field, label, sizeof(label));
	CHECK("rock label resolved", lbl && strcmp(lbl, "Rock & Roll") == 0);

	/* 3. Test resolve options (query search) */
	count = hyle_source_resolve_options(
		"test.tags", "Jazz", 0, 10, opts, 16, &total, id_buf, label_buf);
	CHECK("resolve_options found 1 match for Jazz", count == 1 && total == 1);
	if (count > 0) {
		CHECK("jazz id matches", strcmp(opts[0].id, "jazz") == 0);
		CHECK("jazz label matches", strcmp(opts[0].label, "Jazz Classics") == 0);
	}

	/* 4. Test resolve tokens / comma slugs */
	count = hyle_source_resolve_tokens(
		"test.tags", "rock, jazz", opts, 16, id_buf, label_buf);
	CHECK("resolve_tokens found 2 items", count == 2);
	if (count == 2) {
		CHECK("rock slug resolved", strcmp(opts[0].id, "rock") == 0 && strcmp(opts[0].label, "Rock & Roll") == 0);
		CHECK("jazz slug resolved", strcmp(opts[1].id, "jazz") == 0 && strcmp(opts[1].label, "Jazz Classics") == 0);
	}

	/* 5. Test normalize tokens to slugs */
	char norm[256] = { 0 };
	hyle_source_normalize_tokens_to_slugs("test.tags", "rock\njazz, pop", norm, sizeof(norm));
	CHECK("normalize tokens to slugs", strcmp(norm, "rock,jazz,pop") == 0);

	/* 6. Test get enum options (all records) */
	count = hyle_source_get_enum_options("test.tags", opts, 16, id_buf, label_buf);
	CHECK("get_enum_options returns 3 items", count == 3);

	printf("\nsource_options_test: %s\n", failures == 0 ? "ALL PASS" : "SOME FAILURES");
	return failures ? 1 : 0;
}
