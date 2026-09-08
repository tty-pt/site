#include "engine.h"

#include <ttypt/qmap.h>
#include <stoma/stoma.h>

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	char **buf;
	size_t n;
	size_t cap;
} strs_t;

typedef struct {
	mm_entry_t entry;
	char key[MM_KEY_LEN];
} row_t;

struct mm_engine {
	char path[1024];
	char vec_path[1024];
	uint32_t rec;
	uint32_t entry_hd;
	uint32_t vec_hd;
	stoma_db_t *fts;
	int fts_dirty;
	int fts_built;
};

enum { MM_LEVEL_RAW = 0, MM_LEVEL_COND = 1, MM_LEVEL_SUM = 2 };

static int mm_register_record(uint32_t *rec)
{
	static int done = 0;
	static uint32_t r = 0;

	if (done) {
		*rec = r;
		return 0;
	}
	{
		qmap_record_field_t fields[] = {
			{ "ts",    QM_STR, offsetof(mm_entry_t, ts),    sizeof(((mm_entry_t *)0)->ts),    0, 0, NULL },
			{ "topic", QM_STR, offsetof(mm_entry_t, topic), sizeof(((mm_entry_t *)0)->topic), 0, 0, NULL },
			{ "tags",  QM_STR, offsetof(mm_entry_t, tags),  sizeof(((mm_entry_t *)0)->tags),  0, 0, NULL },
			{ "text",  QM_STR, offsetof(mm_entry_t, text),  sizeof(((mm_entry_t *)0)->text),  0, 0, NULL },
			{ "level", QM_U32, offsetof(mm_entry_t, level), sizeof(uint32_t),                 0, 0, NULL },
		};
		r = qmap_record_register("mem", sizeof(mm_entry_t),
		                         fields, (sizeof(fields) / sizeof(fields[0])));
	}
	if (r == QM_MISS)
		return -1;
	done = 1;
	*rec = r;
	return 0;
}

/* ---- small dynamic string array ------------------------------------ */

static void strs_init(strs_t *s)
{
	s->buf = NULL;
	s->n = 0;
	s->cap = 0;
}

static void strs_free(strs_t *s)
{
	size_t i;
	for (i = 0; i < s->n; i++)
		free(s->buf[i]);
	free(s->buf);
	memset(s, 0, sizeof(*s));
}

static int strs_push(strs_t *s, const char *key)
{
	char *copy;
	char **nb;
	if (s->n == s->cap) {
		size_t ncap = s->cap ? s->cap * 2 : 8;
		nb = realloc(s->buf, sizeof(char *) * ncap);
		if (!nb)
			return -1;
		s->buf = nb;
		s->cap = ncap;
	}
	copy = strdup(key);
	if (!copy)
		return -1;
	s->buf[s->n++] = copy;
	return 0;
}

/* ---- mipmap key helpers -------------------------------------------- */

static int mm_valid_month(const char *ts)
{
	size_t i;
	if (strlen(ts) != 7 || ts[4] != '-')
		return 0;
	for (i = 0; i < 7; i++) {
		if (i == 4)
			continue;
		if (!isdigit((unsigned char)ts[i]))
			return 0;
	}
	return 1;
}

int mm_key_make(const mm_entry_t *e, char *out, size_t outsz)
{
	char kts[MM_KEY_LEN];
	if (!e->ts[0])
		return -1;
	if (mm_keyify(e->ts, kts, sizeof(kts)) < 0)
		return -1;
	if (e->level == MM_LEVEL_RAW) {
		if (e->topic[0])
			return -1;
		return snprintf(out, outsz, "@%s", kts) >= (int)outsz ? -1 : 0;
	}
	if (!e->topic[0])
		return -1;
	if (e->level == MM_LEVEL_SUM && !mm_valid_month(e->ts))
		return -1;
	if (e->level != MM_LEVEL_COND && e->level != MM_LEVEL_SUM)
		return -1;
	return snprintf(out, outsz, "%s@%s", e->topic, kts) >= (int)outsz ? -1 : 0;
}

int mm_key_level(const char *key)
{
	const char *at;
	const char *suffix;
	if (!key || !key[0])
		return -1;
	if (key[0] == '@') {
		if (!key[1])
			return -1;
		return MM_LEVEL_RAW;
	}
	at = strchr(key, '@');
	if (!at || !at[1])
		return -1;
	suffix = at + 1;
	if (mm_valid_month(suffix))
		return MM_LEVEL_SUM;
	return MM_LEVEL_COND;
}

/*
 * Composite keys: record maps split "struct:field". ISO timestamps contain
 * ':' which would corrupt keys, so key form always strips ':'.
 */
int mm_keyify(const char *in, char *out, size_t outsz)
{
	size_t w = 0;
	if (!in || !out || outsz == 0)
		return -1;
	while (*in) {
		if (*in != ':') {
			if (w + 1 >= outsz)
				return -1;
			out[w++] = *in;
		}
		in++;
	}
	out[w] = '\0';
	return 0;
}

/* ---- open/close ---------------------------------------------------- */

mm_t *mm_open(const char *path, char *err, size_t errsz)
{
	mm_t *mm;
	uint32_t vtype;

	if (!path || !path[0]) {
		if (err && errsz)
			snprintf(err, errsz, "mm: empty memory file path");
		return NULL;
	}
	mm = calloc(1, sizeof(*mm));
	if (!mm)
		return NULL;
	strncpy(mm->path, path, sizeof(mm->path) - 1);
	snprintf(mm->vec_path, sizeof(mm->vec_path), "%s.vec", path);

	if (mm_register_record(&mm->rec) < 0) {
		if (err && errsz)
			snprintf(err, errsz, "mm: record registration failed");
		free(mm);
		return NULL;
	}
	vtype = qmap_record_type_id(mm->rec);

	/*
	 * Each map lives in its OWN file. libqmap's save/load of multiple
	 * maps sharing one file is positional and does not round-trip across
	 * processes (save order != load order when maps are opened in a
	 * different sequence, so side-by-side blocks get skipped on load).
	 * One map per file avoids the bug entirely.
	 */
	mm->entry_hd = qmap_open(mm->path, "entry", QM_STR, vtype,
	                         0x1FF, QM_RECORD(mm->rec) | QM_SORTED);
	if (mm->entry_hd == QM_MISS) {
		if (err && errsz)
			snprintf(err, errsz, "mm: cannot open entry database");
		free(mm);
		return NULL;
	}
	mm->vec_hd = qmap_open(mm->vec_path, "vec", QM_STR, QM_STR,
	                       0x1FF, QM_SORTED);
	if (mm->vec_hd == QM_MISS) {
		qmap_close(mm->entry_hd);
		if (err && errsz)
			snprintf(err, errsz, "mm: cannot open vector database");
		free(mm);
		return NULL;
	}
	mm->fts = NULL;
	mm->fts_dirty = 0;
	mm->fts_built = 0;
	return mm;
}

void mm_close(mm_t *mm)
{
	if (!mm)
		return;
	if (mm->fts)
		stoma_close(mm->fts);
	/*
	 * Do NOT call qmap_close on entry_hd / vec_hd here.
	 * qmap has an __attribute__((destructor)) that calls qmap_save()
	 * AFTER main() returns.  If we close the maps first they are removed
	 * from the file's id set, so the destructor sees zero maps, truncates
	 * the file to 0 bytes, and all data is lost.  Leaving the handles
	 * open lets the destructor save correctly, then close everything.
	 */
	free(mm);
}

/* ---- mutations ----------------------------------------------------- */

int mm_store(mm_t *mm, const mm_entry_t *e, char *err, size_t errsz)
{
	char key[MM_KEY_LEN];
	mm_entry_t row;

	if (!mm || !e) {
		if (err && errsz)
			snprintf(err, errsz, "mm: null argument");
		return -1;
	}
	if (mm_key_make(e, key, sizeof(key)) < 0) {
		if (err && errsz)
			snprintf(err, errsz, "mm: invalid entry (level %u, topic '%s', ts '%s')",
			         (unsigned)e->level, e->topic, e->ts);
		return -1;
	}
	memcpy(&row, e, sizeof(row));
	row.text[sizeof(row.text) - 1] = '\0';
	row.ts[sizeof(row.ts) - 1] = '\0';
	row.topic[sizeof(row.topic) - 1] = '\0';
	row.tags[sizeof(row.tags) - 1] = '\0';

	if (qmap_put(mm->entry_hd, key, &row) == QM_MISS) {
		if (err && errsz)
			snprintf(err, errsz, "mm: qmap_put failed for key %s", key);
		return -1;
	}
	mm->fts_dirty = 1;
	qmap_save();
	return 0;
}

int mm_forget(mm_t *mm, const char *key, char *err, size_t errsz)
{
	char kk[MM_KEY_LEN];
	if (!mm || !key || !key[0]) {
		if (err && errsz)
			snprintf(err, errsz, "mm: null key");
		return -1;
	}
	if (mm_keyify(key, kk, sizeof(kk)) < 0)
		return -1;
	if (!qmap_get(mm->entry_hd, kk)) {
		if (err && errsz)
			snprintf(err, errsz, "mm: key not found: %s", key);
		return -1;
	}
	qmap_del(mm->entry_hd, kk);
	mm_vec_del(mm, kk);
	mm->fts_dirty = 1;
	qmap_save();
	return 0;
}

int mm_reset(mm_t *mm, char *err, size_t errsz)
{
	if (!mm) {
		if (err && errsz)
			snprintf(err, errsz, "mm: null engine");
		return -1;
	}
	qmap_drop(mm->entry_hd);
	qmap_drop(mm->vec_hd);
	mm->fts_dirty = 1;
	qmap_save();
	return 0;
}

int mm_get(mm_t *mm, const char *key, mm_entry_t *out)
{
	const mm_entry_t *row;
	char kk[MM_KEY_LEN];
	if (!mm || !key || !out)
		return 0;
	if (mm_keyify(key, kk, sizeof(kk)) < 0)
		return 0;
	row = qmap_get(mm->entry_hd, kk);
	if (!row)
		return 0;
	memcpy(out, row, sizeof(*out));
	return 1;
}

/* ---- full-text (libstoma, lazy rebuild) ---------------------------- */

static int mm_fts_rebuild(mm_t *mm)
{
	uint32_t cur;
	const void *k;
	const void *v;

	if (!mm->fts) {
		mm->fts = stoma_open(0);
		if (!mm->fts)
			return -1;
	}
	stoma_clear(mm->fts);
	cur = qmap_iter(mm->entry_hd, NULL, 0);
	while (qmap_next(&k, &v, cur)) {
		const char *key = (const char *)k;
		const mm_entry_t *row = (const mm_entry_t *)v;
		if (row->text[0])
			stoma_index(mm->fts, "text", key, row->text);
		if (row->tags[0])
			stoma_index(mm->fts, "tags", key, row->tags);
	}
	qmap_fin(cur);
	mm->fts_dirty = 0;
	mm->fts_built = 1;
	return 0;
}

/* Collect keys matching a text query into out (strs). */
static int mm_fts_query(mm_t *mm, const char *textq, strs_t *out)
{
	uint32_t oq_hd;
	uint32_t cur;
	const void *k;
	const void *v;
	int handled = 0;
	size_t qlen;
	int phrase;

	if (mm->fts_dirty || !mm->fts_built)
		if (mm_fts_rebuild(mm) < 0)
			return -1;
	if (!mm->fts)
		return -1;

	qlen = strlen(textq);
	phrase = qlen >= 2 && textq[0] == '"' && textq[qlen - 1] == '"';
	if (phrase)
		qlen -= 2;

	oq_hd = qmap_open(NULL, NULL, QM_STR, QM_STR, 0xFF, 0);
	if (oq_hd == QM_MISS)
		return -1;

	if (qlen == 0) {
		qmap_close(oq_hd);
		return 0;
	}
	/* stoma_query takes '\0'-terminated query; phrase variant too. */
	if (phrase) {
		char *inner = malloc(qlen + 1);
		if (!inner) {
			qmap_close(oq_hd);
			return -1;
		}
		memcpy(inner, textq + 1, qlen);
		inner[qlen] = '\0';
		stoma_query_phrase(mm->fts, "text", inner, oq_hd, &handled);
		free(inner);
	} else {
		stoma_query(mm->fts, "text", textq, oq_hd, &handled);
	}
	if (!handled) {
		qmap_close(oq_hd);
		return 0;
	}

	cur = qmap_iter(oq_hd, NULL, 0);
	while (qmap_next(&k, &v, cur)) {
		if (strs_push(out, (const char *)k) < 0) {
			qmap_fin(cur);
			qmap_close(oq_hd);
			return -1;
		}
	}
	qmap_fin(cur);
	qmap_close(oq_hd);
	return 0;
}

static int strs_has(const strs_t *s, const char *key)
{
	size_t i;
	for (i = 0; i < s->n; i++)
		if (strcmp(s->buf[i], key) == 0)
			return 1;
	return 0;
}

static int mm_cmp_desc(const void *a, const void *b)
{
	return strcmp(*(const char *const *)b, *(const char *const *)a);
}

mm_hit_t *mm_scan(mm_t *mm, const char *topic, const char *prefix,
                  const char *textq, int level, size_t max, size_t *n)
{
	return mm_semantic_scan(mm, topic, prefix, textq, level, max,
	                        NULL, 0, 0.0, n);
}

typedef struct {
	char key[MM_KEY_LEN];
	double score;
} scored_t;

static int scored_cmp(const void *a, const void *b)
{
	const scored_t *sa = (const scored_t *)a;
	const scored_t *sb = (const scored_t *)b;
	if (sb->score > sa->score)
		return 1;
	if (sb->score < sa->score)
		return -1;
	/* ties: newest key first, same as plain mm_scan */
	return strcmp(sb->key, sa->key);
}

mm_hit_t *mm_semantic_scan(mm_t *mm, const char *topic, const char *prefix,
                           const char *textq, int level, size_t max,
                           const float *q, size_t qdim, double min_sim,
                           size_t *n)
{
	strs_t cand;
	uint32_t cur = 0;
	const void *k;
	const void *v;
	size_t i;
	size_t many = 0;
	mm_hit_t *hits = NULL;
	scored_t *sc = NULL;
	float *qb = NULL;

	if (n)
		*n = 0;
	if (!mm)
		return NULL;

	strs_init(&cand);
	strs_t fkeys;
	strs_init(&fkeys);

	if (textq && textq[0]) {
		if (mm_fts_query(mm, textq, &fkeys) < 0)
			goto done;
	}

	if (prefix && prefix[0]) {
		char kp[MM_KEY_LEN];
		size_t plen;
		if (mm_keyify(prefix, kp, sizeof(kp)) < 0)
			goto done;
		plen = strlen(kp);
		cur = qmap_iter(mm->entry_hd, kp, QM_RANGE);
		while (qmap_next(&k, &v, cur)) {
			const char *key = (const char *)k;
			if (strncmp(key, kp, plen) != 0)
				break;
			if (strs_push(&cand, key) < 0)
				break;
		}
		qmap_fin(cur);
	} else if (topic && topic[0]) {
		char pt[MM_KEY_LEN];
		size_t plen;
		snprintf(pt, sizeof(pt), "%s@", topic);
		plen = strlen(pt);
		cur = qmap_iter(mm->entry_hd, pt, QM_RANGE);
		while (qmap_next(&k, &v, cur)) {
			const char *key = (const char *)k;
			if (strncmp(key, pt, plen) != 0)
				break;
			if (strs_push(&cand, key) < 0)
				break;
		}
		qmap_fin(cur);
	} else {
		cur = qmap_iter(mm->entry_hd, NULL, 0);
		while (qmap_next(&k, &v, cur)) {
			if (strs_push(&cand, (const char *)k) < 0)
				break;
		}
		qmap_fin(cur);
	}

	/* filter: full-text hits + level (free the strdup'd keys we drop) */
	for (i = 0; i < cand.n; i++) {
		const char *key = cand.buf[i];
		if ((textq && textq[0] && !strs_has(&fkeys, key)) ||
		    (level >= 0 && mm_key_level(key) != level)) {
			free(cand.buf[i]);
			cand.buf[i] = NULL;
		}
	}
	/* compact candidates (NULL holes) */
	{
		size_t w = 0;
		for (i = 0; i < cand.n; i++) {
			if (cand.buf[i]) {
				if (w != i) {
					cand.buf[w] = cand.buf[i];
					cand.buf[i] = NULL;
				}
				w++;
			}
		}
		cand.n = w;
	}

	if (q && qdim > 0) {
		/* semantic: score every candidate against the query vector */
		size_t nsc = 0;
		qb = malloc(qdim * sizeof(float));
		if (!qb || cand.n == 0)
			goto done;
		sc = calloc(cand.n, sizeof(*sc));
		if (!sc)
			goto done;
		for (i = 0; i < cand.n; i++) {
			const char *key = cand.buf[i];
			double score;
			if (mm_vec_dim(mm, key) != qdim) /* no vector, or dim mismatch */
				continue;
			mm_vec_get(mm, key, qb, qdim);
			score = (double)mm_cosine(q, qb, qdim);
			if (score < min_sim)
				continue;
			strncpy(sc[nsc].key, key, sizeof(sc[nsc].key) - 1);
			sc[nsc].score = score;
			nsc++;
		}
		qsort(sc, nsc, sizeof(*sc), scored_cmp);

		many = nsc;
		if (max > 0 && many > max)
			many = max;
		if (many == 0)
			goto done;
		hits = calloc(many, sizeof(mm_hit_t));
		if (!hits)
			goto done;
		for (i = 0; i < many; i++) {
			const char *key = sc[i].key;
			const mm_entry_t *row = qmap_get(mm->entry_hd, key);
			if (!row)
				continue;
			strncpy(hits[i].key, key, sizeof(hits[i].key) - 1);
			strncpy(hits[i].ts, row->ts, sizeof(hits[i].ts) - 1);
			strncpy(hits[i].tags, row->tags, sizeof(hits[i].tags) - 1);
			hits[i].level = row->level;
			hits[i].score = sc[i].score;
			hits[i].text = strdup(row->text);
		}
		goto done;
	}

	qsort(cand.buf, cand.n, sizeof(char *), mm_cmp_desc);

	many = cand.n;
	if (max > 0 && many > max)
		many = max;
	if (many == 0)
		goto done;

	hits = calloc(many, sizeof(mm_hit_t));
	if (!hits)
		goto done;

	for (i = 0; i < many; i++) {
		const char *key = cand.buf[i];
		const mm_entry_t *row = qmap_get(mm->entry_hd, key);
		if (!row)
			continue;
		strncpy(hits[i].key, key, sizeof(hits[i].key) - 1);
		strncpy(hits[i].ts, row->ts, sizeof(hits[i].ts) - 1);
		strncpy(hits[i].tags, row->tags, sizeof(hits[i].tags) - 1);
		hits[i].level = row->level;
		hits[i].text = strdup(row->text);
	}

done:
	strs_free(&cand);
	strs_free(&fkeys);
	free(sc);
	free(qb);
	if (n)
		*n = many;
	return hits;
}

void mm_hits_free(mm_hit_t *hits, size_t n)
{
	size_t i;
	if (!hits)
		return;
	for (i = 0; i < n; i++)
		free(hits[i].text);
	free(hits);
}

/* ---- vectors (dimension-tagged; provider-external) ------------------ */

int mm_vec_put(mm_t *mm, const char *key, const float *v, size_t n)
{
	size_t i;
	char buf[8192];
	size_t off = 0;
	size_t need;
	if (!mm || !v)
		return -1;
	need = 20 + n * 24;
	if (need > sizeof(buf) || n > 512) {
		fprintf(stderr, "mm: vector too large (%zu dims)\n", n);
		return -1;
	}
	off = (size_t)snprintf(buf, sizeof(buf), "%zu", n);
	for (i = 0; i < n; i++)
		off += (size_t)snprintf(buf + off, sizeof(buf) - off, " %f", (double)v[i]);
	qmap_put(mm->vec_hd, key, buf);
	qmap_save();
	return 0;
}

size_t mm_vec_dim(mm_t *mm, const char *key)
{
	const char *s;
	size_t n;
	(void)n;
	s = qmap_get(mm->vec_hd, key);
	if (!s)
		return 0;
	return (size_t)strtoul(s, NULL, 10);
}

size_t mm_vec_get(mm_t *mm, const char *key, float *out, size_t max)
{
	const char *s;
	size_t dim;
	size_t i;
	const char *p;
	char *end;

	s = qmap_get(mm->vec_hd, key);
	if (!s)
		return 0;
	dim = (size_t)strtoul(s, NULL, 10);
	if (dim > max)
		dim = max;
	p = s;
	while (*p && *p != ' ')
		p++;
	for (i = 0; i < dim; i++) {
		out[i] = strtof(p, &end);
		p = end;
	}
	return dim;
}

void mm_vec_del(mm_t *mm, const char *key)
{
	if (qmap_get(mm->vec_hd, key))
		qmap_del(mm->vec_hd, key);
}

float mm_cosine(const float *a, const float *b, size_t n)
{
	float dot = 0.0f, na = 0.0f, nb = 0.0f;
	size_t i;
	for (i = 0; i < n; i++) {
		dot += a[i] * b[i];
		na += a[i] * a[i];
		nb += b[i] * b[i];
	}
	if (na <= 0.0f || nb <= 0.0f)
		return 0.0f;
	return (float)((double)dot / sqrt((double)na * (double)nb));
}