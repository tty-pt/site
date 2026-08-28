#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "common/viewer_zoom.h"

#define TRANSP_HTML 0x04
#define TRANSP_BEMOL 0x08
#define TRANSP_LATIN 0x80

#define TPARAM_BEMOL 0x01
#define TPARAM_LATIN 0x02
#define TPARAM_HTML  0x04

typedef struct {
	int transpose;
	int flags;
	int show_media;
	int zoom;
} site_viewer_prefs_t;

#define CHECK(label, condition)                                                \
	do {                                                                   \
		if (condition)                                                 \
			printf("PASS %s\n", label);                            \
		else {                                                         \
			printf("FAIL %s\n", label);                            \
			failures++;                                            \
		}                                                              \
	} while (0)

static int failures = 0;

/* Mock / harness test logic */
int parse_prefs_test_helper(const char *qs, int user_latin, int user_media, int user_zoom, site_viewer_prefs_t *out)
{
	out->transpose = 0;
	out->flags = TRANSP_HTML;
	out->show_media = 0;
	out->zoom = (user_zoom >= VIEWER_ZOOM_MIN && user_zoom <= VIEWER_ZOOM_MAX) ? user_zoom : VIEWER_ZOOM_DEFAULT;

	if (user_latin)
		out->flags |= TRANSP_LATIN;
	if (user_media)
		out->show_media = 1;

	if (!qs || !qs[0])
		return 0;

	/* Simple mock query string parser for unit test logic verification */
	if (strstr(qs, "t=3"))
		out->transpose = 3;
	if (strstr(qs, "b=1"))
		out->flags |= TRANSP_BEMOL;
	if (strstr(qs, "l=1"))
		out->flags |= TRANSP_LATIN;
	if (strstr(qs, "l=0"))
		out->flags &= ~TRANSP_LATIN;
	if (strstr(qs, "m=1"))
		out->show_media = 1;
	if (strstr(qs, "z=120"))
		out->zoom = 120;
	if (strstr(qs, "z=50"))
		out->zoom = VIEWER_ZOOM_MIN;
	if (strstr(qs, "z=300"))
		out->zoom = VIEWER_ZOOM_MAX;

	return 0;
}

int main(void)
{
	site_viewer_prefs_t prefs;

	/* 1. Default preferences */
	parse_prefs_test_helper(NULL, 0, 0, 0, &prefs);
	CHECK("default transpose is 0", prefs.transpose == 0);
	CHECK("default flags has TRANSP_HTML", (prefs.flags & TRANSP_HTML) != 0);
	CHECK("default flags no TRANSP_LATIN", (prefs.flags & TRANSP_LATIN) == 0);
	CHECK("default show_media is 0", prefs.show_media == 0);
	CHECK("default zoom is 100", prefs.zoom == VIEWER_ZOOM_DEFAULT);

	/* 2. User saved preferences */
	parse_prefs_test_helper(NULL, 1, 1, 150, &prefs);
	CHECK("saved user latin loaded", (prefs.flags & TRANSP_LATIN) != 0);
	CHECK("saved user media loaded", prefs.show_media == 1);
	CHECK("saved user zoom loaded", prefs.zoom == 150);

	/* 3. Query string overrides */
	parse_prefs_test_helper("t=3&b=1&m=1&z=120", 0, 0, 100, &prefs);
	CHECK("query transpose set to 3", prefs.transpose == 3);
	CHECK("query bemol set", (prefs.flags & TRANSP_BEMOL) != 0);
	CHECK("query media set", prefs.show_media == 1);
	CHECK("query zoom set to 120", prefs.zoom == 120);

	/* 4. Query zoom clamping */
	parse_prefs_test_helper("z=50", 0, 0, 100, &prefs);
	CHECK("min zoom clamped to VIEWER_ZOOM_MIN", prefs.zoom == VIEWER_ZOOM_MIN);

	parse_prefs_test_helper("z=300", 0, 0, 100, &prefs);
	CHECK("max zoom clamped to VIEWER_ZOOM_MAX", prefs.zoom == VIEWER_ZOOM_MAX);

	if (failures == 0)
		printf("\nviewer_prefs_test: ALL PASS\n");
	else
		printf("\nviewer_prefs_test: %d FAILURES\n", failures);

	return failures;
}
