EXTRA_CFLAGS += -I../../external/bud/include
EXTRA_LDLIBS += -L../../external/bud/lib -lbud

$(TARGET): ux/site_ui.c ux/site_ui.h ux/site_paths.c ux/site_layout.c ux/site_forms.c ux/site_media.c ux/site_page.c ux/site_chrome.c ux/site_chrome.h viewer_zoom.h common_encoding.c common_response.c common_storage.c common_strlist.c

include ../../build.mk
