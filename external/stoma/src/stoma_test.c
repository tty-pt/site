#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <locale.h>
#include <ttypt/qmap.h>
#include "stoma/stoma.h"

static int failures = 0;
static int total = 0;

#define CHECK(cond, name)                                                      \
	do {                                                                   \
		total++;                                                       \
		if (!(cond)) {                                                 \
			failures++;                                            \
			printf("FAIL: %s (line %d)\n", name, __LINE__);        \
		}                                                              \
	} while (0)

static int hd_has(unsigned hd, const char *row)
{
	return qmap_get(hd, row) != NULL;
}

int main(void)
{
	stoma_db_t *db = stoma_open(0);
	unsigned out = qmap_open(NULL, NULL, QM_STR, QM_STR, 0xFF, 0);
	int handled = 0;
	uint32_t n;

	if (!db || !out) {
		printf("setup failed\n");
		return 1;
	}

	/* 1. fold */
	{
		char b[64];
		int r = stoma_fold(b, sizeof(b), "Été Aéro");
		CHECK(r > 0 && strcmp(b, "été aéro") == 0, "fold accent+case");
	}

	/* 2-3. index + exact + prefix */
	stoma_index(db, "title", "r1", "Starlight of the night");
	stoma_index(db, "title", "r2", "Station one");
	stoma_index(db, "title", "r3", "st");
	stoma_index(db, "title", "r4", "A Dark Night");

	n = stoma_query(db, "title", "night", out, &handled);
	CHECK(handled == 1 && n == 2, "exact token");
	CHECK(hd_has(out, "r1") && hd_has(out, "r4"), "exact rows");
	qmap_drop(out);

	n = stoma_query(db, "title", "st", out, &handled);
	CHECK(handled == 1 && n == 3, "prefix st");
	CHECK(hd_has(out, "r1") && hd_has(out, "r2") && hd_has(out, "r3"),
	      "prefix rows");
	CHECK(!hd_has(out, "r4"), "prefix excludes non-match");
	qmap_drop(out);

	/* 4. multi-token AND (order-insensitive) */
	stoma_index(db, "title", "r5", "Black Star");
	n = stoma_query(db, "title", "black star", out, &handled);
	CHECK(handled == 1 && n == 1 && hd_has(out, "r5"), "AND two tokens");
	qmap_drop(out);
	n = stoma_query(db, "title", "star black", out, &handled);
	CHECK(n == 1 && hd_has(out, "r5"), "AND order-insensitive");
	qmap_drop(out);

	/* 5. digits-only token */
	stoma_index(db, "title", "r6", "1984");
	n = stoma_query(db, "title", "1984", out, &handled);
	CHECK(handled == 1 && n == 1 && hd_has(out, "r6"), "digits token");
	qmap_drop(out);
	n = stoma_query(db, "title", "19", out, &handled);
	CHECK(n == 1 && hd_has(out, "r6"), "digit prefix");
	qmap_drop(out);

	/* 6. zero-token query → no-op */
	n = stoma_query(db, "title", "---", out, &handled);
	CHECK(handled == 0 && n == 0, "zero-token no-op");
	qmap_drop(out);

	/* 7. per-field isolation */
	stoma_index(db, "author", "r1", "Stardust");
	n = stoma_query(db, "title", "stardust", out, &handled);
	CHECK(handled == 1 && n == 0, "field isolation");
	qmap_drop(out);

	/* 8. token dedup: repeated word matches once */
	stoma_index(db, "title", "r7", "la la la");
	n = stoma_query(db, "title", "la", out, &handled);
	CHECK(handled == 1 && n == 1 && hd_has(out, "r7"), "dedup");
	qmap_drop(out);

	/* 9. clear + re-index */
	stoma_clear(db);
	n = stoma_query(db, "title", "night", out, &handled);
	CHECK(handled == 1 && n == 0, "clear removes entries");
	qmap_drop(out);
	stoma_index(db, "title", "r9", "new world");
	n = stoma_query(db, "title", "new", out, &handled);
	CHECK(n == 1 && hd_has(out, "r9"), "re-index after clear");
	qmap_drop(out);

	/* 10. in-place put on same (field,row): re-indexing a changed value
	 * leaves stale tokens (documented) — full rebuild via clear() is the
	 * caller's contract. */
	stoma_index(db, "title", "r9", "another world");
	n = stoma_query(db, "title", "new", out, &handled);
	CHECK(n == 1, "stale token remains (documented)");
	qmap_drop(out);

	/* 11. fold: lowercase, accents preserved (accent-sensitive) */
	{
		char b[128];

		CHECK(stoma_fold(b, sizeof(b), "Straße") > 0 &&
		              strcmp(b, "straße") == 0,
		      "fold sz");
		CHECK(stoma_fold(b, sizeof(b), "Øresund ærø å") > 0 &&
		              strcmp(b, "øresund ærø å") == 0,
		      "fold oslash ae aa");
		CHECK(stoma_fold(b, sizeof(b), "El Niño açúcar") > 0 &&
		              strcmp(b, "el niño açúcar") == 0,
		      "fold ntilde ccedil");
		CHECK(stoma_fold(b, sizeof(b), "Ünter Öl Ärger") > 0 &&
		              strcmp(b, "ünter öl ärger") == 0,
		      "fold umlauts");
		CHECK(stoma_fold(b, sizeof(b), "ÉTÉ") > 0 &&
		              strcmp(b, "été") == 0,
		      "fold case");
		CHECK(stoma_fold(b, sizeof(b), "Pão") > 0 &&
		              strcmp(b, "pão") == 0,
		      "fold pao preserves accent");
		CHECK(stoma_fold(b, sizeof(b), "não senhôr çãõ") > 0 &&
		              strcmp(b, "não senhôr çãõ") == 0,
		      "fold pt accents");
		CHECK(stoma_fold(b, sizeof(b), "À É Í Ó Ú à é í ó ú") > 0 &&
		              strcmp(b, "à é í ó ú à é í ó ú") == 0,
		      "fold upper accents");
	}

	/* 12. fold buffer boundaries */
	{
		char b[4];

		/* "abc" folds to 3 bytes + NUL → 4-byte buffer is an exact fit
		 */
		CHECK(stoma_fold(b, sizeof(b), "abc") == 3 &&
		              strcmp(b, "abc") == 0,
		      "fold exact fit");
		/* "abcd" = 4 bytes + NUL → 4-byte buffer too small → -1 */
		CHECK(stoma_fold(b, sizeof(b), "abcd") == -1,
		      "fold buffer too small");
		CHECK(stoma_fold(b, sizeof(b), "") == 0, "fold empty string");
	}

	/* 13. non-Latin text is preserved and searchable (accent-sensitive) */
	{
		char b[128];

		CHECK(stoma_fold(b, sizeof(b), "駅東京") == 9 &&
		              strcmp(b, "駅東京") == 0,
		      "fold cjk verbatim");
		stoma_index(db, "title", "r13", "駅東京");
		n = stoma_query(db, "title", "駅", out, &handled);
		CHECK(handled == 1 && n == 1, "cjk searchable");
		qmap_drop(out);
	}

	/* 14. NULL/empty argument contracts */
	CHECK(stoma_index(db, NULL, "r", "v") == -1, "index null field");
	CHECK(stoma_index(db, "t", NULL, "v") == -1, "index null row");
	CHECK(stoma_index(db, "t", "r", NULL) == -1, "index null value");
	CHECK(stoma_index(NULL, "t", "r", "v") == -1, "index null db");
	{
		int h = 99;

		n = stoma_query(NULL, "t", "q", out, &h);
		CHECK(n == 0 && h == 0, "query null db");
		n = stoma_query(db, NULL, "q", out, &h);
		CHECK(n == 0 && h == 0, "query null field");
		n = stoma_query(db, "t", NULL, out, &h);
		CHECK(n == 0 && h == 0, "query null query");
		n = stoma_query(db, "t", "q", 0, &h);
		CHECK(n == 0 && h == 0, "query null out_hd");
	}
	stoma_clear(NULL);
	stoma_close(NULL);

	/* 15. empty / punctuation-only values index nothing */
	stoma_index(db, "title", "r15", "");
	stoma_index(db, "title", "r16", "... !!! ---");
	n = stoma_query(db, "title", "anything", out, &handled);
	CHECK(handled == 1 && n == 0, "empty/punct values never match");
	qmap_drop(out);

	/* 16. out_hd is appended to, NOT cleared (caller contract) */
	stoma_index(db, "title", "r17", "alpha beta");
	stoma_index(db, "title", "r18", "gamma delta");
	qmap_drop(out);
	n = stoma_query(db, "title", "alpha", out, &handled);
	CHECK(n == 1 && hd_has(out, "r17"), "query alpha");
	n = stoma_query(db, "title", "gamma", out, &handled);
	CHECK(n == 1 && hd_has(out, "r17") && hd_has(out, "r18"),
	      "query appends (union)");
	qmap_drop(out);
	n = stoma_query(db, "title", "gamma", out, &handled);
	CHECK(n == 1 && !hd_has(out, "r17") && hd_has(out, "r18"),
	      "drop between queries isolates");

	/* 17. query truncates at STOMA_MAX_TOKENS (64) */
	{
		char big1[1024], big2[1024];
		size_t i;
		int off = 0;

		/* value has tokens t0..t64 + needle = 66 tokens (index is
		 * uncapped); query below has 64 non-matching tokens + needle
		 * as token #65 → #65 is dropped → no match. */
		for (i = 0; i < 65; i++)
			off += snprintf(
			        big1 + off, sizeof(big1) - (size_t)off, "t%zu ",
			        i);
		snprintf(big1 + off, sizeof(big1) - (size_t)off, "needle");
		stoma_index(db, "title", "r19", big1);
		qmap_drop(out);
		off = 0;
		for (i = 0; i < 64; i++)
			off += snprintf(
			        big2 + off, sizeof(big2) - (size_t)off,
			        "zz%zu ", i);
		snprintf(big2 + off, sizeof(big2) - (size_t)off, "needle");
		n = stoma_query(db, "title", big2, out, &handled);
		CHECK(handled == 1 && n == 0, "token 65+ ignored in query");
		qmap_drop(out);
		n = stoma_query(db, "title", "t0 t1 t2", out, &handled);
		CHECK(n == 1 && hd_has(out, "r19"), "first tokens still match");
		qmap_drop(out);
	}

	/* 17b. value larger than the old 8KB fold buffer (regression) */
	{
		/* 8980 'a's + " quarantinemon" (14 chars) = 8994 > 8192 */
		char *big = malloc(9000);
		size_t i;
		int off = 0;

		for (i = 0; i < 8980; i++)
			off += snprintf(big + off, 9000 - (size_t)off, "a");
		snprintf(big + off, 9000 - (size_t)off, " quarantinemon");
		stoma_index(db, "title", "r19b", big);
		free(big);
		qmap_drop(out);
		n = stoma_query(db, "title", "quarantinemon", out, &handled);
		CHECK(handled == 1 && n == 1 && hd_has(out, "r19b"),
		      "token past 8KB matches");
		qmap_drop(out);
		n = stoma_query(db, "title", "aaaa", out, &handled);
		CHECK(n == 1 && hd_has(out, "r19b"),
		      "early tokens still match");
		qmap_drop(out);
	}

	/* 18. query normalization: punctuation and extra spaces */
	stoma_index(db, "title", "r20", "Black Star");
	n = stoma_query(db, "title", "black,  star", out, &handled);
	CHECK(handled == 1 && n == 1 && hd_has(out, "r20"), "punct+spaces AND");
	qmap_drop(out);
	n = stoma_query(db, "title", "  BLACK   STAR ", out, &handled);
	CHECK(handled == 1 && n == 1 && hd_has(out, "r20"), "case+spaces AND");
	qmap_drop(out);

	/* 19. same token in two fields of one row stays field-isolated */
	stoma_index(db, "title", "r21", "love");
	stoma_index(db, "author", "r21", "love");
	stoma_index(db, "author", "r22", "uniquebyname");
	n = stoma_query(db, "title", "love", out, &handled);
	CHECK(handled == 1 && n == 1 && hd_has(out, "r21"), "title love");
	qmap_drop(out);
	n = stoma_query(db, "author", "love", out, &handled);
	CHECK(n == 1 && hd_has(out, "r21"), "author love");
	qmap_drop(out);
	n = stoma_query(db, "title", "uniquebyname", out, &handled);
	CHECK(n == 0, "no cross-field leak");
	qmap_drop(out);

	/* 20. alphanumeric tokens, prefix at token start only */
	stoma_index(db, "title", "r23", "song2 live");
	n = stoma_query(db, "title", "so", out, &handled);
	CHECK(handled == 1 && n == 1 && hd_has(out, "r23"), "alpha prefix");
	qmap_drop(out);
	n = stoma_query(db, "title", "song2", out, &handled);
	CHECK(n == 1 && hd_has(out, "r23"), "exact alnum token");
	qmap_drop(out);
	n = stoma_query(db, "title", "2l", out, &handled);
	CHECK(n == 0, "mid-token not matched");
	qmap_drop(out);

	/* 21. single-char query token */
	n = stoma_query(db, "title", "s", out, &handled);
	CHECK(handled == 1 && n == 2 && hd_has(out, "r20") &&
	              hd_has(out, "r23"),
	      "single-char token");
	qmap_drop(out);

	/* 22. phrase queries (stoma_query_phrase). Indexed tokens must avoid
	 * 's' initials — test 21 pins the 's' prefix count to r20+r23. */
	stoma_index(db, "title", "r24", "Blue Dawn");
	n = stoma_query_phrase(db, "title", "blue dawn", out, &handled);
	CHECK(handled == 1 && n == 1 && hd_has(out, "r24"),
	      "phrase adjacent in order");
	qmap_drop(out);
	n = stoma_query_phrase(db, "title", "dawn blue", out, &handled);
	CHECK(n == 0, "phrase rejects reorder");
	qmap_drop(out);
	n = stoma_query(db, "title", "dawn blue", out, &handled);
	CHECK(n == 1 && hd_has(out, "r24"), "AND stays order-insensitive");
	qmap_drop(out);

	stoma_index(db, "title", "r25", "Blue summer of the dawn");
	n = stoma_query_phrase(db, "title", "blue dawn", out, &handled);
	CHECK(n == 1 && hd_has(out, "r24") && !hd_has(out, "r25"),
	      "phrase rejects spread tokens");
	qmap_drop(out);
	n = stoma_query(db, "title", "blue dawn", out, &handled);
	CHECK(n == 2 && hd_has(out, "r24") && hd_has(out, "r25"),
	      "AND still matches spread tokens");
	qmap_drop(out);

	stoma_index(db, "title", "r26", "blackstar manor");
	n = stoma_query_phrase(db, "title", "black manor", out, &handled);
	CHECK(n == 1 && hd_has(out, "r26"), "phrase allows per-token prefix");
	qmap_drop(out);
	n = stoma_query_phrase(db, "title", "star manor", out, &handled);
	CHECK(n == 0, "phrase prefix must align at positions");
	qmap_drop(out);

	stoma_index(db, "title", "r27", "line one\nline two");
	n = stoma_query_phrase(db, "title", "one line", out, &handled);
	CHECK(n == 1 && hd_has(out, "r27"),
	      "phrase spans line break (token separator)");
	qmap_drop(out);
	n = stoma_query_phrase(db, "title", "two line", out, &handled);
	CHECK(n == 0, "phrase order matters across lines");
	qmap_drop(out);

	stoma_index(db, "title", "r28", "Atenção Coração");
	n = stoma_query_phrase(db, "title", "atenção coração", out, &handled);
	CHECK(n == 1 && hd_has(out, "r28"), "phrase with accents matches");
	qmap_drop(out);
	n = stoma_query_phrase(db, "title", "coracao atencao", out, &handled);
	CHECK(n == 0, "phrase accent-sensitive");
	qmap_drop(out);

	stoma_index(db, "title", "r29", "Morning Dawn");
	n = stoma_query_phrase(db, "title", "morning dawn", out, &handled);
	CHECK(n == 1 && hd_has(out, "r29"), "phrase case-insensitive");
	qmap_drop(out);

	n = stoma_query_phrase(db, "title", "dawn", out, &handled);
	CHECK(handled == 1 && n == 3 && hd_has(out, "r24") &&
	              hd_has(out, "r25") && hd_has(out, "r29"),
	      "single-token phrase equals AND");
	qmap_drop(out);

	n = stoma_query_phrase(db, "title", "blue,  dawn", out, &handled);
	CHECK(n == 1 && hd_has(out, "r24"), "phrase normalizes punct+spaces");
	qmap_drop(out);

	n = stoma_query_phrase(db, "title", "---", out, &handled);
	CHECK(handled == 0 && n == 0, "zero-token phrase no-op");
	qmap_drop(out);

	n = stoma_query_phrase(db, "title", "nowhere at all", out, &handled);
	CHECK(handled == 1 && n == 0, "phrase no match -> 0");
	qmap_drop(out);

	stoma_close(db);
	qmap_close(out);

	printf("Results: %d/%d passed", total - failures, total);
	if (failures > 0)
		printf(", %d FAILED", failures);
	printf("\n");
	return failures > 0 ? 1 : 0;
}
