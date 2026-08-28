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
	char test_dir[] = "/tmp/test_auth_grp_perms_XXXXXX";
	if (!mkdtemp(test_dir)) {
		perror("mkdtemp");
		return 1;
	}

	char item_dir[PATH_MAX];
	snprintf(item_dir, sizeof(item_dir), "%s/gig1", test_dir);
	mkdir(item_dir, 0755);

	char group_file[PATH_MAX], shadow_file[PATH_MAX], passwd_file[PATH_MAX];
	snprintf(group_file, sizeof(group_file), "%s/group", test_dir);
	snprintf(shadow_file, sizeof(shadow_file), "%s/shadow", test_dir);
	snprintf(passwd_file, sizeof(passwd_file), "%s/passwd", test_dir);

	FILE *gf = fopen(group_file, "w");
	if (gf) {
		fputs("www:x:67:\n", gf);
		fclose(gf);
	}
	FILE *sf = fopen(shadow_file, "w");
	if (sf) {
		fputs("owner_user:$2b$12$dummyhashforowneruser1234567890123:1001:67::::::\n", sf);
		fputs("member_user:$2b$12$dummyhashformemberuser123456789012:1002:67::::::\n", sf);
		fputs("stranger_user:$2b$12$dummyhashforstrangeruser12345678:1003:67::::::\n", sf);
		fclose(sf);
	}
	int my_uid = (int)geteuid();
	FILE *pf = fopen(passwd_file, "w");
	if (pf) {
		fprintf(pf, "owner_user:x:%d:67::/home/owner_user:/bin/sh\n", my_uid);
		fprintf(pf, "member_user:x:%d:67::/home/member_user:/bin/sh\n", my_uid + 1);
		fprintf(pf, "stranger_user:x:%d:67::/home/stranger_user:/bin/sh\n", my_uid + 2);
		fclose(pf);
	}

	/* Initialize XY bus and load modules */
	xy_init();
	_xy_init(&xy, "main", 0);
	xy_load("./mods/common/common");
	xy_load("./mods/auth/auth");

	auth_config.etc_dir = test_dir;
	auth_config.users_dir = test_dir;
	auth_config.home_dir = test_dir;
	auth_init();

	/* Setup group and membership */
	auth_create_group("choir_band");
	auth_group_add_member("choir_band", "member_user");

	/* Set item owner */
	item_owner_record(item_dir, "owner_user");

	/* 1. DEV MODE: test item group record, read, check */
	setenv("AUTH_DISK_PERMS", "0", 1);
	setenv("AUTH_ENV", "dev", 1);

	int rc = item_group_record(item_dir, "choir_band");
	CHECK("dev mode item_group_record returns 0", rc == 0);

	char grp_read[64] = { 0 };
	rc = item_group_read(item_dir, grp_read, sizeof(grp_read));
	CHECK("dev mode item_group_read returns 0", rc == 0);
	CHECK("dev mode item_group_read returns choir_band", strcmp(grp_read, "choir_band") == 0);

	CHECK("dev mode item_group_check recognizes member_user", item_group_check(item_dir, "member_user") == 1);
	CHECK("dev mode item_group_check rejects stranger_user", item_group_check(item_dir, "stranger_user") == 0);

	/* 2. Test Write Permissions: ONLY owner can write/edit/delete */
	CHECK("owner_user can write", item_can_write(item_dir, "owner_user") == 1);
	CHECK("member_user CANNOT write", item_can_write(item_dir, "member_user") == 0);
	CHECK("stranger_user CANNOT write", item_can_write(item_dir, "stranger_user") == 0);
	CHECK("anonymous CANNOT write", item_can_write(item_dir, NULL) == 0);

	/* 3. Test Read Permissions: Public item (default) vs Private item */
	/* Public item: everyone can read */
	CHECK("public item: owner can read", item_can_read(item_dir, "owner_user") == 1);
	CHECK("public item: member can read", item_can_read(item_dir, "member_user") == 1);
	CHECK("public item: stranger can read", item_can_read(item_dir, "stranger_user") == 1);
	CHECK("public item: anonymous can read", item_can_read(item_dir, "") == 1);

	/* Mark item private: create <item_dir>/private (in dev) or chmod 0750 (in prod) */
	char private_file[PATH_MAX];
	snprintf(private_file, sizeof(private_file), "%s/private", item_dir);
	FILE *priv_fp = fopen(private_file, "w");
	if (priv_fp) { fputs("1\n", priv_fp); fclose(priv_fp); }

	/* Private item: ONLY owner and group member can read; stranger and anonymous rejected */
	CHECK("private item: owner can read", item_can_read(item_dir, "owner_user") == 1);
	CHECK("private item: member can read", item_can_read(item_dir, "member_user") == 1);
	CHECK("private item: stranger CANNOT read", item_can_read(item_dir, "stranger_user") == 0);
	CHECK("private item: anonymous CANNOT read", item_can_read(item_dir, NULL) == 0);

	/* 4. PROD MODE: verify disk permission checks */
	setenv("AUTH_DISK_PERMS", "1", 1);
	setenv("AUTH_ENV", "prod", 1);

	/* In prod mode with chmod 0750 */
	chmod(item_dir, 0750);
	CHECK("prod mode private item: owner can read", item_can_read(item_dir, "owner_user") == 1);
	CHECK("prod mode private item: stranger CANNOT read", item_can_read(item_dir, "stranger_user") == 0);
	CHECK("prod mode: member_user CANNOT write", item_can_write(item_dir, "member_user") == 0);

	/* Cleanup */
	unlink(private_file);
	unlink(group_file);
	unlink(shadow_file);
	unlink(passwd_file);
	char group_item_file[PATH_MAX];
	snprintf(group_item_file, sizeof(group_item_file), "%s/group", item_dir);
	unlink(group_item_file);
	char owner_item_file[PATH_MAX];
	snprintf(owner_item_file, sizeof(owner_item_file), "%s/owner", item_dir);
	unlink(owner_item_file);
	rmdir(item_dir);
	rmdir(test_dir);

	printf("\nauth_group_permissions_test: %s\n", failures == 0 ? "ALL PASS" : "SOME FAILURES");
	return failures ? 1 : 0;
}
