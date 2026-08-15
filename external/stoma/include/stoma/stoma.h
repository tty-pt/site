#ifndef STOMA_H
#define STOMA_H

#include <stddef.h>
#include <stdint.h>

/*
 * stoma — qmap-backed full-text index.
 *
 * A generic inverted index over (field, token) → row_id. It knows nothing
 * about rows, schemas, or the application: callers choose which fields to
 * index and interpret row_ids. Match semantics are word-token based with
 * prefix expansion: a query token must match the start of an indexed token.
 * Multiple tokens in a query are ANDed.
 */

/*
 * Fold a UTF-8 string to lowercase ASCII base letters (accent
 * insensitivity) using iconv's TRANSLIT tables. Returns the number of bytes
 * written, or -1 if the output buffer is too small (caller falls back to raw
 * comparison).
 */
int stoma_fold(char *out, size_t outsz, const char *in);

typedef struct stoma_db stoma_db_t;

/* Open an index. mask is the qmap hash mask (0 = qmap default). */
stoma_db_t *stoma_open(unsigned mask);

/* Close the index and free all resources. */
void stoma_close(stoma_db_t *db);

/* Drop all entries; the handle stays valid. */
void stoma_clear(stoma_db_t *db);

/*
 * Index every token of value under (field, row_id).
 * Duplicate tokens collapse automatically. Returns 0 on success,
 * -1 on invalid arguments.
 */
int stoma_index(stoma_db_t *db,
	const char *field, const char *row_id, const char *value);

/*
 * Query: every token of `query` must prefix-match in `field`.
 * Matching row_ids are stored as "row_id" → "" into out_hd (a caller-opened
 * qmap; duplicates collapse). *handled is set to 1 when the query produced
 * at least one token, 0 for an empty/zero-token query (no-op — caller should
 * treat it as "matches everything"). Returns the number of matches written.
 */
uint32_t stoma_query(stoma_db_t *db,
	const char *field, const char *query,
	uint32_t out_hd, int *handled);

#endif
