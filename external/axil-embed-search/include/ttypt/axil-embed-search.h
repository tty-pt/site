#ifndef TTYPT_AXIL_EMBED_SEARCH_H
#define TTYPT_AXIL_EMBED_SEARCH_H

#include <stdint.h>
#include <stddef.h>
#include <ttypt/axil.h>
#include <ttypt/sepal.h>
#include <ttypt/lemma.h>
#include <ttypt/corm.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AXIL_EMBED_SEARCH_DEFAULT_URL   "http://127.0.0.1:4242/v1/embeddings"
#define AXIL_EMBED_SEARCH_DEFAULT_MODEL "bge-small"
#define AXIL_EMBED_SEARCH_DEFAULT_LIMIT 10
#define AXIL_EMBED_SEARCH_MAX_QUERY_LEN 4096
#define AXIL_EMBED_SEARCH_MAX_HITS      100

/** Search mode enumeration */
typedef enum {
    SEARCH_MODE_SEPAL  = 0, /**< Pure semantic vector similarity */
    SEARCH_MODE_LEMMA  = 1, /**< Pure lexical prefix match */
    SEARCH_MODE_HYBRID = 2  /**< Hybrid (lexical candidates + vector similarity) */
} search_mode_t;

/** Module state and resource handles */
typedef struct {
    char url[512];
    char model[128];
    char db_path[512];
    sepal_vecstore_t *vs;
    lemma_db_t *ldb;
    uint32_t text_hd;
    uint32_t next_ref;
    int initialized;
} embed_search_state_t;

/** External weak symbol provided by libsepal for embedding fetch */
extern int sepal_embed_fetch(const char *text, float **vec_out, size_t *n_out);

/** Lifecycle management */
int embed_search_init(const char *url, const char *model, const char *db_path);
void embed_search_cleanup(void);

/** Axil request handlers */
int handle_post_embeddings(socket_t cfd, char *body);
int handle_get_search(socket_t cfd, char *body);
int handle_get_ui(socket_t cfd, char *body);

#ifdef __cplusplus
}
#endif

#endif /* TTYPT_AXIL_EMBED_SEARCH_H */
