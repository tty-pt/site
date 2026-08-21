EXTRA_CFLAGS += -I../../external/bud/include
EXTRA_LDLIBS += -L../../external/bud/lib -lbud

include ../../build.mk

VERSION_GEN = ux/version.gen.h
$(VERSION_GEN): ../../htdocs/styles.css ../../htdocs/hyle.css ../../htdocs/bud-client.js ../../htdocs/bud-hydrate.js ../../scripts/gen-asset-version.sh
	sh ../../scripts/gen-asset-version.sh

$(TARGET): $(VERSION_GEN) ux/site_ui.c ux/site_ui.h ux/site_paths.c ux/site_layout.c ux/site_forms.c ux/site_media.c ux/site_page.c ux/site_chrome.c ux/site_chrome.h viewer_zoom.h common_encoding.c common_response.c common_storage.c common_strlist.c bud_adapter.c bud_adapter.h
