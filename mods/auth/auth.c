#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <crypt.h>

#include <ttypt/ndc.h>
#include <ttypt/ndx.h>
#include <ttypt/qmap.h>

#include "papi.h"
#include "auth.h"

#define SESSIONS_DIR "./sessions"

ndx_t ndx;
static uint32_t sessions_hd;

static void
rand_token(char *buf, size_t len)
{
	static const char hex[] = "0123456789abcdef";
	FILE *fp = fopen("/dev/urandom", "r");
	if (!fp) {
		snprintf(buf, len, "%08llx%08llx",
			(unsigned long long)time(NULL),
			(unsigned long long)rand());
		return;
	}
	size_t i = 0;
	while (i + 1 < len) {
		int c = fgetc(fp);
		if (c == EOF) break;
		buf[i++] = hex[(c >> 4) & 0xf];
		buf[i++] = hex[c & 0xf];
	}
	buf[i] = '\0';
	fclose(fp);
}

static void
get_param(char *qs, const char *key, char *out, size_t outlen)
{
	if (!outlen) return;
	out[0] = '\0';
	if (!qs || !key) return;
	size_t klen = strlen(key);
	for (char *p = qs; *p; ) {
		while (*p == '&') p++;
		if (!strncmp(p, key, klen) && p[klen] == '=') {
			char *val = p + klen + 1;
			size_t len = 0;
			while (val[len] && val[len] != '&') len++;
			if (len >= outlen) len = outlen - 1;
			memcpy(out, val, len);
			out[len] = '\0';
			for (size_t i = 0; i < len; i++) {
				if (out[i] == '+') out[i] = ' ';
				else if (out[i] == '%' && i + 2 < len) {
					int c;
					sscanf(out + i + 1, "%2x", &c);
					out[i] = c;
					memmove(out + i + 1, out + i + 3, len - i - 2);
					len -= 2;
				}
			}
			return;
		}
		while (*p && *p != '&') p++;
	}
}

static void
get_cookie(char *cookie, const char *name, char *out, size_t outlen)
{
	if (!outlen) return;
	out[0] = '\0';
	if (!cookie) return;
	size_t nlen = strlen(name);
	for (char *p = cookie; *p; ) {
		while (*p == ' ') p++;
		if (!strncmp(p, name, nlen) && p[nlen] == '=') {
			char *val = p + nlen + 1;
			size_t len = 0;
			while (val[len] && val[len] != '&' && val[len] != ';' && val[len] != ' ') len++;
			if (len >= outlen) len = outlen - 1;
			memcpy(out, val, len);
			out[len] = '\0';
			return;
		}
		while (*p && *p != ';') p++;
		if (*p) p++;
	}
}

static void
respond(int fd, int status, const char *headers, const char *body)
{
	const char *msg = "Error";
	if (status == 200) msg = "OK";
	else if (status == 303) msg = "See Other";
	else if (status == 400) msg = "Bad Request";
	else if (status == 401) msg = "Unauthorized";
	ndc_writef(fd, "HTTP/1.1 %d %s\r\n%s\r\n%s", status, msg,
		headers ? headers : "", body ? body : "");
	ndc_close(fd);
}

static int
valid_id(const char *s)
{
	if (!s || strlen(s) < 2 || strlen(s) > 32) return 0;
	for (; *s; s++)
		if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') ||
		      (*s >= '0' && *s <= '9') || *s == '_' || *s == '-'))
			return 0;
	return 1;
}

static int
check_password(const char *pass, const char *hash)
{
	char *result = crypt(pass, hash);
	return result && strncmp(result, hash, 60) == 0;
}

const char *
auth_session_get(const char *token)
{
	if (!token || !*token) return NULL;
	static char user[64];
	char path[256];
	snprintf(path, sizeof(path), "%s/%s", SESSIONS_DIR, token);
	FILE *fp = fopen(path, "r");
	if (!fp) return NULL;
	if (fgets(user, sizeof(user), fp)) {
		user[strcspn(user, "\n")] = '\0';
		fclose(fp);
		return user;
	}
	fclose(fp);
	return NULL;
}

int
auth_session_set(const char *token, const char *user)
{
	if (!token || !user) return -1;
	if (!sessions_hd) sessions_hd = qmap_open(NULL, NULL, QM_STR, QM_STR, 0xFF, 0);
	char path[256];
	snprintf(path, sizeof(path), "%s/%s", SESSIONS_DIR, token);
	FILE *fp = fopen(path, "w");
	if (!fp) return -1;
	fprintf(fp, "%s", user);
	fclose(fp);
	return qmap_put(sessions_hd, token, user);
}

void
auth_session_delete(const char *token)
{
	if (!token) return;
	if (sessions_hd) qmap_del(sessions_hd, token);
	char path[256];
	snprintf(path, sizeof(path), "%s/%s", SESSIONS_DIR, token);
	unlink(path);
}

static void
h_session(int fd, char *body)
{
	(void)body;
	char cookie[256] = {0}, token[128] = {0};
	ndc_env_get(fd, cookie, "HTTP_COOKIE");
	get_cookie(cookie, "QSESSION", token, sizeof(token));
	const char *user = *token ? auth_session_get(token) : NULL;
	respond(fd, 200, "Content-Type: text/plain\r\n", user ? user : "");
}

static void
h_login(int fd, char *body)
{
	char method[8] = {0}, doc_root[256] = {0};
	ndc_env_get(fd, method, "REQUEST_METHOD");
	ndc_env_get(fd, doc_root, "DOCUMENT_ROOT");
	if (strcmp(method, "POST") != 0) {
		respond(fd, 400, "Content-Type: text/plain\r\n", "Method not allowed");
		return;
	}

	const char *root = *doc_root ? doc_root : ".";

	char user[64] = {0}, pass[64] = {0}, ret[256] = {0};
	get_param(body, "username", user, sizeof(user));
	get_param(body, "password", pass, sizeof(pass));
	get_param(body, "ret", ret, sizeof(ret));

	if (!*user || !*pass) {
		respond(fd, 400, "Content-Type: text/plain\r\n", "Missing username or password");
		return;
	}

	char shadow_path[512];
	snprintf(shadow_path, sizeof(shadow_path), "%s/etc/shadow", root);
	FILE *fp = fopen(shadow_path, "r");
	if (!fp) {
		respond(fd, 400, "Content-Type: text/plain\r\n", "No such user");
		return;
	}

	char line[256], *hash = NULL;
	size_t ulen = strlen(user);
	while (fgets(line, sizeof(line), fp)) {
		if (!strncmp(line, user, ulen) && line[ulen] == ':') {
			static char h[128];
			strncpy(h, line + ulen + 1, sizeof(h) - 1);
			h[strcspn(h, "\n")] = '\0';
			hash = h;
			break;
		}
	}
	fclose(fp);

	if (!hash) {
		respond(fd, 400, "Content-Type: text/plain\r\n", "No such user");
		return;
	}

	if (!check_password(pass, hash)) {
		respond(fd, 401, "Content-Type: text/plain\r\n", "Invalid password");
		return;
	}

	char udir[512];
	snprintf(udir, sizeof(udir), "%s/users/%s", root, user);
	struct stat st;
	if (stat(udir, &st) != 0 || !S_ISDIR(st.st_mode)) {
		respond(fd, 400, "Content-Type: text/plain\r\n", "Account not activated");
		return;
	}

	char token[64];
	rand_token(token, sizeof(token));
	auth_session_set(token, user);

	char hdr[256];
	snprintf(hdr, sizeof(hdr), "Set-Cookie: QSESSION=%s; SameSite=Lax\r\nLocation: %s\r\n",
		token, *ret ? ret : "/");
	respond(fd, 303, hdr, NULL);
}

static void
h_logout(int fd, char *body)
{
	(void)body;
	char cookie[256] = {0}, qs[256] = {0}, token[128] = {0}, ret[256] = {0};
	ndc_env_get(fd, cookie, "HTTP_COOKIE");
	ndc_env_get(fd, qs, "QUERY_STRING");
	get_cookie(cookie, "QSESSION", token, sizeof(token));
	get_param(qs, "ret", ret, sizeof(ret));

	if (*token) auth_session_delete(token);

	char hdr[256];
	snprintf(hdr, sizeof(hdr), "Set-Cookie: QSESSION=; Path=/; Expires=Thu, 01 Jan 1970 00:00:00 GMT\r\nLocation: %s\r\n",
		*ret ? ret : "/");
	respond(fd, 303, hdr, NULL);
}

static void
h_register(int fd, char *body)
{
	char method[8] = {0}, doc_root[256] = {0};
	ndc_env_get(fd, method, "REQUEST_METHOD");
	ndc_env_get(fd, doc_root, "DOCUMENT_ROOT");
	if (strcmp(method, "POST") != 0) {
		respond(fd, 400, "Content-Type: text/plain\r\n", "Method not allowed");
		return;
	}

	const char *root = *doc_root ? doc_root : ".";

	char user[64] = {0}, pass[64] = {0}, pass2[64] = {0}, email[128] = {0};
	get_param(body, "username", user, sizeof(user));
	get_param(body, "password", pass, sizeof(pass));
	get_param(body, "password2", pass2, sizeof(pass2));
	get_param(body, "email", email, sizeof(email));

	if (!valid_id(user)) {
		respond(fd, 400, "Content-Type: text/plain\r\n", "Invalid username");
		return;
	}
	if (strlen(pass) < 4) {
		respond(fd, 400, "Content-Type: text/plain\r\n", "Password too short");
		return;
	}
	if (strcmp(pass, pass2) != 0) {
		respond(fd, 400, "Content-Type: text/plain\r\n", "Passwords don't match");
		return;
	}

	char shadow_path[512], udir[512];
	snprintf(shadow_path, sizeof(shadow_path), "%s/etc/shadow", root);
	snprintf(udir, sizeof(udir), "%s/users/%s", root, user);

	FILE *fp = fopen(shadow_path, "r");
	if (fp) {
		char line[256];
		size_t ulen = strlen(user);
		while (fgets(line, sizeof(line), fp)) {
			if (!strncmp(line, user, ulen) && line[ulen] == ':') {
				fclose(fp);
				respond(fd, 400, "Content-Type: text/plain\r\n", "User exists");
				return;
			}
		}
		fclose(fp);
	}

	if (mkdir(udir, 0755) != 0) {
		respond(fd, 400, "Content-Type: text/plain\r\n", "Failed to create user");
		return;
	}

	char path[512];
	if (*email) {
		snprintf(path, sizeof(path), "%s/email", udir);
		fp = fopen(path, "w");
		if (fp) { fprintf(fp, "%s", email); fclose(fp); }
	}

	char cmd[512], hash[128] = {0};
	snprintf(cmd, sizeof(cmd), "%s/src/htpasswd/htpasswd %s %s", root, user, pass);
	fp = popen(cmd, "r");
	if (fp && fgets(hash, sizeof(hash), fp)) {
		hash[strcspn(hash, "\n")] = '\0';
		pclose(fp);
		fp = fopen(shadow_path, "a");
		if (fp) { fprintf(fp, "%s\n", hash); fclose(fp); }
	} else if (fp) {
		pclose(fp);
	}

	char rcode[32];
	rand_token(rcode, sizeof(rcode));
	snprintf(path, sizeof(path), "%s/rcode", udir);
	fp = fopen(path, "w");
	if (fp) { fprintf(fp, "%s", rcode); fclose(fp); }

	fprintf(stderr, "Register: %s -> /confirm?username=%s&rcode=%s\n",
		*email ? email : "no email", user, rcode);
	respond(fd, 303, "Location: /welcome\r\n", NULL);
}

static void
h_confirm(int fd, char *body)
{
	(void)body;
	char qs[256] = {0}, doc_root[256] = {0}, user[64] = {0}, rcode[64] = {0};
	ndc_env_get(fd, qs, "QUERY_STRING");
	ndc_env_get(fd, doc_root, "DOCUMENT_ROOT");
	get_param(qs, "username", user, sizeof(user));
	get_param(qs, "rcode", rcode, sizeof(rcode));

	if (!*user || !*rcode) {
		respond(fd, 400, "Content-Type: text/plain\r\n", "Missing parameters");
		return;
	}

	const char *root = *doc_root ? doc_root : ".";

	char path[512];
	snprintf(path, sizeof(path), "%s/users/%s/rcode", root, user);
	FILE *fp = fopen(path, "r");
	if (!fp) {
		respond(fd, 400, "Content-Type: text/plain\r\n", "Invalid or expired code");
		return;
	}

	char stored[64] = {0};
	fgets(stored, sizeof(stored), fp);
	fclose(fp);
	stored[strcspn(stored, "\n")] = '\0';

	if (strcmp(rcode, stored) != 0) {
		respond(fd, 400, "Content-Type: text/plain\r\n", "Invalid code");
		return;
	}

	unlink(path);

	char token[64];
	rand_token(token, sizeof(token));
	auth_session_set(token, user);

	char hdr[256];
	snprintf(hdr, sizeof(hdr), "Set-Cookie: QSESSION=%s; SameSite=Lax\r\nLocation: /\r\n", token);
	respond(fd, 303, hdr, NULL);
}

MODULE_API void
ndx_install(void)
{
	if (!sessions_hd) sessions_hd = qmap_open(NULL, NULL, QM_STR, QM_STR, 0xFF, 0);
	ndc_register_handler("/api/session", h_session);
	ndc_register_handler("POST:/login", h_login);
	ndc_register_handler("/logout", h_logout);
	ndc_register_handler("POST:/register", h_register);
	ndc_register_handler("/confirm", h_confirm);
}

MODULE_API void
ndx_open(void)
{
	if (!sessions_hd) sessions_hd = qmap_open(NULL, NULL, QM_STR, QM_STR, 0xFF, 0);
	ndc_register_handler("/api/session", h_session);
	ndc_register_handler("/logout", h_logout);
	ndc_register_handler("/confirm", h_confirm);
}

MODULE_API ndx_t *
get_ndx_ptr(void)
{
	return &ndx;
}
