#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ttypt/corm.h>

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

int mock_collect_options(
	unsigned data_hd,
	unsigned fhd,
	const char *label_field,
	const char *default_opt,
	char (*buf)[128],
	const char **opts,
	int max)
{
	int n = 0;
	if (default_opt && max > 0) {
		snprintf(buf[n], 128, "%s", default_opt);
		opts[n] = buf[n];
		n++;
	}
	if (!data_hd)
		return n;

	uint32_t cur = corm_iter(data_hd, NULL, 0);
	const void *tk, *tv;
	while (corm_next(&tk, &tv, cur) && n < max) {
		const char *id = (const char *)tk;
		const char *name = NULL;
		if (fhd && label_field) {
			char nk[320];
			snprintf(nk, sizeof(nk), "%s:%s", id, label_field);
			name = corm_get(fhd, nk);
		}
		const char *label = name ? name : id;
		int dup = 0;
		for (int di = 0; di < n; di++) {
			if (strcmp(opts[di], label) == 0) {
				dup = 1;
				break;
			}
		}
		if (!dup) {
			snprintf(buf[n], 128, "%s", label);
			opts[n] = buf[n];
			n++;
		}
	}
	corm_fin(cur);
	return n;
}

int main(void)
{
	unsigned data_hd = corm_open(NULL, NULL, CM_STR, CM_STR, 0xFF, 0);
	unsigned fhd = corm_open(NULL, NULL, CM_STR, CM_STR, 0xFF, 0);

	corm_put(data_hd, "chords", "1");
	corm_put(data_hd, "tab", "1");
	corm_put(data_hd, "lyrics", "1");

	corm_put(fhd, "chords:name", "Chords & Lyrics");
	corm_put(fhd, "tab:name", "Tablature");
	corm_put(fhd, "lyrics:name", "Lyrics Only");

	char buf[8][128];
	const char *opts[8];

	int n = mock_collect_options(data_hd, fhd, "name", "any", buf, opts, 8);
	CHECK("collect options count is 4 (default + 3 items)", n == 4);
	CHECK("first option is default 'any'", strcmp(opts[0], "any") == 0);
	CHECK("options contain labels", strcmp(opts[1], "Chords & Lyrics") == 0 ||
	                                strcmp(opts[2], "Chords & Lyrics") == 0 ||
	                                strcmp(opts[3], "Chords & Lyrics") == 0);

	corm_close(data_hd);
	corm_close(fhd);

	if (failures == 0)
		printf("\nsource_dataset_collect_options_test: ALL PASS\n");
	return failures;
}
