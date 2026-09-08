#ifndef MM_ENGINE_H
#define MM_ENGINE_H

#include <stddef.h>
#include <stdint.h>

#define MM_TS_LEN    32
#define MM_TOPIC_LEN 64
#define MM_TAGS_LEN  128
#define MM_TEXT_LEN  8192
#define MM_KEY_LEN   128

typedef struct mm_engine mm_t;

typedef struct {
	char ts[MM_TS_LEN];
	char topic[MM_TOPIC_LEN];
	char tags[MM_TAGS_LEN]; /* newline-separated, searchable */
	char text[MM_TEXT_LEN];
	uint32_t level; /* 0 raw, 1 condensed, 2 summary */
} mm_entry_t;

typedef struct {
	char key[MM_KEY_LEN];
	char ts[MM_TS_LEN];
	char topic[MM_TOPIC_LEN];
	char tags[MM_TAGS_LEN];
	uint32_t level;
	char *text; /* malloc'd; free with mm_hits_free */
} mm_hit_t;

/* Open (or create) the memory file. On error returns NULL and fills err
 * (errsz 0 = silent). */
mm_t *mm_open(const char *path, char *err, size_t errsz);
void mm_close(mm_t *mm);

/* Store an entry. Builds the mipmap key from level/topic/ts. */
int mm_store(mm_t *mm, const mm_entry_t *e, char *err, size_t errsz);
/* Forget one exact mipmap key. */
int mm_forget(mm_t *mm, const char *key, char *err, size_t errsz);
/* Drop every entry (and vector). */
int mm_reset(mm_t *mm, char *err, size_t errsz);
/* Read an exact entry. Returns 1 on success, 0 when absent. */
int mm_get(mm_t *mm, const char *key, mm_entry_t *out);

/* Mipmap key helpers (pure). */
int mm_key_level(const char *key); /* 2 / 1 / 0, or -1 for an invalid key */
int mm_key_make(const mm_entry_t *e, char *out, size_t outsz);
int mm_keyify(const char *in, char *out, size_t outsz);

/* Scan. Routing arguments compose:
 *   topic  -> only keys starting with "topic@"            (gist-first L2 recall)
 *   prefix -> only keys starting with this exact prefix   (zoom: topic@ts / @ts)
 *   textq  -> full-text filter over entry text (prefix/AND; quoted = phrase)
 *   level  -> >=0 restricts to that mipmap level; -1 = any
 * Returns a malloc'd array of max(>=1?) sorted hits, newest first;
 * *n = count. Free with mm_hits_free. */
mm_hit_t *mm_scan(mm_t *mm, const char *topic, const char *prefix,
                  const char *textq, int level, size_t max, size_t *n);
void mm_hits_free(mm_hit_t *hits, size_t n);

/* Optional semantic layer: dimension-tagged vectors (float per record).
 * Storage is local; computation of vectors is provider-external (Phase 3). */
int mm_vec_put(mm_t *mm, const char *key, const float *v, size_t n);
size_t mm_vec_dim(mm_t *mm, const char *key);
size_t mm_vec_get(mm_t *mm, const char *key, float *out, size_t max);
void mm_vec_del(mm_t *mm, const char *key);
float mm_cosine(const float *a, const float *b, size_t n);

#endif