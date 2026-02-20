#include <ndc.h>
#include <ndc-ndx.h>
#include <string.h>
#include <stdio.h>

void
test_handler(int cfd, char *body) {
	fprintf(stderr, "huh?\n");
	/* ndc_writef(cfd, "Status: 200 OK\n\nOK"); */
}

static int
load_modules_from_file(const char *path)
{
	FILE *fp = fopen(path, "r");
	if (!fp)
		return 0;

	char line[512];
	while (fgets(line, sizeof(line), fp)) {
		size_t len = strlen(line);
		while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
			line[len - 1] = '\0';
			len--;
		}
		if (len == 0 || line[0] == '#')
			continue;
		ndx_load(line);
	}

	fclose(fp);
	return 1;
}

void ndx_open(void)
{
	ndc_register_handler("/test", test_handler);
	load_modules_from_file("./mods.load");
}

void ndx_install(void) {
	ndc_register_handler("/test", test_handler);
	load_modules_from_file("./mods.load");
}
