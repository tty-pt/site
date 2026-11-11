#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

#include <bud/bud.h>
#include "mods/common/ux/list_state.h"
#include "mods/common/ux/list_bin.c"
#include "mods/index/ux/list_json.c"

static double now_us(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec * 1000000.0 + (double)ts.tv_nsec / 1000.0;
}

static void build_state(list_state_t *st, int rows)
{
	static char id_pool[LIST_MAX_ROWS][32];
	static char val_pool[LIST_MAX_ROWS * 4][128];
	int i;

	memset(st, 0, sizeof(*st));
	snprintf(st->module, sizeof(st->module), "song");
	snprintf(st->display_name, sizeof(st->display_name), "Song");
	snprintf(st->content_field, sizeof(st->content_field), "lyrics");
	snprintf(st->content_label, sizeof(st->content_label), "Lyrics");
	snprintf(st->content_placeholder, sizeof(st->content_placeholder), "Search lyrics...");
	snprintf(st->username, sizeof(st->username), "user");
	snprintf(st->query, sizeof(st->query), "genre=rock");
	st->custom = 1;
	snprintf(st->q, sizeof(st->q), "search");
	snprintf(st->sort_field, sizeof(st->sort_field), "title");
	snprintf(st->lang, sizeof(st->lang), "en");
	st->sort_asc = 1;
	st->page = 1;
	st->per_page = rows;
	st->total = rows * 10;
	st->has_page = 1;

	st->ncols = 4;
	snprintf(st->cols[0].key, sizeof(st->cols[0].key), "id");
	snprintf(st->cols[0].label, sizeof(st->cols[0].label), "ID");
	st->cols[0].type = 0;

	snprintf(st->cols[1].key, sizeof(st->cols[1].key), "title");
	snprintf(st->cols[1].label, sizeof(st->cols[1].label), "Title");
	st->cols[1].type = 1;

	snprintf(st->cols[2].key, sizeof(st->cols[2].key), "author");
	snprintf(st->cols[2].label, sizeof(st->cols[2].label), "Author");
	st->cols[2].type = 1;

	snprintf(st->cols[3].key, sizeof(st->cols[3].key), "genre");
	snprintf(st->cols[3].label, sizeof(st->cols[3].label), "Genre");
	st->cols[3].type = 2;

	st->nids = rows;
	for (i = 0; i < rows; i++) {
		snprintf(id_pool[i], sizeof(id_pool[i]), "track-%d", i + 1);
		st->ids[i] = id_pool[i];

		snprintf(val_pool[i * 4 + 0], sizeof(val_pool[0]), "%d", i + 1);
		snprintf(val_pool[i * 4 + 1], sizeof(val_pool[0]), "Track Title %d - Symphony in C", i + 1);
		snprintf(val_pool[i * 4 + 2], sizeof(val_pool[0]), "Composer / Artist %d", (i % 20) + 1);
		snprintf(val_pool[i * 4 + 3], sizeof(val_pool[0]), "Rock / Orchestral %d", (i % 5) + 1);

		st->values[i * 4 + 0] = val_pool[i * 4 + 0];
		st->values[i * 4 + 1] = val_pool[i * 4 + 1];
		st->values[i * 4 + 2] = val_pool[i * 4 + 2];
		st->values[i * 4 + 3] = val_pool[i * 4 + 3];
	}
}

static void run_bench(int rows, int iters)
{
	list_state_t st;
	list_state_t dst;
	char json_buf[524288];
	char bin_buf[262144];
	double t0, t1;
	double json_ser_us, bin_ser_us;
	double json_des_us, bin_des_us;
	int json_sz, bin_sz;
	int i;

	build_state(&st, rows);

	/* Warmup and size check */
	json_sz = list_state_to_json(&st, json_buf, sizeof(json_buf));
	(void)json_sz;
	json_sz = (int)strlen(json_buf);

	bin_sz = list_state_to_bin(&st, bin_buf, sizeof(bin_buf));

	/* Serialization benchmark */
	t0 = now_us();
	for (i = 0; i < iters; i++) {
		list_state_to_json(&st, json_buf, sizeof(json_buf));
	}
	t1 = now_us();
	json_ser_us = (t1 - t0) / (double)iters;

	t0 = now_us();
	for (i = 0; i < iters; i++) {
		list_state_to_bin(&st, bin_buf, sizeof(bin_buf));
	}
	t1 = now_us();
	bin_ser_us = (t1 - t0) / (double)iters;

	/* Deserialization benchmark */
	t0 = now_us();
	for (i = 0; i < iters; i++) {
		list_state_from_json_len(&dst, json_buf, (size_t)json_sz);
	}
	t1 = now_us();
	json_des_us = (t1 - t0) / (double)iters;

	t0 = now_us();
	for (i = 0; i < iters; i++) {
		list_state_from_bin(&dst, bin_buf, (size_t)bin_sz);
	}
	t1 = now_us();
	bin_des_us = (t1 - t0) / (double)iters;

	double size_reduction = 100.0 * (1.0 - ((double)bin_sz / (double)json_sz));
	double ser_speedup = json_ser_us / bin_ser_us;
	double des_speedup = json_des_us / bin_des_us;

	printf("| %4d | %8d B | %8d B | %5.1f%% | %8.2f us | %7.2f us | %5.1fx | %8.2f us | %7.2f us | %5.1fx |\n",
	       rows, json_sz, bin_sz, size_reduction,
	       json_ser_us, bin_ser_us, ser_speedup,
	       json_des_us, bin_des_us, des_speedup);
}

int main(void)
{
	printf("| Rows |  JSON Size |  HYLT Size | Reduction |  JSON Ser   | HYLT Ser  | Ser Gain |  JSON Deser | HYLT Deser| Deser Gain|\n");
	printf("|-----:|-----------:|-----------:|----------:|------------:|----------:|---------:|------------:|----------:|----------:|\n");

	run_bench(10, 1000);
	run_bench(50, 500);
	run_bench(250, 200);

	return 0;
}
