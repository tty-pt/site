/* Replays the LIVE frame stream captured from a real axil daemon
 * (tests/fixtures/nd-move/*.hex, regenerated with
 * ND_FRAME_DUMP=tests/fixtures/nd-move tests/integration/07-nd-move-refresh.sh)
 * through the client exactly as wasm_ws_on_channel does, and checks that the
 * rendered map / room header follow the player's moves. */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

#include "nd/bcp.h"
#include "nd/bcp-meta.h"
#include <bud/bud.h>
#include "nd_scene.h"
#include "nd_ui.h"

int respond_html(int fd, const char *html)
{
	(void)fd;
	(void)html;
	return 0;
}

static nd_client_state_t st;

static int hexval(int c)
{
	return c <= '9' ? c - '0' : (c | 32) - 'a' + 10;
}

/* Feed every frame of a fixture; returns the number of frames decoded. */
static int replay(const char *name)
{
	char path[256], *line = NULL;
	size_t cap = 0;
	int frames = 0;
	snprintf(path, sizeof(path), "tests/fixtures/nd-move/%s.hex", name);
	FILE *f = fopen(path, "r");
	if (!f) {
		fprintf(stderr, "missing fixture %s\n", path);
		exit(1);
	}
	while (getline(&line, &cap, f) > 0) {
		size_t hl = strcspn(line, "\r\n"), n = hl / 2;
		uint8_t *buf = malloc(n + 1);
		for (size_t i = 0; i < n; i++)
			buf[i] = (uint8_t)(hexval(line[2 * i]) << 4 | hexval(line[2 * i + 1]));
		nd_bcp_decode(buf, n, &st);
		free(buf);
		frames++;
	}
	free(line);
	fclose(f);
	return frames;
}

/* The `#room-map` element's inner HTML plus the room header, as rendered. */
static char *render_view(void)
{
	char *html = bud_render_html(nd_ui_render(&st));
	assert(html);
	char *m = strstr(html, "<nd-map id=\"room-map\"");
	assert(m);
	char *e = strstr(m, "</nd-map>");
	assert(e);
	char *h = strstr(html, "id=\"room-header\"");
	assert(h);
	char *he = strchr(h, '<');
	assert(he);
	/* header text after the open tag */
	char *ht = strchr(h, '>');
	size_t hl = ht ? (size_t)(strstr(ht, "</") - ht) : 0;
	size_t ml = (size_t)(e - m);
	char *out = malloc(ml + hl + 2);
	memcpy(out, m, ml);
	memcpy(out + ml, ht, hl);
	out[ml + hl] = '\0';
	bud_free_string(html);
	return out;
}

int main(void)
{
	memset(&st, 0, sizeof(st));

	assert(replay("login") > 5);
	assert(st.map_buf[0]);
	uint32_t room0 = st.room.ref;
	char name0[sizeof(st.room.name)];
	snprintf(name0, sizeof(name0), "%s", st.room.name);
	char *view0 = render_view();
	assert(strstr(view0, "!"));

	assert(replay("move-k") > 3);
	uint32_t room1 = st.room.ref;
	char *view1 = render_view();
	assert(room1 != room0);
	assert(strcmp(view0, view1) != 0);
	printf("PASS: map and room header changed after moving north (#%u -> #%u)\n", room0, room1);

	assert(replay("move-j") > 3);
	char *view2 = render_view();
	assert(st.room.ref == room0);
	assert(strcmp(st.room.name, name0) == 0);
	assert(strcmp(view1, view2) != 0);
	printf("PASS: moving back south restores the original room and refreshes the map\n");

	free(view0);
	free(view1);
	free(view2);
	printf("ALL PASS: test_nd_move_refresh\n");
	return 0;
}
