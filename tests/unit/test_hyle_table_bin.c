#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>

#include "mods/common/ux/list_state.h"
#include "mods/common/ux/list_bin.c"

static void test_hylt_roundtrip(void)
{
	list_state_t orig;
	list_state_t decoded;
	char bin_buf[65536];
	int bin_len;
	int i, j;

	memset(&orig, 0, sizeof(orig));
	snprintf(orig.module, sizeof(orig.module), "song");
	snprintf(orig.display_name, sizeof(orig.display_name), "Song");
	snprintf(orig.content_field, sizeof(orig.content_field), "lyrics");
	snprintf(orig.content_label, sizeof(orig.content_label), "Lyrics");
	snprintf(orig.content_placeholder, sizeof(orig.content_placeholder), "Search songs...");
	snprintf(orig.username, sizeof(orig.username), "quirinpa");
	snprintf(orig.query, sizeof(orig.query), "genre=rock&lang=pt");
	orig.custom = 1;
	snprintf(orig.q, sizeof(orig.q), "Amália");
	snprintf(orig.sort_field, sizeof(orig.sort_field), "title");
	snprintf(orig.lang, sizeof(orig.lang), "pt");
	orig.sort_asc = 1;
	orig.page = 2;
	orig.per_page = 15;
	orig.total = 142;
	orig.has_page = 1;

	orig.ncols = 4;
	/* Col 0: id */
	snprintf(orig.cols[0].key, sizeof(orig.cols[0].key), "id");
	snprintf(orig.cols[0].label, sizeof(orig.cols[0].label), "ID");
	orig.cols[0].type = 0;

	/* Col 1: title */
	snprintf(orig.cols[1].key, sizeof(orig.cols[1].key), "title");
	snprintf(orig.cols[1].label, sizeof(orig.cols[1].label), "Title");
	orig.cols[1].type = 1;

	/* Col 2: author */
	snprintf(orig.cols[2].key, sizeof(orig.cols[2].key), "author");
	snprintf(orig.cols[2].label, sizeof(orig.cols[2].label), "Author");
	orig.cols[2].type = 1;
	snprintf(orig.cols[2].target_source, sizeof(orig.cols[2].target_source), "artist");

	/* Col 3: genre */
	snprintf(orig.cols[3].key, sizeof(orig.cols[3].key), "genre");
	snprintf(orig.cols[3].label, sizeof(orig.cols[3].label), "Genre");
	orig.cols[3].type = 2;
	orig.cols[3].opt_start = 0;
	orig.cols[3].opt_count = 3;

	/* Options */
	orig.nopts = 3;
	snprintf(orig.opts[0].id, sizeof(orig.opts[0].id), "fado");
	snprintf(orig.opts[0].label, sizeof(orig.opts[0].label), "Fado Tradicional");
	snprintf(orig.opts[1].id, sizeof(orig.opts[1].id), "rock");
	snprintf(orig.opts[1].label, sizeof(orig.opts[1].label), "Rock Português");
	snprintf(orig.opts[2].id, sizeof(orig.opts[2].id), "jazz");
	snprintf(orig.opts[2].label, sizeof(orig.opts[2].label), "Jazz");

	/* Rows */
	orig.nids = 5;
	const char *sample_ids[5] = { "song-101", "song-102", "song-103", "song-104", "song-105" };
	const char *sample_cells[5][4] = {
		{ "101", "Canção do Mar", "Ferrer Trindade", "fado" },
		{ "102", "Pão e Vinho", "Zeca Afonso", "folk" },
		{ "103", "Chuva", "Jorge Fernando", "fado" },
		{ "104", "Contentores", "Xutos & Pontapés", "rock" },
		{ "105", "Barco Negro", "Caco Velho", "fado" }
	};

	for (i = 0; i < orig.nids; i++) {
		orig.ids[i] = sample_ids[i];
		for (j = 0; j < orig.ncols; j++) {
			orig.values[i * orig.ncols + j] = sample_cells[i][j];
		}
	}

	/* 1. Serialize to HYLT binary */
	bin_len = list_state_to_bin(&orig, bin_buf, sizeof(bin_buf));
	assert(bin_len > 0);
	assert((size_t)bin_len >= sizeof(hylt_header_t) + sizeof(hylt_meta_t));

	/* Verify header layout */
	const hylt_header_t *hdr = (const hylt_header_t *)bin_buf;
	assert(hdr->magic == HYLT_MAGIC);
	assert(hdr->version == HYLT_VERSION);
	assert(hdr->total == 142);
	assert(hdr->page == 2);
	assert(hdr->per_page == 15);
	assert(hdr->ncols == 4);
	assert(hdr->nids == 5);
	assert(hdr->nopts == 3);
	assert(hdr->flags & HYLT_FLAG_SORT_ASC);
	assert(hdr->flags & HYLT_FLAG_CUSTOM);
	assert(hdr->flags & HYLT_FLAG_HAS_PAGE);

	/* 2. Zero-copy deserialize from HYLT binary */
	int rc = list_state_from_bin(&decoded, bin_buf, (size_t)bin_len);
	assert(rc == 0);

	/* 3. Field parity checks */
	assert(strcmp(decoded.module, "song") == 0);
	assert(strcmp(decoded.display_name, "Song") == 0);
	assert(strcmp(decoded.content_field, "lyrics") == 0);
	assert(strcmp(decoded.content_label, "Lyrics") == 0);
	assert(strcmp(decoded.username, "quirinpa") == 0);
	assert(strcmp(decoded.query, "genre=rock&lang=pt") == 0);
	assert(decoded.custom == 1);
	assert(strcmp(decoded.q, "Amália") == 0);
	assert(strcmp(decoded.sort_field, "title") == 0);
	assert(strcmp(decoded.lang, "pt") == 0);
	assert(decoded.sort_asc == 1);
	assert(decoded.page == 2);
	assert(decoded.per_page == 15);
	assert(decoded.total == 142);
	assert(decoded.has_page == 1);
	assert(decoded.ncols == 4);
	assert(decoded.nids == 5);
	assert(decoded.nopts == 3);

	/* Check columns */
	assert(strcmp(decoded.cols[0].key, "id") == 0);
	assert(strcmp(decoded.cols[1].key, "title") == 0);
	assert(strcmp(decoded.cols[2].target_source, "artist") == 0);
	assert(decoded.cols[3].opt_count == 3);

	/* Check options */
	assert(strcmp(decoded.opts[0].id, "fado") == 0);
	assert(strcmp(decoded.opts[0].label, "Fado Tradicional") == 0);
	assert(strcmp(decoded.opts[1].id, "rock") == 0);
	assert(strcmp(decoded.opts[2].id, "jazz") == 0);

	/* Check rows and cells */
	for (i = 0; i < orig.nids; i++) {
		assert(strcmp(decoded.ids[i], sample_ids[i]) == 0);
		for (j = 0; j < orig.ncols; j++) {
			assert(strcmp(decoded.values[i * orig.ncols + j], sample_cells[i][j]) == 0);
		}
	}

	/* 4. TRUE ZERO-COPY POINTER PROOF:
	 * decoded.ids[i] and decoded.values[...] MUST point directly inside bin_buf */
	for (i = 0; i < decoded.nids; i++) {
		assert(decoded.ids[i] >= bin_buf && decoded.ids[i] < bin_buf + bin_len);
		for (j = 0; j < decoded.ncols; j++) {
			const char *cell = decoded.values[i * decoded.ncols + j];
			assert(cell >= bin_buf && cell < bin_buf + bin_len);
		}
	}

	printf("PASS: HYLT full roundtrip and zero-copy pointer mapping verified\n");
}

static void test_hylt_corrupt_buffers(void)
{
	list_state_t orig;
	list_state_t decoded;
	char bin_buf[4096];
	int bin_len;

	memset(&orig, 0, sizeof(orig));
	snprintf(orig.module, sizeof(orig.module), "poem");
	orig.ncols = 1;
	orig.nids = 1;
	snprintf(orig.cols[0].key, sizeof(orig.cols[0].key), "title");
	orig.ids[0] = "p1";
	orig.values[0] = "Mensagem";

	bin_len = list_state_to_bin(&orig, bin_buf, sizeof(bin_buf));
	assert(bin_len > 0);

	/* 1. Too short */
	assert(list_state_from_bin(&decoded, bin_buf, 10) == -1);

	/* 2. Corrupt magic */
	char corrupt[4096];
	memcpy(corrupt, bin_buf, (size_t)bin_len);
	hylt_header_t *hdr = (hylt_header_t *)corrupt;
	hdr->magic = 0x12345678;
	assert(list_state_from_bin(&decoded, corrupt, (size_t)bin_len) == -1);

	/* 3. Bad version */
	memcpy(corrupt, bin_buf, (size_t)bin_len);
	hdr = (hylt_header_t *)corrupt;
	hdr->version = 99;
	assert(list_state_from_bin(&decoded, corrupt, (size_t)bin_len) == -1);

	/* 4. Truncated length */
	assert(list_state_from_bin(&decoded, bin_buf, (size_t)bin_len - 5) == -1);

	/* 5. Exceeded ncols limit */
	memcpy(corrupt, bin_buf, (size_t)bin_len);
	hdr = (hylt_header_t *)corrupt;
	hdr->ncols = LIST_MAX_COLS + 1;
	assert(list_state_from_bin(&decoded, corrupt, (size_t)bin_len) == -1);

	/* 6. Non-null-terminated arena */
	memcpy(corrupt, bin_buf, (size_t)bin_len);
	corrupt[bin_len - 1] = 'X';
	assert(list_state_from_bin(&decoded, corrupt, (size_t)bin_len) == -1);

	printf("PASS: HYLT corrupt buffer validation verified\n");
}

static void test_hylt_empty_table(void)
{
	list_state_t orig;
	list_state_t decoded;
	char bin_buf[4096];
	int bin_len;

	memset(&orig, 0, sizeof(orig));
	snprintf(orig.module, sizeof(orig.module), "empty");
	orig.ncols = 0;
	orig.nids = 0;
	orig.nopts = 0;

	bin_len = list_state_to_bin(&orig, bin_buf, sizeof(bin_buf));
	assert(bin_len > 0);

	assert(list_state_from_bin(&decoded, bin_buf, (size_t)bin_len) == 0);
	assert(strcmp(decoded.module, "empty") == 0);
	assert(decoded.ncols == 0);
	assert(decoded.nids == 0);
	assert(decoded.nopts == 0);

	printf("PASS: HYLT empty table edge case verified\n");
}

int main(void)
{
	printf("=== Running HYLT Binary Wire Protocol Tests ===\n");
	test_hylt_roundtrip();
	test_hylt_corrupt_buffers();
	test_hylt_empty_table();
	printf("ALL HYLT WIRE PROTOCOL TESTS PASSED\n");
	return 0;
}
