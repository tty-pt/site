#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>

#include <ttypt/ndc.h>
#include <ttypt/ndx.h>
#include <ttypt/ndx-mod.h>
#include <ttypt/qmap.h>

#include "../common/common.h"
#include "../auth/auth.h"
#include "../mpfd/mpfd.h"
#include "../ssr/ssr.h"

static uint32_t sb_index_db = 0;

/* Parse a songbook line: chord_id:transpose:format */
static int
parse_sb_line(const char *line, char *chord_id, int *transpose, char *format)
{
	char *colon1 = strchr(line, ':');
	if (!colon1)
		return -1;

	char *colon2 = strchr(colon1 + 1, ':');
	if (!colon2)
		return -1;

	/* Extract chord_id */
	size_t id_len = colon1 - line;
	if (id_len > 127)
		id_len = 127;
	strncpy(chord_id, line, id_len);
	chord_id[id_len] = '\0';

	/* Extract transpose */
	*transpose = atoi(colon1 + 1);

	/* Extract format */
	strncpy(format, colon2 + 1, 127);
	format[127] = '\0';

	/* Remove trailing newline from format if present */
	size_t fmt_len = strlen(format);
	while (fmt_len > 0 && (format[fmt_len - 1] == '\n' || format[fmt_len - 1] == '\r')) {
		format[--fmt_len] = '\0';
	}

	return 0;
}

/* Check if user owns a songbook */
static int
check_sb_ownership(const char *sb_path, const char *username)
{
	if (!username || !*username)
		return 0;

	char owner_path[1024];
	snprintf(owner_path, sizeof(owner_path), "%s/.owner", sb_path);

	FILE *fp = fopen(owner_path, "r");
	if (!fp)
		return 0;

	char owner[64] = {0};
	size_t n = fread(owner, 1, sizeof(owner) - 1, fp);
	fclose(fp);
	if (n == 0)
		return 0;
	owner[n] = '\0';

	/* Remove trailing newline */
	if (owner[n - 1] == '\n')
		owner[n - 1] = '\0';

	return strcmp(owner, username) == 0;
}

/* Check if user owns the choir associated with this songbook */
static int
check_choir_ownership_for_sb(const char *doc_root, const char *sb_path, const char *username)
{
	char choir_file[1024];
	snprintf(choir_file, sizeof(choir_file), "%s/choir", sb_path);

	FILE *fp = fopen(choir_file, "r");
	if (!fp)
		return 0;

	char choir_id[64] = {0};
	size_t n = fread(choir_id, 1, sizeof(choir_id) - 1, fp);
	fclose(fp);
	if (n == 0)
		return 0;
	choir_id[n] = '\0';

	/* Remove trailing newline */
	if (choir_id[n - 1] == '\n')
		choir_id[n - 1] = '\0';

	/* Check choir ownership */
	char choir_path[1024];
	snprintf(choir_path, sizeof(choir_path), "%s/items/choir/items/%s", doc_root, choir_id);

	char owner_path[1024];
	snprintf(owner_path, sizeof(owner_path), "%s/.owner", choir_path);

	FILE *ofp = fopen(owner_path, "r");
	if (!ofp)
		return 0;

	char owner[64] = {0};
	n = fread(owner, 1, sizeof(owner) - 1, ofp);
	fclose(ofp);
	if (n == 0)
		return 0;
	owner[n] = '\0';

	if (owner[n - 1] == '\n')
		owner[n - 1] = '\0';

	return strcmp(owner, username) == 0;
}

/* Get random chord by type/format */
static int
get_random_chord_by_type(const char *doc_root, const char *type, char *out_id, size_t out_len)
{
	char chords_path[512];
	snprintf(chords_path, sizeof(chords_path), "%s/items/chords/items", doc_root);

	DIR *dir = opendir(chords_path);
	if (!dir)
		return -1;

	/* Collect matching chords */
	char **candidates = NULL;
	int count = 0;
	int capacity = 0;

	struct dirent *entry;
	while ((entry = readdir(dir)) != NULL) {
		if (entry->d_name[0] == '.')
			continue;

		/* Check if this chord has the matching type */
		char type_path[1024];
		snprintf(type_path, sizeof(type_path), "%s/%s/type", chords_path, entry->d_name);

		FILE *fp = fopen(type_path, "r");
		if (!fp) {
			/* No type file - matches "any" */
			if (strcmp(type, "any") != 0)
				continue;
		} else {
			char chord_type[128] = {0};
			size_t n = fread(chord_type, 1, sizeof(chord_type) - 1, fp);
			fclose(fp);
			if (n > 0) {
				chord_type[n] = '\0';
				/* Remove trailing newline */
				if (chord_type[n - 1] == '\n')
					chord_type[n - 1] = '\0';

				/* Check if type matches (case-insensitive) */
				if (strcasecmp(chord_type, type) != 0 && strcmp(type, "any") != 0)
					continue;
			}
		}

		/* Add to candidates */
		if (count >= capacity) {
			capacity = capacity == 0 ? 16 : capacity * 2;
			candidates = realloc(candidates, capacity * sizeof(char *));
		}
		candidates[count] = strdup(entry->d_name);
		count++;
	}

	closedir(dir);

	if (count == 0) {
		free(candidates);
		return -1;
	}

	/* Select random candidate */
	int idx = rand() % count;
	strncpy(out_id, candidates[idx], out_len - 1);
	out_id[out_len - 1] = '\0';

	/* Free candidates */
	for (int i = 0; i < count; i++) {
		free(candidates[i]);
	}
	free(candidates);

	return 0;
}

/* POST /api/sb/create - Create new songbook */
static int
handle_sb_create(int fd, char *body)
{
	/* Get current user */
	char cookie[256] = {0};
	char token[64] = {0};
	ndc_env_get(fd, cookie, "HTTP_COOKIE");
	call_get_cookie(cookie, token, sizeof(token));
	const char *username = call_get_session_user(token);

	if (!username || !*username) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 401);
		ndc_body(fd, "Login required");
		return 1;
	}

	/* Parse form data */
	call_mpfd_parse(fd, body);

	char id[128] = {0};
	char title[256] = {0};
	char choir[128] = {0};
	int id_len = call_mpfd_get("id", id, sizeof(id) - 1);
	int title_len = call_mpfd_get("title", title, sizeof(title) - 1);
	int choir_len = call_mpfd_get("choir", choir, sizeof(choir) - 1);

	if (id_len <= 0 || title_len <= 0 || choir_len <= 0) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 400);
		ndc_body(fd, "Missing id, title, or choir");
		return 1;
	}

	id[id_len] = '\0';
	title[title_len] = '\0';
	choir[choir_len] = '\0';

	/* Get doc root */
	char doc_root[256] = {0};
	ndc_env_get(fd, doc_root, "DOCUMENT_ROOT");
	if (!doc_root[0])
		strcpy(doc_root, ".");

	/* Check choir ownership */
	char choir_path[512];
	snprintf(choir_path, sizeof(choir_path), "%s/items/choir/items/%s", doc_root, choir);

	char choir_owner_path[1024];
	snprintf(choir_owner_path, sizeof(choir_owner_path), "%s/.owner", choir_path);

	FILE *cfp = fopen(choir_owner_path, "r");
	if (!cfp) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 400);
		ndc_body(fd, "Choir not found");
		return 1;
	}

	char choir_owner[64] = {0};
	size_t n = fread(choir_owner, 1, sizeof(choir_owner) - 1, cfp);
	fclose(cfp);
	if (n > 0 && choir_owner[n - 1] == '\n')
		choir_owner[n - 1] = '\0';

	if (strcmp(choir_owner, username) != 0) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 403);
		ndc_body(fd, "You don't own this choir");
		return 1;
	}

	/* Create songbook directory */
	char sb_path[512];
	snprintf(sb_path, sizeof(sb_path), "%s/items/sb/items/%s", doc_root, id);

	if (mkdir(sb_path, 0755) == -1 && errno != EEXIST) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 500);
		ndc_body(fd, "Failed to create songbook directory");
		return 1;
	}

	/* Write .owner */
	char owner_path[1024];
	snprintf(owner_path, sizeof(owner_path), "%s/.owner", sb_path);
	FILE *ofp = fopen(owner_path, "w");
	if (ofp) {
		fwrite(username, 1, strlen(username), ofp);
		fclose(ofp);
	}

	/* Write title */
	char title_path[1024];
	snprintf(title_path, sizeof(title_path), "%s/title", sb_path);
	FILE *tfp = fopen(title_path, "w");
	if (tfp) {
		fwrite(title, 1, strlen(title), tfp);
		fclose(tfp);
	}

	/* Write choir reference */
	char choir_file[1024];
	snprintf(choir_file, sizeof(choir_file), "%s/choir", sb_path);
	FILE *chfp = fopen(choir_file, "w");
	if (chfp) {
		fwrite(choir, 1, strlen(choir), chfp);
		fclose(chfp);
	}

	/* Initialize data.txt with choir's format */
	char format_path[1024];
	snprintf(format_path, sizeof(format_path), "%s/format", choir_path);

	char data_path[1024];
	snprintf(data_path, sizeof(data_path), "%s/data.txt", sb_path);
	FILE *dfp = fopen(data_path, "w");
	if (dfp) {
		FILE *ffp = fopen(format_path, "r");
		if (ffp) {
			/* Read choir format and create empty slots */
			char line[128];
			while (fgets(line, sizeof(line), ffp)) {
				/* Remove trailing newline */
				size_t len = strlen(line);
				while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
					line[--len] = '\0';
				if (len > 0)
					fprintf(dfp, ":0:%s\n", line);
			}
			fclose(ffp);
		} else {
			/* Use default format */
			fprintf(dfp, ":0:entrada\n");
			fprintf(dfp, ":0:aleluia\n");
			fprintf(dfp, ":0:ofertorio\n");
			fprintf(dfp, ":0:santo\n");
			fprintf(dfp, ":0:comunhao\n");
			fprintf(dfp, ":0:acao_de_gracas\n");
			fprintf(dfp, ":0:saida\n");
		}
		fclose(dfp);
	}

	/* Increment choir counter */
	char counter_path[1024];
	snprintf(counter_path, sizeof(counter_path), "%s/counter", choir_path);
	int counter = 0;
	FILE *cntfp = fopen(counter_path, "r");
	if (cntfp) {
		fscanf(cntfp, "%d", &counter);
		fclose(cntfp);
	}
	counter++;
	cntfp = fopen(counter_path, "w");
	if (cntfp) {
		fprintf(cntfp, "%d", counter);
		fclose(cntfp);
	}

	/* Add to index database */
	qmap_put(sb_index_db, id, title);

	/* Redirect to edit page */
	char location[256];
	snprintf(location, sizeof(location), "/sb/%s/edit", id);
	ndc_header(fd, "Location", location);
	ndc_head(fd, 303);
	ndc_close(fd);
	return 0;
}

/* POST /api/sb/:id/edit - Edit songbook */
static int
handle_sb_edit(int fd, char *body)
{
	/* Get current user */
	char cookie[256] = {0};
	char token[64] = {0};
	ndc_env_get(fd, cookie, "HTTP_COOKIE");
	call_get_cookie(cookie, token, sizeof(token));
	const char *username = call_get_session_user(token);

	if (!username || !*username) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 401);
		ndc_body(fd, "Login required");
		return 1;
	}

	/* Get songbook ID */
	char id[128] = {0};
	ndc_env_get(fd, id, "PATTERN_PARAM_ID");

	char doc_root[256] = {0};
	ndc_env_get(fd, doc_root, "DOCUMENT_ROOT");
	if (!doc_root[0])
		strcpy(doc_root, ".");

	char sb_path[512];
	snprintf(sb_path, sizeof(sb_path), "%s/items/sb/items/%s", doc_root, id);

	/* Check if songbook exists */
	struct stat st;
	if (stat(sb_path, &st) != 0 || !S_ISDIR(st.st_mode)) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 404);
		ndc_body(fd, "Songbook not found");
		return 1;
	}

	/* Check ownership */
	if (!check_sb_ownership(sb_path, username)) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 403);
		ndc_body(fd, "You don't own this songbook");
		return 1;
	}

	/* Parse form data */
	call_mpfd_parse(fd, body);

	/* Get amount */
	char amount_str[16] = {0};
	int amount_len = call_mpfd_get("amount", amount_str, sizeof(amount_str) - 1);
	int amount = amount_len > 0 ? atoi(amount_str) : 0;

	if (amount <= 0) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 400);
		ndc_body(fd, "Invalid amount");
		return 1;
	}

	/* Build new data.txt content */
	char data_path[1024];
	snprintf(data_path, sizeof(data_path), "%s/data.txt", sb_path);

	FILE *dfp = fopen(data_path, "w");
	if (!dfp) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 500);
		ndc_body(fd, "Failed to write data file");
		return 1;
	}

	for (int i = 0; i < amount; i++) {
		char song_key[32], t_key[32], fmt_key[32];
		snprintf(song_key, sizeof(song_key), "song_%d", i);
		snprintf(t_key, sizeof(t_key), "t_%d", i);
		snprintf(fmt_key, sizeof(fmt_key), "fmt_%d", i);

		char song[128] = {0};
		char t[16] = {0};
		char fmt[128] = {0};

		call_mpfd_get(song_key, song, sizeof(song) - 1);
		call_mpfd_get(t_key, t, sizeof(t) - 1);
		call_mpfd_get(fmt_key, fmt, sizeof(fmt) - 1);

		fprintf(dfp, "%s:%s:%s\n", 
			song[0] ? song : "",
			t[0] ? t : "0",
			fmt[0] ? fmt : "any");
	}

	fclose(dfp);

	/* Redirect to view page */
	char location[256];
	snprintf(location, sizeof(location), "/sb/%s", id);
	ndc_header(fd, "Location", location);
	ndc_head(fd, 303);
	ndc_close(fd);
	return 0;
}

/* POST /api/sb/:id/transpose - Transpose single song */
static int
handle_sb_transpose(int fd, char *body)
{
	/* Get current user */
	char cookie[256] = {0};
	char token[64] = {0};
	ndc_env_get(fd, cookie, "HTTP_COOKIE");
	call_get_cookie(cookie, token, sizeof(token));
	const char *username = call_get_session_user(token);

	if (!username || !*username) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 401);
		ndc_body(fd, "Login required");
		return 1;
	}

	/* Get songbook ID */
	char id[128] = {0};
	ndc_env_get(fd, id, "PATTERN_PARAM_ID");

	char doc_root[256] = {0};
	ndc_env_get(fd, doc_root, "DOCUMENT_ROOT");
	if (!doc_root[0])
		strcpy(doc_root, ".");

	char sb_path[512];
	snprintf(sb_path, sizeof(sb_path), "%s/items/sb/items/%s", doc_root, id);

	/* Check if songbook exists */
	struct stat st;
	if (stat(sb_path, &st) != 0 || !S_ISDIR(st.st_mode)) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 404);
		ndc_body(fd, "Songbook not found");
		return 1;
	}

	/* Check ownership */
	if (!check_sb_ownership(sb_path, username)) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 403);
		ndc_body(fd, "You don't own this songbook");
		return 1;
	}

	/* Parse form data */
	call_mpfd_parse(fd, body);

	char n_str[16] = {0};
	char t_str[16] = {0};
	call_mpfd_get("n", n_str, sizeof(n_str) - 1);
	call_mpfd_get("t", t_str, sizeof(t_str) - 1);

	int line_num = atoi(n_str);
	int new_transpose = atoi(t_str);

	/* Read current data.txt */
	char data_path[1024];
	snprintf(data_path, sizeof(data_path), "%s/data.txt", sb_path);

	FILE *fp = fopen(data_path, "r");
	if (!fp) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 500);
		ndc_body(fd, "Failed to read data file");
		return 1;
	}

	/* Read all lines */
	char **lines = NULL;
	int line_count = 0;
	int line_capacity = 0;
	char buffer[512];

	while (fgets(buffer, sizeof(buffer), fp)) {
		if (line_count >= line_capacity) {
			line_capacity = line_capacity == 0 ? 16 : line_capacity * 2;
			lines = realloc(lines, line_capacity * sizeof(char *));
		}
		lines[line_count] = strdup(buffer);
		line_count++;
	}
	fclose(fp);

	/* Update the specified line */
	if (line_num >= 0 && line_num < line_count) {
		char chord_id[128], format[128];
		int transpose;

		if (parse_sb_line(lines[line_num], chord_id, &transpose, format) == 0) {
			free(lines[line_num]);
			char new_line[512];
			snprintf(new_line, sizeof(new_line), "%s:%d:%s\n", chord_id, new_transpose, format);
			lines[line_num] = strdup(new_line);
		}
	}

	/* Write back */
	fp = fopen(data_path, "w");
	if (fp) {
		for (int i = 0; i < line_count; i++) {
			fputs(lines[i], fp);
		}
		fclose(fp);
	}

	/* Free lines */
	for (int i = 0; i < line_count; i++) {
		free(lines[i]);
	}
	free(lines);

	/* Redirect back with anchor */
	char location[256];
	snprintf(location, sizeof(location), "/sb/%s#%d", id, line_num);
	ndc_header(fd, "Location", location);
	ndc_head(fd, 303);
	ndc_close(fd);
	return 0;
}

/* POST /api/sb/:id/randomize - Randomize song selection */
static int
handle_sb_randomize(int fd, char *body)
{
	/* Get current user */
	char cookie[256] = {0};
	char token[64] = {0};
	ndc_env_get(fd, cookie, "HTTP_COOKIE");
	call_get_cookie(cookie, token, sizeof(token));
	const char *username = call_get_session_user(token);

	if (!username || !*username) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 401);
		ndc_body(fd, "Login required");
		return 1;
	}

	/* Get songbook ID */
	char id[128] = {0};
	ndc_env_get(fd, id, "PATTERN_PARAM_ID");

	char doc_root[256] = {0};
	ndc_env_get(fd, doc_root, "DOCUMENT_ROOT");
	if (!doc_root[0])
		strcpy(doc_root, ".");

	char sb_path[512];
	snprintf(sb_path, sizeof(sb_path), "%s/items/sb/items/%s", doc_root, id);

	/* Check if songbook exists */
	struct stat st;
	if (stat(sb_path, &st) != 0) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 404);
		ndc_body(fd, "Songbook not found");
		return 1;
	}

	/* Check ownership */
	if (!check_sb_ownership(sb_path, username)) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 403);
		ndc_body(fd, "You don't own this songbook");
		return 1;
	}

	/* Parse form data */
	call_mpfd_parse(fd, body);

	char n_str[16] = {0};
	call_mpfd_get("n", n_str, sizeof(n_str) - 1);
	int line_num = atoi(n_str);

	/* Read current data.txt */
	char data_path[1024];
	snprintf(data_path, sizeof(data_path), "%s/data.txt", sb_path);

	FILE *fp = fopen(data_path, "r");
	if (!fp) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 500);
		ndc_body(fd, "Failed to read data file");
		return 1;
	}

	/* Read all lines */
	char **lines = NULL;
	int line_count = 0;
	int line_capacity = 0;
	char buffer[512];

	while (fgets(buffer, sizeof(buffer), fp)) {
		if (line_count >= line_capacity) {
			line_capacity = line_capacity == 0 ? 16 : line_capacity * 2;
			lines = realloc(lines, line_capacity * sizeof(char *));
		}
		lines[line_count] = strdup(buffer);
		line_count++;
	}
	fclose(fp);

	/* Get format from specified line and randomize */
	if (line_num >= 0 && line_num < line_count) {
		char chord_id[128], format[128];
		int transpose;

		if (parse_sb_line(lines[line_num], chord_id, &transpose, format) == 0) {
			/* Get random chord by format */
			char new_chord[128] = {0};
			if (get_random_chord_by_type(doc_root, format, new_chord, sizeof(new_chord)) == 0) {
				free(lines[line_num]);
				char new_line[512];
				snprintf(new_line, sizeof(new_line), "%s:%d:%s\n", new_chord, transpose, format);
				lines[line_num] = strdup(new_line);
			}
		}
	}

	/* Write back */
	fp = fopen(data_path, "w");
	if (fp) {
		for (int i = 0; i < line_count; i++) {
			fputs(lines[i], fp);
		}
		fclose(fp);
	}

	/* Free lines */
	for (int i = 0; i < line_count; i++) {
		free(lines[i]);
	}
	free(lines);

	/* Redirect back with anchor */
	char location[256];
	snprintf(location, sizeof(location), "/sb/%s#%d", id, line_num);
	ndc_header(fd, "Location", location);
	ndc_head(fd, 303);
	ndc_close(fd);
	return 0;
}

/* DELETE /api/sb/:id - Delete songbook */
static int
handle_sb_delete(int fd, char *body)
{
	(void)body;

	/* Get current user */
	char cookie[256] = {0};
	char token[64] = {0};
	ndc_env_get(fd, cookie, "HTTP_COOKIE");
	call_get_cookie(cookie, token, sizeof(token));
	const char *username = call_get_session_user(token);

	if (!username || !*username) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 401);
		ndc_body(fd, "Login required");
		return 1;
	}

	/* Get songbook ID */
	char id[128] = {0};
	ndc_env_get(fd, id, "PATTERN_PARAM_ID");

	char doc_root[256] = {0};
	ndc_env_get(fd, doc_root, "DOCUMENT_ROOT");
	if (!doc_root[0])
		strcpy(doc_root, ".");

	char sb_path[512];
	snprintf(sb_path, sizeof(sb_path), "%s/items/sb/items/%s", doc_root, id);

	/* Check if songbook exists */
	struct stat st;
	if (stat(sb_path, &st) != 0) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 404);
		ndc_body(fd, "Songbook not found");
		return 1;
	}

	/* Check ownership */
	if (!check_sb_ownership(sb_path, username)) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 403);
		ndc_body(fd, "You don't own this songbook");
		return 1;
	}

	/* Remove from database */
	qmap_del(sb_index_db, id);

	/* Delete files */
	char path_buf[1024];
	snprintf(path_buf, sizeof(path_buf), "%s/.owner", sb_path);
	unlink(path_buf);
	snprintf(path_buf, sizeof(path_buf), "%s/title", sb_path);
	unlink(path_buf);
	snprintf(path_buf, sizeof(path_buf), "%s/choir", sb_path);
	unlink(path_buf);
	snprintf(path_buf, sizeof(path_buf), "%s/data.txt", sb_path);
	unlink(path_buf);
	rmdir(sb_path);

	/* Redirect to songbook list */
	ndc_header(fd, "Location", "/sb");
	ndc_head(fd, 303);
	ndc_close(fd);
	return 0;
}

MODULE_API void
ndx_install(void)
{
	char db_path[512];
	snprintf(db_path, sizeof(db_path), "./items/sb/items/index.db");
	sb_index_db = qmap_open(db_path, "rw", QM_STR, QM_STR, 0xFF, 0);

	ndx_load("./mods/auth/auth");
	ndx_load("./mods/common/common");
	ndx_load("./mods/mpfd/mpfd");
	ndx_load("./mods/ssr/ssr");

	ndc_register_handler("POST:/api/sb/create", handle_sb_create);
	ndc_register_handler("POST:/api/sb/:id/edit", handle_sb_edit);
	ndc_register_handler("POST:/api/sb/:id/transpose", handle_sb_transpose);
	ndc_register_handler("POST:/api/sb/:id/randomize", handle_sb_randomize);
	ndc_register_handler("DELETE:/api/sb/:id", handle_sb_delete);

	call_ssr_register_module("sb", "Songbook");
}

MODULE_API void
ndx_open(void)
{
}
