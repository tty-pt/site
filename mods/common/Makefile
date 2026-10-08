BUD_DIR = $(REPO_ROOT)/external/libbud
STOMA_DIR = $(REPO_ROOT)/external/libstoma
HYLE_BUD_DIR = $(REPO_ROOT)/external/libhyle-bud
HYLE_SOURCE_DIR = $(REPO_ROOT)/external/libhyle-source
EXTRA_CFLAGS += -I../../external/libbud/include -I$(HYLE_BUD_DIR)/include -I$(STOMA_DIR)/include
EXTRA_LDLIBS += -lbud
EXTRA_LDLIBS += -lstoma
EXTRA_LDLIBS += -lhyle-bud
EXTRA_LDLIBS += -lhyle-source

include ../../build.mk

VERSION_GEN = ux/version.gen.h
$(VERSION_GEN): ../../htdocs/styles.css ../../htdocs/hyle.css ../../htdocs/bud-client.js ../../htdocs/bud-hydrate.js ../../htdocs/hyle-fragments.js ../../scripts/gen-asset-version.sh
	sh ../../scripts/gen-asset-version.sh

$(TARGET): $(VERSION_GEN) ux/site_ui.c ux/site_ui.h ux/site_paths.c ux/site_layout.c ux/site_forms.c ux/site_media.c ux/site_page.c ux/site_chrome.c ux/site_chrome.h viewer_zoom.h common_encoding.c common_response.c common_storage.c common_strlist.c
