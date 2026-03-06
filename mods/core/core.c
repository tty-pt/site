#include <ttypt/ndc.h>
#include <ttypt/ndc-ndx.h>
#include <string.h>
#include <stdio.h>

static int
load_modules_from_file(const char *path)
{
	char mod_line[1030];
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
		snprintf(mod_line, sizeof(mod_line), "mods/%s/%s", line, line);
		ndx_load(mod_line);
	}

	fclose(fp);
	return 1;
}

void ndx_open(void)
{
	load_modules_from_file("./mods.load");
}

void ndx_install(void) {
	load_modules_from_file("./mods.load");
}
