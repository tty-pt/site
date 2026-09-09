#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <bud/bud.h>
#include <bud/bud_jsx.h>
#include <hyle/schema.h>
#include <hyle/picker.h>
#include <hyle/source.h>
#include <hyle-bud/hyle-bud.h>
#include <hyle-source/hyle_source.h>
#include <ttypt/axil.h>
#include <ttypt/qmap.h>
#include <ttypt/xy.h>
#include "mods/common/common.h"
#include "mods/source/source.h"

#define CHECK(label, condition)                                                \
	do {                                                                   \
		if (condition)                                                 \
			printf("PASS: %s\n", label);                           \
		else {                                                         \
			printf("FAIL: %s (line %d)\n", label, __LINE__);        \
			failures++;                                            \
		}                                                              \
	} while (0)

static int failures = 0;

typedef struct {
	char name[64];
	int age;
} test_person_t;

static const bud_field_desc_t person_fields[] = {
	{ .key = "name", .offset = offsetof(test_person_t, name), .size = 64, .is_int = 0 },
	{ .key = "age", .offset = offsetof(test_person_t, age), .size = sizeof(int), .is_int = 1 },
	{ 0 }
};

struct fe_test_ctx {
	int count;
	char titles[4][32];
};

static void test_ordered_each_cb(
        int idx, const char *key, unsigned fhd, void *user)
{
	(void)idx;
	struct fe_test_ctx *c = user;
	const char *t = hyle_qmap_get_field_str(fhd, key, "title");
	if (t && c->count < 4)
		snprintf(c->titles[c->count++], 32, "%s", t);
}

typedef struct {
	char name[64];
} test_band_rec_t;

typedef struct {
	char title[64];
	char band[64];
} test_concert_rec_t;

struct ref_cb_ctx {
	int count;
	char ids[4][32];
};

static void test_referencing_cb(const char *id, void *user)
{
	struct ref_cb_ctx *c = user;
	if (id && c->count < 4)
		snprintf(c->ids[c->count++], 32, "%s", id);
}

int main(void)
{
	setbuf(stdout, NULL);
	printf("=== Testing new external abstractions and site simplification ===\n");

	/* 1. Bud DOM constructors */
	{
		bud_node *hidden = bud_hidden_input("song_id", "s42");
		char *html = bud_render_html(hidden);
		CHECK("bud_hidden_input produces hidden input tag",
		      html && strstr(html, "type=\"hidden\"") &&
		      strstr(html, "name=\"song_id\"") &&
		      strstr(html, "value=\"s42\""));
		bud_free_string(html);
		bud_free(hidden);

		bud_node *hidden_int = bud_hidden_input_int("n", 7);
		html = bud_render_html(hidden_int);
		CHECK("bud_hidden_input_int serializes integer value",
		      html && strstr(html, "type=\"hidden\"") &&
		      strstr(html, "name=\"n\"") &&
		      strstr(html, "value=\"7\""));
		bud_free_string(html);
		bud_free(hidden_int);

		bud_node *btn = bud_submit_btn("Save Song", "btn btn-primary");
		html = bud_render_html(btn);
		CHECK("bud_submit_btn produces submit button with text and class",
		      html && strstr(html, "type=\"submit\"") &&
		      strstr(html, "class=\"btn btn-primary\"") &&
		      strstr(html, "Save Song"));
		bud_free_string(html);
		bud_free(btn);

		bud_node *link = bud_link("/song/123", "My Song", "item-link");
		html = bud_render_html(link);
		CHECK("bud_link produces anchor tag with href and label",
		      html && strstr(html, "href=\"/song/123\"") &&
		      strstr(html, "class=\"item-link\"") &&
		      strstr(html, "My Song"));
		bud_free_string(html);
		bud_free(link);
	}

	/* 2. JSX macros integration */
	{
		bud_node *form = lx_n("form", lx_attr("action", "/test"),
		                      lx_hidden("csrf_token", "token123"),
		                      lx_hidden_int("row", 5),
		                      lx_submit("Submit", "btn"),
		                      lx_link("/back", "Cancel", "btn-sec"));
		char *html = bud_render_html(form);
		CHECK("JSX tree renders hidden inputs, submit button, and link",
		      html && strstr(html, "value=\"token123\"") &&
		      strstr(html, "value=\"5\"") &&
		      strstr(html, "Submit") &&
		      strstr(html, "Cancel"));
		bud_free_string(html);
		bud_free(form);
	}

	/* 3. bud_state_apply_len negative length auto-calculation */
	{
		test_person_t p;
		memset(&p, 0, sizeof(p));
		const char *json = "{\"name\":\"Bob\",\"age\":30}";
		/* Pass len = -1: should automatically compute strlen(json) */
		bud_state_apply_len(&p, person_fields, json, (size_t)-1);
		CHECK("bud_state_apply_len handles (size_t)-1 gracefully",
		      strcmp(p.name, "Bob") == 0 && p.age == 30);
	}

	/* 4. Libhyle-bud action picker component */
	{
		const char *pref_names[] = { "zoom", "transpose" };
		int pref_vals[] = { 100, 2 };
		hyle_bud_action_picker_spec_t spec = {
			.key = "song_id",
			.label = "Song",
			.target = "song.items",
			.get_action = "/gig/g1",
			.post_action = "/api/gig/g1/song/add",
			.form_id = "test-pick-form",
			.csrf_token = "csrf-secret-token",
			.submit_label = "Add Song",
			.scope = "0",
			.auto_submit = 1,
			.pref_names = pref_names,
			.pref_vals = pref_vals,
			.n_prefs = 2
		};

		bud_node *picker_node = hyle_bud_action_picker(&spec, NULL);
		CHECK("hyle_bud_action_picker returns non-NULL node", picker_node != NULL);
		if (picker_node) {
			char *html = bud_render_html(picker_node);
			CHECK("hyle_bud_action_picker contains sibling form",
			      html && strstr(html, "class=\"pick-sibling-form\"") != NULL);
			CHECK("hyle_bud_action_picker preserves GET preferences in hidden inputs",
			      html && strstr(html, "name=\"zoom\"") && strstr(html, "value=\"100\"") &&
			      strstr(html, "name=\"transpose\"") && strstr(html, "value=\"2\""));
			CHECK("hyle_bud_action_picker contains POST form with CSRF and submit button",
			      html && strstr(html, "id=\"test-pick-form\"") &&
			      strstr(html, "value=\"csrf-secret-token\"") &&
			      strstr(html, "Add Song"));
			bud_free_string(html);
			bud_free(picker_node);
		}
	}

	/* 5. Axil param, req_param, and boolean extraction */
	{
		axil_query_parse("item_id=abc456&count=42&zero=0&neg=-5&active=1&disabled=false&on_flag=on&off_flag=off");
		char val[64] = { 0 };
		int rc = axil_param(-1, "item_id", val, sizeof(val));
		CHECK("axil_param fetches parsed query param",
		      rc > 0 && strcmp(val, "abc456") == 0);

		int count = axil_param_int(-1, "count", 0);
		CHECK("axil_param_int parses positive integer", count == 42);

		int zero = axil_param_int(-1, "zero", -1);
		CHECK("axil_param_int parses zero correctly", zero == 0);

		int neg = axil_param_int(-1, "neg", 0);
		CHECK("axil_param_int parses negative integer", neg == -5);

		int def = axil_param_int(-1, "nonexistent", 999);
		CHECK("axil_param_int returns default for nonexistent key", def == 999);

		/* Boolean flags */
		CHECK("axil_param_bool parses '1'", axil_param_bool(-1, "active", 0) == 1);
		CHECK("axil_param_bool parses 'false'", axil_param_bool(-1, "disabled", 1) == 0);
		CHECK("axil_param_bool parses 'on'", axil_param_bool(-1, "on_flag", 0) == 1);
		CHECK("axil_param_bool parses 'off'", axil_param_bool(-1, "off_flag", 1) == 0);
		CHECK("axil_param_bool default fallback", axil_param_bool(-1, "missing", 1) == 1);

		/* Auto-parsing body via axil_req_param */
		char body_buf[64] = { 0 };
		const char *post_body = "body_field=hello_world&body_num=123&body_flag=true";
		int rcb = axil_req_param(-1, post_body, "body_field", body_buf, sizeof(body_buf));
		CHECK("axil_req_param auto-parses body", rcb > 0 && strcmp(body_buf, "hello_world") == 0);
		CHECK("axil_req_param_int parses from body", axil_req_param_int(-1, post_body, "body_num", 0) == 123);
		CHECK("axil_req_param_bool parses from body", axil_req_param_bool(-1, post_body, "body_flag", 0) == 1);
	}

	/* 6. Axil response shortcuts */
	{
		int rc200 = axil_respond_json(-1, 200, "{\"status\":\"ok\"}");
		CHECK("axil_respond_json 200 returns 0", rc200 == 0);

		int rc404 = axil_respond_json(-1, 404, "{\"error\":\"not found\"}");
		CHECK("axil_respond_json 404 returns 1", rc404 == 1);

		int rcnoc = axil_respond_no_content(-1);
		CHECK("axil_respond_no_content returns 0", rcnoc == 0);

		int rcjok = axil_respond_json_ok(-1);
		CHECK("axil_respond_json_ok returns 0", rcjok == 0);
	}

	/* 7. Libhyle-source ordered partition operations */
	{
		static const hyle_field_t ord_fields[] = {
			{ .name = "title", .type = HYLE_FIELD_STRING, .writable = 1 },
			{ .name = "transpose", .type = HYLE_FIELD_INT, .writable = 1 },
			{ .name = "format", .type = HYLE_FIELD_STRING, .writable = 1 }
		};
		hyle_source_register_ordered(
		        "test.ord", ord_fields, 3, "part", 0, 0, NULL, NULL, NULL);

		const char *n1[] = { "title", "transpose", "format" };
		const char *v1[] = { "Track 1", "0", "chords" };
		int rc1 = hyle_source_ordered_append_and_save(
		        "test.ord", "p1", n1, v1, 3);
		CHECK("hyle_source_ordered_append_and_save row 0 returns 0", rc1 == 0);

		const char *n2[] = { "title", "transpose", "format" };
		const char *v2[] = { "Track 2", "2", "tabs" };
		int rc2 = hyle_source_ordered_append_and_save(
		        "test.ord", "p1", n2, v2, 3);
		CHECK("hyle_source_ordered_append_and_save row 1 returns 0", rc2 == 0);

		int count = hyle_source_ordered_count("test.ord", "p1");
		CHECK("hyle_source_ordered_count returns 2", count == 2);

		const char *t0 = hyle_source_ordered_get_field(
		        "test.ord", "p1", 0, "title");
		CHECK("hyle_source_ordered_get_field row 0 title is 'Track 1'",
		      t0 && strcmp(t0, "Track 1") == 0);

		const char *t1 = hyle_source_ordered_get_field(
		        "test.ord", "p1", 1, "title");
		CHECK("hyle_source_ordered_get_field row 1 title is 'Track 2'",
		      t1 && strcmp(t1, "Track 2") == 0);

		int rc_set = hyle_source_ordered_set_field(
		        "test.ord", "p1", 0, "transpose", "5");
		CHECK("hyle_source_ordered_set_field returns 0", rc_set == 0);

		const char *tr0 = hyle_source_ordered_get_field(
		        "test.ord", "p1", 0, "transpose");
		CHECK("hyle_source_ordered_get_field updated transpose is '5'",
		      tr0 && strcmp(tr0, "5") == 0);

		/* Find and replace */
		int f_idx = hyle_source_ordered_find("test.ord", "p1", "title", "Track 2");
		CHECK("hyle_source_ordered_find finds 'Track 2' at index 1", f_idx == 1);
		int f_none = hyle_source_ordered_find("test.ord", "p1", "title", "No Such Track");
		CHECK("hyle_source_ordered_find returns -1 for missing", f_none == -1);

		const char *rep_n[] = { "title", "transpose", "format" };
		const char *rep_v[] = { "Track 2 Replaced", "3", "lyrics" };
		int rc_rep = hyle_source_ordered_replace_row("test.ord", "p1", 1, rep_n, rep_v, 3);
		CHECK("hyle_source_ordered_replace_row returns 0", rc_rep == 0);
		const char *rep_t = hyle_source_ordered_get_field("test.ord", "p1", 1, "title");
		CHECK("hyle_source_ordered_get_field row 1 is replaced",
		      rep_t && strcmp(rep_t, "Track 2 Replaced") == 0);

		/* Restore row 1 title for subsequent checks */
		const char *n2_rst[] = { "title", "transpose", "format" };
		const char *v2_rst[] = { "Track 2", "2", "tabs" };
		hyle_source_ordered_replace_row("test.ord", "p1", 1, n2_rst, v2_rst, 3);

		struct fe_test_ctx fe_ctx = { 0 };
		int fe_tot = hyle_source_ordered_for_each(
		        "test.ord", "p1", test_ordered_each_cb, &fe_ctx);
		CHECK("hyle_source_ordered_for_each total is 2", fe_tot == 2);
		CHECK("hyle_source_ordered_for_each visited 2 rows", fe_ctx.count == 2);
		CHECK("hyle_source_ordered_for_each row 0 is 'Track 1'",
		      fe_ctx.count > 0 && strcmp(fe_ctx.titles[0], "Track 1") == 0);
		CHECK("hyle_source_ordered_for_each row 1 is 'Track 2'",
		      fe_ctx.count > 1 && strcmp(fe_ctx.titles[1], "Track 2") == 0);

		int rc_rem = hyle_source_ordered_remove_and_save(
		        "test.ord", "p1", 0);
		CHECK("hyle_source_ordered_remove_and_save returns 0", rc_rem == 0);
		CHECK("hyle_source_ordered_count after remove is 1",
		      hyle_source_ordered_count("test.ord", "p1") == 1);
		const char *rem_t0 = hyle_source_ordered_get_field(
		        "test.ord", "p1", 0, "title");
		CHECK("hyle_source_ordered_get_field row 0 after remove is 'Track 2'",
		      rem_t0 && strcmp(rem_t0, "Track 2") == 0);

		/* Test remove matching */
		int rc_rem_m = hyle_source_ordered_remove_matching("test.ord", "p1", "title", "Track 2");
		CHECK("hyle_source_ordered_remove_matching returns 0", rc_rem_m == 0);
		CHECK("count after remove_matching is 0", hyle_source_ordered_count("test.ord", "p1") == 0);
	}

	/* 8. Libhyle-source referencing / relation querying */
	{
		static const hyle_source_desc_t band_schema[] = {
			{ .key = "name", .offset = offsetof(test_band_rec_t, name), .size = 64, .qm_type = HYLE_QM_STR, .type = HYLE_FIELD_STRING, .writable = 1 },
			{ 0 }
		};
		hyle_source_setup(
		        "test.band", "name", sizeof(test_band_rec_t), "test.band",
		        band_schema, 1, HYLE_SOURCE_FLAG_VOLATILE, NULL);

		static const hyle_source_desc_t concert_schema[] = {
			{ .key = "title", .offset = offsetof(test_concert_rec_t, title), .size = 64, .qm_type = HYLE_QM_STR, .type = HYLE_FIELD_STRING, .writable = 1 },
			{ .key = "band", .offset = offsetof(test_concert_rec_t, band), .size = 64, .qm_type = HYLE_QM_REFERENCE, .type = HYLE_FIELD_REFERENCE, .ref_source = "test.band", .writable = 1 },
			{ 0 }
		};
		hyle_source_setup(
		        "test.concert", "title", sizeof(test_concert_rec_t),
		        "test.concert", concert_schema, 2, HYLE_SOURCE_FLAG_VOLATILE,
		        NULL);

		unsigned b_hd = hyle_source_get_fields_hd("test.band");
		qmap_field_put(b_hd, "b1", "name", "The Beatles");

		unsigned c_hd = hyle_source_get_fields_hd("test.concert");
		qmap_field_put(c_hd, "c1", "title", "Concert 1");
		qmap_field_put(c_hd, "c1", "band", "b1");
		qmap_field_put(c_hd, "c2", "title", "Concert 2");
		qmap_field_put(c_hd, "c2", "band", "b1");
		qmap_field_put(c_hd, "c3", "title", "Concert 3");
		qmap_field_put(c_hd, "c3", "band", "b2");

		const char *matched[8] = { 0 };
		size_t n_matched = hyle_source_find_referencing(
		        "test.concert", "band", "b1", matched, 8);
		CHECK("hyle_source_find_referencing finds 2 concerts for b1", n_matched == 2);
		CHECK("matched concert 0 is c1", matched[0] && strcmp(matched[0], "c1") == 0);
		CHECK("matched concert 1 is c2", matched[1] && strcmp(matched[1], "c2") == 0);

		struct ref_cb_ctx r_ctx = { 0 };
		size_t n_each = hyle_source_for_each_referencing(
		        "test.concert", "band", "b1", test_referencing_cb, &r_ctx);
		CHECK("hyle_source_for_each_referencing visits 2 items",
		      n_each == 2 && r_ctx.count == 2);
		CHECK("hyle_source_for_each_referencing item 0 is c1",
		      r_ctx.count > 0 && strcmp(r_ctx.ids[0], "c1") == 0);
		CHECK("hyle_source_for_each_referencing item 1 is c2",
		      r_ctx.count > 1 && strcmp(r_ctx.ids[1], "c2") == 0);

		/* Direct field get/set tests */
		const char *b_orig = hyle_source_get_field("test.band", "b1", "name");
		CHECK("hyle_source_get_field reads 'The Beatles'",
		      b_orig && strcmp(b_orig, "The Beatles") == 0);

		int rc_bf = hyle_source_set_field(0, "test.band", "b1", "name", "The Wings");
		CHECK("hyle_source_set_field returns 0", rc_bf == 0);
		const char *b_mod = hyle_source_get_field("test.band", "b1", "name");
		CHECK("hyle_source_get_field reads updated 'The Wings'",
		      b_mod && strcmp(b_mod, "The Wings") == 0);

		/* Test int getters/setters */
		hyle_source_set_field_int(0, "test.band", "b1", "name", 4);
		int n_mem = hyle_source_get_field_int("test.band", "b1", "name", 0);
		CHECK("hyle_source_get_field_int reads 4", n_mem == 4);
		int n_def = hyle_source_get_field_int("test.band", "b1", "missing_field", 99);
		CHECK("hyle_source_get_field_int default fallback", n_def == 99);
	}

	/* 9. Test hyle_bud_form with FIELD_FILE renders textarea */
	{
		typedef struct {
			char id[64];
			char title[64];
		} test_poem_meta_t;

		static const hyle_schema_desc_t poem_schema[] = {
			FIELD_TEXT(id, test_poem_meta_t),
			FIELD_TEXT(title, test_poem_meta_t, .required = 1, .min_length = 1, .in_meta = 1),
			FIELD_FILE(body_content, "pt_PT.html"),
			FIELD_END
		};

		test_poem_meta_t meta = { .id = "poem1", .title = "Ode to Spring" };
		bud_node *form = hyle_bud_form(
		        poem_schema, &meta, "/poem/poem1/edit", "/poem/poem1",
		        "Save", "csrf_test_tok", NULL, "Poem Content Line 1\nLine 2");
		CHECK("hyle_bud_form with FIELD_FILE produces node", form != NULL);
		char *html = bud_render_html(form);
		CHECK("form html is non-NULL", html != NULL);
		CHECK("form html contains input title", strstr(html, "name=\"title\"") != NULL);
		CHECK("form html contains textarea body_content", strstr(html, "textarea name=\"body_content\"") != NULL);
		CHECK("form html contains vstr value", strstr(html, "Poem Content Line 1") != NULL);
		bud_free_string(html);
	}

	printf("\nTotal failures: %d\n", failures);
	return failures > 0 ? 1 : 0;
}
