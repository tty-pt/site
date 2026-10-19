#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <hyle-source/hyle_source.h>

#define CHECK(label, condition)                                                \
	do {                                                                   \
		if (condition)                                                 \
			printf("PASS %s\n", label);                            \
		else {                                                         \
			printf("FAIL %s\n", label);                            \
			failures++;                                            \
		}                                                              \
	} while (0)

static int failures = 0;

int main(void)
{
	/* 1. Test ID safety checks */
	CHECK("safe id 'abc-123_xyz'", hyle_source_is_safe_id("abc-123_xyz") == 1);
	CHECK("reject empty id", hyle_source_is_safe_id("") == 0);
	CHECK("reject NULL id", hyle_source_is_safe_id(NULL) == 0);
	CHECK("reject path traversal '../foo'", hyle_source_is_safe_id("../foo") == 0);
	CHECK("reject path slash 'foo/bar'", hyle_source_is_safe_id("foo/bar") == 0);
	CHECK("reject special char 'foo@bar'", hyle_source_is_safe_id("foo@bar") == 0);

	/* 2. Test file write and slurp */
	const char *test_dir = "/tmp/test_hyle_source_utils";
	mkdir(test_dir, 0755);

	char test_file[256];
	snprintf(test_file, sizeof(test_file), "%s/test.txt", test_dir);

	const char *content = "Hello, pure C world!\nLine 2 with special chars: @#$&*\n";
	int wr = hyle_source_write_file(test_file, content, strlen(content));
	CHECK("write file returns 0", wr == 0);

	char *slurped = hyle_source_slurp_file(test_file);
	CHECK("slurp file not NULL", slurped != NULL);
	if (slurped) {
		CHECK("slurped content matches written", strcmp(slurped, content) == 0);
		free(slurped);
	}

	/* 3. Test missing file slurp */
	char *missing = hyle_source_slurp_file("/tmp/nonexistent_file_xyz_123.txt");
	CHECK("missing file returns NULL", missing == NULL);

	/* 4. Test recursive directory removal */
	char sub_dir[256], sub_file[256];
	snprintf(sub_dir, sizeof(sub_dir), "%s/subdir", test_dir);
	mkdir(sub_dir, 0755);
	snprintf(sub_file, sizeof(sub_file), "%s/inner.txt", sub_dir);
	hyle_source_write_file(sub_file, "nested", 6);

	int rm_res = hyle_source_remove_path_recursive(test_dir);
	CHECK("remove recursive returns 0", rm_res == 0);
	CHECK("dir is removed", access(test_dir, F_OK) != 0);

	/* 5. Test doc root resolution */
	char doc_root[256] = { 0 };
	const char *dr = hyle_source_resolve_doc_root(doc_root, sizeof(doc_root));
	CHECK("doc root resolved", dr != NULL && dr[0] != '\0');

	printf("\nsource_utils_test: %s\n", failures == 0 ? "ALL PASS" : "SOME FAILURES");
	return failures ? 1 : 0;
}
