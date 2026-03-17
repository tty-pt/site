#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* #include <errno.h> */
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
#include "../index/index.h"
#include "../song/song.h"

#define SONGBOOK_ITEMS_PATH "items/songbook/items"
#define SONG_ITEMS_PATH "items/song/items"

static unsigned index_hd;

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

/* Get random chord by type/format - uses song module's type index */
static int
get_random_chord_by_type(const char *doc_root, const char *type, char *out_id, size_t out_len)
{
	(void)doc_root;

	char *random_id = NULL;
	if (call_song_get_random_by_type(type, &random_id) != 0) {
		return -1;
	}

	if (random_id) {
		strncpy(out_id, random_id, out_len - 1);
		out_id[out_len - 1] = '\0';
		free(random_id);
		return 0;
	}

	return -1;
}

/* POST /api/songbook/create - Create new songbook */
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
	snprintf(sb_path, sizeof(sb_path), "%s/items/songbook/items/%s", doc_root, id);

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
	call_index_put(index_hd, id, title);

	/* Redirect to edit page */
	char location[256];
	snprintf(location, sizeof(location), "/songbook/%s/edit", id);
	ndc_header(fd, "Location", location);
	ndc_head(fd, 303);
	ndc_close(fd);
	return 0;
}

/* GET /songbook/:id/edit - read songbook and proxy to Fresh */
static int
handle_sb_edit_get(int fd, char *body)
{
	(void)body;

	char id[128] = {0};
	ndc_env_get(fd, id, "PATTERN_PARAM_ID");

	char doc_root[256] = {0};
	ndc_env_get(fd, doc_root, "DOCUMENT_ROOT");
	if (!doc_root[0])
		strcpy(doc_root, ".");

	char sb_path[512];
	snprintf(sb_path, sizeof(sb_path), "%s/items/songbook/items/%s", doc_root, id);

	/* Check if songbook exists */
	struct stat st;
	if (stat(sb_path, &st) != 0 || !S_ISDIR(st.st_mode)) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 404);
		ndc_body(fd, "Songbook not found");
		return 1;
	}

	/* Read title */
	char title[256] = {0};
	char title_path[512];
	snprintf(title_path, sizeof(title_path), "%s/title", sb_path);
	FILE *tfp = fopen(title_path, "r");
	if (tfp) {
		if (fgets(title, sizeof(title) - 1, tfp)) {
			size_t len = strlen(title);
			if (len > 0 && title[len - 1] == '\n')
				title[len - 1] = '\0';
		}
		fclose(tfp);
	}

	/* Read choir */
	char choir[128] = {0};
	char choir_path[512];
	snprintf(choir_path, sizeof(choir_path), "%s/choir", sb_path);
	FILE *cfp = fopen(choir_path, "r");
	if (cfp) {
		if (fgets(choir, sizeof(choir) - 1, cfp)) {
			size_t len = strlen(choir);
			if (len > 0 && choir[len - 1] == '\n')
				choir[len - 1] = '\0';
		}
		fclose(cfp);
	}

	/* Read data.txt */
	char *data_content = NULL;
	size_t data_len = 0;
	char data_path[512];
	snprintf(data_path, sizeof(data_path), "%s/data.txt", sb_path);
	FILE *dfp = fopen(data_path, "r");
	if (dfp) {
		fseek(dfp, 0, SEEK_END);
		long fsize = ftell(dfp);
		fseek(dfp, 0, SEEK_SET);
		if (fsize > 0 && fsize < 65536) {
			data_content = malloc(fsize + 1);
			if (data_content) {
				data_len = fread(data_content, 1, fsize, dfp);
				data_content[data_len] = '\0';
			}
		}
		fclose(dfp);
	}

	/* Build POST body */
	char post_body[70000] = {0};
	size_t pos = 0;

	pos += snprintf(post_body + pos, sizeof(post_body) - pos, "title=");
	if (title[0]) {
		for (size_t i = 0; title[i] && pos < sizeof(post_body) - 4; i++) {
			char c = title[i];
			if (c == '%') { pos += snprintf(post_body + pos, 4, "%%25"); }
			else if (c == ' ') { pos += snprintf(post_body + pos, 4, "%%20"); }
			else if (c == '\n') { pos += snprintf(post_body + pos, 4, "%%0A"); }
			else { post_body[pos++] = c; }
		}
	}

	pos += snprintf(post_body + pos, sizeof(post_body) - pos, "&choir=");
	if (choir[0]) {
		for (size_t i = 0; choir[i] && pos < sizeof(post_body) - 4; i++) {
			char c = choir[i];
			if (c == '%') { pos += snprintf(post_body + pos, 4, "%%25"); }
			else if (c == ' ') { pos += snprintf(post_body + pos, 4, "%%20"); }
			else if (c == '\n') { pos += snprintf(post_body + pos, 4, "%%0A"); }
			else { post_body[pos++] = c; }
		}
	}

	if (data_content && data_len > 0) {
		pos += snprintf(post_body + pos, sizeof(post_body) - pos, "&songs=");
		for (size_t i = 0; i < data_len && pos < sizeof(post_body) - 10; i++) {
			char c = data_content[i];
			if (c == '%') { pos += snprintf(post_body + pos, 4, "%%25"); }
			else if (c == ' ') { pos += snprintf(post_body + pos, 4, "%%20"); }
			else if (c == '\n') { pos += snprintf(post_body + pos, 4, "%%0A"); }
			else if (c == ':') { pos += snprintf(post_body + pos, 4, "%%3A"); }
			else { post_body[pos++] = c; }
		}
	}

	if (data_content) free(data_content);

	/* POST to Fresh */
	return call_core_post(fd, post_body, strlen(post_body));
}

/* POST /songbook/:id/edit - Edit songbook */
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
	snprintf(sb_path, sizeof(sb_path), "%s/items/songbook/items/%s", doc_root, id);

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
	snprintf(location, sizeof(location), "/songbook/%s", id);
	ndc_header(fd, "Location", location);
	ndc_head(fd, 303);
	ndc_close(fd);
	return 0;
}

/* POST /api/songbook/:id/transpose - Transpose single song */
static int
handle_sb_transpose(int fd, char *body)
{
	/* Get current user */
	/* char cookie[256] = {0}; */
	char token[64] = {0};
  /*
	ndc_env_get(fd, cookie, "HTTP_COOKIE");
	call_get_cookie(cookie, token, sizeof(token));
	const char *username = call_get_session_user(token);

	if (!username || !*username) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 401);
		ndc_body(fd, "Login required");
		return 1;
	}
  */

	/* Get songbook ID */
	char id[128] = {0};
	ndc_env_get(fd, id, "PATTERN_PARAM_ID");

	char doc_root[256] = {0};
	ndc_env_get(fd, doc_root, "DOCUMENT_ROOT");
	if (!doc_root[0])
		strcpy(doc_root, ".");

	char sb_path[512];
	snprintf(sb_path, sizeof(sb_path), "%s/items/songbook/items/%s", doc_root, id);

	/* Check if songbook exists */
	struct stat st;
	if (stat(sb_path, &st) != 0 || !S_ISDIR(st.st_mode)) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 404);
		ndc_body(fd, "Songbook not found");
		return 1;
	}

	/* Check ownership */
  /*
	if (!check_sb_ownership(sb_path, username)) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 403);
		ndc_body(fd, "You don't own this songbook");
		return 1;
	}
  */

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
	snprintf(location, sizeof(location), "/songbook/%s#%d", id, line_num);
	ndc_header(fd, "Location", location);
	ndc_head(fd, 303);
	ndc_close(fd);
	return 0;
}

/* POST /api/songbook/:id/randomize - Randomize song selection */
static int
handle_sb_randomize(int fd, char *body)
{
	/* Get current user */
  /*
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
  */

	/* Get songbook ID */
	char id[128] = {0};
	ndc_env_get(fd, id, "PATTERN_PARAM_ID");

	char doc_root[256] = {0};
	ndc_env_get(fd, doc_root, "DOCUMENT_ROOT");
	if (!doc_root[0])
		strcpy(doc_root, ".");

	char sb_path[512];
	snprintf(sb_path, sizeof(sb_path), "%s/items/songbook/items/%s", doc_root, id);

	/* Check if songbook exists */
	struct stat st;
	if (stat(sb_path, &st) != 0) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 404);
		ndc_body(fd, "Songbook not found");
		return 1;
	}

	/* Check ownership */
  /*
	if (!check_sb_ownership(sb_path, username)) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 403);
		ndc_body(fd, "You don't own this songbook");
		return 1;
	}
  */

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
	snprintf(location, sizeof(location), "/songbook/%s#%d", id, line_num);
	ndc_header(fd, "Location", location);
	ndc_head(fd, 303);
	ndc_close(fd);
	return 0;
}

/* DELETE /api/songbook/:id - Delete songbook */
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
	snprintf(sb_path, sizeof(sb_path), "%s/items/songbook/items/%s", doc_root, id);

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

	/* Remove from database - index module handles this */
	/* Note: index module doesn't have a del function, title file removal will handle it on next scan */

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
	ndc_header(fd, "Location", "/songbook");
	ndc_head(fd, 303);
	ndc_close(fd);
	return 0;
}

static char *
songbook_json(int fd)
{
	char doc_root[256] = {0};
	char id[128] = {0};
	char sb_path[512];
	char path_buf[1024];

	char title[256] = {0}, owner[64] = {0}, choir[64] = {0};
	char data[8192] = {0};
	char esc_title[512], esc_owner[128], esc_choir[128], esc_data[16384];

	ndc_env_get(fd, doc_root, "DOCUMENT_ROOT");
	ndc_env_get(fd, id, "PATTERN_PARAM_ID");

	if (!id[0]) return NULL;

	snprintf(sb_path, sizeof(sb_path), "%s/%s/%s",
		doc_root[0] ? doc_root : ".", SONGBOOK_ITEMS_PATH, id);

	/* Read title */
	snprintf(path_buf, sizeof(path_buf), "%s/title", sb_path);
	FILE *tfp = fopen(path_buf, "r");
	if (tfp) {
		if (fgets(title, sizeof(title) - 1, tfp)) {
			size_t l = strlen(title);
			if (l > 0 && title[l - 1] == '\n') title[l - 1] = '\0';
		}
		fclose(tfp);
	} else {
		return NULL;
	}

	/* Read owner */
	snprintf(path_buf, sizeof(path_buf), "%s/.owner", sb_path);
	FILE *ofp = fopen(path_buf, "r");
	if (ofp) {
		if (fgets(owner, sizeof(owner) - 1, ofp)) {
			size_t l = strlen(owner);
			if (l > 0 && owner[l - 1] == '\n') owner[l - 1] = '\0';
		}
		fclose(ofp);
	}

	/* Read choir */
	snprintf(path_buf, sizeof(path_buf), "%s/choir", sb_path);
	FILE *cfp = fopen(path_buf, "r");
	if (cfp) {
		if (fgets(choir, sizeof(choir) - 1, cfp)) {
			size_t l = strlen(choir);
			if (l > 0 && choir[l - 1] == '\n') choir[l - 1] = '\0';
		}
		fclose(cfp);
	}

	/* Read data.txt - contains lines of chord_id:transpose:format */
	snprintf(path_buf, sizeof(path_buf), "%s/data.txt", sb_path);
	FILE *dfp = fopen(path_buf, "r");
	if (dfp) {
		size_t n = fread(data, 1, sizeof(data) - 1, dfp);
		data[n] = '\0';
		fclose(dfp);
	}

	/* Parse data lines and build songs JSON array */
	/* Format: chord_id:transpose:format */
	char *songs_json = malloc(16384);
	if (!songs_json) return NULL;
	songs_json[0] = '[';
	songs_json[1] = '\0';

	char *line = strtok(data, "\n");
	int first = 1;
	while (line) {
		char chord_id[128] = {0};
		int transpose = 0;
		char format[64] = {0};

		char *colon1 = strchr(line, ':');
		if (colon1) {
			size_t id_len = colon1 - line;
			if (id_len > 127) id_len = 127;
			strncpy(chord_id, line, id_len);

			char *colon2 = strchr(colon1 + 1, ':');
			if (colon2) {
				strncpy(format, colon2 + 1, sizeof(format) - 1);
				*colon2 = '\0';
			}
			transpose = atoi(colon1 + 1);
		} else {
			strncpy(chord_id, line, sizeof(chord_id) - 1);
		}

		/* Remove trailing \r if present */
		size_t fmt_len = strlen(format);
		while (fmt_len > 0 && (format[fmt_len - 1] == '\r' || format[fmt_len - 1] == '\n')) {
			format[--fmt_len] = '\0';
		}

		if (chord_id[0]) {
			/* Read chord title */
			char chord_title[256] = {0};
			char chord_title_esc[512];
			char chord_path[512];
			int original_key = 0;
			snprintf(chord_path, sizeof(chord_path), "%s/%s/%s/title",
				doc_root[0] ? doc_root : ".", SONG_ITEMS_PATH, chord_id);
			FILE *ctfp = fopen(chord_path, "r");
			if (ctfp) {
				if (fgets(chord_title, sizeof(chord_title) - 1, ctfp)) {
					size_t l = strlen(chord_title);
					if (l > 0 && chord_title[l - 1] == '\n') chord_title[l - 1] = '\0';
				}
				fclose(ctfp);
			}

			/* Read and transpose chord data */
			char chord_data[4096] = {0};
			char chord_data_esc[12288];
			snprintf(chord_path, sizeof(chord_path), "%s/%s/%s/data.txt",
				doc_root[0] ? doc_root : ".", SONG_ITEMS_PATH, chord_id);
			FILE *cdfp = fopen(chord_path, "r");
			if (cdfp) {
				size_t n = fread(chord_data, 1, sizeof(chord_data) - 1, cdfp);
				chord_data[n] = '\0';
				fclose(cdfp);

				/* Reset key detection for each new song */
			call_song_reset_key(0);

				/* Detect key from original data first (transpose=0) */
			call_song_transpose(chord_data, 0, 0, NULL, &original_key);
			if (original_key < 0) original_key = 0;
			
			/* Then transpose if needed */
			char *transposed = NULL;
			if (transpose != 0) {
				call_song_transpose(chord_data, transpose, 0, &transposed, NULL);
				if (transposed) {
					strncpy(chord_data, transposed, sizeof(chord_data) - 1);
					free(transposed);
				}
			}

				call_json_escape(chord_data, chord_data_esc, sizeof(chord_data_esc));
			}

			call_json_escape(chord_title, chord_title_esc, sizeof(chord_title_esc));

			char esc_format[128];
			call_json_escape(format, esc_format, sizeof(esc_format));

			if (!first) strcat(songs_json, ",");
			first = 0;

			char song_buf[20480];
			snprintf(song_buf, sizeof(song_buf),
				"{\"chordId\":\"%s\",\"transpose\":%d,\"format\":\"%s\",\"chordTitle\":\"%s\",\"chordData\":\"%s\",\"originalKey\":%d}",
				chord_id, transpose, esc_format, chord_title_esc, chord_data_esc, original_key);
			strcat(songs_json, song_buf);
		}

		line = strtok(NULL, "\n");
	}

	strcat(songs_json, "]");

	/* Escape fields */
	call_json_escape(title, esc_title, sizeof(esc_title));
	call_json_escape(owner, esc_owner, sizeof(esc_owner));
	call_json_escape(choir, esc_choir, sizeof(esc_choir));

	size_t resp_len = strlen(esc_title) + strlen(esc_owner) +
		strlen(esc_choir) + strlen(songs_json) + 256;

	char *response = malloc(resp_len);
	if (response) {
		snprintf(response, resp_len,
			"{"
			"\"id\":\"%s\","
			"\"title\":\"%s\","
			"\"owner\":\"%s\","
			"\"choir\":\"%s\","
			"\"songs\":%s"
			"}",
			id, esc_title, esc_owner, esc_choir, songs_json);
	}

	free(songs_json);
	return response;
}

static int
songbook_details_handler(int fd, char *body)
{
	(void)body;

	char *json = songbook_json(fd);
	int result = call_core_post(fd, json, json ? strlen(json) : 0);
	free(json);
	return result;
}

void ndx_install(void)
{
	/* ndx_load("./mods/common/common"); */
	ndx_load("./mods/index/index");
	ndx_load("./mods/mpfd/mpfd");
	ndx_load("./mods/auth/auth");
	ndx_load("./mods/song/song");

	ndc_register_handler("GET:/songbook/:id",
			songbook_details_handler);

	ndc_register_handler("POST:/songbook/:id/randomize", handle_sb_randomize);
	ndc_register_handler("POST:/songbook/:id/transpose", handle_sb_transpose);
	/*
	ndc_register_handler("POST:/api/songbook/create", handle_sb_create);
	ndc_register_handler("GET:/songbook/:id/edit", handle_sb_edit_get);
	ndc_register_handler("POST:/songbook/:id/edit", handle_sb_edit);
	*/

	index_hd = call_index_open("Songbook", 0, 1);
}

void ndx_open(void) {}
