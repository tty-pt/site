#include <ttypt/ndx-mod.h>

#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <crypt.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>

#include <ttypt/ndc.h>
#include <ttypt/qmap.h>

#include "../common/common.h"
#include "../index/index.h"

static unsigned users_map;
static unsigned sessions_map;

struct user {
	char active;
	int  uid;
	char hash[64];
};

static unsigned auth_hd = 0;

/* ------------------------------------------------------------------ */
/* Path helpers                                                         */
/* ------------------------------------------------------------------ */

static void
shadow_path(char *buf, size_t len)
{
#ifdef __OpenBSD__
	snprintf(buf, len, "./etc/master.passwd");
#else
	snprintf(buf, len, "./etc/shadow");
#endif
}

static void
passwd_path(char *buf, size_t len)
{
	snprintf(buf, len, "./etc/passwd");
}

/* ------------------------------------------------------------------ */
/* UID allocation                                                       */
/* ------------------------------------------------------------------ */

/* Scan ./etc/passwd for the highest UID; return max+1 (min 1000). */
static int
next_uid(void)
{
	char path[512];
	passwd_path(path, sizeof(path));
	FILE *f = fopen(path, "r");
	int max_uid = 999;
	if (!f)
		return 1000;
	char line[512];
	while (fgets(line, sizeof(line), f)) {
		/* field 3 is uid: login:password:uid:... */
		char *p = line;
		int field = 0;
		while (*p && field < 2) {
			if (*p == ':')
				field++;
			p++;
		}
		if (field == 2) {
			int uid = (int)strtol(p, NULL, 10);
			if (uid > max_uid)
				max_uid = uid;
		}
	}
	fclose(f);
	return max_uid + 1;
}

/* ------------------------------------------------------------------ */
/* Session helpers                                                      */
/* ------------------------------------------------------------------ */

NDX_DEF(const char *, get_session_user,
		const char *, token)
{
	if (!token || !*token)
		return NULL;
	return qmap_get(sessions_map, token);
}

NDX_DEF(int, get_user_uid, const char *, username)
{
	struct user *u = (struct user *)qmap_get(users_map, username);
	return u ? u->uid : -1;
}

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
	buf[len - 1] = '\0';
	fclose(f);
}

/* ------------------------------------------------------------------ */
/* Password hashing                                                     */
/* ------------------------------------------------------------------ */

/*
 * Generate a random bcrypt salt: $2b$12$<22 base64 chars>
 * Bcrypt base64 alphabet: ./ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789
 * Proper encoding: 16 random bytes → 22 chars, 6 bits per char from
 * the concatenated bit stream (MSB first), matching OpenBSD's bcrypt.
 */
static void
generate_bcrypt_salt(char *buf, size_t len)
{
	static const char b64[] =
		"./ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
		"0123456789";
	unsigned char rnd[16];
	FILE *f = fopen("/dev/urandom", "r");
	if (f) {
		if (fread(rnd, 1, sizeof(rnd), f) != sizeof(rnd))
			memset(rnd, 0, sizeof(rnd));
		fclose(f);
	} else {
		unsigned long long t = (unsigned long long)time(NULL);
		for (int i = 0; i < 16; i++)
			rnd[i] = (unsigned char)(t >> (i % 8));
	}

	/*
	 * Pack 16 bytes (128 bits) into 22 base64 chars (132 bits),
	 * consuming 6 bits per character from the concatenated bit stream,
	 * MSB first — identical to the encoding OpenBSD bcrypt_gensalt uses.
	 */
	char salt[23];
	unsigned int buf_bits = 0;
	int bits_left = 0;
	int out = 0;
	for (int i = 0; i < 16 && out < 22; i++) {
		buf_bits = (buf_bits << 8) | rnd[i];
		bits_left += 8;
		while (bits_left >= 6 && out < 22) {
			bits_left -= 6;
			salt[out++] = b64[(buf_bits >> bits_left) & 0x3f];
		}
	}
	/* emit any remaining bits (padded with zeros) */
	if (out < 22 && bits_left > 0)
		salt[out++] = b64[(buf_bits << (6 - bits_left)) & 0x3f];
	salt[22] = '\0';

	snprintf(buf, len, "$2b$12$%s", salt);
}

/* ------------------------------------------------------------------ */
/* Shadow / passwd file I/O                                            */
/* ------------------------------------------------------------------ */

/*
 * Append a new user to the shadow file in full OS format.
 *
 * Linux /etc/shadow (9 fields):
 *   login:password:lastchg:min:max:warn:inactive:expire:reserved
 *
 * OpenBSD /etc/master.passwd (10 fields):
 *   login:password:uid:gid:class:change:expire:gecos:home_dir:shell
 */
static int
shadow_append(const char *username, const char *hash, int uid)
{
	char path[512];
	shadow_path(path, sizeof(path));
	FILE *f = fopen(path, "a");
	if (!f)
		return -1;
#ifdef __OpenBSD__
	fprintf(f, "%s:%s:%d:67::0:0:%s:/home/%s:/bin/sh\n",
		username, hash, uid, username, username);
#else
	long lastchg = (long)(time(NULL) / 86400);
	fprintf(f, "%s:%s:%ld:0:99999:7:::\n",
		username, hash, lastchg);
	(void)uid;
#endif
	fclose(f);
	return 0;
}

/*
 * Append a new user to ./etc/passwd in full OS format.
 *
 * Linux /etc/passwd (7 fields):
 *   login:password:uid:gid:gecos:home_dir:shell
 *   password field is 'x' (shadow lookup)
 *
 * OpenBSD /etc/passwd (10 fields, public — no hash):
 *   login:password:uid:gid:class:change:expire:gecos:home_dir:shell
 *   password field is '*' (hash lives in master.passwd)
 */
static int
passwd_append(const char *username, int uid)
{
	char path[512];
	passwd_path(path, sizeof(path));
	FILE *f = fopen(path, "a");
	if (!f)
		return -1;
#ifdef __OpenBSD__
	fprintf(f, "%s:*:%d:67::0:0:%s:/home/%s:/bin/sh\n",
		username, uid, username, username);
#else
	fprintf(f, "%s:x:%d:67::/home/%s:/bin/sh\n",
		username, uid, username);
#endif
	fclose(f);
	return 0;
}

/*
 * Append username to the www: line in ./etc/group.
 */
static int
group_append(const char *username)
{
	FILE *in  = fopen("./etc/group", "r");
	FILE *out = fopen("./etc/group.tmp", "w");
	if (!out) {
		if (in) fclose(in);
		return -1;
	}
	if (in) {
		char line[4096];
		while (fgets(line, sizeof(line), in)) {
			size_t ll = strlen(line);
			while (ll > 0 && (line[ll-1] == '\n' || line[ll-1] == '\r'))
				line[--ll] = '\0';
			if (strncmp(line, "www:", 4) == 0)
				fprintf(out, "%s,%s\n", line, username);
			else
				fprintf(out, "%s\n", line);
		}
		fclose(in);
	}
	fclose(out);
	rename("./etc/group.tmp", "./etc/group");
	return 0;
}

#ifdef __OpenBSD__
static void
run_pwd_mkdb(void)
{
	pid_t pid = fork();
	if (pid == 0) {
		char *argv[] = {
			"pwd_mkdb", "-d", "./etc",
			"./etc/master.passwd", NULL
		};
		execv("/usr/sbin/pwd_mkdb", argv);
		_exit(1);
	} else if (pid > 0) {
		waitpid(pid, NULL, 0);
	}
}
#endif

/*
 * Rewrite the shadow file, replacing only the hash field (field 2)
 * for the given username, preserving all other fields on the line.
 */
static int
shadow_update(const char *username, const char *new_hash)
{
	char path[512], tmp[560];
	shadow_path(path, sizeof(path));
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);

	FILE *in  = fopen(path, "r");
	FILE *out = fopen(tmp, "w");
	if (!out)
		return -1;

	if (in) {
		char line[512];
		while (fgets(line, sizeof(line), in)) {
			/* strip trailing newline for clean reassembly */
			size_t ll = strlen(line);
			while (ll > 0 && (line[ll-1] == '\n' || line[ll-1] == '\r'))
				line[--ll] = '\0';

			char *colon1 = strchr(line, ':');
			if (!colon1) {
				fprintf(out, "%s\n", line);
				continue;
			}

			size_t ulen = (size_t)(colon1 - line);
			if (ulen == strlen(username) &&
			    strncmp(line, username, ulen) == 0) {
				/* find end of hash field (field 2) */
				char *colon2 = strchr(colon1 + 1, ':');
				if (colon2) {
					/* preserve fields 3+ */
					fprintf(out, "%s:%s%s\n",
						username, new_hash, colon2);
				} else {
					/* old 2-field format — no trailing fields */
					fprintf(out, "%s:%s\n",
						username, new_hash);
				}
				continue;
			}
			fprintf(out, "%s\n", line);
		}
		fclose(in);
	}
	fclose(out);
	rename(tmp, path);
	return 0;
}

/* ------------------------------------------------------------------ */
/* Startup: load users from shadow + passwd files                      */
/* ------------------------------------------------------------------ */

/*
 * Load shadow file into users_map.
 * Handles both old 2-field format (login:hash) and full OS formats
 * (9-field Linux shadow, 10-field OpenBSD master.passwd) by extracting
 * only field 2 as the hash and field 3 as uid (OpenBSD only).
 * Active state: user is active if they have ./users/<name>/active file
 * OR if they have no ./users/<name>/rcode file (old-site compat).
 */
static void
load_shadow(void)
{
	char path[512];
	shadow_path(path, sizeof(path));
	FILE *f = fopen(path, "r");
	if (!f)
		return;

	char line[512];
	while (fgets(line, sizeof(line), f)) {
		size_t ll = strlen(line);
		while (ll > 0 && (line[ll-1] == '\n' || line[ll-1] == '\r'))
			line[--ll] = '\0';

		/* field 1: username */
		char *colon1 = strchr(line, ':');
		if (!colon1)
			continue;
		*colon1 = '\0';
		const char *uname = line;
		if (!*uname)
			continue;

		/* field 2: hash — stop at next colon */
		char *hash_start = colon1 + 1;
		char *colon2 = strchr(hash_start, ':');
		size_t hash_len;
		if (colon2) {
			hash_len = (size_t)(colon2 - hash_start);
		} else {
			hash_len = strlen(hash_start);
		}
		if (!hash_len)
			continue;

		struct user u = {0};
		if (hash_len >= sizeof(u.hash))
			hash_len = sizeof(u.hash) - 1;
		memcpy(u.hash, hash_start, hash_len);
		u.hash[hash_len] = '\0';

#ifdef __OpenBSD__
		/* field 3: uid (master.passwd has uid here) */
		if (colon2) {
			char *colon3 = strchr(colon2 + 1, ':');
			size_t uid_len = colon3
				? (size_t)(colon3 - (colon2 + 1))
				: strlen(colon2 + 1);
			char uid_str[16] = {0};
			if (uid_len < sizeof(uid_str)) {
				memcpy(uid_str, colon2 + 1, uid_len);
				u.uid = (int)strtol(uid_str, NULL, 10);
			}
		}
#endif

		/* active: has active file, or no rcode file */
		char active_path[560], rcode_path[560];
		snprintf(active_path, sizeof(active_path),
			"./users/%s/active", uname);
		snprintf(rcode_path, sizeof(rcode_path),
			"./users/%s/rcode", uname);
		struct stat st;
		int has_active = (stat(active_path, &st) == 0);
		int has_rcode  = (stat(rcode_path,  &st) == 0);
		u.active = (has_active || !has_rcode) ? 1 : 0;

		qmap_put(users_map, uname, &u);
	}
	fclose(f);
}

/*
 * On Linux, uid lives in ./etc/passwd (field 3), not in shadow.
 * Read passwd and update uid in any already-loaded users_map entry.
 */
#ifndef __OpenBSD__
static void
load_passwd(void)
{
	char path[512];
	passwd_path(path, sizeof(path));
	FILE *f = fopen(path, "r");
	if (!f)
		return;

	char line[512];
	while (fgets(line, sizeof(line), f)) {
		size_t ll = strlen(line);
		while (ll > 0 && (line[ll-1] == '\n' || line[ll-1] == '\r'))
			line[--ll] = '\0';

		/* field 1: username */
		char *c1 = strchr(line, ':');
		if (!c1) continue;
		*c1 = '\0';
		const char *uname = line;

		/* field 2: password (skip) */
		char *c2 = strchr(c1 + 1, ':');
		if (!c2) continue;

		/* field 3: uid */
		char *c3 = strchr(c2 + 1, ':');
		size_t uid_len = c3
			? (size_t)(c3 - (c2 + 1))
			: strlen(c2 + 1);
		char uid_str[16] = {0};
		if (uid_len == 0 || uid_len >= sizeof(uid_str))
			continue;
		memcpy(uid_str, c2 + 1, uid_len);
		int uid = (int)strtol(uid_str, NULL, 10);

		struct user *u = (struct user *)qmap_get(users_map, uname);
		if (u)
			u->uid = uid;
	}
	fclose(f);
}
#endif

/* ------------------------------------------------------------------ */
/* Ownership helpers                                                    */
/* ------------------------------------------------------------------ */

NDX_DEF(int, item_record_ownership,
	const char *, item_path, const char *, username)
{
	if (geteuid() == 0) {
		int uid = call_get_user_uid(username);
		if (uid >= 0)
			chown(item_path, (uid_t)uid, (gid_t)-1);
	} else {
		char owner_path[1024];
		snprintf(owner_path, sizeof(owner_path), "%s/owner", item_path);
		FILE *fp = fopen(owner_path, "w");
		if (fp) {
			fwrite(username, 1, strlen(username), fp);
			fclose(fp);
		}
	}
	return 0;
}

NDX_DEF(int, item_check_ownership,
	const char *, item_path, const char *, username)
{
	if (!username || !*username)
		return 0;

	if (geteuid() == 0) {
		struct stat st;
		if (stat(item_path, &st) != 0)
			return 0;
		int uid = call_get_user_uid(username);
		return uid >= 0 && (uid_t)uid == st.st_uid;
	} else {
		char owner_path[1024];
		snprintf(owner_path, sizeof(owner_path), "%s/owner", item_path);
		FILE *fp = fopen(owner_path, "r");
		if (!fp) return 0;
		char owner[64] = {0};
		if (fgets(owner, sizeof(owner) - 1, fp))
			owner[strcspn(owner, "\n")] = '\0';
		fclose(fp);
		return owner[0] && strcmp(owner, username) == 0;
	}
}

NDX_DEF(int, item_read_owner,
	const char *, item_path, char *, out, size_t, outlen)
{
	if (!out || outlen == 0)
		return -1;
	out[0] = '\0';

	if (geteuid() == 0) {
		struct stat st;
		if (stat(item_path, &st) != 0)
			return -1;
		char buf[4096];
		struct passwd pw, *result = NULL;
		if (getpwuid_r(st.st_uid, &pw, buf, sizeof(buf), &result) == 0
		    && result) {
			strncpy(out, result->pw_name, outlen - 1);
			out[outlen - 1] = '\0';
			return 0;
		}
		return -1;
	} else {
		char owner_path[1024];
		snprintf(owner_path, sizeof(owner_path), "%s/owner", item_path);
		FILE *fp = fopen(owner_path, "r");
		if (!fp) return -1;
		if (fgets(out, (int)outlen, fp))
			out[strcspn(out, "\n")] = '\0';
		else
			out[0] = '\0';
		fclose(fp);
		return out[0] ? 0 : -1;
	}
}

NDX_DEF(int, item_unlink_owner, const char *, item_path)
{
	if (geteuid() != 0) {
		char owner_path[1024];
		snprintf(owner_path, sizeof(owner_path), "%s/owner", item_path);
		unlink(owner_path);
	}
	return 0;
}

/* ------------------------------------------------------------------ */
/* HTTP handlers                                                        */
/* ------------------------------------------------------------------ */

static int
handle_session(int fd, char *body)
{
	(void)body;

	char cookie[256] = {0};
	char token[64]   = {0};

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
	     token[64], cookie[128];

	call_query_parse(body);
	call_query_param("username", username, sizeof(username));
	call_query_param("password", password, sizeof(password));
	call_query_param("ret",      redirect, sizeof(redirect));

	if (!*username || !*password) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 400);
		ndc_body(fd, "Missing username or password");
		return 1;
	}

	struct user *user = (struct user *)qmap_get(users_map, username);
	if (!user) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 400);
		ndc_body(fd, "User not found");
		return 1;
	}

	char *hash = crypt(password, user->hash);
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

	ndc_header(fd, "Location",  *redirect ? redirect : "/");
	ndc_header(fd, "Set-Cookie", cookie);
	ndc_header(fd, "Connection", "close");
	ndc_set_flags(fd, DF_TO_CLOSE);
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

	ndc_header(fd, "Location",  "/");
	ndc_header(fd, "Set-Cookie",
		"QSESSION=; Path=/; Max-Age=0; SameSite=Lax");
	ndc_header(fd, "Connection", "close");
	ndc_set_flags(fd, DF_TO_CLOSE);
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
	char username[64], password[64], password_confirm[64], email[128];
	char salt[64], cookie[128], token[64];
	char user_dir[512];

	struct user user = {0};

	call_query_parse(body);
	call_query_param("username",  username,         sizeof(username));
	call_query_param("password",  password,         sizeof(password));
	call_query_param("password2", password_confirm, sizeof(password_confirm));
	call_query_param("email",     email,            sizeof(email));
	char redirect[256] = {0};
	call_query_param("ret",       redirect,         sizeof(redirect));

	size_t ulen = strlen(username);
	if (ulen < 2 || ulen > 32) {
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
	char *hash = crypt(password, salt);
	if (!hash) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 500);
		ndc_body(fd, "Password hashing failed");
		return 1;
	}

	int uid = next_uid();

	strncpy(user.hash, hash, sizeof(user.hash) - 1);
	user.active = 1;
	user.uid    = uid;

	qmap_put(users_map, username, &user);

	if (shadow_append(username, user.hash, uid) != 0)
		fprintf(stderr, "auth: warning: could not write shadow file\n");

	if (passwd_append(username, uid) != 0)
		fprintf(stderr, "auth: warning: could not write passwd file\n");

	if (group_append(username) != 0)
		fprintf(stderr, "auth: warning: could not update group file\n");

#ifdef __OpenBSD__
	run_pwd_mkdb();
#endif

	/* Create user directory */
	snprintf(user_dir, sizeof(user_dir), "./users/%s", username);
	if (mkdir(user_dir, 0755) && errno != EEXIST)
		fprintf(stderr, "auth: warning: could not create user dir %s\n",
			user_dir);

	/* Create home directory */
	char home_dir[512];
	snprintf(home_dir, sizeof(home_dir), "./home/%s", username);
	if (mkdir(home_dir, 0755) && errno != EEXIST)
		fprintf(stderr, "auth: warning: could not create home dir %s\n",
			home_dir);

	/* Write email */
	if (*email) {
		char email_path[560];
		snprintf(email_path, sizeof(email_path), "%s/email", user_dir);
		FILE *ef = fopen(email_path, "w");
		if (ef) { fputs(email, ef); fclose(ef); }
	}

	/* Auto-login: create session */
	generate_token(token, sizeof(token));
	qmap_put(sessions_map, token, username);

	snprintf(cookie, sizeof(cookie),
		"QSESSION=%s; Path=/; SameSite=Lax", token);

	ndc_header(fd, "Location",  *redirect ? redirect : "/");
	ndc_header(fd, "Set-Cookie", cookie);
	ndc_header(fd, "Connection", "close");
	ndc_set_flags(fd, DF_TO_CLOSE);
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
	call_query_param("r", code,     sizeof(code));

	if (!*username || !*code) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 400);
		ndc_body(fd, "Missing parameters");
		return 1;
	}

	existing = (struct user *)qmap_get(users_map, username);
	if (!existing || existing->active) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 400);
		ndc_body(fd, "Invalid confirmation");
		return 1;
	}

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

	memcpy(&user, existing, sizeof(user));
	user.active = 1;
	qmap_put(users_map, username, &user);

	snprintf(active_path, sizeof(active_path),
		"./users/%s/active", username);
	FILE *af = fopen(active_path, "w");
	if (af) fclose(af);

	remove(rcode_path);

	generate_token(token, sizeof(token));
	qmap_put(sessions_map, token, username);

	snprintf(cookie, sizeof(cookie),
		"QSESSION=%s; Path=/; SameSite=Lax", token);

	ndc_header(fd, "Location",  "/");
	ndc_header(fd, "Set-Cookie", cookie);
	ndc_header(fd, "Connection", "close");
	ndc_set_flags(fd, DF_TO_CLOSE);
	ndc_head(fd, 303);
	ndc_close(fd);
	return 0;
}

/* ------------------------------------------------------------------ */
/* Module init                                                          */
/* ------------------------------------------------------------------ */

void ndx_install(void)
{
	ndx_load("./mods/index/index");
	ndx_load("./mods/common/common");

	users_map = qmap_open(NULL, "users", QM_STR,
		qmap_reg(sizeof(struct user)), 0xFFFF, 0);
	sessions_map = qmap_open(NULL, "sess", QM_STR,
		QM_STR, 0xFF, 0);

	mkdir("./etc",  0755);
	mkdir("./users", 0755);
	mkdir("./home",  0755);

#ifndef __OpenBSD__
	{
		FILE *ns = fopen("./etc/nsswitch.conf", "wx");
		if (ns) {
			fputs("passwd:     files\n", ns);
			fputs("shadow:     files\n", ns);
			fputs("group:      files\n", ns);
			fclose(ns);
		}
	}
#endif

	load_shadow();
#ifndef __OpenBSD__
	load_passwd();
#endif

	ndc_register_handler("POST:/auth/login",    handle_login);
	ndc_register_handler("POST:/auth/register", handle_register);
	ndc_register_handler("GET:/auth/api/session", handle_session);
	ndc_register_handler("/auth/logout",        handle_logout);
	ndc_register_handler("/auth/confirm",       handle_confirm);

	auth_hd = qmap_open(NULL, "hd", QM_STR, QM_STR, 0xFF, 0);

	call_index_open("Auth", 0, 0);
}

void ndx_open(void) {}
