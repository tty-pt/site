#ifndef INDEX_UX_LIST_SORT_C
#define INDEX_UX_LIST_SORT_C

#include <string.h>
#include <stdio.h>
#include "list_state.h"

static int list_str_case_cmp(const char *s1, const char *s2)
{
	if (!s1) s1 = "";
	if (!s2) s2 = "";
	while (*s1 && *s2) {
		unsigned char c1 = (unsigned char)*s1;
		unsigned char c2 = (unsigned char)*s2;
		if (c1 >= 'A' && c1 <= 'Z') c1 += ('a' - 'A');
		if (c2 >= 'A' && c2 <= 'Z') c2 += ('a' - 'A');
		if (c1 != c2)
			return (int)c1 - (int)c2;
		s1++;
		s2++;
	}
	return (int)(unsigned char)*s1 - (int)(unsigned char)*s2;
}

int list_state_sort(list_state_t *st, const char *field)
{
	int col_idx = -1;
	int i;

	if (!st || !field || !field[0] || st->nids <= 1)
		return 0;

	for (i = 0; i < st->ncols && i < LIST_MAX_COLS; i++) {
		if (strcmp(st->cols[i].key, field) == 0) {
			col_idx = i;
			break;
		}
	}
	if (col_idx < 0)
		return -1;

	if (strcmp(st->sort_field, field) == 0) {
		st->sort_asc = !st->sort_asc;
	} else {
		snprintf(st->sort_field, sizeof(st->sort_field), "%s", field);
		st->sort_asc = 1;
	}

	/* Bounded Shell sort over row indices */
	int order[LIST_MAX_ROWS];
	int gap, j, temp;
	int n = st->nids;
	if (n > LIST_MAX_ROWS)
		n = LIST_MAX_ROWS;

	for (i = 0; i < n; i++)
		order[i] = i;

	for (gap = n / 2; gap > 0; gap /= 2) {
		for (i = gap; i < n; i++) {
			temp = order[i];
			for (j = i; j >= gap; j -= gap) {
				int row_prev = order[j - gap];
				const char *va = st->values[row_prev * st->ncols + col_idx];
				const char *vb = st->values[temp * st->ncols + col_idx];
				int cmp = list_str_case_cmp(va, vb);
				if (!st->sort_asc)
					cmp = -cmp;

				if (cmp > 0)
					order[j] = order[j - gap];
				else
					break;
			}
			order[j] = temp;
		}
	}

	/* Permute rows in-place */
	const char *new_ids[LIST_MAX_ROWS];
	const char *new_vals[LIST_MAX_ROWS * LIST_MAX_COLS];

	for (i = 0; i < n; i++) {
		int src_row = order[i];
		new_ids[i] = st->ids[src_row];
		for (j = 0; j < st->ncols; j++) {
			new_vals[i * st->ncols + j] = st->values[src_row * st->ncols + j];
		}
	}
	for (i = 0; i < n; i++) {
		st->ids[i] = new_ids[i];
		for (j = 0; j < st->ncols; j++) {
			st->values[i * st->ncols + j] = new_vals[i * st->ncols + j];
		}
	}

	return 0;
}

#endif
