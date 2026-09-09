#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <bud/bud.h>
#include <hyle/schema.h>
#include <hyle/picker.h>
#include <hyle-bud/hyle-bud.h>
#include <ttypt/xy.h>
#include "mods/common/common.h"
#define INDEX_IMPL
#include "mods/index/index.h"

void _xy_init(void *ptr, const char *fname, uint64_t region_id);

/* Forward declare functions under test from mods/index/index.so */
int list_fill_state(list_state_t *state, const char *dataset_id, const char *raw_qs, int allow_fields);
int list_fill_free(list_state_t *state);
int list_respond_page(int fd, list_state_t *state, const char *module, const char *username);

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

int main(void)
{
	printf("=== Testing index and list/detail shared helpers ===\n");

	xy_init();
	_xy_init(&xy, "main", 0);
	xy_load("./mods/common/common");
	xy_load("./mods/index/index");

	/* 1. detail_state_build: basic fields assignment */
	{
		detail_state_t state;
		memset(&state, 0, sizeof(state));
		detail_state_build_spec_t spec = {
			.module = "song",
			.id = "s123",
			.username = "alice",
			.item_path = NULL,
			.fd = -1,
			.flags = 0,
			.wasm_module = "song_detail"
		};

		int rc = detail_state_build(&state, &spec, "Song Title", "/song/s123");
		CHECK("detail_state_build returns 0", rc == 0);
		CHECK("detail_state_build sets module", strcmp(state.module, "song") == 0);
		CHECK("detail_state_build sets id", strcmp(state.id, "s123") == 0);
		CHECK("detail_state_build sets username", strcmp(state.username, "alice") == 0);
		CHECK("detail_state_build sets title", strcmp(state.title, "Song Title") == 0);
		CHECK("detail_state_build sets path", strcmp(state.path, "/song/s123") == 0);
		CHECK("detail_state_build sets wasm_module", strcmp(state.wasm_module, "song_detail") == 0);
		CHECK("detail_state_build without flags leaves is_owner 0", state.is_owner == 0);
	}

	/* 2. detail_state_build: NULL inputs safe */
	{
		int rc = detail_state_build(NULL, NULL, NULL, NULL);
		CHECK("detail_state_build NULL returns -1", rc == -1);
	}

	/* 3. detail_respond_page: NULL state or layout returns -1 */
	{
		int rc = detail_respond_page(-1, NULL, NULL);
		CHECK("detail_respond_page NULL args returns -1", rc == -1);
	}

	/* 4. list_fill_state: parses and clamps query parameters */
	{
		list_state_t state;
		memset(&state, 0, sizeof(state));
		snprintf(state.module, sizeof(state.module), "testmod");

		/* Nonexistent dataset safely produces 0 items with parsed query params */
		const char *qs = "page=3&per_page=500&q=blues&custom=1&sort=title:desc";
		int rc = list_fill_state(&state, "nonexistent.dataset", qs, 0);
		CHECK("list_fill_state returns -1 for unregistered dataset", rc == -1);
		CHECK("list_fill_state sets page=3", state.page == 3);
		CHECK("list_fill_state sets has_page=1", state.has_page == 1);
		CHECK("list_fill_state clamps per_page to LIST_MAX_ROWS (256)",
		      state.per_page == LIST_MAX_ROWS);
		CHECK("list_fill_state sets q='blues'", strcmp(state.q, "blues") == 0);
		CHECK("list_fill_state sets custom=1", state.custom == 1);
		CHECK("list_fill_state sets sort_field='title'",
		      strcmp(state.sort_field, "title") == 0);
		CHECK("list_fill_state sets sort_asc=0 (desc)", state.sort_asc == 0);

		rc = list_fill_free(&state);
		CHECK("list_fill_free returns 0", rc == 0);
	}

	/* 5. list_respond_page: NULL args returns -1 */
	{
		int rc = list_respond_page(-1, NULL, NULL, NULL);
		CHECK("list_respond_page NULL state/module returns -1", rc == -1);
	}

	printf("\nTotal failures: %d\n", failures);
	return failures > 0 ? 1 : 0;
}
