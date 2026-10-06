/* The bus contract that passworded connect depends on.
 *
 * XY hooks reached through XY_DECL dispatch zero-fill their result when no
 * module implements them (xy.h adapter_call: fn == NULL -> memset 0). Security
 * predicates dispatched through the bus must therefore report "deny" as zero:
 * auth_password_matches() returns NON-zero on success precisely so that an
 * absent axil-auth fails every login closed instead of open. This test pins
 * the zero-default itself; the polarity convention is pinned by
 * axil_auth_account_test.
 */

#include <stdio.h>

#include <ttypt/xy.h>

/* A hook nothing implements, in this or any loaded module. */
XY_DECL(int, xy_test_unimplemented_hook, int, x);

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

int
main(void)
{
	/* No module is loaded here at all, so this is the bare default. */
	CHECK("unimplemented hook returns zero",
	    xy_test_unimplemented_hook(1) == 0);
	CHECK("unimplemented hook returns zero for any argument",
	    xy_test_unimplemented_hook(0) == 0);

	printf("\nxy_hook_default_test: %s\n",
	    failures == 0 ? "ALL PASS" : "SOME FAILURES");
	return failures ? 1 : 0;
}
