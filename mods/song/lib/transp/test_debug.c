#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "transp.h"

int main()
{
	transp_ctx_t *ctx = transp_init();
	if (!ctx) {
		printf("FAIL: ctx init\n");
		return 1;
	}

	// Test individual chords
	char *test1 = "E/G#";
	char *result1 = transp_buffer(ctx, test1, 2, 0);
	printf("Input: '%s' -> Output: '%s'\n",
	       test1,
	       result1 ? result1 : "(null)");
	free(result1);

	// Reset key
	transp_reset_key(ctx);

	// Test with space
	char *test2 = "E/G# A";
	char *result2 = transp_buffer(ctx, test2, 2, 0);
	printf("Input: '%s' -> Output: '%s'\n",
	       test2,
	       result2 ? result2 : "(null)");
	free(result2);

	transp_free(ctx);
	return 0;
}
