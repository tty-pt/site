#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "nd_ui_internal.h"

/* Real (unmocked) lamina-seven driver in the game-2d layout: the stage canvas
 * and the mini-map canvas must carry DIFFERENT intrinsic sizes, because the
 * lamina bridge routes each flushed frame to the canvas of matching size. */
int main(void)
{
	nd_client_state_t st;
	memset(&st, 0, sizeof(st));
	strcpy(st.user, "hero");
	strcpy(st.preferred_driver, "lamina-seven");
	strcpy(st.preferred_map_driver, "lamina");
	st.layout_mode = ND_LAYOUT_GAME_2D;

	char *html = bud_render_html(nd_ui_render(&st));
	assert(html);
	char want_stage[96], want_map[96];
	snprintf(want_stage, sizeof(want_stage), "width=\"%d\"", ND_CANVAS_W);
	snprintf(want_map, sizeof(want_map), "width=\"%d\"", ND_MAP_CANVAS_W);

	const char *stage = strstr(html, "id=\"seven-stage-canvas\"");
	const char *map = strstr(html, "id=\"room-map-canvas\"");
	assert(stage && map);
	assert(strstr(html, "game-2d-mode"));
	assert(strstr(html, "id=\"term\"") == NULL);
	assert(strstr(html, "data-bud-terminal") == NULL);   /* no live xterm host left */
	assert(strstr(html, "<nd-stage") != NULL);          /* a tag the terminal <div> is replaced by */
	/* each canvas tag carries its own size attribute */
	const char *tag_end = strchr(stage, '>');
	char *s_tag = strndup(stage, (size_t)(tag_end - stage));
	assert(strstr(s_tag, "data-bud-canvas") && strstr(s_tag, want_stage));
	/* the stage follows the page: the bridge resizes it, CSS fills the holder */
	assert(strstr(s_tag, "data-bud-canvas-fit"));
	assert(strstr(s_tag, "w-full") && strstr(s_tag, "h-full"));
	assert(strstr(s_tag, "w-[640px]") == NULL && strstr(s_tag, "border-2") == NULL);
	free(s_tag);
	tag_end = strchr(map, '>');
	char *m_tag = strndup(map, (size_t)(tag_end - map));
	assert(strstr(m_tag, "data-bud-canvas") && strstr(m_tag, want_map));
	assert(strstr(m_tag, "data-bud-canvas-fit") == NULL);   /* the mini-map keeps its own size */
	free(m_tag);
	assert(ND_CANVAS_W != ND_MAP_CANVAS_W && ND_CANVAS_H != ND_MAP_CANVAS_H);
	/* the mini-map is an overlay on top of the stage, not beside it */
	{
		const char *ov = strstr(html, "id=\"map-overlay\"");
		assert(ov && ov > stage);
		const char *ov_end = strchr(ov, '>');
		char *ov_tag = strndup(ov, (size_t)(ov_end - ov));
		assert(strstr(ov_tag, "absolute") && strstr(ov_tag, "z-10"));
		assert(strstr(ov_tag, "top:12px") && strstr(ov_tag, "right:12px"));
		free(ov_tag);
		assert(map > ov);
		assert(strstr(html, "game-2d-mode relative"));
	}

	bud_free_string(html);

	/* A non-seven stage keeps the terminal. */
	strcpy(st.preferred_driver, "tty");
	st.layout_mode = ND_LAYOUT_STANDARD_HYBRID;
	html = bud_render_html(nd_ui_render(&st));
	assert(strstr(html, "id=\"seven-stage-canvas\"") == NULL);
	assert(strstr(html, "id=\"term\""));
	bud_free_string(html);

	printf("PASS: seven stage and mini-map canvases are distinct-size, bridge-routable\n");
	return 0;
}
