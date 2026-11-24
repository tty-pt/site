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
	assert(strstr(html, "id=\"term\"") != NULL);
	assert(strstr(html, "data-bud-terminal") != NULL);   /* live xterm host in floating RPG chat */
	assert(strstr(html, "<nd-stage") != NULL);          /* a tag for the game stage */
	assert(strstr(html, "id=\"seven-shader-status\"") != NULL);
	assert(strstr(html, "Compiling shaders...") != NULL);
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
		assert(strstr(ov_tag, "top:12px") && strstr(ov_tag, "right:12px") && strstr(ov_tag, "bottom:76px"));
		free(ov_tag);
		assert(map > ov);
		const char *rh = strstr(html, "id=\"room-header\"");
		const char *bra = strstr(html, "id=\"bottom-right-actions\"");
		assert(rh != NULL && rh < ov);
		assert(bra != NULL && bra > ov);
		assert(strstr(html, "max-h-[36vh]"));
		assert(strstr(html, "id=\"chat-resize-ne\""));
		assert(strstr(html, "id=\"hud-win-map\""));
		assert(strstr(html, "id=\"hud-win-target\"") == NULL);
		assert(strstr(html, "id=\"hud-win-contents\""));
		assert(strstr(html, "id=\"bottom-left-windows\""));
		assert(strstr(html, "hud-win-toolbar"));
		assert(strstr(html, "data-bud-persist-key=\"nd-win-chat\""));
		assert(strstr(html, "data-bud-persist-key=\"nd-win-title\""));
		assert(strstr(html, "data-bud-persist-key=\"nd-win-map\""));
		assert(strstr(html, "data-bud-persist-key=\"nd-win-target\"") == NULL);
		assert(strstr(html, "data-bud-persist-key=\"nd-win-contents\""));
		assert(strstr(html, "data-bud-persist-key=\"nd-win-actions\"") == NULL);
		assert(strstr(html, "id=\"actions-toolbar\"") == NULL);
		assert(strstr(html, "id=\"toggle-3d-btn\"") == NULL);
		assert(strstr(html, "data-bud-action=\"cycle-viewport\"") == NULL);
		assert(strstr(html, "equip-socket"));
		assert(strstr(html, "rpg-stat-row"));
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

	/* User-selected Seven driver survives ALLOW_CHOICE ND_BCP_REGION frames,
	 * while ND_REGION_LOCKED still overrides user choice. */
	{
		extern void wasm_ws_on_channel(uint8_t channel, const uint8_t *buf, size_t len);
		const char *init = "{\"preferred_driver\":\"lamina-seven\",\"preferred_map_driver\":\"lamina\",\"layout_mode\":3}";
		assert(nd_stl_init(init, (int)strlen(init)) == 0);
		assert(g_user_driver_override == 1);
		assert(strcmp(client_state.preferred_driver, "lamina-seven") == 0);
		assert(client_state.layout_mode == ND_LAYOUT_GAME_2D);

		uint8_t rframe[128];
		nd_bcp_writer_t w;
		nd_bcp_writer_init(&w, rframe, sizeof(rframe));
		nd_bcp_write_u8(&w, ND_BCP_MAGIC_0);
		nd_bcp_write_u8(&w, ND_BCP_MAGIC_1);
		nd_bcp_write_u8(&w, ND_BCP_REGION);
		nd_bcp_write_str(&w, "tty");
		nd_bcp_write_str(&w, "ascii-bud");
		nd_bcp_write_u8(&w, ND_LAYOUT_STANDARD_HYBRID);
		nd_bcp_write_u8(&w, ND_REGION_ALLOW_CHOICE | ND_REGION_UNIVERSE_OWNER);
		size_t rn = nd_bcp_writer_len(&w, rframe);
		wasm_ws_on_channel(1, rframe, rn);
		assert(strcmp(client_state.preferred_driver, "lamina-seven") == 0);
		assert(client_state.layout_mode == ND_LAYOUT_GAME_2D);
		assert((client_state.pres_flags & ND_REGION_UNIVERSE_OWNER) != 0);

		/* Locked region must still override user choice */
		rframe[rn - 1] = ND_REGION_LOCKED;
		wasm_ws_on_channel(1, rframe, rn);
		assert(strcmp(client_state.preferred_driver, "tty") == 0);
		assert(client_state.layout_mode == ND_LAYOUT_STANDARD_HYBRID);
		printf("PASS: user-selected viewport driver survives ALLOW_CHOICE ND_BCP_REGION and respects LOCKED\n");
	}

	printf("PASS: seven stage and mini-map canvases are distinct-size, bridge-routable\n");
	return 0;
}
