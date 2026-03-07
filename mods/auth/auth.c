#include <ttypt/ndx-mod.h>

#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <crypt.h>

#include <ttypt/ndc.h>
#include <ttypt/qmap.h>

#include "../common/common.h"
#include "../index/index.h"

static unsigned users_map;
static unsigned sessions_map;

struct user {
	char active;
	char hash[64];
	char email[128];
	char confirm_code[64];
};

static unsigned auth_hd = 0;

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

static int
handle_session(int fd, char *body)
{
	(void)body;

	char cookie[256] = {0};
	char token[64] = {0};

	ndc_env_get(fd, cookie, "HTTP_COOKIE");
	call_query_param(cookie, "QSESSION",
			token, sizeof(token));

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
		salt[30], token[64], cookie[128], *hash;

	struct user *user;

	fprintf(stderr, "AUTH DEBUG handle_login: "
			"body=%p body_str='%s' len=%lu\n", 
		(void*) body,
		body ? body : "(null)",
		body ? strlen(body) : 0);
	
	call_query_param(body, "username",
			username, sizeof(username));

	fprintf(stderr, "AUTH DEBUG: After query_param, "
			"username='%s'\n", username);

	call_query_param(body, "password",
			password, sizeof(password));

	call_query_param(body, "ret",
			redirect, sizeof(redirect));

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


	strncpy(salt, user->hash, 29);
	hash = crypt(password, salt);

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
			"QSESSION=%s", token);

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

	char cookie[256], query[256],
		token[64], redirect[256];

	ndc_env_get(fd, cookie, "HTTP_COOKIE");
	ndc_env_get(fd, query, "QUERY_STRING");
	
	call_query_param(cookie, "QSESSION",
			token, sizeof(token));

	call_query_param(query, "ret",
			redirect, sizeof(redirect));

	if (*token)
		qmap_del(sessions_map, token);

	ndc_header(fd, "Location",
			*redirect ? redirect : "/");

	ndc_header(fd, "Set-Cookie", "");
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
	char username[64], password[64], *hash,
		password_confirm[64], email[128];

	size_t username_len;
	struct user user;

	fprintf(stderr, "AUTH DEBUG handle_register: "
			"body=%p body_str='%s' len=%lu\n", 
			(void*)body, body ? body : "(null)",
			body ? strlen(body) : 0);

	call_query_param(body, "username",
			username, sizeof(username));

	call_query_param(body, "password",
			password, sizeof(password));

	call_query_param(body, "password2",
			password_confirm,
			sizeof(password_confirm));

	call_query_param(body, "email",
			email, sizeof(email));

	username_len = strlen(username);

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

	hash = crypt(password,
			"$2b$12$abcdefghijklmnopqrstuu");

	if (!hash) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 400);
		ndc_body(fd, "Password hashing failed");
		return 1;
	}

	strncpy(user.hash, hash, sizeof(user.hash) - 1);
	strncpy(user.email, email, sizeof(user.email) - 1);
	generate_token(user.confirm_code,
			sizeof(user.confirm_code));

	qmap_put(users_map, username, &user);

	fprintf(stderr, "Register: /confirm?u=%s&r=%s\n",
		username, user.confirm_code);

	ndc_header(fd, "Location", "/welcome");
	ndc_head(fd, 303);
	ndc_close(fd);
	return 0;
}

static int handle_confirm(int fd, char *body)
{
	(void)body;

	char query[256], username[64], code[64],
		token[64], cookie[128];

	struct user user, *existing;

	ndc_env_get(fd, query, "QUERY_STRING");
	
	call_query_param(query, "u",
			username, sizeof(username));

	call_query_param(query, "r",
			code, sizeof(code));

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

	if (strcmp(code, existing->confirm_code)) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 400);
		ndc_body(fd, "Wrong confirmation code");
		return 1;
	}

	memcpy(&user, existing, sizeof(user));
	user.active = 1;
	qmap_put(users_map, username, &user);

	generate_token(token, sizeof(token));
	qmap_put(sessions_map, token, username);

	snprintf(cookie, sizeof(cookie),
			"QSESSION=%s", token);

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
			      qmap_reg(sizeof(struct user)), 0xFF, 0);
	sessions_map = qmap_open(NULL, "sess", QM_STR,
				 QM_STR, 0xFF, 0);

	ndc_register_handler("POST:/login", handle_login);
	ndc_register_handler("POST:/register", handle_register);
	ndc_register_handler("/api/session", handle_session);
	ndc_register_handler("/logout", handle_logout);
	ndc_register_handler("/confirm", handle_confirm);

	auth_hd = qmap_open(NULL, "hd",
			QM_STR, QM_STR, 0xFF, 0);

	call_index_open("Auth", 0, 0);
}

void ndx_open(void) {}
