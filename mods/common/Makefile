BUD_DIR = $(REPO_ROOT)/external/bud
STOMA_DIR = $(REPO_ROOT)/external/stoma
HYLE_DIR = $(REPO_ROOT)/external/hyle
EXTRA_CFLAGS += -I../../external/bud/include -I$(HYLE_DIR)/c/libhyle-bud/include -I$(STOMA_DIR)/include
EXTRA_LDLIBS += -L$(BUD_DIR)/lib -lbud -Wl,-rpath,$(BUD_DIR)/lib
EXTRA_LDLIBS += -L$(STOMA_DIR)/lib -lstoma -Wl,-rpath,$(STOMA_DIR)/lib
EXTRA_LDLIBS += -L$(HYLE_DIR)/c/libhyle-bud/lib -lhyle-bud -Wl,-rpath,$(HYLE_DIR)/c/libhyle-bud/lib
EXTRA_LDLIBS += -L$(HYLE_DIR)/c/libhyle-source/lib -lhyle-source -Wl,-rpath,$(HYLE_DIR)/c/libhyle-source/lib

include ../../build.mk

VERSION_GEN = ux/version.gen.h
$(VERSION_GEN): ../../htdocs/styles.css ../../htdocs/hyle.css ../../htdocs/bud-client.js ../../htdocs/bud-hydrate.js ../../htdocs/hyle-fragments.js ../../scripts/gen-asset-version.sh
	sh ../../scripts/gen-asset-version.sh

$(TARGET): $(VERSION_GEN) ux/site_ui.c ux/site_ui.h ux/site_paths.c ux/site_layout.c ux/site_forms.c ux/site_media.c ux/site_page.c ux/site_chrome.c ux/site_chrome.h viewer_zoom.h common_encoding.c common_response.c common_storage.c common_strlist.c
