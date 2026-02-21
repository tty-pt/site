#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <crypt.h>

#include <ttypt/ndc.h>
#include <ttypt/ndx.h>
#include <ttypt/qmap.h>

#include "papi.h"

ndx_t ndx;
static uint32_t users_map;
static uint32_t sessions_map;

struct user {
	char active;
	char hash[64];
	char email[128];
	char confirm_code[64];
};

static void
generate_token(char *buf, size_t len)
{
	FILE *f = fopen("/dev/urandom", "r");
	if (!f) {
		snprintf(buf, len, "%llx", (unsigned long long)time(NULL));
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

static void
query_param(char *query, const char *key, char *out, size_t out_len)
{
	if (!out || !out_len)
		return;
	out[0] = 0;
	if (!query)
		return;

	size_t key_len = strlen(key);
	for (char *p = query; *p; ) {
		while (*p == '&')
			p++;
		if (!strncmp(p, key, key_len) && p[key_len] == '=') {
			char *val = p + key_len + 1;
			size_t n = 0;
			while (val[n] && val[n] != '&')
				n++;
			if (n >= out_len)
				n = out_len - 1;
			memcpy(out, val, n);
			out[n] = 0;

			size_t j = 0;
			for (size_t i = 0; out[i]; i++) {
				if (out[i] == '+') {
					out[j++] = ' ';
				} else if (out[i] == '%' && out[i+1] && out[i+2]) {
					int c;
					sscanf(out + i + 1, "%2x", &c);
					out[j++] = c;
					i += 2;
				} else {
					out[j++] = out[i];
				}
			}
			out[j] = 0;
			return;
		}
		while (*p && *p != '&')
			p++;
	}
}

static void
handle_session(int fd, char *body)
{
	(void)body;

	char cookie[256] = {0};
	char token[64] = {0};

	ndc_env_get(fd, cookie, "HTTP_COOKIE");
	query_param(cookie, "QSESSION", token, sizeof(token));

	const char *username = qmap_get(sessions_map, token);
	ndc_head(fd, 200);
	ndc_header(fd, "Content-Type", "text/plain");
	ndc_body(fd, username ? username : "");
}

static void
handle_login(int fd, char *body)
{
	char username[64] = {0};
	char password[64] = {0};
	char redirect[256] = {0};

	query_param(body, "username", username, sizeof(username));
	query_param(body, "password", password, sizeof(password));
	query_param(body, "ret", redirect, sizeof(redirect));

	if (!*username || !*password) {
		ndc_head(fd, 400);
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_body(fd, "Missing username or password");
		return;
	}

	struct user *user = (struct user *)qmap_get(users_map, username);
	if (!user) {
		ndc_head(fd, 400);
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_body(fd, "User not found");
		return;
	}

	char salt[30] = {0};
	strncpy(salt, user->hash, 29);
	char *hash = crypt(password, salt);

	if (!hash || strcmp(hash, user->hash)) {
		ndc_head(fd, 401);
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_body(fd, "Invalid password");
		return;
	}

	if (!user->active) {
		ndc_head(fd, 400);
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_body(fd, "Account not confirmed");
		return;
	}

	char token[64];
	generate_token(token, sizeof(token));
	qmap_put(sessions_map, token, username);
	char cookie[128];
	snprintf(cookie, sizeof(cookie), "QSESSION=%s", token);
	ndc_head(fd, 303);
	ndc_header(fd, "Location", *redirect ? redirect : "/");
	ndc_header(fd, "Set-Cookie", cookie);
	ndc_close(fd);
}

static void
handle_logout(int fd, char *body)
{
	(void)body;

	char cookie[256] = {0};
	char query[256] = {0};
	char token[64] = {0};
	char redirect[256] = {0};

	ndc_env_get(fd, cookie, "HTTP_COOKIE");
	ndc_env_get(fd, query, "QUERY_STRING");
	query_param(cookie, "QSESSION", token, sizeof(token));
	query_param(query, "ret", redirect, sizeof(redirect));

	if (*token)
		qmap_del(sessions_map, token);

	ndc_head(fd, 303);
	ndc_header(fd, "Location", *redirect ? redirect : "/");
	ndc_header(fd, "Set-Cookie", "");
	ndc_close(fd);
}

static int
valid_username_char(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
	       (c >= '0' && c <= '9') || c == '_' || c == '-';
}

static void
handle_register(int fd, char *body)
{
	char username[64] = {0};
	char password[64] = {0};
	char password_confirm[64] = {0};
	char email[128] = {0};

	query_param(body, "username", username, sizeof(username));
	query_param(body, "password", password, sizeof(password));
	query_param(body, "password2", password_confirm, sizeof(password_confirm));
	query_param(body, "email", email, sizeof(email));

	size_t username_len = strlen(username);
	if (username_len < 2 || username_len > 32) {
		ndc_head(fd, 400);
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_body(fd, "Username must be 2-32 characters");
		return;
	}

	for (char *p = username; *p; p++) {
		if (!valid_username_char(*p)) {
			ndc_head(fd, 400);
			ndc_header(fd, "Content-Type", "text/plain");
			ndc_body(fd, "Invalid username character");
			return;
		}
	}

	if (strlen(password) < 4) {
		ndc_head(fd, 400);
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_body(fd, "Password too short");
		return;
	}

	if (strcmp(password, password_confirm)) {
		ndc_head(fd, 400);
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_body(fd, "Passwords do not match");
		return;
	}

	if (qmap_get(users_map, username)) {
		ndc_head(fd, 400);
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_body(fd, "Username already exists");
		return;
	}

	struct user user = {0};
	char *hash = crypt(password, "$2b$12$abcdefghijklmnopqrstuu");
	if (!hash) {
		ndc_head(fd, 400);
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_body(fd, "Password hashing failed");
		return;
	}

	strncpy(user.hash, hash, sizeof(user.hash) - 1);
	strncpy(user.email, email, sizeof(user.email) - 1);
	generate_token(user.confirm_code, sizeof(user.confirm_code));
	qmap_put(users_map, username, &user);

	fprintf(stderr, "Register: /confirm?u=%s&r=%s\n",
		username, user.confirm_code);
	ndc_head(fd, 303);
	ndc_header(fd, "Location", "/welcome");
	ndc_close(fd);
}

static void
handle_confirm(int fd, char *body)
{
	(void)body;

	char query[256] = {0};
	char username[64] = {0};
	char code[64] = {0};

	ndc_env_get(fd, query, "QUERY_STRING");
	query_param(query, "u", username, sizeof(username));
	query_param(query, "r", code, sizeof(code));

	if (!*username || !*code) {
		ndc_head(fd, 400);
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_body(fd, "Missing parameters");
		return;
	}

	struct user *existing = (struct user *)qmap_get(users_map, username);
	if (!existing || existing->active) {
		ndc_head(fd, 400);
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_body(fd, "Invalid confirmation");
		return;
	}

	if (strcmp(code, existing->confirm_code)) {
		ndc_head(fd, 400);
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_body(fd, "Wrong confirmation code");
		return;
	}

	struct user user;
	memcpy(&user, existing, sizeof(user));
	user.active = 1;
	qmap_put(users_map, username, &user);

	char token[64];
	generate_token(token, sizeof(token));
	qmap_put(sessions_map, token, username);
	char cookie[128];
	snprintf(cookie, sizeof(cookie), "QSESSION=%s", token);
	ndc_head(fd, 303);
	ndc_header(fd, "Location", "/");
	ndc_header(fd, "Set-Cookie", cookie);
	ndc_close(fd);
}

MODULE_API void
ndx_install(void)
{
	users_map = qmap_open("auth.qmap", "users", QM_STR,
			      qmap_reg(sizeof(struct user)), 0xFF, 0);
	sessions_map = qmap_open("auth.qmap", "sess", QM_STR,
				 QM_STR, 0xFF, 0);
	ndc_register_handler("/api/session", handle_session);
	ndc_register_handler("POST:/login", handle_login);
	ndc_register_handler("/logout", handle_logout);
	ndc_register_handler("POST:/register", handle_register);
	ndc_register_handler("/confirm", handle_confirm);
}

MODULE_API void
ndx_open(void)
{
	ndc_register_handler("/api/session", handle_session);
	ndc_register_handler("/logout", handle_logout);
	ndc_register_handler("/confirm", handle_confirm);
}

MODULE_API ndx_t *
get_ndx_ptr(void)
{
	return &ndx;
}
