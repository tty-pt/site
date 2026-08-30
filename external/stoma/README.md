# stoma — Search Tokenization and Accent-Sensitive Inverted Index

Fast in-memory inverted index and string tokenization library backed by `libqmap`.

## Overview

`stoma` powers the full-text search (FTS) engine for `hyle`. It builds an inverted index over `(field, token) -> row_id`.

## Key Features

- **Accent-Sensitive String Folding (`stoma_fold`):** Lowercases ASCII (A-Z) and Latin-1 supplement uppercase characters while strictly preserving diacritical marks (`pão` $\neq$ `pao`).
- **Prefix Matching:** Searches match word beginnings (e.g. query `cor` matches `coração`).
- **Contiguous Phrase Queries (`stoma_query_phrase`):** Matches multi-word sequences in exact token order across line breaks and punctuation.
- **Pure C / Zero External Encoding Dependencies:** Operates without `iconv` or system locale dependencies for fast, predictable execution.

## Key APIs (`include/stoma/stoma.h`)

```c
/* String lowercase folding (accent-preserving) */
int stoma_fold(char *out, size_t outsz, const char *in);

/* Open / close index */
stoma_db_t *stoma_open(unsigned mask);
void stoma_close(stoma_db_t *db);
void stoma_clear(stoma_db_t *db);

/* Index field value for record */
int stoma_index(stoma_db_t *db, const char *field, const char *row_id, const char *value);

/* Query index with token prefix matching */
uint32_t stoma_query(stoma_db_t *db, const char *field, const char *query,
                     uint32_t out_hd, int *handled);

/* Query index for exact contiguous phrases */
uint32_t stoma_query_phrase(stoma_db_t *db, const char *field, const char *query,
                            uint32_t out_hd, int *handled);
```

## Dependencies

- `external/libqmap` — Hash map storage
