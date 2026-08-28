#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <bud/bud.h>
#include "../../mods/common/ux/site_media.c"

#define CHECK(label, condition)                                                \
	do {                                                                   \
		if (condition)                                                 \
			printf("PASS: %s\n", label);                           \
		else {                                                         \
			printf("FAIL: %s\n", label);                           \
			failures++;                                            \
		}                                                              \
	} while (0)

static int failures = 0;

int main(void)
{
	printf("=== Testing site_media (YouTube & PDF conditional buttons) ===\n");

	/* 1. Neither YT nor PDF nor audio -> returns NULL / empty */
	{
		bud_node *node = site_ui_render_media_slot("", "", "");
		CHECK("render_media_slot empty strings returns NULL", node == NULL);

		node = site_ui_render_media_slot(NULL, NULL, NULL);
		CHECK("render_media_slot NULL pointers returns NULL", node == NULL);

		char out[1024] = { 0 };
		int has = site_ui_build_media_html("", "", "", out, sizeof(out));
		CHECK("build_media_html empty strings returns 0", has == 0 && out[0] == '\0');

		has = site_ui_build_media_html(NULL, NULL, NULL, out, sizeof(out));
		CHECK("build_media_html NULL pointers returns 0", has == 0 && out[0] == '\0');
	}

	/* 2. Valid YT ID only, empty PDF string -> ONLY YT button, NO PDF button */
	{
		const char *yt = "dQw4w9WgXcQ";
		const char *pdf = "";
		bud_node *node = site_ui_render_media_slot(yt, "", pdf);
		CHECK("render_media_slot with valid YT returns non-NULL", node != NULL);
		if (node) {
			char *html = bud_render_html(node);
			CHECK("rendered HTML has YouTube button",
			      html && strstr(html, "https://www.youtube.com/watch?v=dQw4w9WgXcQ") != NULL);
			CHECK("rendered HTML has Watch on YouTube title",
			      html && strstr(html, "title=\"Watch on YouTube\"") != NULL);
			CHECK("rendered HTML DOES NOT have PDF button",
			      html && strstr(html, "View PDF") == NULL && strstr(html, "href=\"\"") == NULL);
			if (html)
				bud_free_string(html);
		}

		char out[1024] = { 0 };
		int has = site_ui_build_media_html(yt, "", pdf, out, sizeof(out));
		CHECK("build_media_html with valid YT returns 1", has == 1);
		CHECK("build_media_html has YouTube button",
		      strstr(out, "https://www.youtube.com/watch?v=dQw4w9WgXcQ") != NULL);
		CHECK("build_media_html DOES NOT have PDF button",
		      strstr(out, "View PDF") == NULL && strstr(out, "href=\"\"") == NULL);
	}

	/* 3. Valid full YouTube URL (watch?v=...) -> extracts 11-char ID and renders canonical link */
	{
		const char *yt = "https://www.youtube.com/watch?v=dQw4w9WgXcQ&feature=share";
		bud_node *node = site_ui_render_media_slot(yt, NULL, NULL);
		CHECK("render_media_slot with YouTube watch URL returns non-NULL", node != NULL);
		if (node) {
			char *html = bud_render_html(node);
			CHECK("rendered HTML has canonical watch URL",
			      html && strstr(html, "https://www.youtube.com/watch?v=dQw4w9WgXcQ") != NULL);
			CHECK("rendered HTML does NOT have PDF button",
			      html && strstr(html, "View PDF") == NULL);
			if (html)
				bud_free_string(html);
		}

		char out[1024] = { 0 };
		int has = site_ui_build_media_html(yt, NULL, NULL, out, sizeof(out));
		CHECK("build_media_html with YouTube watch URL returns 1", has == 1);
		CHECK("build_media_html has canonical watch URL",
		      strstr(out, "https://www.youtube.com/watch?v=dQw4w9WgXcQ") != NULL);
	}

	/* 4. Valid youtu.be short URL -> extracts 11-char ID */
	{
		const char *yt = "https://youtu.be/dQw4w9WgXcQ";
		bud_node *node = site_ui_render_media_slot(yt, NULL, NULL);
		CHECK("render_media_slot with youtu.be URL returns non-NULL", node != NULL);
		if (node) {
			char *html = bud_render_html(node);
			CHECK("rendered HTML has canonical watch URL from youtu.be",
			      html && strstr(html, "https://www.youtube.com/watch?v=dQw4w9WgXcQ") != NULL);
			if (html)
				bud_free_string(html);
		}

		char out[1024] = { 0 };
		int has = site_ui_build_media_html(yt, NULL, NULL, out, sizeof(out));
		CHECK("build_media_html with youtu.be URL returns 1", has == 1);
		CHECK("build_media_html has canonical watch URL from youtu.be",
		      strstr(out, "https://www.youtube.com/watch?v=dQw4w9WgXcQ") != NULL);
	}

	/* 5. Valid YouTube shorts URL -> extracts 11-char ID */
	{
		const char *yt = "https://www.youtube.com/shorts/dQw4w9WgXcQ";
		bud_node *node = site_ui_render_media_slot(yt, NULL, NULL);
		CHECK("render_media_slot with shorts URL returns non-NULL", node != NULL);
		if (node) {
			char *html = bud_render_html(node);
			CHECK("rendered HTML has canonical watch URL from shorts",
			      html && strstr(html, "https://www.youtube.com/watch?v=dQw4w9WgXcQ") != NULL);
			if (html)
				bud_free_string(html);
		}
	}

	/* 6. Invalid YouTube ID/URL -> no YT button */
	{
		const char *bad_yts[] = {
			"not-a-youtube-id",
			"https://example.com/dQw4w9WgXcQ",
			"https://evil.youtube.com.attacker.com/dQw4w9WgXcQ",
			"short",
			"   ",
			"??????????"
		};
		for (size_t i = 0; i < sizeof(bad_yts) / sizeof(bad_yts[0]); i++) {
			bud_node *node = site_ui_render_media_slot(bad_yts[i], NULL, NULL);
			CHECK("bad YT input yields NULL node", node == NULL);
			char out[1024] = { 0 };
			int has = site_ui_build_media_html(bad_yts[i], NULL, NULL, out, sizeof(out));
			CHECK("bad YT input yields 0 and empty html", has == 0 && out[0] == '\0');
		}
	}

	/* 7. Valid PDF only, empty/NULL YT -> ONLY PDF button, NO YT button */
	{
		const char *pdf = "https://example.com/scores/song.pdf";
		bud_node *node = site_ui_render_media_slot("", NULL, pdf);
		CHECK("render_media_slot with valid PDF returns non-NULL", node != NULL);
		if (node) {
			char *html = bud_render_html(node);
			CHECK("rendered HTML has PDF link",
			      html && strstr(html, "https://example.com/scores/song.pdf") != NULL);
			CHECK("rendered HTML has View PDF title",
			      html && strstr(html, "title=\"View PDF\"") != NULL);
			CHECK("rendered HTML DOES NOT have YouTube button",
			      html && strstr(html, "Watch on YouTube") == NULL);
			if (html)
				bud_free_string(html);
		}

		char out[1024] = { 0 };
		int has = site_ui_build_media_html("", NULL, pdf, out, sizeof(out));
		CHECK("build_media_html with valid PDF returns 1", has == 1);
		CHECK("build_media_html has PDF link",
		      strstr(out, "https://example.com/scores/song.pdf") != NULL);
		CHECK("build_media_html DOES NOT have YouTube button",
		      strstr(out, "Watch on YouTube") == NULL);
	}

	/* 8. Invalid PDF URLs (HTTP, javascript, malicious) -> NO PDF button */
	{
		const char *bad_pdfs[] = {
			"http://insecure.example.com/sheet.pdf",
			"javascript:alert(1)",
			"https://example.com/<script>alert(1)</script>",
			"   ",
			"not-a-url"
		};
		for (size_t i = 0; i < sizeof(bad_pdfs) / sizeof(bad_pdfs[0]); i++) {
			bud_node *node = site_ui_render_media_slot(NULL, NULL, bad_pdfs[i]);
			CHECK("bad PDF yields NULL node", node == NULL);
			char out[1024] = { 0 };
			int has = site_ui_build_media_html(NULL, NULL, bad_pdfs[i], out, sizeof(out));
			CHECK("bad PDF yields 0 and empty html", has == 0 && out[0] == '\0');
		}
	}

	/* 9. Both valid YT and valid PDF -> Both buttons rendered */
	{
		const char *yt = "dQw4w9WgXcQ";
		const char *pdf = "https://example.com/sheet.pdf";
		bud_node *node = site_ui_render_media_slot(yt, NULL, pdf);
		CHECK("both valid returns non-NULL", node != NULL);
		if (node) {
			char *html = bud_render_html(node);
			CHECK("both: has YouTube button",
			      html && strstr(html, "https://www.youtube.com/watch?v=dQw4w9WgXcQ") != NULL);
			CHECK("both: has PDF button",
			      html && strstr(html, "https://example.com/sheet.pdf") != NULL);
			if (html)
				bud_free_string(html);
		}

		char out[1024] = { 0 };
		int has = site_ui_build_media_html(yt, NULL, pdf, out, sizeof(out));
		CHECK("both: build_media_html returns 1", has == 1);
		CHECK("both: build_media_html has YouTube",
		      strstr(out, "https://www.youtube.com/watch?v=dQw4w9WgXcQ") != NULL);
		CHECK("both: build_media_html has PDF",
		      strstr(out, "https://example.com/sheet.pdf") != NULL);
	}

	printf("\nTotal failures: %d\n", failures);
	return failures > 0 ? 1 : 0;
}
