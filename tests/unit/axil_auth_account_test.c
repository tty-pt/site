/* Account-level security tests for axil-auth.
 *
 * Covers the two properties that make the terminal gate meaningful:
 *
 *   auth_username_taken  -- a name already present in the passwd database must
 *                           not be claimable by registration. load_passwd()
 *                           deliberately does not seed users_map from passwd
 *                           (it only patches uid for names already loaded from
 *                           shadow), so a passwd-only entry was claimable: the
 *                           duplicate check consulted users_map alone.
 *
 *   auth_password_matches -- exported credential check used by passworded
 *                           connect. Must fail closed and must not distinguish
 *                           an unknown user from a wrong password.
 *
 * auth.h expands XY_DECL into a static inline bus dispatcher, which zero-fills
 * its result when no module implements the hook. So this TU defines AUTH_IMPL and
 * calls the exported symbol directly, the way libaxil-auth.c does.
 */

#include <ttypt/auth-config.h>

int auth_password_matches(const char *username, const char *password);

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <limits.h>
#include <crypt.h>

#define CHECK(label, condition)                                                \
	do {                                                                   \
		if (condition)                                                 \
			printf("PASS %s\n", label);                            \
		else {                                                         \
			printf("FAIL %s (line %d)\n", label, __LINE__);        \
			failures++;                                            \
		}                                                              \
	} while (0)

static int failures = 0;

/* 1. a real account, in both shadow and passwd, hash we control */
static const char *ALICE_PW = "alice-secret-pw";

/* 2. present in passwd only -- the F13 squatting fixture */
static const char *ORPHAN = "passwd_only_squatter";

static void
write_file(const char *path, const char *content)
{
	FILE *f = fopen(path, "w");
	if (!f) {
		perror(path);
		exit(1);
	}
	fputs(content, f);
	fclose(f);
}

int
main(void)
{
	char test_dir[] = "/tmp/test_axil_auth_account_XXXXXX";
	if (!mkdtemp(test_dir)) {
		perror("mkdtemp");
		return 1;
	}

	char path[PATH_MAX];
	char shadow_file[PATH_MAX], passwd_file[PATH_MAX], group_file[PATH_MAX];

	snprintf(path, sizeof(path), "%s/shadow", test_dir);
	snprintf(shadow_file, sizeof(shadow_file), "%s", path);
	const char *shadow_path = shadow_file;

	/* A genuine bcrypt hash so the verify path is exercised for real. */
	char *hash = crypt(ALICE_PW, "$2b$12$abcdefghijklmnopqrstuv");
	if (!hash || strncmp(hash, "$2b$", 4) != 0) {
		fprintf(stderr, "FATAL: no usable bcrypt from crypt(): %s\n",
		    hash ? hash : "(null)");
		return 1;
	}

	/* shadow drives users_map. 'locked' carries the conventional no-op hash,
	 * which crypt() rejects, so it must never authenticate. */
	char shadow[4096];
	snprintf(shadow, sizeof(shadow),
	    "alice:%s:1001:67::::::\n"
	    "locked:*:1002:67::::::\n",
	    hash);
	write_file(shadow_path, shadow);

	snprintf(passwd_file, sizeof(passwd_file), "%s/passwd", test_dir);
	char passwd[4096];
	snprintf(passwd, sizeof(passwd),
	    "alice:x:1001:67::/home/alice:/bin/false\n"
	    "locked:x:1002:67::/home/locked:/bin/false\n"
	    /* no shadow row for this one: claimable before the fix */
	    "%s:x:1003:67::/home/%s:/bin/sh\n",
	    ORPHAN, ORPHAN);
	write_file(passwd_file, passwd);

	snprintf(group_file, sizeof(group_file), "%s/group", test_dir);
	write_file(group_file, "www:x:67:\n");

	auth_config.etc_dir = test_dir;
	auth_config.users_dir = test_dir;
	auth_config.home_dir = test_dir;
	auth_init();

	/* --- registration surface: name availability ---------------------- */

	CHECK("registered name (in shadow) is taken",
	    auth_username_taken("alice") != 0);

	/* The regression that matters: present in passwd, absent from shadow. */
	CHECK("passwd-only name is taken (F13 squatting)",
	    auth_username_taken(ORPHAN) != 0);

	CHECK("locked hash name is taken", auth_username_taken("locked") != 0);

	CHECK("unknown name is available", auth_username_taken("brand_new_name") == 0);

	CHECK("empty name is available (caller validates separately)",
	    auth_username_taken("") == 0);

	/* A real system account must be unavailable even though it appears in
	 * neither of our files -- gate 3 would resolve it. */
	if (getpwnam("root")) {
		CHECK("system account 'root' is taken",
		    auth_username_taken("root") != 0);
	}

	/* --- credential check ---------------------------------------------- */

	CHECK("correct password is accepted",
	    auth_password_matches("alice", ALICE_PW) != 0);

	CHECK("wrong password is rejected",
	    auth_password_matches("alice", "not-the-password") == 0);

	CHECK("empty password is rejected",
	    auth_password_matches("alice", "") == 0);

	CHECK("unknown user is rejected",
	    auth_password_matches("no_such_user_at_all", ALICE_PW) == 0);

	/* Uniform failure: callers must not be able to enumerate accounts. */
	CHECK("unknown user and wrong password are indistinguishable",
	    auth_password_matches("no_such_user_at_all", ALICE_PW)
	        == auth_password_matches("alice", "not-the-password"));

	/* A no-op hash must never authenticate, so `connect root <pw>` cannot
	 * succeed for a locked system account. */
	CHECK("a '*'-locked account is rejected",
	    auth_password_matches("locked", ALICE_PW) == 0);
	CHECK("a passwd-only account is rejected",
	    auth_password_matches(ORPHAN, ALICE_PW) == 0);

	/* NULL arguments must not crash a caller on a malformed prompt. */
	CHECK("NULL username is rejected",
	    auth_password_matches(NULL, ALICE_PW) == 0);
	CHECK("NULL password is rejected",
	    auth_password_matches("alice", NULL) == 0);
	CHECK("username_taken tolerates NULL", auth_username_taken(NULL) == 0);

	/* --- the on-disk invariant the terminal gate depends on ------------ */

	FILE *pf = fopen(passwd_file, "r");
	CHECK("passwd file readable", pf != NULL);
	if (pf) {
		char content[8192] = { 0 };
		size_t n = fread(content, 1, sizeof(content) - 1, pf);
		fclose(pf);
		(void)n;
		CHECK("passwd file holds its rows", strstr(content, "alice:") != NULL);
	}

	unlink(shadow_file);
	unlink(passwd_file);
	unlink(group_file);
	rmdir(test_dir);

	printf("\naxil_auth_account_test: %s\n",
	    failures == 0 ? "ALL PASS" : "SOME FAILURES");
	return failures ? 1 : 0;
}