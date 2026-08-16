#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ttypt/qmap.h>
#include "stoma/stoma.h"

#define STOMA_MAX_TOKENS 64

/* Declared in token.c (internal). */
void stoma_tokenize(
        const char *folded, void (*cb)(const char *tok, size_t len, void *user),
        void *user);

struct stoma_db {
	unsigned hd;     /* QM_SORTED, QM_STR → QM_STR inverted index */
	unsigned doc_hd; /* QM_SORTED, QM_STR → QM_STR folded text per
	                    (field,row) */
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
	db->doc_hd = qmap_open(NULL, NULL, QM_STR, QM_STR, mask, QM_SORTED);
	if (!db->doc_hd) {
		qmap_close(db->hd);
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
	qmap_close(db->doc_hd);
	free(db);
}

void stoma_clear(stoma_db_t *db)
{
	if (!db)
		return;
	qmap_drop(db->hd);
	qmap_drop(db->doc_hd);
}

/* ---- index ---- */

typedef struct {
	const char *field;
	const char *row_id;
	unsigned hd;
} index_ctx_t;

static void index_token(const char *tok, size_t len, void *user)
{
	index_ctx_t *ctx = (index_ctx_t *)user;
	size_t fld = strlen(ctx->field);
	size_t rid = strlen(ctx->row_id);
	char *key;

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

int stoma_index(
        stoma_db_t *db, const char *field, const char *row_id,
        const char *value)
{
	index_ctx_t ctx;
	size_t vlen;
	char *folded;

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

	/* Side table: store the folded text per (field,row_id) so phrase
	 * queries can re-tokenize a document and test token adjacency. */
	{
		size_t fld = strlen(field);
		size_t rid = strlen(row_id);
		size_t dlen = fld + 1 + rid;
		char *dkey = malloc(dlen + 1);
		size_t fl = strlen(folded);
		char *fdoc = malloc(fl + 1);

		if (dkey && fdoc) {
			memcpy(dkey, field, fld);
			dkey[fld] = '\t';
			memcpy(dkey + fld + 1, row_id, rid);
			dkey[dlen] = '\0';
			memcpy(fdoc, folded, fl + 1);
			qmap_put(db->doc_hd, dkey, fdoc);
		}
		free(dkey);
		free(fdoc);
	}

	free(folded);
	return 0;
}

/* ---- query ---- */

typedef struct {
	const char *toks[STOMA_MAX_TOKENS];
	size_t len[STOMA_MAX_TOKENS];
	size_t n;
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

/* ---- phrase verification ---- */

typedef struct {
	const char *tok;
	size_t len;
} dtok_t;

typedef struct {
	dtok_t *ent;
	size_t n;
	size_t cap;
} dctoks_t;

static void collect_doc_token(const char *tok, size_t len, void *user)
{
	dctoks_t *t = (dctoks_t *)user;

	if (t->n == t->cap) {
		size_t ncap = t->cap ? t->cap * 2 : 64;
		dtok_t *nt = realloc(t->ent, ncap * sizeof(*nt));

		if (!nt)
			return;
		t->ent = nt;
		t->cap = ncap;
	}
	t->ent[t->n].tok = tok;
	t->ent[t->n].len = len;
	t->n++;
}

/* Contiguous-subsequence test: the query tokens must appear in order with
 * per-token prefix matching. A single query token is trivially present. */
static int doc_matches_phrase(const dctoks_t *d, const collect_ctx_t *q)
{
	size_t i;
	size_t j;

	if (q->n > d->n)
		return 0;
	if (q->n == 1)
		return 1;
	for (i = 0; i + q->n <= d->n; i++) {
		for (j = 0; j < q->n; j++) {
			if (d->ent[i + j].len < q->len[j])
				break;
			if (memcmp(d->ent[i + j].tok, q->toks[j], q->len[j]) !=
			    0)
				break;
		}
		if (j == q->n)
			return 1;
	}
	return 0;
}

static uint32_t stoma_query_any(
        stoma_db_t *db, const char *field, const char *query, uint32_t out_hd,
        int *handled, int phrase)
{
	collect_ctx_t toks;
	size_t fld;
	size_t qlen;
	char *folded;
	unsigned cur_hd = 0;
	uint32_t matches = 0;
	size_t i;

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
		size_t plen = fld + 1 + toks.len[i];
		char *prefix = malloc(plen + 1);
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

	if (cur_hd) {
		uint32_t cur = qmap_iter(cur_hd, NULL, 0);
		const void *k;
		const void *v;

		while (qmap_next(&k, &v, cur)) {
			const char *rid = (const char *)k;
			int keep = 1;

			if (phrase && toks.n > 1) {
				size_t dfl = fld + 1 + strlen(rid);
				char *dkey = malloc(dfl + 1);
				const char *dtext;
				dctoks_t d;

				if (!dkey)
					break;
				memcpy(dkey, field, fld);
				dkey[fld] = '\t';
				memcpy(dkey + fld + 1, rid, strlen(rid));
				dkey[dfl] = '\0';
				dtext = (const char *)qmap_get(
				        db->doc_hd, dkey);
				free(dkey);
				memset(&d, 0, sizeof(d));
				if (dtext) {
					stoma_tokenize(
					        dtext, collect_doc_token, &d);
					keep = doc_matches_phrase(&d, &toks);
				} else {
					keep = 0;
				}
				free(d.ent);
			}
			if (keep) {
				qmap_put(out_hd, rid, "");
				matches++;
			}
		}
		qmap_fin(cur);
		qmap_close(cur_hd);
	}

	free(folded);

	return matches;
}

uint32_t stoma_query(
        stoma_db_t *db, const char *field, const char *query, uint32_t out_hd,
        int *handled)
{
	return stoma_query_any(db, field, query, out_hd, handled, 0);
}

uint32_t stoma_query_phrase(
        stoma_db_t *db, const char *field, const char *query, uint32_t out_hd,
        int *handled)
{
	return stoma_query_any(db, field, query, out_hd, handled, 1);
}
