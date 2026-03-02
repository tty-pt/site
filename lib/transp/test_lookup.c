#include "transp.h"
#include <stdio.h>
#include <stdlib.h>

int main(void) {
	transp_ctx_t *ctx = transp_init();
	
	// Try with HTML flag
	const char *line = "Amazing Grace, how sweet the sound\n";
	printf("Input: %s", line);
	
	char *result = transp_buffer(ctx, line, 0, TRANSP_HTML);
	printf("Output: %s\n", result);
	
	free(result);
	transp_free(ctx);
	return 0;
}
