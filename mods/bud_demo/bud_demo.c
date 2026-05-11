#include <ttypt/axil.h>
#include <ttypt/ndx-mod.h>
#include <stdio.h>
#include "bud/bud.h"

extern bud_node *bud_app_render(void);

static int handler(int fd, char *path)
{
	bud_node *app = bud_app_render();
	if (!app) {
		axil_header_set(
		        fd, "Content-Type", "text/plain; charset=utf-8");
		axil_respond(fd, 500, "Failed to render DOM");
		return 0;
	}
	char *html = bud_render_hydrated_html(app);
	bud_free(app);
	if (!html) {
		axil_header_set(
		        fd, "Content-Type", "text/plain; charset=utf-8");
		axil_respond(fd, 500, "HTML render failed");
		return 0;
	}
	char page[8192];
	snprintf(
	        page,
	        sizeof(page),
	        "<!DOCTYPE html>\n"
	        "<html lang=\"en\">\n"
	        "<head>\n"
	        "<title>Bud Demo</title>\n"
	        "<meta charset=\"utf-8\">\n"
	        "<style>"
	        "body{font-family:sans-serif;padding:2rem}"
	        ".card{border:1px solid #ccc;padding:1rem;border-radius:8px}"
	        "button{font-size:1.2rem;padding:.5rem 1rem;cursor:pointer}"
	        "</style>\n"
	        "<script type=\"module\" src=\"/bud_demo.js\"></script>\n"
	        "</head>\n"
	        "<body>\n%s\n</body>\n</html>",
	        html);
	axil_header_set(fd, "Content-Type", "text/html");
	axil_respond(fd, 200, page);
	bud_free_string(html);
	return 0;
}

void ndx_install(void)
{
	axil_register_handler("GET:/bud-demo", handler);
}
