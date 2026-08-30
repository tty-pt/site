#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <hyle/field.h>

char *source_util_slurp_file(const char *path);
#define slurp_file source_util_slurp_file
#define source_dsv_load hyle_source_dsv_load
#define source_dsv_save hyle_source_dsv_save

size_t hyle_source_get_field_count(const char *source_id);
const char *hyle_source_get_field_name(const char *source_id, size_t idx);
hyle_field_type_t hyle_source_get_field_type(const char *source_id, size_t idx);
int hyle_source_put(
        const char *source_id, const char *row_id, const char **names,
        const char **values, size_t count);
int hyle_source_ordered_count(const char *source_id, const char *pval);
const char *
hyle_source_ordered_key_at(const char *source_id, const char *pval, int pos);
const char *
qmap_field_get(unsigned hd, const char *item_id, const char *field_name);

#define COMMON_H
#define QMAP_H
#define HYLE_SOURCE_H
#define SOURCE_H

#include "../../external/hyle/c/libhyle-source/src/dsv.c"

#define TEST_SOURCE "grp.songs"
#define TEST_PARTITION "legacy-grp"
#define TEST_FIELDS_HD 17u
#define MAX_ROWS 4
#define FIELD_COUNT 4

typedef struct {
	char key[128];
	char values[FIELD_COUNT][128];
	int present[FIELD_COUNT];
} mock_row_t;

static const char *field_names[FIELD_COUNT] = { "song", "transpose", "format",
	                                        "pinned" };
static const hyle_field_type_t field_types[FIELD_COUNT] = {
	HYLE_FIELD_REFERENCE, HYLE_FIELD_INT, HYLE_FIELD_STRING, HYLE_FIELD_INT
};
static mock_row_t rows[MAX_ROWS];
static int row_count;
static int failures;

#define CHECK(label, condition)                                                \
	do {                                                                   \
		if (condition)                                                 \
			printf("PASS %s\n", label);                            \
		else {                                                         \
			printf("FAIL %s\n", label);                            \
			failures++;                                            \
		}                                                              \
	} while (0)

static int field_index(const char *name)
{
	int i;

	for (i = 0; i < FIELD_COUNT; i++) {
		if (strcmp(field_names[i], name) == 0)
			return i;
	}
	return -1;
}

static mock_row_t *row_find(const char *key)
{
	int i;

	for (i = 0; i < row_count; i++) {
		if (strcmp(rows[i].key, key) == 0)
			return &rows[i];
	}
	return NULL;
}

char *slurp_file(const char *path)
{
	FILE *fp;
	char *buf;
	long len;

	fp = fopen(path, "rb");
	if (!fp)
		return NULL;
	if (fseek(fp, 0, SEEK_END) != 0) {
		fclose(fp);
		return NULL;
	}
	len = ftell(fp);
	if (len < 0 || fseek(fp, 0, SEEK_SET) != 0) {
		fclose(fp);
		return NULL;
	}
	buf = malloc((size_t)len + 1);
	if (!buf) {
		fclose(fp);
		return NULL;
	}
	if (fread(buf, 1, (size_t)len, fp) != (size_t)len) {
		free(buf);
		fclose(fp);
		return NULL;
	}
	buf[len] = '\0';
	fclose(fp);
	return buf;
}

size_t hyle_source_get_field_count(const char *source_id)
{
	CHECK("DSV asks for the registered source",
	      strcmp(source_id, TEST_SOURCE) == 0);
	return FIELD_COUNT;
}

const char *hyle_source_get_field_name(const char *source_id, size_t idx)
{
	(void)source_id;
	return idx < FIELD_COUNT ? field_names[idx] : NULL;
}

hyle_field_type_t hyle_source_get_field_type(const char *source_id, size_t idx)
{
	(void)source_id;
	return idx < FIELD_COUNT ? field_types[idx] : HYLE_FIELD_INVERSE;
}

int hyle_source_put(
        const char *source_id, const char *row_id, const char **names,
        const char **values, size_t count)
{
	mock_row_t *row;
	size_t i;

	(void)source_id;
	row = row_find(row_id);
	if (!row) {
		if (row_count == MAX_ROWS)
			return -1;
		row = &rows[row_count++];
		snprintf(row->key, sizeof(row->key), "%s", row_id);
	}
	for (i = 0; i < count; i++) {
		int fi = field_index(names[i]);

		if (fi < 0)
			continue;
		snprintf(
		        row->values[fi], sizeof(row->values[fi]), "%s",
		        values[i]);
		row->present[fi] = 1;
	}
	return 0;
}

int hyle_source_ordered_count(const char *source_id, const char *pval)
{
	(void)source_id;
	(void)pval;
	return row_count;
}

const char *
hyle_source_ordered_key_at(const char *source_id, const char *pval, int pos)
{
	(void)source_id;
	(void)pval;
	return pos >= 0 && pos < row_count ? rows[pos].key : NULL;
}

const char *
qmap_field_get(unsigned hd, const char *item_id, const char *field_name)
{
	mock_row_t *row;
	int fi;

	if (hd != TEST_FIELDS_HD)
		return NULL;
	row = row_find(item_id);
	fi = field_index(field_name);
	if (!row || fi < 0 || !row->present[fi])
		return NULL;
	return row->values[fi];
}

static int write_text(const char *path, const char *text)
{
	FILE *fp;

	fp = fopen(path, "wb");
	if (!fp)
		return -1;
	if (fwrite(text, 1, strlen(text), fp) != strlen(text)) {
		fclose(fp);
		return -1;
	}
	return fclose(fp);
}

static void
make_fixture(char *root, size_t root_size, char *data, size_t data_size)
{
	char var[512];
	char grp[512];
	char item[512];

	snprintf(root, root_size, "/tmp/dsv-legacy-XXXXXX");
	if (!mkdtemp(root)) {
		perror("mkdtemp");
		exit(2);
	}
	snprintf(var, sizeof(var), "%s/var", root);
	snprintf(grp, sizeof(grp), "%s/var/grp", root);
	snprintf(item, sizeof(item), "%s/var/grp/%s", root, TEST_PARTITION);
	if (mkdir(var, 0700) != 0 || mkdir(grp, 0700) != 0 ||
	    mkdir(item, 0700) != 0)
	{
		perror("mkdir fixture");
		exit(2);
	}
	snprintf(
	        data, data_size, "%s/var/grp/%s/data.txt", root,
	        TEST_PARTITION);
}

int main(void)
{
	char root[256];
	char data[512];
	char *rewritten;
	mock_row_t *row;
	const char *mutation_names[] = { "transpose" };
	const char *mutation_values[] = { "-2" };
	const char *pinned;

	make_fixture(root, sizeof(root), data, sizeof(data));
	if (write_text(data, "song_id:3:communion\nmalformed:only\n") != 0) {
		perror("write fixture");
		return 2;
	}

	CHECK("legacy DSV load succeeds",
	      source_dsv_load(
	              TEST_SOURCE, TEST_PARTITION, TEST_FIELDS_HD, root) == 0);
	CHECK("two-column row is rejected", row_count == 1);
	row = row_find(TEST_PARTITION "__0000");
	CHECK("legacy row is loaded", row != NULL);
	CHECK("song survives legacy load",
	      row && strcmp(row->values[0], "song_id") == 0);
	CHECK("transpose survives legacy load",
	      row && strcmp(row->values[1], "3") == 0);
	CHECK("format survives legacy load",
	      row && strcmp(row->values[2], "communion") == 0);
	pinned =
	        row ? qmap_field_get(TEST_FIELDS_HD, row->key, "pinned") : NULL;
	CHECK("missing pinned field stays absent", pinned == NULL);
	CHECK("missing pinned field reads as zero",
	      !pinned || atoi(pinned) == 0);

	CHECK("mutation succeeds",
	      hyle_source_put(
	              TEST_SOURCE, row->key, mutation_names, mutation_values,
	              1) == 0);
	CHECK("save after mutation succeeds",
	      source_dsv_save(
	              TEST_SOURCE, TEST_PARTITION, TEST_FIELDS_HD, root) == 0);
	rewritten = slurp_file(data);
	CHECK("save rewrites the legacy row with four columns",
	      rewritten && strcmp(rewritten, "song_id:-2:communion:\n") == 0);
	free(rewritten);

	unlink(data);
	{
		char item[512];
		char grp[512];
		char var[512];

		snprintf(
		        item, sizeof(item), "%s/var/grp/%s", root,
		        TEST_PARTITION);
		snprintf(grp, sizeof(grp), "%s/var/grp", root);
		snprintf(var, sizeof(var), "%s/var", root);
		rmdir(item);
		rmdir(grp);
		rmdir(var);
		rmdir(root);
	}

	if (failures)
		printf("dsv_legacy_test: %d failures\n", failures);
	else
		printf("dsv_legacy_test: all assertions passed\n");
	return failures != 0;
}
