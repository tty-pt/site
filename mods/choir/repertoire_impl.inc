/* Choir-owned repertoire storage implementation. */

NDX_LISTENER(int, repertoire_parse_line,
	const char *, line, repertoire_row_t *, row)
{
	if (!row)
		return -1;

	memset(row, 0, sizeof(*row));
	row->parsed = (parse_item_line(line, row->id, &row->value, row->format) == 0);
	return row->parsed ? 0 : -1;
}

static int
repertoire_format_line(const repertoire_row_t *row, char *out, size_t out_sz)
{
	if (!row || !out || out_sz == 0)
		return -1;
	return snprintf(out, out_sz, "%s:%d:%s\n",
		row->id, row->value, row->format);
}

NDX_LISTENER(int, repertoire_rows_load,
	const char *, path, repertoire_row_t **, rows, size_t *, count)
{
	FILE *fp;
	repertoire_row_t *loaded = NULL;
	size_t used = 0;
	size_t cap = 0;
	char line[512];

	if (!rows || !count)
		return -1;

	*rows = NULL;
	*count = 0;

	fp = fopen(path, "r");
	if (!fp)
		return 0;

	while (fgets(line, sizeof(line), fp)) {
		repertoire_row_t row = {0};
		if (repertoire_parse_line(line, &row) != 0 || !row.id[0])
			continue;

		if (used == cap) {
			size_t new_cap = cap ? cap * 2 : 8;
			repertoire_row_t *tmp = realloc(loaded, new_cap * sizeof(*loaded));
			if (!tmp) {
				fclose(fp);
				free(loaded);
				return -1;
			}
			loaded = tmp;
			cap = new_cap;
		}

		loaded[used++] = row;
	}

	fclose(fp);
	*rows = loaded;
	*count = used;
	return 0;
}

NDX_LISTENER(int, repertoire_file_append,
	const char *, path, const repertoire_row_t *, row)
{
	FILE *fp;
	char line[512];

	if (!path || !row)
		return -1;
	if (repertoire_format_line(row, line, sizeof(line)) < 0)
		return -1;

	fp = fopen(path, "a");
	if (!fp)
		return -1;
	if (fputs(line, fp) == EOF) {
		fclose(fp);
		return -1;
	}
	if (fclose(fp) != 0)
		return -1;
	return 0;
}

NDX_LISTENER(int, repertoire_rows_write,
	const char *, path, const repertoire_row_t *, rows, size_t, count)
{
	FILE *fp = fopen(path, "w");
	if (!fp)
		return -1;

	for (size_t i = 0; i < count; i++) {
		char line[512];
		if (repertoire_format_line(&rows[i], line, sizeof(line)) < 0 ||
				fputs(line, fp) == EOF) {
			fclose(fp);
			return -1;
		}
	}

	if (fclose(fp) != 0)
		return -1;
	return 0;
}

NDX_LISTENER(int, repertoire_file_rewrite,
	const char *, path, repertoire_line_fn, cb, void *, user)
{
	char tmp_path[PATH_MAX];
	int n = snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
	if (n < 0 || (size_t)n >= sizeof(tmp_path))
		return -1;

	FILE *rfp = fopen(path, "r");
	FILE *wfp = fopen(tmp_path, "w");
	if (!wfp) {
		if (rfp) fclose(rfp);
		return -1;
	}

	char line[512];
	char out[512];
	int idx = 0;
	int changed = 0;
	int io_err = 0;

	while (rfp && fgets(line, sizeof(line), rfp)) {
		repertoire_row_t row = {0};
		int parsed = (repertoire_parse_line(line, &row) == 0);

		out[0] = '\0';
		int rc = cb(idx, line, parsed, row.id, row.value, row.format, user,
			out, sizeof(out));

		if (rc == REPERTOIRE_LINE_SKIP) {
			changed++;
		} else if (rc == REPERTOIRE_LINE_REPLACE) {
			changed++;
			if (fputs(out, wfp) == EOF) {
				io_err = 1;
				break;
			}
		} else {
			if (fputs(line, wfp) == EOF) {
				io_err = 1;
				break;
			}
		}
		idx++;
	}

	if (rfp) fclose(rfp);
	if (fclose(wfp) != 0) io_err = 1;

	if (io_err) {
		unlink(tmp_path);
		return -1;
	}

	if (rename(tmp_path, path) != 0) {
		unlink(tmp_path);
		return -1;
	}

	return changed;
}
