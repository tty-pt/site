#include <ttypt/ndx-mod.h>

#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <crypt.h>
#include <sys/stat.h>
#include <errno.h>
#include <dirent.h>

#include <ttypt/ndc.h>
#include <ttypt/qmap.h>

#include "../common/common.h"
#include "../index/index.h"

static unsigned users_map;
static unsigned sessions_map;

struct user {
	char active;
	char hash[64];
};

static unsigned auth_hd = 0;

/* Path helpers */
static void
shadow_path(char *buf, size_t len)
{
	char doc_root[256] = {0};
	/* doc_root is empty when running as root (chroot); use "." */
	snprintf(buf, len, "%s/etc/shadow",
		doc_root[0] ? doc_root : ".");
}

static void
users_dir(char *buf, size_t len)
{
	snprintf(buf, len, "./users");
}

NDX_DEF(const char *, get_session_user,
		const char *, token)
{
	if (!token || !*token)
		return NULL;
	return qmap_get(sessions_map, token);
}

static void
generate_token(char *buf, size_t len)
{
	FILE *f = fopen("/dev/urandom", "r");
	if (!f) {
		snprintf(buf, len, "%llx", (unsigned long long)
				time(NULL));
		return;
	}
	for (size_t i = 0; i + 1 < len; ) {
		int c = fgetc(f);
		if (c == EOF)
			break;
		buf[i++] = "0123456789abcdef"[(c >> 4) & 15];
		buf[i++] = "0123456789abcdef"[c & 15];
	}
	buf[len - 1] = 0;
	fclose(f);
}

/* Generate a random bcrypt salt: $2b$12$<22 base64 chars> */
static void
generate_bcrypt_salt(char *buf, size_t len)
{
	static const char b64[] =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
		"0123456789./";
	unsigned char rnd[17];
	FILE *f = fopen("/dev/urandom", "r");
	if (f) {
		if (fread(rnd, 1, sizeof(rnd), f) != sizeof(rnd))
			memset(rnd, 0, sizeof(rnd));
		fclose(f);
	} else {
		unsigned long long t = (unsigned long long)time(NULL);
		for (int i = 0; i < 17; i++) {
			rnd[i] = (unsigned char)(t >> (i % 8));
		}
	}
	/* Encode 17 bytes → 22 base64 chars (bcrypt uses its own base64) */
	int out = 0;
	for (int i = 0; i < 17 && out < 22; i++) {
		buf[out++] = b64[rnd[i] & 63];
		if (out < 22)
			buf[out++] = b64[(rnd[i] >> 2) & 63];
	}
	/* Build the full salt string */
	char salt_chars[23];
	memcpy(salt_chars, buf, 22);
	salt_chars[22] = '\0';
	snprintf(buf, len, "$2b$12$%s", salt_chars);
}

/* Append a line to the shadow file */
static int
shadow_append(const char *username, const char *hash)
{
	char path[512];
	shadow_path(path, sizeof(path));
	FILE *f = fopen(path, "a");
	if (!f)
		return -1;
	fprintf(f, "%s:%s\n", username, hash);
	fclose(f);
	return 0;
}

/* Rewrite the shadow file (for updates) */
static int
shadow_update(const char *username, const char *new_hash)
{
	char path[512], tmp[560];
	shadow_path(path, sizeof(path));
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);

	FILE *in = fopen(path, "r");
	FILE *out = fopen(tmp, "w");
	if (!out)
		return -1;

	if (in) {
		char line[256];
		while (fgets(line, sizeof(line), in)) {
			char *colon = strchr(line, ':');
			if (colon) {
				size_t ulen = (size_t)(colon - line);
				if (ulen == strlen(username) &&
				    strncmp(line, username, ulen) == 0) {
					fprintf(out, "%s:%s\n", username, new_hash);
					continue;
				}
			}
			fputs(line, out);
		}
		fclose(in);
	}
	fclose(out);
	rename(tmp, path);
	return 0;
}

/* Load shadow file into users_map qmap */
static void
load_shadow(void)
{
	char path[512];
	shadow_path(path, sizeof(path));
	FILE *f = fopen(path, "r");
	if (!f)
		return;

	char line[256];
	while (fgets(line, sizeof(line), f)) {
		/* strip trailing newline */
		size_t ll = strlen(line);
		while (ll > 0 && (line[ll-1] == '\n' || line[ll-1] == '\r'))
			line[--ll] = '\0';

		char *colon = strchr(line, ':');
		if (!colon)
			continue;
		*colon = '\0';
		const char *uname = line;
		const char *hash  = colon + 1;
		if (!*uname || !*hash)
			continue;

		struct user u = {0};
		strncpy(u.hash, hash, sizeof(u.hash) - 1);

		/* Check for active flag file */
		char active_path[512];
		snprintf(active_path, sizeof(active_path),
			"./users/%s/active", uname);
		struct stat st;
		u.active = (stat(active_path, &st) == 0) ? 1 : 0;

		qmap_put(users_map, uname, &u);
	}
	fclose(f);
}

static int
handle_session(int fd, char *body)
{
	(void)body;

	char cookie[256] = {0};
	char token[64] = {0};

	ndc_env_get(fd, cookie, "HTTP_COOKIE");
	call_get_cookie(cookie, token, sizeof(token));

	const char *username = qmap_get(sessions_map, token);
	ndc_header(fd, "Content-Type", "text/plain");
	ndc_head(fd, 200);
	ndc_body(fd, username ? username : "");
	return 0;
}

static int
handle_login(int fd, char *body)
{
	char username[64], password[64], redirect[256],
		token[64], cookie[128], *hash;

	struct user *user;

	call_query_parse(body);

	call_query_param("username", username, sizeof(username));
	call_query_param("password", password, sizeof(password));
	call_query_param("ret", redirect, sizeof(redirect));

	if (!*username || !*password) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 400);
		ndc_body(fd, "Missing username or password");
		return 1;
	}

	user = (struct user *) qmap_get(users_map, username);

	if (!user) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 400);
		ndc_body(fd, "User not found");
		return 1;
	}

	hash = crypt(password, user->hash);

	if (!hash || strcmp(hash, user->hash)) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 401);
		ndc_body(fd, "Invalid password");
		return 1;
	}

	if (!user->active) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 400);
		ndc_body(fd, "Account not confirmed");
		return 1;
	}

	generate_token(token, sizeof(token));
	qmap_put(sessions_map, token, username);

	snprintf(cookie, sizeof(cookie),
			"QSESSION=%s; Path=/; SameSite=Lax", token);

	ndc_header(fd, "Location",
			*redirect ? redirect : "/");

	ndc_header(fd, "Set-Cookie", cookie);
	ndc_head(fd, 303);
	ndc_close(fd);
	return 0;
}

static int
handle_logout(int fd, char *body)
{
	(void)body;

	char cookie[256], token[64];

	ndc_env_get(fd, cookie, "HTTP_COOKIE");
	call_get_cookie(cookie, token, sizeof(token));

	if (*token)
		qmap_del(sessions_map, token);

	ndc_header(fd, "Location", "/");
	ndc_header(fd, "Set-Cookie",
		"QSESSION=; Path=/; Max-Age=0; SameSite=Lax");
	ndc_head(fd, 303);
	ndc_close(fd);
	return 0;
}

static int
valid_username_char(char c)
{
	return (c >= 'a' && c <= 'z')
		|| (c >= 'A' && c <= 'Z')
		|| (c >= '0' && c <= '9')
		|| c == '_' || c == '-';
}

static int
handle_register(int fd, char *body)
{
	char username[64], password[64],
		password_confirm[64], email[128];
	char salt[64], *hash;
	char user_dir[512], rcode_path[560];
	char rcode[64];

	struct user user = {0};

	call_query_parse(body);

	call_query_param("username", username, sizeof(username));
	call_query_param("password", password, sizeof(password));
	call_query_param("password2", password_confirm,
		sizeof(password_confirm));
	call_query_param("email", email, sizeof(email));

	size_t username_len = strlen(username);

	if (username_len < 2 || username_len > 32) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 400);
		ndc_body(fd, "Username must be 2-32 characters");
		return 1;
	}

	for (char *p = username; *p; p++) {
		if (!valid_username_char(*p)) {
			ndc_header(fd, "Content-Type", "text/plain");
			ndc_head(fd, 400);
			ndc_body(fd, "Invalid username character");
			return 1;
		}
	}

	if (strlen(password) < 4) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 400);
		ndc_body(fd, "Password too short");
		return 1;
	}

	if (strcmp(password, password_confirm)) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 400);
		ndc_body(fd, "Passwords do not match");
		return 1;
	}

	if (qmap_get(users_map, username)) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 400);
		ndc_body(fd, "Username already exists");
		return 1;
	}

	generate_bcrypt_salt(salt, sizeof(salt));
	hash = crypt(password, salt);

	if (!hash) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 500);
		ndc_body(fd, "Password hashing failed");
		return 1;
	}

	strncpy(user.hash, hash, sizeof(user.hash) - 1);
	user.active = 0;

	/* Write to in-memory qmap */
	qmap_put(users_map, username, &user);

	/* Persist to shadow file */
	if (shadow_append(username, user.hash) != 0) {
		fprintf(stderr, "auth: warning: could not write shadow file\n");
	}

	/* Create user directory and rcode file */
	snprintf(user_dir, sizeof(user_dir), "./users/%s", username);
	if (mkdir(user_dir, 0755) && errno != EEXIST) {
		fprintf(stderr, "auth: warning: could not create user dir %s\n",
			user_dir);
	}

	/* Write email */
	if (*email) {
		char email_path[560];
		snprintf(email_path, sizeof(email_path),
			"%s/email", user_dir);
		FILE *ef = fopen(email_path, "w");
		if (ef) {
			fputs(email, ef);
			fclose(ef);
		}
	}

	/* Write confirmation code */
	generate_token(rcode, sizeof(rcode));
	snprintf(rcode_path, sizeof(rcode_path), "%s/rcode", user_dir);
	FILE *rf = fopen(rcode_path, "w");
	if (rf) {
		fputs(rcode, rf);
		fclose(rf);
	}

	fprintf(stderr, "Register: /auth/confirm?u=%s&r=%s\n",
		username, rcode);

	ndc_header(fd, "Location", "/welcome");
	ndc_head(fd, 303);
	ndc_close(fd);
	return 0;
}

static int
handle_confirm(int fd, char *body)
{
	(void)body;

	char query[256], username[64], code[64],
		token[64], cookie[128];
	char rcode_path[512], active_path[512];
	char stored_rcode[64] = {0};

	struct user user, *existing;

	ndc_env_get(fd, query, "QUERY_STRING");

	call_query_parse(query);

	call_query_param("u", username, sizeof(username));
	call_query_param("r", code, sizeof(code));

	if (!*username || !*code) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 400);
		ndc_body(fd, "Missing parameters");
		return 1;
	}

	existing = (struct user *)
		qmap_get(users_map, username);

	if (!existing || existing->active) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 400);
		ndc_body(fd, "Invalid confirmation");
		return 1;
	}

	/* Read rcode from file */
	snprintf(rcode_path, sizeof(rcode_path),
		"./users/%s/rcode", username);
	FILE *rf = fopen(rcode_path, "r");
	if (!rf) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 400);
		ndc_body(fd, "No confirmation pending");
		return 1;
	}
	if (!fgets(stored_rcode, sizeof(stored_rcode), rf)) {
		fclose(rf);
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 400);
		ndc_body(fd, "Could not read confirmation code");
		return 1;
	}
	fclose(rf);

	/* Strip trailing newline */
	size_t rlen = strlen(stored_rcode);
	while (rlen > 0 && (stored_rcode[rlen-1] == '\n' ||
	                    stored_rcode[rlen-1] == '\r'))
		stored_rcode[--rlen] = '\0';

	if (strcmp(code, stored_rcode)) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 400);
		ndc_body(fd, "Wrong confirmation code");
		return 1;
	}

	/* Activate in qmap */
	memcpy(&user, existing, sizeof(user));
	user.active = 1;
	qmap_put(users_map, username, &user);

	/* Create active flag file */
	snprintf(active_path, sizeof(active_path),
		"./users/%s/active", username);
	FILE *af = fopen(active_path, "w");
	if (af)
		fclose(af);

	/* Remove rcode file */
	remove(rcode_path);

	/* Create session */
	generate_token(token, sizeof(token));
	qmap_put(sessions_map, token, username);

	snprintf(cookie, sizeof(cookie),
		"QSESSION=%s; Path=/; SameSite=Lax", token);

	ndc_header(fd, "Location", "/");
	ndc_header(fd, "Set-Cookie", cookie);
	ndc_head(fd, 303);
	ndc_close(fd);

	return 0;
}

void ndx_install(void)
{
	ndx_load("./mods/index/index");
	ndx_load("./mods/common/common");

	users_map = qmap_open(NULL, "users", QM_STR,
			qmap_reg(sizeof(struct user)), 0xFFFF, 0);
	sessions_map = qmap_open(NULL, "sess", QM_STR,
			QM_STR, 0xFF, 0);

	/* Ensure directories exist */
	mkdir("./etc", 0755);
	mkdir("./users", 0755);

	/* Preload users from shadow file */
	load_shadow();

	ndc_register_handler("POST:/auth/login", handle_login);
	ndc_register_handler("POST:/auth/register", handle_register);
	ndc_register_handler("GET:/auth/api/session", handle_session);
	ndc_register_handler("/auth/logout", handle_logout);
	ndc_register_handler("/auth/confirm", handle_confirm);

	auth_hd = qmap_open(NULL, "hd", QM_STR, QM_STR, 0xFF, 0);

	call_index_open("Auth", 0, 0);
}

void ndx_open(void) {}
