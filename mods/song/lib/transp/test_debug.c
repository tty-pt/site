#include "transp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
	transp_ctx_t *ctx = transp_init();

	const char *song =
	    "1. Verse\n"
	    "C       G       Am      F\n"
	    "Amazing Grace, how sweet the sound\n";

	printf("Processing song:\n%s\n", song);
	printf("Transposing by +2 with HTML...\n\n");

	char *result = transp_buffer(ctx, song, 2, TRANSP_HTML);
	printf("Result:\n%s\n\n", result);

	free(result);
	transp_free(ctx);
	return 0;
}
