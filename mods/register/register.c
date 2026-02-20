#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>

#include <ttypt/ndc.h>
#include <ttypt/ndx.h>
#include <ttypt/qmap.h>

#include "papi.h"

static uint32_t sessions_hd;

static void
rand_str(char *buf, size_t len)
{
	FILE *fp = fopen("/dev/urandom", "r");
	if (!fp) {
		snprintf(buf, len, "%lld%lld",
			(long long)time(NULL), (long long)rand());
		return;
	}
	size_t r = fread(buf, 1, len - 1, fp);
	buf[r] = '\0';
	fclose(fp);
}

static void
get_query_param(char *qs, const char *key, char *out, size_t outlen)
{
	out[0] = '\0';
	if (!qs || !*qs || !key)
		return;
	size_t keylen = strlen(key);
	char *p = qs;
	while (*p) {
		while (*p == '&') p++;
		if (!strncmp(p, key, keylen) && p[keylen] == '=') {
			char *amp = strchr(p, '&');
			size_t len;
			if (amp)
				len = amp - p - keylen - 1;
			else
				len = strlen(p) - keylen - 1;
			if (len >= outlen) len = outlen - 1;
			strncpy(out, p + keylen + 1, len);
			out[len] = '\0';
			return;
		}
		while (*p && *p != '&') p++;
	}
}

static void
url_decode(const char *src, char *dst, size_t dstlen)
{
	size_t j = 0;
	while (*src && j < dstlen - 1) {
		if (*src == '%' && src[1] && src[2]) {
			int c;
			sscanf(src + 1, "%2x", &c);
			dst[j++] = c;
			src += 3;
		} else if (*src == '+') {
			dst[j++] = ' ';
			src++;
		} else {
			dst[j++] = *src++;
		}
	}
	dst[j] = '\0';
}

static int
valid_id(const char *username)
{
	if (!username || !*username)
		return 0;
	size_t len = strlen(username);
	if (len < 2 || len > 32)
		return 0;
	for (size_t i = 0; i < len; i++) {
		char c = username[i];
		if (!((c >= 'a' && c <= 'z') ||
		      (c >= 'A' && c <= 'Z') ||
		      (c >= '0' && c <= '9') ||
		      c == '_' || c == '-'))
		return 0;
	}
	return 1;
}

static int
valid_password(const char *password)
{
	if (!password || strlen(password) < 4)
		return 0;
	return 1;
}

static void
session_set(const char *token, const char *username)
{
	if (!sessions_hd) {
		sessions_hd = qmap_open(NULL, NULL, QM_STR, QM_STR, 0xFF, 0);
	}
	qmap_put(sessions_hd, token, username);
}

static void
send_error(int fd, const char *err)
{
	ndc_writef(fd,
		"HTTP/1.1 400 Bad Request\r\n"
		"Content-Type: text/plain; charset=UTF-8\r\n\r\n"
		"%s\n",
		err ? err : "Bad request");
	ndc_close(fd);
}

static void
send_redirect(int fd, const char *location)
{
	ndc_writef(fd, "HTTP/1.1 303 See Other\r\n");
	ndc_writef(fd, "Location: %s\r\n\r\n", location);
	ndc_close(fd);
}

static void
register_user(int fd, char *body, char *doc_root)
{
	char username[64] = { 0 };
	char password[64] = { 0 };
	char password2[64] = { 0 };
	char email[128] = { 0 };

	get_query_param(body, "username", username, sizeof(username));
	get_query_param(body, "password", password, sizeof(password));
	get_query_param(body, "password2", password2, sizeof(password2));
	get_query_param(body, "email", email, sizeof(email));

	url_decode(username, username, sizeof(username));
	url_decode(password, password, sizeof(password));
	url_decode(password2, password2, sizeof(password2));
	url_decode(email, email, sizeof(email));

	if (!valid_id(username)) {
		send_error(fd, "Invalid username (2-32 chars, alphanumeric + _ -)");
		return;
	}

	if (!valid_password(password)) {
		send_error(fd, "Password too short (min 4 chars)");
		return;
	}

	if (strcmp(password, password2) != 0) {
		send_error(fd, "Passwords don't match");
		return;
	}

	char htpasswd_path[512];
	snprintf(htpasswd_path, sizeof(htpasswd_path),
		"%s/etc/shadow", doc_root[0] ? doc_root : ".");

	FILE *fp = fopen(htpasswd_path, "r");
	if (fp) {
		char line[256];
		while (fgets(line, sizeof(line), fp)) {
			char *colon = strchr(line, ':');
			if (colon && !strncmp(line, username, colon - line)) {
				fclose(fp);
				send_error(fd, "User already exists");
				return;
			}
		}
		fclose(fp);
	}

	char user_dir[512];
	snprintf(user_dir, sizeof(user_dir),
		"%s/users/%s", doc_root[0] ? doc_root : ".", username);

	if (mkdir(user_dir, 0755) != 0) {
		send_error(fd, "Failed to create user directory");
		return;
	}

	char email_path[512];
	snprintf(email_path, sizeof(email_path), "%s/email", user_dir);
	fp = fopen(email_path, "w");
	if (fp) {
		fprintf(fp, "%s", email);
		fclose(fp);
	}

	char add_cmd[512];
	snprintf(add_cmd, sizeof(add_cmd),
		"%s/src/htpasswd/htpasswd %s %s",
		doc_root[0] ? doc_root : ".", username, password);
	fp = popen(add_cmd, "r");
	char hash[128] = { 0 };
	if (fp) {
		if (fgets(hash, sizeof(hash), fp)) {
			hash[strcspn(hash, "\n")] = 0;
		}
		pclose(fp);
	}
	if (*hash) {
		fp = fopen(htpasswd_path, "a");
		if (fp) {
			fprintf(fp, "%s\n", hash);
			fclose(fp);
		}
	}

	char rcode[64];
	rand_str(rcode, sizeof(rcode));

	char rcode_path[512];
	snprintf(rcode_path, sizeof(rcode_path), "%s/rcode", user_dir);
	fp = fopen(rcode_path, "w");
	if (fp) {
		fprintf(fp, "%s", rcode);
		fclose(fp);
	}

	char confirm_url[512];
	snprintf(confirm_url, sizeof(confirm_url),
		"/confirm?username=%s&rcode=%s", username, rcode);

	fp = popen("which femail", "r");
	if (fp) {
		char out[32] = { 0 };
		if (fgets(out, sizeof(out), fp)) {
			pclose(fp);

			char mail_cmd[1024];
			snprintf(mail_cmd, sizeof(mail_cmd),
				"femail -f noreply@tty.pt %s <<EOF\n"
				"Subject: Registration on tty.pt\n\n"
				"Welcome to tty.pt!\n"
				"To confirm your email, visit:\n"
				"https://tty.pt%s\n"
				"You will then be able to use your account.\n"
				"Thank you!\n"
				"EOF",
				email, confirm_url);
			popen(mail_cmd, "r");
		} else {
			pclose(fp);
			fprintf(stderr, "Registration: femail not found, confirmation at %s\n", confirm_url);
		}
	} else {
		fprintf(stderr, "Registration: femail not found, confirmation at %s\n", confirm_url);
	}

	send_redirect(fd, "/welcome");
}

static void
register_handler(int fd, char *body)
{
	char method[16] = { 0 };
	char doc_root[256] = { 0 };

	ndc_env_get(fd, method, "REQUEST_METHOD");
	ndc_env_get(fd, doc_root, "DOCUMENT_ROOT");

	if (!strcmp(method, "POST")) {
		register_user(fd, body, doc_root);
		return;
	}

	send_error(fd, "Method not allowed");
}

MODULE_API void
ndx_install(void)
{
	sessions_hd = qmap_open(NULL, NULL, QM_STR, QM_STR, 0xFF, 0);
	ndc_register_handler("POST:/register", register_handler);
}

MODULE_API void
ndx_open(void)
{
}

MODULE_API ndx_t *
get_ndx_ptr(void)
{
	static ndx_t ndx;
	return &ndx;
}
