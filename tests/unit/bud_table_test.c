#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <bud/bud.h>
#include <hyle-bud/hyle-bud.h>

static void test_table_render(void)
{
	const char *col_keys[] = { "title", "author", "type" };
	const char *col_labels[] = { "Title", "Author", "Type" };
	const char *ids[] = { "song1", "song2" };
	const char *values[] = {
		"Song One", "Alice", "Chords",
		"Song Two", "Bob", "Lyrics"
	};

	hyle_bud_row_action_t act;
	memset(&act, 0, sizeof(act));
	act.kind = HYLE_ROW_ACTION_LINK;
	act.aria_base = "Open";

	bud_node *table = hyle_bud_table_actions(
		col_keys, col_labels, 3,
		ids, 2, values,
		"song", "title", 1, "custom=1", &act
	);
	assert(table != NULL);

	char *html = bud_render_html(table);
	assert(html != NULL);

	assert(strstr(html, "<table") != NULL);
	assert(strstr(html, "<thead") != NULL);
	assert(strstr(html, "<tbody") != NULL);
	assert(strstr(html, "Song One") != NULL);
	assert(strstr(html, "/song/song1") != NULL);
	assert(strstr(html, "Song Two") != NULL);
	assert(strstr(html, "/song/song2") != NULL);
	assert(strstr(html, "hyle-sort-button") != NULL);

	bud_free_string(html);
	printf("PASS table rendering with sorting and row actions\n");
}

static void test_pagination_render(void)
{
	bud_node *pag = hyle_bud_pagination(2, 10, 35, 10, "");
	assert(pag != NULL);

	char *html = bud_render_html(pag);
	assert(html != NULL);

	assert(strstr(html, "hyle-pagination") != NULL);
	assert(strstr(html, "Page 2") != NULL);
	assert(strstr(html, "10 of 35 rows") != NULL);
	assert(strstr(html, "value=\"1\"") != NULL); // Prev page
	assert(strstr(html, "value=\"3\"") != NULL); // Next page
	assert(strstr(html, "name=\"per_page\"") != NULL);

	bud_free_string(html);
	printf("PASS pagination rendering with per_page options\n");
}

int main(void)
{
	test_table_render();
	test_pagination_render();
	printf("\nbud_table_test: ALL PASS\n");
	return 0;
}
