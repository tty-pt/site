#include "engine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int fails = 0;
static int checks = 0;

#define CHECK(cond, msg)                                                     \
	do {                                                          \
		checks++;                                             \
		if (!(cond)) {                                         \
			fails++;                                         \
			fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__,   \
			        __LINE__, msg);                          \
		}                                                      \
	} while (0)

static char g_path[256];

static mm_t *t_open(char *err, size_t errsz)
{
	return mm_open(g_path, err, errsz);
}

static void seed(mm_t *mm)
{
	mm_entry_t e = {0};

	e.level = 2;
	strcpy(e.topic, "mirror");
	strcpy(e.ts, "2025-05");
	strcpy(e.text, "May gist: mirror and rain.");
	CHECK(mm_store(mm, &e, NULL, 0) == 0, "store L2 mirror 2025-05");

	strcpy(e.topic, "mirror");
	strcpy(e.ts, "2025-06");
	strcpy(e.text, "June gist: puddle.");
	CHECK(mm_store(mm, &e, NULL, 0) == 0, "store L2 mirror 2025-06");

	strcpy(e.topic, "song");
	strcpy(e.ts, "2025-05");
	strcpy(e.text, "Song gist.");
	CHECK(mm_store(mm, &e, NULL, 0) == 0, "store L2 song 2025-05");

	e.level = 1;
	strcpy(e.topic, "mirror");
	strcpy(e.ts, "2025-05-14T17:05");
	strcpy(e.tags, "metaphor\npuddle");
	strcpy(e.text, "condensed insight A");
	CHECK(mm_store(mm, &e, NULL, 0) == 0, "store L1 mirror 17:05");

	strcpy(e.topic, "mirror");
	strcpy(e.ts, "2025-05-14T17:07");
	strcpy(e.tags, "");
	strcpy(e.text, "condensed insight B");
	CHECK(mm_store(mm, &e, NULL, 0) == 0, "store L1 mirror 17:07");

	e.level = 0;
	strcpy(e.topic, "");
	strcpy(e.ts, "2025-05-14T17:05");
	strcpy(e.tags, "");
	strcpy(e.text, "user: raw transcript of looking into a mirror");
	CHECK(mm_store(mm, &e, NULL, 0) == 0, "store L0 raw @17:05");
}

static int hits_of(mm_t *mm, const char *topic, const char *prefix,
                   const char *q, int level, size_t max, mm_hit_t **out)
{
	size_t n;
	*out = mm_scan(mm, topic, prefix, q, level, max, &n);
	return (int)n;
}

static void test_key_helpers(void)
{
	printf("test_key_helpers\n");

	mm_entry_t e = {0};
	char key[MM_KEY_LEN];

	e.level = 0;
	strcpy(e.ts, "2025-05-14T17:05");
	CHECK(mm_key_make(&e, key, sizeof(key)) == 0, "L0 key ok");
	CHECK(strcmp(key, "@2025-05-14T1705") == 0, "L0 key value (colons stripped)");
	CHECK(mm_key_level(key) == 0, "L0 level");

	e.level = 1;
	strcpy(e.topic, "mirror");
	strcpy(e.ts, "2025-05-14T17:05");
	CHECK(mm_key_make(&e, key, sizeof(key)) == 0, "L1 key ok");
	CHECK(strcmp(key, "mirror@2025-05-14T1705") == 0, "L1 key value");
	CHECK(mm_key_level(key) == 1, "L1 level");

	e.level = 2;
	strcpy(e.topic, "mirror");
	strcpy(e.ts, "2025-05");
	CHECK(mm_key_make(&e, key, sizeof(key)) == 0, "L2 key ok");
	CHECK(strcmp(key, "mirror@2025-05") == 0, "L2 key value");
	CHECK(mm_key_level(key) == 2, "L2 level");

	e.level = 2;
	strcpy(e.ts, "2025-05-14"); /* not a month */
	CHECK(mm_key_make(&e, key, sizeof(key)) != 0, "L2 rejects full ts");

	e.level = 1;
	e.topic[0] = '\0';
	CHECK(mm_key_make(&e, key, sizeof(key)) != 0, "L1 requires topic");

	CHECK(mm_key_level("junk") == -1, "invalid key rejected");
	CHECK(mm_key_level("a@b") == 1, "topic@b is L1");
}

static void test_roundtrip(void)
{
	printf("test_roundtrip\n");
	char err[256];
	mm_t *mm = t_open(err, sizeof(err));
	CHECK(mm != NULL, "open works");
	if (!mm)
		return;

	seed(mm);

	mm_entry_t row;
	CHECK(mm_get(mm, "mirror@2025-05", &row) == 1, "get L2 mirror");
	CHECK(strcmp(row.text, "May gist: mirror and rain.") == 0, "L2 text");
	CHECK(row.level == 2, "L2 level saved");

	CHECK(mm_get(mm, "@2025-05-14T17:05", &row) == 1, "get L0 raw");
	CHECK(strcmp(row.text,
	             "user: raw transcript of looking into a mirror") == 0,
	      "L0 text");
	CHECK(mm_get(mm, "nope@2025-05", &row) == 0, "get missing");

	mm_close(mm);
}

static void test_scan_routing(void)
{
	printf("test_scan_routing\n");
	char err[256];
	mm_t *mm = t_open(err, sizeof(err));
	mm_hit_t *hits;
	int n;

	if (!mm)
		return;
	seed(mm);

	n = hits_of(mm, "mirror", NULL, NULL, 2, 0, &hits);
	CHECK(n == 2, "mirror level 2 has 2 entries");
	CHECK(strcmp(hits[0].key, "mirror@2025-06") == 0, "newest first");
	CHECK(strcmp(hits[1].text, "May gist: mirror and rain.") == 0, "L2 text");
	mm_hits_free(hits, (size_t)n);

	n = hits_of(mm, "mirror", NULL, NULL, 1, 0, &hits);
	CHECK(n == 2, "mirror level 1 has 2 entries");
	CHECK(strcmp(hits[0].key, "mirror@2025-05-14T1707") == 0, "L1 newest first");
	mm_hits_free(hits, (size_t)n);

	n = hits_of(mm, NULL, "@2025-05-14T17:05", NULL, -1, 0, &hits);
	CHECK(n == 1, "prefix zoom to raw");
	CHECK(hits[0].level == 0, "raw level");
	mm_hits_free(hits, (size_t)n);

	n = hits_of(mm, "mirror", NULL, NULL, 0, 0, &hits);
	CHECK(n == 0, "topic routing yields nothing at level 0");
	mm_hits_free(hits, (size_t)n);

	n = hits_of(mm, NULL, NULL, NULL, -1, 0, &hits);
	CHECK(n == 6, "unfiltered scan sees all entries");
	mm_hits_free(hits, (size_t)n);

	n = hits_of(mm, "mirror", NULL, NULL, 2, 1, &hits);
	CHECK(n == 1, "max=1 truncates");
	CHECK(strcmp(hits[0].key, "mirror@2025-06") == 0, "max keeps newest");
	mm_hits_free(hits, (size_t)n);

	mm_close(mm);
}

static void test_fts(void)
{
	printf("test_fts\n");
	char err[256];
	mm_t *mm = t_open(err, sizeof(err));
	mm_hit_t *hits;
	int n;

	if (!mm)
		return;
	seed(mm);

	/* single token, prefix match */
	n = hits_of(mm, NULL, NULL, "puddle", -1, 0, &hits);
	CHECK(n == 1, "puddle matches June gist");
	CHECK(strcmp(hits[0].key, "mirror@2025-06") == 0, "puddle hit is June");
	mm_hits_free(hits, (size_t)n);

	/* multi-token AND */
	n = hits_of(mm, NULL, NULL, "mirror rain", -1, 0, &hits);
	CHECK(n == 1, "both tokens AND");
	mm_hits_free(hits, (size_t)n);

	/* absent token */
	n = hits_of(mm, NULL, NULL, "mirror zzzqq", -1, 0, &hits);
	CHECK(n == 0, "absent token no match");
	mm_hits_free(hits, (size_t)n);

	/* combine fts with topic+level routing */
	n = hits_of(mm, "mirror", NULL, "mirror", 2, 0, &hits);
	CHECK(n == 1, "topic+level+fts combined");
	mm_hits_free(hits, (size_t)n);

	/* phrase query (quoted) */
	n = hits_of(mm, NULL, NULL, "\"condensed insight A\"", -1, 0, &hits);
	CHECK(n == 1, "phrase matches contiguous");
	mm_hits_free(hits, (size_t)n);

	n = hits_of(mm, NULL, NULL, "\"insight A condensed\"", -1, 0, &hits);
	CHECK(n == 0, "phrase rejects reordered tokens");
	mm_hits_free(hits, (size_t)n);

	/* accent sensitivity: pão != pao */
	mm_entry_t e = {0};
	e.level = 1;
	strcpy(e.topic, "food");
	strcpy(e.ts, "2025-05-14T17:20");
	strcpy(e.text, "A refeição tinha pão fresco.");
	CHECK(mm_store(mm, &e, NULL, 0) == 0, "store accent entry");
	n = hits_of(mm, NULL, NULL, "pao", -1, 0, &hits);
	CHECK(n == 0, "pao does not match pão");
	mm_hits_free(hits, (size_t)n);
	n = hits_of(mm, NULL, NULL, "pão", -1, 0, &hits);
	CHECK(n == 1, "pão matches pão");
	mm_hits_free(hits, (size_t)n);

	mm_close(mm);
}

static void test_forget(void)
{
	printf("test_forget\n");
	char err[256];
	mm_t *mm = t_open(err, sizeof(err));
	mm_hit_t *hits;
	mm_entry_t row;
	int n;

	if (!mm)
		return;
	seed(mm);

	CHECK(mm_forget(mm, "mirror@2025-05", err, sizeof(err)) == 0, "forget ok");
	CHECK(mm_get(mm, "mirror@2025-05", &row) == 0, "entry gone");
	n = hits_of(mm, "mirror", NULL, NULL, 2, 0, &hits);
	CHECK(n == 1, "level 2 scan excludes forgotten");
	mm_hits_free(hits, (size_t)n);
	/* FTS rebuilt after forget: mirror should no longer match the L2 set */
	n = hits_of(mm, NULL, NULL, "mirror rain", -1, 0, &hits);
	CHECK(n == 0, "fts drops forgotten entry");

	CHECK(mm_forget(mm, "missing@2025-05", err, sizeof(err)) != 0,
	      "forget missing key errors");

	mm_close(mm);
}

static void test_persistence(void)
{
	printf("test_persistence\n");
	char err[256];
	mm_t *mm;
	mm_hit_t *hits;
	int n;

	mm = t_open(err, sizeof(err));
	if (!mm)
		return;
	seed(mm);
	mm_close(mm);

	mm = t_open(err, sizeof(err));
	CHECK(mm != NULL, "reopen works");
	if (!mm)
		return;
	n = hits_of(mm, "mirror", NULL, NULL, 2, 0, &hits);
	CHECK(n == 2, "data survives reopen");
	mm_hits_free(hits, (size_t)n);
	n = hits_of(mm, NULL, NULL, "mirror rain", -1, 0, &hits);
	CHECK(n == 1, "fts rebuilds from store after reopen");
	mm_hits_free(hits, (size_t)n);
	mm_close(mm);
}

static void test_reset(void)
{
	printf("test_reset\n");
	char err[256];
	mm_t *mm;
	mm_hit_t *hits;
	int n;

	mm = t_open(err, sizeof(err));
	if (!mm)
		return;
	seed(mm);
	CHECK(mm_reset(mm, err, sizeof(err)) == 0, "reset ok");
	n = hits_of(mm, NULL, NULL, NULL, -1, 0, &hits);
	CHECK(n == 0, "reset empties store");
	mm_hits_free(hits, (size_t)n);
	mm_close(mm);

	mm = t_open(err, sizeof(err));
	if (!mm)
		return;
	n = hits_of(mm, NULL, NULL, NULL, -1, 0, &hits);
	CHECK(n == 0, "reset persists across reopen");
	mm_hits_free(hits, (size_t)n);
	mm_close(mm);
}

static void test_vectors(void)
{
	printf("test_vectors\n");
	char err[256];
	mm_t *mm = t_open(err, sizeof(err));
	float a[4] = { 0.0f, 1.0f, 0.0f, 0.0f };
	float b[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
	float out[8];
	size_t n;

	if (!mm)
		return;

	CHECK(mm_vec_put(mm, "@2025-05-14", a, 4) == 0, "vec put");
	CHECK(mm_vec_dim(mm, "@2025-05-14") == 4, "vec dim");
	n = mm_vec_get(mm, "@2025-05-14", out, 8);
	CHECK(n == 4, "vec get count");
	CHECK(out[0] == 0.0f && out[1] == 1.0f, "vec values");
	CHECK(mm_cosine(a, b, 4) < 0.0001f, "orthogonal cosine ~ 0");
	CHECK(mm_cosine(a, a, 4) > 0.9999f, "self cosine ~ 1");
	CHECK(mm_vec_dim(mm, "missing@x") == 0, "vec dim missing");

	mm_vec_del(mm, "@2025-05-14");
	CHECK(mm_vec_dim(mm, "@2025-05-14") == 0, "vec del");
	mm_close(mm);
}

static void test_big_vectors(void)
{
	printf("test_big_vectors\n");
	char err[256];
	mm_t *mm = t_open(err, sizeof(err));
	const size_t D = 896;
	const size_t D_OTHER = 768;
	float *a, *b, *q, *out, *other, *huge;
	mm_hit_t *hits;
	size_t i, nn;
	int n;

	if (!mm)
		return;

	a = malloc(D * sizeof(float));
	b = malloc(D * sizeof(float));
	q = malloc(D * sizeof(float));
	out = malloc(D * sizeof(float));
	other = malloc(D_OTHER * sizeof(float));
	huge = malloc((MM_VEC_MAX + 1) * sizeof(float));
	if (!a || !b || !q || !out || !other || !huge) {
		CHECK(0, "big_vectors: allocation");
		free(a); free(b); free(q); free(out); free(other); free(huge);
		mm_close(mm);
		return;
	}
	/* a = one-hot at 0; b = one-hot at 1 (orthogonal to a); q = a. */
	for (i = 0; i < D; i++) {
		a[i] = (i == 0) ? 1.0f : 0.0f;
		b[i] = (i == 1) ? 1.0f : 0.0f;
		q[i] = a[i];
	}
	for (i = 0; i < D_OTHER; i++)
		other[i] = 0.5f;
	for (i = 0; i <= MM_VEC_MAX; i++)
		huge[i] = 1.0f;

	/* pure cosine on 896 dims */
	CHECK(mm_cosine(a, a, D) > 0.9999f, "896-dim self cosine ~ 1");
	CHECK(mm_cosine(a, b, D) < 0.0001f, "896-dim orthogonal cosine ~ 0");

	/* over-cap put is rejected cleanly, never truncated */
	CHECK(mm_vec_put(mm, "huge@9999-99", huge, MM_VEC_MAX + 1) == -1,
	      "MM_VEC_MAX+1 put rejected");

	/* three L2 "big" entries: 896, 896(orth), 768 (dim mismatch) */
	{
		mm_entry_t e = { 0 };
		e.level = 2;
		strcpy(e.topic, "big");
		strcpy(e.ts, "2025-05");
		strcpy(e.text, "big a");
		CHECK(mm_store(mm, &e, NULL, 0) == 0, "store big 05");
		CHECK(mm_vec_put(mm, "big@2025-05", a, D) == 0, "vec 896 a");
		strcpy(e.ts, "2025-06");
		strcpy(e.text, "big b");
		CHECK(mm_store(mm, &e, NULL, 0) == 0, "store big 06");
		CHECK(mm_vec_put(mm, "big@2025-06", b, D) == 0, "vec 896 b");
		strcpy(e.ts, "2025-07");
		strcpy(e.text, "big other");
		CHECK(mm_store(mm, &e, NULL, 0) == 0, "store big 07");
		CHECK(mm_vec_put(mm, "big@2025-07", other, D_OTHER) == 0,
		      "vec 768 (mismatch)");
	}

	/* dim round-trip */
	CHECK(mm_vec_dim(mm, "big@2025-05") == D, "896-dim round-trip");
	n = (int)mm_vec_get(mm, "big@2025-05", out, D);
	CHECK(n == 896, "896-dim vec get count");
	CHECK(out[0] == 1.0f, "896-dim first value");
	CHECK(out[D - 1] == 0.0f, "896-dim last value");

	/* over-max get reports a mismatch, not a truncation */
	n = (int)mm_vec_get(mm, "big@2025-05", out, 512);
	CHECK(n == 0, "vec get with max < dim returns 0 (mismatch)");

	/* semantic scan: only the two 896-dim entries rank; 768 skipped */
	{
		hits = mm_semantic_scan(mm, NULL, NULL, NULL, -1, 0, q, D, 0.0, &nn);
		n = (int)nn;
	}
	CHECK(n == 2, "semantic: only 896-dim entries rank");
	CHECK(strcmp(hits[0].key, "big@2025-05") == 0,
	      "semantic: highest 896 cosine first");
	CHECK(hits[0].score > 0.9999, "semantic: 896 self cosine ~ 1");
	CHECK(hits[1].score < 0.0001, "semantic: orthogonal 896 cosine ~ 0");
	for (i = 0; i < (size_t)n; i++)
		CHECK(strcmp(hits[i].key, "big@2025-07") != 0,
		      "semantic: 768-dim entry skipped");
	mm_hits_free(hits, nn);

	/* topic/level routing still composes under big-dim semantic query */
	{
		hits = mm_semantic_scan(mm, "big", NULL, NULL, 2, 0, q, D, 0.0, &nn);
		n = (int)nn;
	}
	CHECK(n == 2, "semantic: topic+level routing under 896-dim query");
	mm_hits_free(hits, nn);

	free(a); free(b); free(q); free(out); free(other); free(huge);
	mm_close(mm);
}

static void test_semantic(void)
{
	printf("test_semantic\n");
	char err[256];
	mm_t *mm = t_open(err, sizeof(err));
	mm_hit_t *hits;
	int n;
	int i;
	float x[3] = { 1.0f, 0.0f, 0.0f };
	float y[3] = { 0.0f, 1.0f, 0.0f };
	float xy[3] = { 0.9f, 0.1f, 0.0f };
	float u2[2] = { 1.0f, 0.0f };

	if (!mm)
		return;
	seed(mm);

	/* vectors on three mirror entries */
	CHECK(mm_vec_put(mm, "mirror@2025-05", x, 3) == 0, "vec May");
	CHECK(mm_vec_put(mm, "mirror@2025-06", y, 3) == 0, "vec June");
	CHECK(mm_vec_put(mm, "mirror@2025-05-14T1705", xy, 3) == 0, "vec L1");
	/* an entry with a DIFFERENT dimension: must be skipped, not compared */
	CHECK(mm_vec_put(mm, "mirror@2025-05-14T1707", u2, 2) == 0, "vec other dim");
	/* song + L0 raw have no vectors at all: must be skipped */

	hits = NULL;
	n = (int)(size_t)0;
	{
		size_t nn;
		hits = mm_semantic_scan(mm, NULL, NULL, NULL, -1, 0, x, 3, 0.0, &nn);
		n = (int)nn;
	}
	CHECK(n == 3, "semantic: only vector-bearing 3-dim entries");
	for (i = 0; i < n; i++) {
		if (strcmp(hits[i].key, "mirror@2025-05-14T1707") == 0)
			CHECK(0, "semantic: dim-mismatch entry excluded");
		if (strcmp(hits[i].key, "song@2025-05") == 0)
			CHECK(0, "semantic: vector-less entry excluded");
	}
	CHECK(strcmp(hits[0].key, "mirror@2025-05") == 0,
	      "semantic: highest cosine first");
	CHECK(hits[0].score > 0.9999, "semantic: self cosine ~ 1");
	CHECK(strcmp(hits[1].key, "mirror@2025-05-14T1705") == 0,
	      "semantic: 0.9-x cosine second");
	CHECK(hits[1].score > 0.99 && hits[1].score < 1.0,
	      "semantic: cosine ~0.994");
	/* hits[2] is the orthogonal y -> cosine 0, last. */
	CHECK(hits[2].score < 0.0001, "semantic: orthogonal cosine ~ 0");
	mm_hits_free(hits, (size_t)n);

	/* --like equivalent: query with the stored xy vector */
	{
		size_t nn;
		hits = mm_semantic_scan(mm, NULL, NULL, NULL, -1, 0, xy, 3, 0.0, &nn);
		n = (int)nn;
	}
	CHECK(strcmp(hits[0].key, "mirror@2025-05-14T1705") == 0,
	      "semantic: query xy ranks L1 first");
	mm_hits_free(hits, (size_t)n);

	/* min_sim filters */
	{
		size_t nn;
		hits = mm_semantic_scan(mm, NULL, NULL, NULL, -1, 0, x, 3, 0.5, &nn);
		n = (int)nn;
	}
	CHECK(n == 2, "semantic: min_sim 0.5 keeps 2 entries");
	mm_hits_free(hits, (size_t)n);

	/* max cap */
	{
		size_t nn;
		hits = mm_semantic_scan(mm, NULL, NULL, NULL, -1, 1, x, 3, 0.0, &nn);
		n = (int)nn;
	}
	CHECK(n == 1, "semantic: max cap");
	CHECK(strcmp(hits[0].key, "mirror@2025-05") == 0,
	      "semantic: max keeps top cosine");
	mm_hits_free(hits, (size_t)n);

	/* routing still composes (topic mirror, level 2) */
	{
		size_t nn;
		hits = mm_semantic_scan(mm, "mirror", NULL, NULL, 2, 0, x, 3, 0.0, &nn);
		n = (int)nn;
	}
	CHECK(n == 2, "semantic: topic+level routing under vector query");
	CHECK(strcmp(hits[0].key, "mirror@2025-05") == 0,
	      "semantic: routing keeps ranking");
	mm_hits_free(hits, (size_t)n);

	/* no query vector -> plain mm_scan (wrapper) unaffected */
	{
		size_t nn;
		hits = mm_scan(mm, "mirror", NULL, NULL, 2, 0, &nn);
		n = (int)nn;
	}
	CHECK(n == 2, "plain scan still works after semantic additions");
	CHECK(hits[0].score == 0.0, "plain scan has no score");
	mm_hits_free(hits, (size_t)n);

	mm_close(mm);
}

int main(void)
{
	char tmpl[] = "/tmp/mm_test_XXXXXX";
	int fd;

	fd = mkstemp(tmpl);
	if (fd < 0) {
		fprintf(stderr, "mkstemp failed\n");
		return 2;
	}
	close(fd);
	unlink(tmpl);
	snprintf(g_path, sizeof(g_path), "%s", tmpl);

	test_key_helpers();
	test_roundtrip();
	test_scan_routing();
	test_fts();
	test_forget();
	test_persistence();
	test_reset();
	test_vectors();
	test_big_vectors();
	test_semantic();

	if (fails == 0)
		printf("\nALL MM TESTS PASSED (%d checks)\n", checks);
	else
		fprintf(stderr, "\n%d/%d MM CHECKS FAILED\n", fails, checks);
	return fails == 0 ? 0 : 1;
}