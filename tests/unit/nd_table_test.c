/* nd_table_load: declarative DSV tables owned by tenant modules. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ttypt/xy.h>
#include <nd/law-types.h>

int nd_table_load(const char *module, const char *table, nd_table_row_fn row, void *user);

struct acc { int rows; size_t maxn; char first[64]; char last[64]; };

static int collect(const char *const *f, size_t n, void *user)
{
	struct acc *a = user;
	if (!a->rows)
		snprintf(a->first, sizeof(a->first), "%s", f[0]);
	snprintf(a->last, sizeof(a->last), "%s", f[0]);
	if (n > a->maxn)
		a->maxn = n;
	a->rows++;
	return 0;
}

static int stop_at_two(const char *const *f, size_t n, void *user)
{
	(void)f; (void)n;
	return ++*(int *)user >= 2 ? -1 : 0;
}

int main(void)
{
	char dir[] = "/tmp/nd_table_XXXXXX";
	char path[128];
	struct acc a;
	FILE *fp;

	assert(mkdtemp(dir));
	snprintf(path, sizeof(path), "%s/t.dsv", dir);
	fp = fopen(path, "w");
	fputs("# header comment\n\n  # indented comment\nalpha|1|x\r\nbeta|2\n\ngamma|3|y|z\n", fp);
	fclose(fp);
	setenv("AXIL_ND_DATA", dir, 1);

	memset(&a, 0, sizeof(a));
	assert(nd_table_load("nd-none", "t", collect, &a) == 3);
	assert(a.rows == 3 && a.maxn == 4);
	assert(!strcmp(a.first, "alpha") && !strcmp(a.last, "gamma"));
	printf("PASS: rows, comments, blanks, CRLF, ragged fields\n");

	int cnt = 0;
	assert(nd_table_load("nd-none", "t", stop_at_two, &cnt) == 1);
	printf("PASS: callback abort stops iteration\n");

	assert(nd_table_load("nd-none", "missing", collect, &a) == -1);
	printf("PASS: missing table reports -1\n");

	/* The shipped tenant tables must be present and well-formed. */
	unsetenv("AXIL_ND_DATA");
	unsetenv("AXIL_ND_DB");
	static const struct { const char *m, *t; int rows; size_t cols; } shipped[] = {
		{ "nd-spell", "spells", 11, 6 }, { "nd-class", "classes", 4, 2 },
		{ "nd-race", "races", 5, 8 }, { "nd-biome", "biomes", 19, 2 },
		{ "nd-mob", "mobs", 14, 9 }, { "nd-plant", "plants", 10, 12 },
		{ "nd-stone", "stones", 1, 3 }, { "nd-wts", "wts", 12, 1 },
	};
	for (size_t i = 0; i < sizeof(shipped) / sizeof(shipped[0]); i++) {
		memset(&a, 0, sizeof(a));
		assert(nd_table_load(shipped[i].m, shipped[i].t, collect, &a) == shipped[i].rows);
		assert(a.maxn == shipped[i].cols);
	}
	printf("PASS: shipped spells/classes/races/biomes/mobs/plants/stones/wts tables\n");

	snprintf(path, sizeof(path), "%s/t.dsv", dir);
	remove(path);
	rmdir(dir);
	printf("ALL ND TABLE TESTS PASSED\n");
	return 0;
}
