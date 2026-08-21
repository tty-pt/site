#ifndef COMMON_LIST_STATE_H
#define COMMON_LIST_STATE_H

/* Neutral, framework-free list state — COMPLY.md §6.3. No axil/qmap/source/hyle/XY. */
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
#define LIST_MAX_OPTS 256

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

#endif
