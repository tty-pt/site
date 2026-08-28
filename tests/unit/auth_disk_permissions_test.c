#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <limits.h>

#include <ttypt/auth.h>
#include <ttypt/xy.h>
#include "../mods/common/common.h"
#include "../mods/auth/auth.h"

void _xy_init(void *ptr, const char *fname, uint64_t region_id);

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

int main(void)
{
	char test_dir[] = "/tmp/test_auth_disk_perms_XXXXXX";
	if (!mkdtemp(test_dir)) {
		perror("mkdtemp");
		return 1;
	}

	char item_dir[PATH_MAX];
	snprintf(item_dir, sizeof(item_dir), "%s/item1", test_dir);
	mkdir(item_dir, 0755);

	char owner_file[PATH_MAX];
	snprintf(owner_file, sizeof(owner_file), "%s/owner", item_dir);

	/* Setup dummy shadow and passwd files in test etc directory */
	char shadow_file[PATH_MAX], passwd_file[PATH_MAX];
	snprintf(shadow_file, sizeof(shadow_file), "%s/shadow", test_dir);
	snprintf(passwd_file, sizeof(passwd_file), "%s/passwd", test_dir);

	FILE *sf = fopen(shadow_file, "w");
	if (sf) {
		fputs("alice:$2b$12$dummyhashforalicetestingshadowentry12345:1001:67::::::\n", sf);
		fputs("bob:$2b$12$dummyhashforbobtestingshadowentry123456:1002:67::::::\n", sf);
		fclose(sf);
	}
	FILE *pf = fopen(passwd_file, "w");
	if (pf) {
		fputs("alice:x:1001:67::/home/alice:/bin/sh\n", pf);
		fputs("bob:x:1002:67::/home/bob:/bin/sh\n", pf);
		fclose(pf);
	}

	/* Initialize XY bus and load site modules */
	xy_init();
	_xy_init(&xy, "main", 0);
	xy_load("./mods/common/common");
	xy_load("./mods/auth/auth");

	/* Initialize auth subsystem with test configuration */
	auth_config.etc_dir = test_dir;
	auth_config.users_dir = test_dir;
	auth_config.home_dir = test_dir;
	auth_init();

	/* 1. Test UID reverse lookup API */
	char resolved_user[64] = { 0 };
	int rc = auth_get_username(1000, resolved_user, sizeof(resolved_user));
	/* May or may not find user 1000 depending on passwd, but function must not crash */
	CHECK("auth_get_username handles invalid/unregistered uid safely",
	      auth_get_username(999999, resolved_user, sizeof(resolved_user)) == -1);

	/* 2. DEV MODE: verify owner file is written and read */
	setenv("AUTH_DISK_PERMS", "0", 1);
	setenv("AUTH_ENV", "dev", 1);

	rc = item_owner_record(item_dir, "alice");
	CHECK("dev mode item_owner_record returns 0", rc == 0);
	CHECK("dev mode creates owner file", access(owner_file, F_OK) == 0);

	char read_buf[64] = { 0 };
	rc = item_owner_read(item_dir, read_buf, sizeof(read_buf));
	CHECK("dev mode item_owner_read returns 0", rc == 0);
	CHECK("dev mode item_owner_read returns alice", strcmp(read_buf, "alice") == 0);

	CHECK("dev mode item_owner_check matches alice", item_owner_check(item_dir, "alice") == 1);
	CHECK("dev mode item_owner_check rejects bob", item_owner_check(item_dir, "bob") == 0);

	/* 3. PROD MODE: verify disk permissions are strictly followed */
	setenv("AUTH_DISK_PERMS", "1", 1);
	setenv("AUTH_ENV", "prod", 1);

	/* Write a conflicting owner file with 'bob' to prove prod mode ignores it */
	FILE *fp = fopen(owner_file, "w");
	if (fp) {
		fputs("bob\n", fp);
		fclose(fp);
	}
	CHECK("planted conflicting owner file exists", access(owner_file, F_OK) == 0);

	/* In prod mode, item_owner_record cleans up owner file and relies on disk permissions */
	struct stat st;
	if (stat(item_dir, &st) == 0) {
		/* If we simulate alice having current process uid */
		rc = item_owner_record(item_dir, "alice");
		CHECK("prod mode item_owner_record cleans up owner file", access(owner_file, F_OK) != 0);

		/* When checking ownership in prod mode, it must check st.st_uid, NOT the owner file */
		int check_alice = item_owner_check(item_dir, "alice");
		int check_bob = item_owner_check(item_dir, "bob");
		
		int alice_uid = auth_get_uid("alice");
		if (alice_uid >= 0 && (uid_t)alice_uid == st.st_uid) {
			CHECK("prod mode matches disk UID for alice", check_alice == 1);
			CHECK("prod mode rejects bob despite any file", check_bob == 0);
		} else {
			/* If alice UID != st.st_uid, prod mode correctly rejects both */
			CHECK("prod mode strictly checks disk UID", check_bob == 0);
		}
	}

	/* Cleanup */
	unlink(owner_file);
	rmdir(item_dir);
	rmdir(test_dir);

	printf("\nauth_disk_permissions_test: %s\n", failures == 0 ? "ALL PASS" : "SOME FAILURES");
	return failures ? 1 : 0;
}
