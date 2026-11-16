#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <hyle-source/hyle_source.h>

int main(void)
{
	printf("=== Running hyle-source client-side cache unit tests ===\n");

	/* Initial state: cache is empty */
	hyle_source_cache_clear();
	assert(hyle_source_cache_count(NULL) == 0);
	assert(hyle_source_cache_get("song", "page=1", NULL) == NULL);

	/* Put first entry */
	const char *json1 = "{\"page\":1,\"total\":42,\"rows\":[\"s1\",\"s2\"]}";
	int rc = hyle_source_cache_put("song", "page=1&per_page=10", json1, strlen(json1));
	assert(rc == 0);
	assert(hyle_source_cache_count("song") == 1);
	assert(hyle_source_cache_count(NULL) == 1);

	/* Get first entry */
	size_t len1 = 0;
	const char *hit1 = hyle_source_cache_get("song", "page=1&per_page=10", &len1);
	assert(hit1 != NULL);
	assert(len1 == strlen(json1));
	assert(strcmp(hit1, json1) == 0);

	/* Miss on different query */
	assert(hyle_source_cache_get("song", "page=2&per_page=10", NULL) == NULL);

	/* Miss on different dataset */
	assert(hyle_source_cache_get("poem", "page=1&per_page=10", NULL) == NULL);

	/* Put second entry (different query) */
	const char *json2 = "{\"page\":2,\"total\":42,\"rows\":[\"s3\",\"s4\"]}";
	rc = hyle_source_cache_put("song", "page=2&per_page=10", json2, strlen(json2));
	assert(rc == 0);
	assert(hyle_source_cache_count("song") == 2);

	/* Put entry for different dataset */
	const char *json_poem = "{\"module\":\"poem\",\"rows\":[\"p1\"]}";
	rc = hyle_source_cache_put("poem", "q=sun", json_poem, 0);
	assert(rc == 0);
	assert(hyle_source_cache_count("poem") == 1);
	assert(hyle_source_cache_count("song") == 2);
	assert(hyle_source_cache_count(NULL) == 3);

	/* Overwrite existing entry */
	const char *json1_updated = "{\"page\":1,\"total\":43,\"rows\":[\"s1\",\"s2\",\"s0\"]}";
	rc = hyle_source_cache_put("song", "page=1&per_page=10", json1_updated, 0);
	assert(rc == 0);
	assert(hyle_source_cache_count("song") == 2);
	const char *hit1_updated = hyle_source_cache_get("song", "page=1&per_page=10", NULL);
	assert(hit1_updated != NULL);
	assert(strcmp(hit1_updated, json1_updated) == 0);

	/* Invalidate by dataset */
	hyle_source_cache_invalidate("song");
	assert(hyle_source_cache_count("song") == 0);
	assert(hyle_source_cache_get("song", "page=1&per_page=10", NULL) == NULL);
	assert(hyle_source_cache_get("poem", "q=sun", NULL) != NULL);
	assert(hyle_source_cache_count(NULL) == 1);

	/* Clear everything */
	hyle_source_cache_clear();
	assert(hyle_source_cache_count(NULL) == 0);
	assert(hyle_source_cache_get("poem", "q=sun", NULL) == NULL);

	printf("PASS: All hyle-source client-side cache tests passed!\n");
	return 0;
}
