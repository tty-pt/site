#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <bud/bud.h>
#include <hyle/schema.h>
#include <hyle/picker.h>
#include <hyle-bud/hyle-bud.h>
#include "mods/common/ux/site_ui.h"

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
	char title[64];
	int count;
} test_state_t;

static const bud_field_desc_t test_fields[] = {
	{ .key = "title", .offset = offsetof(test_state_t, title), .size = 64, .is_int = 0 },
	{ .key = "count", .offset = offsetof(test_state_t, count), .size = sizeof(int), .is_int = 1 },
	{ 0 }
};

int main(void)
{
	printf("=== Testing WASM and site_ui state helpers ===\n");

	/* 1. wasm_state_init with valid JSON */
	{
		test_state_t state;
		memset(&state, 0, sizeof(state));
		const char *json = "{\"title\":\"Hello World\",\"count\":42}";
		wasm_state_init(json, (int)strlen(json), test_fields, &state);
		CHECK("wasm_state_init sets string field",
		      strcmp(state.title, "Hello World") == 0);
		CHECK("wasm_state_init sets int field",
		      state.count == 42);
	}

	/* 2. wasm_state_init with empty JSON preserves defaults */
	{
		test_state_t state;
		memset(&state, 0, sizeof(state));
		state.count = 99;
		wasm_state_init("{}", 2, test_fields, &state);
		CHECK("wasm_state_init empty JSON leaves zeroed string",
		      state.title[0] == '\0');
		CHECK("wasm_state_init empty JSON preserves count",
		      state.count == 99);
	}

	/* 3. wasm_state_init with NULL json and -1 len */
	{
		test_state_t state;
		memset(&state, 0, sizeof(state));
		wasm_state_init(NULL, -1, test_fields, &state);
		CHECK("wasm_state_init NULL json does not crash",
		      state.title[0] == '\0');
	}

	/* 4. wasm_picker_init with NULL json does not crash */
	{
		site_ui_picker_buffer_t buf;
		pick_view_t pv;
		memset(&buf, 0, sizeof(buf));
		memset(&pv, 0, sizeof(pv));
		wasm_picker_init(NULL, 0, "song", "song.items", NULL, 0, &buf, &pv);
		CHECK("wasm_picker_init NULL json does not crash",
		      pv.entries[0].npage == 0);
	}

	/* 5. wasm_picker_init with valid JSON populates picker buffer and view */
	{
		site_ui_picker_buffer_t buf;
		pick_view_t pv;
		memset(&buf, 0, sizeof(buf));
		memset(&pv, 0, sizeof(pv));
		const char *json =
		        "{\"pick_opts\":[{\"id\":\"s1\",\"label\":\"Song One\"},"
		        "{\"id\":\"s2\",\"label\":\"Song Two\"}],"
		        "\"pick_sel\":[],"
		        "\"pick_per_page\":10,"
		        "\"pick_total\":2}";
		wasm_picker_init(
		        json, strlen(json), "song", "song.items",
		        "search", 1, &buf, &pv);
		CHECK("wasm_picker_init sets entry key",
		      strcmp(pv.entries[0].key, "song") == 0);
		CHECK("wasm_picker_init sets entry target",
		      strcmp(pv.entries[0].target, "song.items") == 0);
		CHECK("wasm_picker_init populates 2 page options",
		      pv.entries[0].npage == 2);
		CHECK("wasm_picker_init opt 0 id is correct",
		      pv.entries[0].page_opts[0].id != NULL &&
		      strcmp(pv.entries[0].page_opts[0].id, "s1") == 0);
		CHECK("wasm_picker_init opt 0 label is correct",
		      pv.entries[0].page_opts[0].label != NULL &&
		      strcmp(pv.entries[0].page_opts[0].label, "Song One") == 0);
		CHECK("wasm_picker_init opt 1 id is correct",
		      pv.entries[0].page_opts[1].id != NULL &&
		      strcmp(pv.entries[0].page_opts[1].id, "s2") == 0);
		CHECK("wasm_picker_init per_page set to 10",
		      pv.entries[0].per_page == 10);
		CHECK("wasm_picker_init total set to 2",
		      pv.entries[0].total == 2);
	}

	/* 6. site_ui_state_head wraps JSON in script tag */
	{
		char *head = site_ui_state_head("{\"test\":123}");
		CHECK("site_ui_state_head returns non-NULL", head != NULL);
		if (head) {
			CHECK("site_ui_state_head contains json",
			      strstr(head, "{\"test\":123}") != NULL);
			CHECK("site_ui_state_head contains id=\"bud-state\"",
			      strstr(head, "id=\"bud-state\"") != NULL);
			CHECK("site_ui_state_head contains application/json",
			      strstr(head, "type=\"application/json\"") != NULL);
			free(head);
		}

		char *null_head = site_ui_state_head(NULL);
		CHECK("site_ui_state_head NULL returns NULL", null_head == NULL);
		char *empty_head = site_ui_state_head("");
		CHECK("site_ui_state_head empty string returns NULL", empty_head == NULL);
	}

	printf("\nTotal failures: %d\n", failures);
	return failures > 0 ? 1 : 0;
}
