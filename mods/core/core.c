#include <ttypt/ndc.h>
#include <ttypt/ndc-ndx.h>
#include <string.h>
#include <stdio.h>

#if 0
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

static int
load_modules_from_file(const char *path)
{
	char mod_line[1030];
	char line[512];
	FILE *fp = fopen(path, "r");

	if (!fp)
		return 0;

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

#if 0
static socket_t
frsh_upstream(socket_t client_fd)
{
	socket_t upstream = socket(AF_INET, SOCK_STREAM, 0);
	if (upstream < 0) return -1;

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(3000);
	inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

	if (connect(upstream, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		close(upstream);
		return -1;
	}

	return upstream;
}
#endif

void ndx_install(void) {
	load_modules_from_file("./mods.load");
	ndx_load("./mods/ssr/ssr");
	/* ndc_ws_handler("/_frsh/alive", frsh_upstream); */
}
