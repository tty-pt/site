#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "../common/common.h"
#include <ttypt/qmap.h>
#include <hyle/source.h>

#define SOURCE_IMPL
#include "source.h"

static void dsv_path(
        char *buf, size_t sz, const char *doc_root, const char *source_id,
        const char *pval)
{
	const char *dot = strchr(source_id, '.');
	if (dot) {
		size_t plen = (size_t)(dot - source_id);
		snprintf(
		        buf, sz, "%s/var/%.*s/%s/data.txt", doc_root,
		        (int)plen, source_id, pval);
	} else {
		snprintf(
		        buf, sz, "%s/var/%s/%s/data.txt", doc_root,
		        source_id, pval);
	}
}

XY_IMPL(int, source_dsv_load,
	const char *, source_id,
	const char *, pval,
	unsigned, fhd,
	void *, user)
{
	const char *doc_root = (const char *)user;
	char dpath[PATH_MAX];
	char *raw, *p;
	size_t nfields, j;
	int pos = 0;

	(void)fhd;
	if (!doc_root)
		return -1;

	dsv_path(dpath, sizeof(dpath), doc_root, source_id, pval);
	raw = slurp_file(dpath);
	if (!raw)
		return 0;

	nfields = hyle_source_get_field_count(source_id);

	p = raw;
	while (*p) {
		char *nl = strchr(p, '\n');
		size_t llen = nl ? (size_t)(nl - p) : strlen(p);
		if (llen > 0 && p[0] != '#') {
			const char *names[128];
			const char *vals[128];
			char line[2048];
			char *parts[128];
			int nparts = 0, vi;
			char *cur;

			snprintf(line, sizeof(line), "%.*s", (int)llen, p);
			cur = line;
			parts[nparts++] = cur;
			while (*cur && nparts < (int)nfields) {
				if (*cur == ':') {
					*cur = '\0';
					parts[nparts++] = cur + 1;
				}
				cur++;
			}
			if (nparts == (int)nfields) {
				char key[128];
				vi = 0;
				for (j = 0; j < nfields && vi < nparts; j++) {
					if (hyle_source_get_field_type(
					            source_id, j) ==
					    HYLE_FIELD_INVERSE)
						continue;
					names[vi] = hyle_source_get_field_name(
					        source_id, j);
					vals[vi] = parts[vi];
					vi++;
				}
				snprintf(key, sizeof(key), "%s__%04d",
				         pval, pos);
				hyle_source_put(
				        source_id, key, names, vals, vi);
				pos++;
			}
		}
		p = nl ? nl + 1 : p + strlen(p);
	}
	free(raw);

	/* No save here: load_fn runs before ordered_ensure_loaded recounts
	 * order_hd, so saving would rewrite data.txt empty. Disk stays
	 * untouched; later mutations save via append/insert/remove. */
	return 0;
}

XY_IMPL(int, source_dsv_save,
	const char *, source_id,
	const char *, pval,
	unsigned, fhd,
	void *, user)
{
	const char *doc_root = (const char *)user;
	char dpath[PATH_MAX], tmp[PATH_MAX + 4];
	int n, i;
	FILE *fp;
	size_t nfields, j;

	if (!doc_root)
		return -1;

	dsv_path(dpath, sizeof(dpath), doc_root, source_id, pval);
	snprintf(tmp, sizeof(tmp), "%s.tmp", dpath);
	fp = fopen(tmp, "w");
	if (!fp)
		return -1;

	nfields = hyle_source_get_field_count(source_id);
	n = hyle_source_ordered_count(source_id, pval);
	for (i = 0; i < n; i++) {
		const char *key;
		int first;

		key = hyle_source_ordered_key_at(source_id, pval, i);
		if (!key)
			continue;
		first = 1;
		for (j = 0; j < nfields; j++) {
			const char *val;

			if (hyle_source_get_field_type(source_id, j) ==
			    HYLE_FIELD_INVERSE)
				continue;
			val = qmap_field_get(
			        fhd, key,
			        hyle_source_get_field_name(source_id, j));
			if (first) {
				fprintf(fp, "%s", val ? val : "");
				first = 0;
			} else {
				fprintf(fp, ":%s", val ? val : "");
			}
		}
		fprintf(fp, "\n");
	}
	fclose(fp);
	rename(tmp, dpath);
	return 0;
}
