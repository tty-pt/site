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

	char *input = "A4+\nF#m7(11)\nE/G#\n";
	char *result = transp_buffer(ctx, input, 2, TRANSP_HTML);

	printf("Input:  %s", input);
	printf("Output: %s", result ? result : "(null)");

	if (result) {
		int has_A4 = strstr(result, "B4+") != NULL;
		int has_F = strstr(result, "Gm7") != NULL; // F# + 2 = G
		int has_E = strstr(result, "F#") != NULL;  // E + 2 = F#
		printf("\nA4+ -> B4+: %s\n", has_A4 ? "PASS" : "FAIL");
		printf("F#m7(11) -> Gm7(11): %s\n", has_F ? "PASS" : "FAIL");
		printf("E/G# -> F#/A#: %s\n", has_E ? "PASS" : "FAIL");
	}

	free(result);
	transp_free(ctx);
	return 0;
}
