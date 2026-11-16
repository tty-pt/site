#ifndef COMMON_LIST_STATE_H
#define COMMON_LIST_STATE_H

#include <stddef.h>
#include <stdint.h>

/* Neutral, framework-free list state — COMPLY.md §6.3. No
 * axil/corm/source/hyle/XY. */
typedef struct {
	char key[64];
	char label[64];
	int type;
	char target_source[64];
	unsigned target_hd;
	char filter[16];
} col_t;

#define LIST_MAX_COLS 32
#define LIST_MAX_ROWS 256
#define LIST_MAX_OPTS 1024

typedef struct {
	char id[64];
	char label[128];
} list_opt_t;

typedef struct {
	char key[64];
	char label[64];
	int type;
	char target_source[64];
	char filter[16];
	char current[512];
	int opt_start;
	int opt_count;
} list_col_t;

typedef struct {
	char module[64];
	char display_name[64];
	char content_field[64];
	char content_label[64];
	char content_placeholder[128];
	char username[64];
	char query[512];
	int custom;
	char q[512];
	char sort_field[64];
	char lang[32];
	int sort_asc;
	int page, per_page, total, has_page;
	int ncols;
	list_col_t cols[LIST_MAX_COLS];
	int nopts;
	list_opt_t opts[LIST_MAX_OPTS];
	int nids;
	const char *ids[LIST_MAX_ROWS];
	const char *values[LIST_MAX_ROWS * LIST_MAX_COLS];
} list_state_t;

/* ── Canonical Binary Table Wire Protocol (HYLT) ─────────────── */

#define HYLT_MAGIC 0x544C5948 /* 'H' 'Y' 'L' 'T' in little-endian byte order (0x48594C54) */
#define HYLT_VERSION 1

#define HYLT_FLAG_SORT_ASC  0x0001
#define HYLT_FLAG_CUSTOM    0x0002
#define HYLT_FLAG_HAS_PAGE  0x0004

#pragma pack(push, 1)
typedef struct {
	uint32_t magic;         /* 0x48594C54 */
	uint16_t version;       /* 1 */
	uint16_t flags;         /* HYLT_FLAG_* */
	uint32_t total;         /* Total items in dataset */
	uint16_t page;          /* Current page number */
	uint16_t per_page;      /* Items per page */
	uint16_t ncols;         /* Number of columns */
	uint16_t nids;          /* Number of rows (IDs) */
	uint16_t nopts;         /* Number of options */
	uint16_t reserved;      /* Padding / reserved */
	uint32_t arena_offset;  /* Byte offset to string arena */
	uint32_t arena_size;    /* Size of string arena in bytes */
} hylt_header_t;

typedef struct {
	uint32_t module_offset;
	uint32_t display_name_offset;
	uint32_t content_field_offset;
	uint32_t content_label_offset;
	uint32_t content_placeholder_offset;
	uint32_t username_offset;
	uint32_t query_offset;
	uint32_t q_offset;
	uint32_t sort_field_offset;
	uint32_t lang_offset;
} hylt_meta_t;

typedef struct {
	uint32_t key_offset;
	uint32_t label_offset;
	uint32_t target_source_offset;
	uint32_t filter_offset;
	uint32_t current_offset;
	uint16_t type;
	uint16_t opt_start;
	uint16_t opt_count;
	uint16_t reserved;
} hylt_col_wire_t;

typedef struct {
	uint32_t id_offset;
	uint32_t label_offset;
} hylt_opt_wire_t;
#pragma pack(pop)

#ifndef LIST_WEAK
#define LIST_WEAK __attribute__((weak))
#endif

/* Forward declaration for Bud node */
struct bud_node;

LIST_WEAK int list_state_to_json(const list_state_t *state, char *out, size_t out_sz);
LIST_WEAK void list_state_from_json(list_state_t *state, const char *json);
LIST_WEAK void list_state_from_json_len(
        list_state_t *state, const char *json, size_t len);
LIST_WEAK int list_state_to_bin(const list_state_t *state, void *out, size_t out_sz);
LIST_WEAK int list_state_from_bin(list_state_t *state, const void *buf, size_t len);
LIST_WEAK int list_has_query(const list_state_t *st);
LIST_WEAK int list_state_sort(list_state_t *state, const char *field);
LIST_WEAK struct bud_node *list_render(const list_state_t *state);

#endif
