#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <limits.h>

#include <ttypt/auth.h>

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
	char test_dir[] = "/tmp/test_axil_auth_groups_XXXXXX";
	if (!mkdtemp(test_dir)) {
		perror("mkdtemp");
		return 1;
	}

	char group_file[PATH_MAX], shadow_file[PATH_MAX], passwd_file[PATH_MAX];
	snprintf(group_file, sizeof(group_file), "%s/group", test_dir);
	snprintf(shadow_file, sizeof(shadow_file), "%s/shadow", test_dir);
	snprintf(passwd_file, sizeof(passwd_file), "%s/passwd", test_dir);

	/* Pre-populate base group file with www group */
	FILE *gf = fopen(group_file, "w");
	if (gf) {
		fputs("www:x:67:\n", gf);
		fclose(gf);
	}
	FILE *sf = fopen(shadow_file, "w");
	if (sf) {
		fputs("alice:$2b$12$dummyhashforalicetestingshadowentry12345:1001:67::::::\n", sf);
		fputs("bob:$2b$12$dummyhashforbobtestingshadowentry123456:1002:67::::::\n", sf);
		fputs("carol:$2b$12$dummyhashforcaroltestingshadowentry1234:1003:67::::::\n", sf);
		fclose(sf);
	}
	FILE *pf = fopen(passwd_file, "w");
	if (pf) {
		fputs("alice:x:1001:67::/home/alice:/bin/sh\n", pf);
		fputs("bob:x:1002:67::/home/bob:/bin/sh\n", pf);
		fputs("carol:x:1003:67::/home/carol:/bin/sh\n", pf);
		fclose(pf);
	}

	auth_config.etc_dir = test_dir;
	auth_config.users_dir = test_dir;
	auth_config.home_dir = test_dir;
	auth_init();

	/* 1. Test create group */
	int gid1 = auth_create_group("choir_soprano");
	CHECK("create group returns positive GID >= 2000", gid1 >= 2000);

	int gid1_lookup = auth_get_gid("choir_soprano");
	CHECK("lookup GID matches created GID", gid1_lookup == gid1);

	char grpname[64] = { 0 };
	int rc = auth_get_grpname(gid1, grpname, sizeof(grpname));
	CHECK("get grpname by GID returns 0", rc == 0);
	CHECK("resolved grpname is choir_soprano", strcmp(grpname, "choir_soprano") == 0);

	/* 2. Test group members */
	CHECK("alice not in group initially", auth_user_in_group("alice", "choir_soprano") == 0);
	CHECK("bob not in group initially", auth_user_in_group("bob", "choir_soprano") == 0);

	rc = auth_group_add_member("choir_soprano", "alice");
	CHECK("add alice to group returns 0", rc == 0);
	CHECK("alice in group after add", auth_user_in_group("alice", "choir_soprano") == 1);
	CHECK("bob still not in group", auth_user_in_group("bob", "choir_soprano") == 0);

	rc = auth_group_add_member("choir_soprano", "bob");
	CHECK("add bob to group returns 0", rc == 0);
	CHECK("bob in group after add", auth_user_in_group("bob", "choir_soprano") == 1);
	CHECK("alice still in group", auth_user_in_group("alice", "choir_soprano") == 1);

	char members_buf[256] = { 0 };
	rc = auth_group_get_members("choir_soprano", members_buf, sizeof(members_buf));
	CHECK("get members returns 0", rc == 0);
	CHECK("members contain alice", strstr(members_buf, "alice") != NULL);
	CHECK("members contain bob", strstr(members_buf, "bob") != NULL);

	/* 3. Test remove member */
	rc = auth_group_del_member("choir_soprano", "alice");
	CHECK("remove alice returns 0", rc == 0);
	CHECK("alice not in group after remove", auth_user_in_group("alice", "choir_soprano") == 0);
	CHECK("bob still in group after removing alice", auth_user_in_group("bob", "choir_soprano") == 1);

	/* 4. Test multiple groups */
	int gid2 = auth_create_group("choir_tenor");
	CHECK("second group gets different GID", gid2 > gid1);
	auth_group_add_member("choir_tenor", "carol");
	CHECK("carol in choir_tenor", auth_user_in_group("carol", "choir_tenor") == 1);
	CHECK("carol not in choir_soprano", auth_user_in_group("carol", "choir_soprano") == 0);

	/* 5. Verify /etc/group on-disk persistence */
	FILE *rf = fopen(group_file, "r");
	CHECK("group file opened", rf != NULL);
	if (rf) {
		char content[4096] = { 0 };
		fread(content, 1, sizeof(content) - 1, rf);
		fclose(rf);
		CHECK("group file contains choir_soprano with bob", strstr(content, "choir_soprano") != NULL && strstr(content, "bob") != NULL);
		CHECK("group file contains choir_tenor with carol", strstr(content, "choir_tenor") != NULL && strstr(content, "carol") != NULL);
	}

	/* Cleanup */
	unlink(group_file);
	unlink(shadow_file);
	unlink(passwd_file);
	rmdir(test_dir);

	printf("\naxil_auth_groups_test: %s\n", failures == 0 ? "ALL PASS" : "SOME FAILURES");
	return failures ? 1 : 0;
}
