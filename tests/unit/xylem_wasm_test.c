#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <ttypt/xy-mod.h>

#define CHECK(label, condition)                                                \
	do {                                                                   \
		if (condition)                                                 \
			printf("PASS: %s\n", label);                           \
		else {                                                         \
			printf("FAIL: %s (line %d)\n", label, __LINE__);        \
			failures++;                                            \
		}                                                              \
	} while (0)

static int failures = 0;

/* --- Hook Definitions --- */
XY_DECL(int, test_hook_add, int, a, int, b);
XY_DECL(int, test_hook_chain, int, val);
XY_DECL(int, test_hook_unregistered, int, x);

/* --- Listener 1 for test_hook_add --- */
XY_LISTENER_TAG(int, test_hook_add, listener_a, int, a, int, b)
{
	return a + b;
}

/* --- Listener 1 for test_hook_chain --- */
static int g_chain_order[4] = {0};
static int g_chain_idx = 0;

XY_LISTENER_TAG(int, test_hook_chain, first, int, val)
{
	g_chain_order[g_chain_idx++] = 1;
	return val * 2;
}

/* --- Listener 2 for test_hook_chain --- */
XY_LISTENER_TAG(int, test_hook_chain, second, int, val)
{
	(void)val;
	g_chain_order[g_chain_idx++] = 2;
	int prev = 0;
	if (xy_last(&prev) == XY_OK) {
		return prev + 10;
	}
	return -1;
}

int main(void)
{
	printf("=== Testing in-WASM Xylem Hook Dispatcher ===\n");

	/* 1. Basic Hook Call */
	{
		int res = test_hook_add(3, 4);
		CHECK("test_hook_add dispatches and returns 7", res == 7);
		CHECK("xy_errno() returns XY_OK", xy_errno() == XY_OK);
	}

	/* 2. Multi-Listener Chaining with xy_last() */
	{
		g_chain_idx = 0;
		int res = test_hook_chain(5);
		/* Listener 'first' computes 5 * 2 = 10 */
		/* Listener 'second' reads prev (10) + 10 = 20 */
		CHECK("test_hook_chain final value is 20", res == 20);
		CHECK("Both listeners ran in sequence", g_chain_idx == 2);
		CHECK("Listener execution order verified", g_chain_order[0] == 1 && g_chain_order[1] == 2);
	}

	/* 3. Unregistered Hook */
	{
		int res = test_hook_unregistered(42);
		CHECK("Unregistered hook returns 0", res == 0);
		CHECK("xy_errno() returns XY_ERR_NOTFOUND", xy_errno() == XY_ERR_NOTFOUND);
	}

	/* 3b. Error Handling for XY_ERR_INVALID */
	{
		/* Null adapter passed to xy_call returns XY_ERR_INVALID */
		int ret = xy_call(NULL, NULL, NULL);
		CHECK("xy_call(NULL, NULL, NULL) returns XY_ERR_INVALID", ret == XY_ERR_INVALID);
		CHECK("xy_errno() returns XY_ERR_INVALID", xy_errno() == XY_ERR_INVALID);

		/* Null name passed to xy_areg returns (unsigned)-1 */
		unsigned areg_ret = xy_areg(NULL, NULL);
		CHECK("xy_areg(NULL, NULL) returns (unsigned)-1", areg_ret == (unsigned)-1);
		CHECK("xy_errno() is XY_ERR_INVALID after bad xy_areg", xy_errno() == XY_ERR_INVALID);

		const char *msg_inv = xy_strerror(XY_ERR_INVALID);
		CHECK("xy_strerror(XY_ERR_INVALID) valid", msg_inv && strstr(msg_inv, "Invalid") != NULL);
	}

	/* 4. String error messages */
	{
		const char *msg_ok = xy_strerror(XY_OK);
		const char *msg_nf = xy_strerror(XY_ERR_NOTFOUND);
		CHECK("xy_strerror(XY_OK) valid", msg_ok && strcmp(msg_ok, "Success") == 0);
		CHECK("xy_strerror(XY_ERR_NOTFOUND) valid", msg_nf && strstr(msg_nf, "not found") != NULL);
	}

	/* 5. Region stubs */
	{
		CHECK("xy_current_region() returns 0", xy_current_region() == 0);
		CHECK("xy_current_region_plen() returns 0", xy_current_region_plen() == 0);
		CHECK("xy_region_exists(0, 0) returns 0", xy_region_exists(0, 0) == 0);
	}

	if (failures == 0) {
		printf("ALL XYLEM WASM TESTS PASSED\n");
		return 0;
	}
	printf("TESTS FAILED: %d\n", failures);
	return 1;
}
