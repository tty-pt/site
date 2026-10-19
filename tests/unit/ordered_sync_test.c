#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
	const char *form_field_prefix;
	const char *schema_field_name;
	const char *default_value;
	int is_primary_key;
} hyle_ordered_field_sync_t;

typedef struct {
	char key[64];
	char val[128];
} mock_form_entry_t;

typedef struct {
	mock_form_entry_t entries[32];
	int count;
} mock_form_t;

static int mock_getter(const char *name, char *buf, size_t sz, void *user)
{
	mock_form_t *form = (mock_form_t *)user;
	for (int i = 0; i < form->count; i++) {
		if (strcmp(form->entries[i].key, name) == 0) {
			snprintf(buf, sz, "%s", form->entries[i].val);
			return (int)strlen(buf);
		}
	}
	return -1;
}

static void extract_datalist_id(const char *in, char *out, size_t sz)
{
	const char *open_paren = strrchr(in, '(');
	const char *close_paren = strrchr(in, ')');
	if (open_paren && close_paren && close_paren > open_paren + 1) {
		size_t len = close_paren - open_paren - 1;
		if (len < sz) {
			memcpy(out, open_paren + 1, len);
			out[len] = '\0';
			return;
		}
	}
	snprintf(out, sz, "%s", in);
}

typedef struct {
	char song[64];
	char transpose[16];
	char format[32];
} mock_synced_row_t;

int mock_sync_form(
	const char *amount_param,
	const char *remove_prefix,
	const hyle_ordered_field_sync_t *fields,
	size_t n_fields,
	mock_form_t *form,
	mock_synced_row_t *out_rows,
	int max_rows)
{
	char amt_buf[16] = { 0 };
	int amount = 0;
	int synced_count = 0;

	if (mock_getter(amount_param, amt_buf, sizeof(amt_buf), form) > 0)
		amount = atoi(amt_buf);

	for (int i = 0; i < amount && synced_count < max_rows; i++) {
		char rem_name[64];
		char rem_val[16];
		snprintf(rem_name, sizeof(rem_name), "%s_%d", remove_prefix, i);
		if (mock_getter(rem_name, rem_val, sizeof(rem_val), form) > 0)
			continue; /* Marked for removal */

		char row_vals[8][128];
		int valid = 1;

		for (size_t f = 0; f < n_fields; f++) {
			char fld_name[64];
			snprintf(fld_name, sizeof(fld_name), "%s_%d", fields[f].form_field_prefix, i);
			char raw_val[128] = { 0 };
			if (mock_getter(fld_name, raw_val, sizeof(raw_val), form) > 0) {
				if (fields[f].is_primary_key) {
					extract_datalist_id(raw_val, row_vals[f], sizeof(row_vals[f]));
					if (!row_vals[f][0]) {
						valid = 0;
						break;
					}
				} else {
					snprintf(row_vals[f], sizeof(row_vals[f]), "%s", raw_val);
				}
			} else {
				if (fields[f].is_primary_key) {
					valid = 0;
					break;
				}
				snprintf(row_vals[f], sizeof(row_vals[f]), "%s",
					fields[f].default_value ? fields[f].default_value : "");
			}
		}

		if (valid) {
			snprintf(out_rows[synced_count].song, sizeof(out_rows[synced_count].song), "%s", row_vals[0]);
			snprintf(out_rows[synced_count].transpose, sizeof(out_rows[synced_count].transpose), "%s", row_vals[1]);
			snprintf(out_rows[synced_count].format, sizeof(out_rows[synced_count].format), "%s", row_vals[2]);
			synced_count++;
		}
	}
	return synced_count;
}

int main(void)
{
	mock_form_t form = {
		.entries = {
			{ "amount", "3" },
			{ "song_0", "Wonderwall (song-wonderwall)" },
			{ "key_0", "2" },
			{ "fmt_0", "chords" },
			{ "song_1", "Let It Be (song-let-it-be)" },
			{ "key_1", "-1" },
			{ "fmt_1", "any" },
			{ "remove_1", "1" }, /* Removed */
			{ "song_2", "Yesterday (song-yesterday)" },
			{ "key_2", "0" },
			{ "fmt_2", "tab" }
		},
		.count = 11
	};

	hyle_ordered_field_sync_t fields[] = {
		{ "song", "song", "", 1 },
		{ "key", "transpose", "0", 0 },
		{ "fmt", "format", "any", 0 }
	};

	mock_synced_row_t out[8];
	int count = mock_sync_form("amount", "remove", fields, 3, &form, out, 8);

	CHECK("synced count is 2 (skipped removed item 1)", count == 2);
	if (count == 2) {
		CHECK("row 0 song id extracted", strcmp(out[0].song, "song-wonderwall") == 0);
		CHECK("row 0 transpose is 2", strcmp(out[0].transpose, "2") == 0);
		CHECK("row 0 format is chords", strcmp(out[0].format, "chords") == 0);

		CHECK("row 1 song id extracted", strcmp(out[1].song, "song-yesterday") == 0);
		CHECK("row 1 transpose is 0", strcmp(out[1].transpose, "0") == 0);
		CHECK("row 1 format is tab", strcmp(out[1].format, "tab") == 0);
	}

	if (failures == 0)
		printf("\nordered_sync_test: ALL PASS\n");
	return failures;
}
