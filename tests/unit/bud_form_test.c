#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <bud/bud.h>
#include <hyle/schema.h>
#include <hyle/picker.h>
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

typedef struct {
	char id[64];
	char title[128];
	char author[128];
	char type[256];
	char data[1024];
	int tempo;
	int is_public;
	char event_date[32];
} test_song_record_t;

static const hyle_schema_desc_t test_song_desc[] = {
	{ .key = "id", .offset = offsetof(test_song_record_t, id), .size = 64, .qm_type = BUD_QM_STR, .writable = 0 },
	{ .key = "title", .offset = offsetof(test_song_record_t, title), .size = 128, .qm_type = BUD_QM_STR, .required = 1, .min_length = 2, .writable = 1 },
	{ .key = "author", .offset = offsetof(test_song_record_t, author), .size = 128, .qm_type = BUD_QM_STR, .writable = 1 },
	{ .key = "type", .offset = offsetof(test_song_record_t, type), .size = 256, .qm_type = BUD_QM_STR, .source_type = HYLE_BUD_MULTI_REFERENCE, .ref_source = "song.types", .writable = 1 },
	{ .key = "tempo", .offset = offsetof(test_song_record_t, tempo), .size = sizeof(int), .is_int = 1, .source_type = HYLE_BUD_INT, .writable = 1 },
	{ .key = "is_public", .offset = offsetof(test_song_record_t, is_public), .size = sizeof(int), .source_type = HYLE_BUD_BOOL, .writable = 1 },
	{ .key = "event_date", .offset = offsetof(test_song_record_t, event_date), .size = 32, .qm_type = BUD_QM_STR, .writable = 1 },
	{ .key = "data", .offset = offsetof(test_song_record_t, data), .size = 1024, .qm_type = BUD_QM_VSTR, .file = "data.txt", .writable = 1 },
	{ 0 }
};

int main(void)
{
	/* 1. Test Add Form (record is NULL) */
	bud_node *add_form = hyle_bud_form(
		test_song_desc, NULL, "/song/add", "/song/", "Add", "csrf-12345", NULL, NULL);

	CHECK("add_form node created", add_form != NULL);
	if (add_form) {
		char *html = bud_render_html(add_form);
		CHECK("html rendered", html != NULL);
		if (html) {
			CHECK("action /song/add present", strstr(html, "action=\"/song/add\"") != NULL);
			CHECK("POST method present", strstr(html, "method=\"POST\"") != NULL);
			CHECK("csrf hidden input present", strstr(html, "value=\"csrf-12345\"") != NULL);
			CHECK("title input present", strstr(html, "name=\"title\"") != NULL);
			CHECK("author input present", strstr(html, "name=\"author\"") != NULL);
			CHECK("data textarea present", strstr(html, "textarea") != NULL && strstr(html, "name=\"data\"") != NULL);
			CHECK("submit button present", strstr(html, "Add") != NULL);
			CHECK("cancel link present", strstr(html, "href=\"/song/\"") != NULL);
			bud_free_string(html);
		}
	}

	/* 2. Test Edit Form (populated record) */
	test_song_record_t rec;
	memset(&rec, 0, sizeof(rec));
	snprintf(rec.title, sizeof(rec.title), "Amazing Grace");
	snprintf(rec.author, sizeof(rec.author), "John Newton");
	rec.tempo = 120;
	rec.is_public = 1;
	snprintf(rec.event_date, sizeof(rec.event_date), "2026-08-29");

	bud_node *edit_form = hyle_bud_form(
		test_song_desc, &rec, "/song/amazing/edit", "/song/amazing", "Save", "csrf-67890", NULL, "C G Am F");

	CHECK("edit_form node created", edit_form != NULL);
	if (edit_form) {
		char *html = bud_render_html(edit_form);
		CHECK("edit html rendered", html != NULL);
		if (html) {
			CHECK("action /song/amazing/edit present", strstr(html, "action=\"/song/amazing/edit\"") != NULL);
			CHECK("title value Amazing Grace present", strstr(html, "value=\"Amazing Grace\"") != NULL);
			CHECK("title required attribute present", strstr(html, "required") != NULL);
			CHECK("title minlength attribute present", strstr(html, "minlength=\"2\"") != NULL);
			CHECK("title maxlength attribute present", strstr(html, "maxlength=\"127\"") != NULL);
			CHECK("author value John Newton present", strstr(html, "value=\"John Newton\"") != NULL);
			CHECK("tempo number input present", strstr(html, "type=\"number\"") != NULL && strstr(html, "name=\"tempo\"") != NULL);
			CHECK("tempo value 120 present", strstr(html, "value=\"120\"") != NULL);
			CHECK("is_public checkbox input present", strstr(html, "type=\"checkbox\"") != NULL && strstr(html, "name=\"is_public\"") != NULL);
			CHECK("is_public checked attribute present", strstr(html, "checked") != NULL);
			CHECK("event_date generic label present", strstr(html, "Event Date:") != NULL);
			CHECK("chords C G Am F in textarea", strstr(html, "C G Am F") != NULL);
			CHECK("submit button Save present", strstr(html, "Save") != NULL);
			bud_free_string(html);
		}
	}

	printf("\nbud_form_test: %s\n", failures == 0 ? "ALL PASS" : "SOME FAILURES");
	return failures ? 1 : 0;
}
