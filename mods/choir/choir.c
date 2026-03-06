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


static uint32_t choir_index_db = 0;

/* Default format types if choir doesn't define custom ones */
static const char *default_formats[] = {
	"entrada",
	"aleluia",
	"ofertorio",
	"santo",
	"comunhao",
	"acao_de_gracas",
	"saida",
	"any",
	NULL
};

/* Check if user owns a choir */
static int
check_choir_ownership(const char *choir_path, const char *username)
{
	if (!username || !*username)
		return 0;

	char owner_path[1024];
	snprintf(owner_path, sizeof(owner_path), "%s/.owner", choir_path);

	FILE *fp = fopen(owner_path, "r");
	if (!fp)
		return 0;

	char owner[64] = {0};
	size_t n = fread(owner, 1, sizeof(owner) - 1, fp);
	fclose(fp);
	if (n == 0)
		return 0;
	owner[n] = '\0';

	/* Remove trailing newline if present */
	if (owner[n - 1] == '\n')
		owner[n - 1] = '\0';

	return strcmp(owner, username) == 0;
}

/* POST /api/choir/create - Create new choir */
static int
handle_choir_create(int fd, char *body)
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
	int id_len = call_mpfd_get("id", id, sizeof(id) - 1);
	int title_len = call_mpfd_get("title", title, sizeof(title) - 1);

	if (id_len <= 0 || title_len <= 0) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 400);
		ndc_body(fd, "Missing id or title");
		return 1;
	}

	id[id_len] = '\0';
	title[title_len] = '\0';

	/* Build choir directory path */
	char doc_root[256] = {0};
	ndc_env_get(fd, doc_root, "DOCUMENT_ROOT");
	char choir_path[512];
	snprintf(choir_path, sizeof(choir_path), "%s/items/choir/items/%s",
		doc_root[0] ? doc_root : ".", id);

	/* Create directory */
	if (mkdir(choir_path, 0755) == -1 && errno != EEXIST) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 500);
		ndc_body(fd, "Failed to create choir directory");
		return 1;
	}

	/* Write .owner file */
	char owner_path[1024];
	snprintf(owner_path, sizeof(owner_path), "%s/.owner", choir_path);
	FILE *ofp = fopen(owner_path, "w");
	if (!ofp) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 500);
		ndc_body(fd, "Failed to write owner file");
		return 1;
	}
	fwrite(username, 1, strlen(username), ofp);
	fclose(ofp);

	/* Write title file */
	char title_path[1024];
	snprintf(title_path, sizeof(title_path), "%s/title", choir_path);
	FILE *tfp = fopen(title_path, "w");
	if (!tfp) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 500);
		ndc_body(fd, "Failed to write title file");
		return 1;
	}
	fwrite(title, 1, strlen(title), tfp);
	fclose(tfp);

	/* Initialize counter to 0 */
	char counter_path[1024];
	snprintf(counter_path, sizeof(counter_path), "%s/counter", choir_path);
	FILE *cfp = fopen(counter_path, "w");
	if (cfp) {
		fprintf(cfp, "0");
		fclose(cfp);
	}

	/* Add to index database */
	qmap_put(choir_index_db, id, title);

	/* Redirect to choir page */
	char location[256];
	snprintf(location, sizeof(location), "/choir/%s", id);
	ndc_header(fd, "Location", location);
	ndc_head(fd, 303);
	ndc_close(fd);
	return 0;
}

/* POST /api/choir/:id/edit - Edit choir */
static int
handle_choir_edit(int fd, char *body)
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

	/* Get choir ID from URL pattern */
	char id[128] = {0};
	ndc_env_get(fd, id, "PATTERN_PARAM_ID");

	/* Build choir path */
	char doc_root[256] = {0};
	ndc_env_get(fd, doc_root, "DOCUMENT_ROOT");
	char choir_path[512];
	snprintf(choir_path, sizeof(choir_path), "%s/items/choir/items/%s",
		doc_root[0] ? doc_root : ".", id);

	/* Check if choir exists */
	struct stat st;
	if (stat(choir_path, &st) != 0 || !S_ISDIR(st.st_mode)) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 404);
		ndc_body(fd, "Choir not found");
		return 1;
	}

	/* Check ownership */
	if (!check_choir_ownership(choir_path, username)) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 403);
		ndc_body(fd, "You don't own this choir");
		return 1;
	}

	/* Parse form data */
	call_mpfd_parse(fd, body);

	char title[256] = {0};
	char format[2048] = {0};
	int title_len = call_mpfd_get("title", title, sizeof(title) - 1);
	int format_len = call_mpfd_get("format", format, sizeof(format) - 1);

	if (title_len > 0) {
		title[title_len] = '\0';

		/* Update title file */
		char title_path[1024];
		snprintf(title_path, sizeof(title_path), "%s/title", choir_path);
		FILE *tfp = fopen(title_path, "w");
		if (tfp) {
			fwrite(title, 1, strlen(title), tfp);
			fclose(tfp);

			/* Update in database */
			qmap_put(choir_index_db, id, title);
		}
	}

	if (format_len > 0) {
		format[format_len] = '\0';

		/* Update format file */
		char format_path[1024];
		snprintf(format_path, sizeof(format_path), "%s/format", choir_path);
		FILE *ffp = fopen(format_path, "w");
		if (ffp) {
			fwrite(format, 1, strlen(format), ffp);
			fclose(ffp);
		}
	}

	/* Redirect back to choir page */
	char location[256];
	snprintf(location, sizeof(location), "/choir/%s", id);
	ndc_header(fd, "Location", location);
	ndc_head(fd, 303);
	ndc_close(fd);
	return 0;
}

/* DELETE /api/choir/:id - Delete choir */
static int
handle_choir_delete(int fd, char *body)
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

	/* Get choir ID from URL pattern */
	char id[128] = {0};
	ndc_env_get(fd, id, "PATTERN_PARAM_ID");

	/* Build choir path */
	char doc_root[256] = {0};
	ndc_env_get(fd, doc_root, "DOCUMENT_ROOT");
	char choir_path[512];
	snprintf(choir_path, sizeof(choir_path), "%s/items/choir/items/%s",
		doc_root[0] ? doc_root : ".", id);

	/* Check if choir exists */
	struct stat st;
	if (stat(choir_path, &st) != 0 || !S_ISDIR(st.st_mode)) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 404);
		ndc_body(fd, "Choir not found");
		return 1;
	}

	/* Check ownership */
	if (!check_choir_ownership(choir_path, username)) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 403);
		ndc_body(fd, "You don't own this choir");
		return 1;
	}

	/* Remove from database */
	qmap_del(choir_index_db, id);

	/* Delete directory (simple approach - just remove key files) */
	char path_buf[1024];
	snprintf(path_buf, sizeof(path_buf), "%s/.owner", choir_path);
	unlink(path_buf);
	snprintf(path_buf, sizeof(path_buf), "%s/title", choir_path);
	unlink(path_buf);
	snprintf(path_buf, sizeof(path_buf), "%s/counter", choir_path);
	unlink(path_buf);
	snprintf(path_buf, sizeof(path_buf), "%s/format", choir_path);
	unlink(path_buf);
	rmdir(choir_path);

	/* Redirect to choir list */
	ndc_header(fd, "Location", "/choir");
	ndc_head(fd, 303);
	ndc_close(fd);
	return 0;
}

MODULE_API void
ndx_install(void)
{
	char db_path[512];
	snprintf(db_path, sizeof(db_path), "./items/choir/items/index.db");
	choir_index_db = qmap_open(db_path, "rw", QM_STR, QM_STR, 0xFF, 0);

	ndx_load("./mods/auth/auth");
	ndx_load("./mods/common/common");
	ndx_load("./mods/mpfd/mpfd");

	ndc_register_handler("POST:/api/choir/create", handle_choir_create);
	ndc_register_handler("POST:/api/choir/:id/edit", handle_choir_edit);
	ndc_register_handler("DELETE:/api/choir/:id", handle_choir_delete);

	call_ssr_register_module("choir", "Choirs");
}

MODULE_API void
ndx_open(void)
{
}
