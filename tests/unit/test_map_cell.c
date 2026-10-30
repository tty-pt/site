#include <assert.h>
#include <stdio.h>
#include <nd/map-cell.h>

static void
check(const char *s, int want_ret, char want_glyph, int want_fg, const char *what)
{
	uint8_t g, fg;
	int r = nd_map_cell_parse(s, &g, &fg);
	assert(r == want_ret);
	assert(g == (uint8_t)want_glyph);
	assert(fg == want_fg);
	printf("PASS: %s\n", what);
}

int main(void)
{
	printf("=== Testing ND map cell parser ===\n");

	/* nd-plant on_empty_tile: BOLD, fg green, glyph, fg again, RESET_BOLD(dim) */
	check("\033[1m\033[32mT\033[32m\033[2m", 1, 'T', 10, "bold green tree folds to bright green (10)");
	/* non-bold yellow birch */
	check("\033[33mx\033[33m", 1, 'x', 3, "plain yellow shrub stays colour 3");
	/* bold white (colour 7 -> 15) */
	check("\033[1m\033[37m!\033[2m", 1, '!', 15, "bold white folds to 15");
	/* bold bright already (>=8) must not double fold */
	check("\033[1m\033[0m\033[1m\033[31mA", 1, 'A', 9, "bold red folds to 9");
	/* reset clears bold */
	check("\033[1m\033[0m\033[34mi", 1, 'i', 4, "reset clears bold");
	/* blank cell */
	check(" ", 0, 0, 7, "blank cell has no glyph");
	check("", 0, 0, 7, "empty string has no glyph");
	check("\033[0m ", 0, 0, 7, "escape then blank has no glyph");
	/* glyph without any escape */
	check("t", 1, 't', 7, "bare glyph defaults to colour 7");
	/* NULL is tolerated */
	check(NULL, 0, 0, 7, "NULL is tolerated");

	/* engine enum color -> ANSI index */
	assert(nd_color_to_ansi(0) == 0);  /* BLACK */
	assert(nd_color_to_ansi(1) == 1);  /* RED */
	assert(nd_color_to_ansi(2) == 3);  /* YELLOW */
	assert(nd_color_to_ansi(3) == 2);  /* GREEN */
	assert(nd_color_to_ansi(4) == 6);  /* CYAN */
	assert(nd_color_to_ansi(5) == 4);  /* BLUE */
	assert(nd_color_to_ansi(6) == 5);  /* MAGENTA */
	assert(nd_color_to_ansi(7) == 7);  /* WHITE */
	printf("PASS: enum color maps to ANSI indices\n");

	printf("ALL MAP CELL TESTS PASSED\n");
	return 0;
}
