#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ttypt/qmap.h>
#include "stoma/stoma.h"

#define STOMA_MAX_TOKENS 64

/* Declared in token.c (internal). */
void stoma_tokenize(const char *folded,
	void (*cb)(const char *tok, size_t len, void *user),
	void *user);

struct stoma_db {
	unsigned hd;	/* QM_SORTED, QM_STR → QM_STR inverted index */
};

stoma_db_t *stoma_open(unsigned mask)
{
	stoma_db_t *db;

	db = calloc(1, sizeof(*db));
	if (!db)
		return NULL;
	db->hd = qmap_open(NULL, NULL, QM_STR, QM_STR, mask, QM_SORTED);
	if (!db->hd) {
		free(db);
		return NULL;
	}
	return db;
}

void stoma_close(stoma_db_t *db)
{
	if (!db)
		return;
	qmap_close(db->hd);
	free(db);
}

void stoma_clear(stoma_db_t *db)
{
	if (db)
		qmap_drop(db->hd);
}

/* ---- index ---- */

typedef struct {
	const char *field;
	const char *row_id;
	unsigned   hd;
} index_ctx_t;

static void index_token(const char *tok, size_t len, void *user)
{
	index_ctx_t *ctx = (index_ctx_t *)user;
	size_t       fld = strlen(ctx->field);
	size_t       rid = strlen(ctx->row_id);
	char        *key;

	key = malloc(fld + rid + len + 3);
	if (!key)
		return;
	memcpy(key, ctx->field, fld);
	key[fld] = '\t';
	memcpy(key + fld + 1, tok, len);
	key[fld + 1 + len] = '\t';
	memcpy(key + fld + 2 + len, ctx->row_id, rid);
	key[fld + 2 + len + rid] = '\0';
	qmap_put(ctx->hd, key, "");
	free(key);
}

int stoma_index(stoma_db_t *db,
	const char *field, const char *row_id, const char *value)
{
	index_ctx_t ctx;
	size_t      vlen;
	char       *folded;

	if (!db || !field || !row_id || !value)
		return -1;
	/* The fold never grows its output, so strlen+1 always fits. */
	vlen = strlen(value);
	folded = malloc(vlen + 1);
	if (!folded)
		return -1;
	if (stoma_fold(folded, vlen + 1, value) < 0) {
		free(folded);
		return 0;
	}
	ctx.field = field;
	ctx.row_id = row_id;
	ctx.hd = db->hd;
	stoma_tokenize(folded, index_token, &ctx);
	free(folded);
	return 0;
}

/* ---- query ---- */

typedef struct {
	const char *toks[STOMA_MAX_TOKENS];
	size_t      len[STOMA_MAX_TOKENS];
	size_t      n;
} collect_ctx_t;

static void collect_token(const char *tok, size_t len, void *user)
{
	collect_ctx_t *c = (collect_ctx_t *)user;

	if (c->n < STOMA_MAX_TOKENS) {
		c->toks[c->n] = tok;
		c->len[c->n] = len;
		c->n++;
	}
}

uint32_t stoma_query(stoma_db_t *db,
	const char *field, const char *query,
	uint32_t out_hd, int *handled)
{
	collect_ctx_t toks;
	size_t        fld;
	size_t        qlen;
	char         *folded;
	unsigned      cur_hd = 0;
	uint32_t      matches = 0;
	size_t        i;

	if (handled)
		*handled = 0;
	if (!db || !field || !query || !out_hd)
		return 0;
	/* The fold never grows its output, so strlen+1 always fits. */
	qlen = strlen(query);
	folded = malloc(qlen + 1);
	if (!folded)
		return 0;
	if (stoma_fold(folded, qlen + 1, query) < 0) {
		free(folded);
		return 0;
	}

	toks.n = 0;
	stoma_tokenize(folded, collect_token, &toks);
	if (toks.n == 0) {
		free(folded);
		return 0;
	}
	if (handled)
		*handled = 1;

	fld = strlen(field);

	for (i = 0; i < toks.n; i++) {
		unsigned nxt_hd;
		size_t   plen = fld + 1 + toks.len[i];
		char    *prefix = malloc(plen + 1);
		uint32_t cur;
		const void *k;
		const void *v;

		if (!prefix)
			break;
		memcpy(prefix, field, fld);
		prefix[fld] = '\t';
		memcpy(prefix + fld + 1, toks.toks[i], toks.len[i]);
		prefix[plen] = '\0';

		nxt_hd = qmap_open(NULL, NULL, QM_STR, QM_STR, 0xFF, 0);
		if (!nxt_hd) {
			free(prefix);
			break;
		}

		/* Prefix scan: index map is QM_SORTED, QM_RANGE iterates from
		 * the lower bound to the end; break once the prefix no longer
		 * matches (contiguous keys). */
		cur = qmap_iter(db->hd, prefix, QM_RANGE);
		while (qmap_next(&k, &v, cur)) {
			const char *key = (const char *)k;
			const char *sep1;
			const char *sep2;

			if (strncmp(key, prefix, plen) != 0)
				break;
			sep1 = strchr(key, '\t');
			if (!sep1)
				continue;
			sep2 = strchr(sep1 + 1, '\t');
			if (!sep2)
				continue;
			if (!cur_hd || qmap_get(cur_hd, sep2 + 1))
				qmap_put(nxt_hd, sep2 + 1, "");
		}
		qmap_fin(cur);
		free(prefix);

		if (cur_hd)
			qmap_close(cur_hd);
		cur_hd = nxt_hd;
	}

	free(folded);

	if (cur_hd) {
		uint32_t cur = qmap_iter(cur_hd, NULL, 0);
		const void *k;
		const void *v;

		while (qmap_next(&k, &v, cur)) {
			qmap_put(out_hd, (const char *)k, "");
			matches++;
		}
		qmap_fin(cur);
		qmap_close(cur_hd);
	}

	return matches;
}
