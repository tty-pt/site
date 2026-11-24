HYLE_DIR = $(REPO_ROOT)/external/libhyle
HYLE_BUD_DIR = $(REPO_ROOT)/external/libhyle-bud
PITCH_DIR = $(REPO_ROOT)/external/libpitch
EXTRA_CFLAGS += -I../common -I$(PITCH_DIR)/include -I$(HYLE_BUD_DIR)/include -I$(REPO_ROOT)/external/axil-hyle/include
EXTRA_LDLIBS += -lbud -lpitch
EXTRA_LDLIBS += -lhyle
EXTRA_LDLIBS += -lhyle-bud
EXTRA_LDLIBS += -laxil-hyle
LIST_UX_DEPS = ../index/ux/list.c ../index/ux/list_state.h \
	../index/ux/list_query.c ../index/ux/list_filters.c \
	../index/ux/list_layout.c ../index/ux/list_json.c \
	../index/ux/list_render.c

include ../../build.mk

$(TARGET): $(LIST_UX_DEPS)
