REPO_ROOT ?= $(shell cd ../.. && pwd)

CC ?= cc
AR ?= ar

FT_CFLAGS := $(shell pkg-config --cflags freetype2 2>/dev/null || echo -I/usr/include/freetype2)
FT_LDLIBS := $(shell pkg-config --libs freetype2 2>/dev/null || echo -lfreetype)

CFLAGS ?= -O2 -g -Wall -Wextra -Wpedantic
ALL_CFLAGS = $(CFLAGS) -DLAM_WEBGL_NATIVE -DLAM_NATIVE_CLIENT -DAXIL_WS_NATIVE \
	-I. \
	-I$(REPO_ROOT)/external/axil/include \
	-I$(REPO_ROOT)/external/libbud/include \
	-I$(REPO_ROOT)/external/liblamina/include \
	-I$(REPO_ROOT)/external/libcorm/include \
	-I$(REPO_ROOT)/external/libstele/include \
	$(FT_CFLAGS)

LDFLAGS = -L$(REPO_ROOT)/external/liblamina/lib \
	-L$(REPO_ROOT)/external/libbud/lib \
	-L$(REPO_ROOT)/lib
LDLIBS = -llamina -lbud -lcorm -lstele -lxxhash -lpng $(FT_LDLIBS) \
	-lGL -lGLEW -lglfw -lEGL -lX11 -lm

LIB_OBJS = bud_lamina.o term_ansi.o axil_ws_native.o
LIB = libbud-lamina.a

.PHONY: all clean test test-asan

all: $(LIB)

$(LIB): $(LIB_OBJS)
	$(AR) rcs $@ $(LIB_OBJS)

%.o: %.c bud_lamina.h term_ansi.h axil_ws_native.h
	$(CC) $(ALL_CFLAGS) -c -o $@ $<

test_bud_lamina: test_bud_lamina.c $(LIB)
	$(CC) $(ALL_CFLAGS) -o $@ test_bud_lamina.c $(LIB) $(LDFLAGS) $(LDLIBS)

test_ws_native: test_ws_native.c axil_ws_native.o
	$(CC) $(ALL_CFLAGS) -o $@ test_ws_native.c axil_ws_native.o

test: test_bud_lamina test_ws_native
	./test_ws_native
	LD_LIBRARY_PATH=$(REPO_ROOT)/external/liblamina/lib:$(REPO_ROOT)/external/libbud/lib:$(REPO_ROOT)/lib ./test_bud_lamina

test-asan: bud_lamina.c term_ansi.c test_bud_lamina.c bud_lamina.h term_ansi.h
	$(CC) -O1 -g -Wall -Wextra -Wpedantic -fsanitize=address,undefined -fno-omit-frame-pointer \
		-DLAM_WEBGL_NATIVE -DLAM_NATIVE_CLIENT -DAXIL_WS_NATIVE \
		-I. \
		-I$(REPO_ROOT)/external/axil/include \
		-I$(REPO_ROOT)/external/libbud/include \
		-I$(REPO_ROOT)/external/liblamina/include \
		-I$(REPO_ROOT)/external/libcorm/include \
		-I$(REPO_ROOT)/external/libstele/include \
		$(FT_CFLAGS) \
		-o test_bud_lamina_asan test_bud_lamina.c bud_lamina.c term_ansi.c $(LDFLAGS) $(LDLIBS)
	ASAN_OPTIONS=detect_leaks=1 LSAN_OPTIONS=suppressions=/dev/null \
		LD_LIBRARY_PATH=$(REPO_ROOT)/external/liblamina/lib:$(REPO_ROOT)/external/libbud/lib:$(REPO_ROOT)/lib \
		./test_bud_lamina_asan

clean:
	rm -f $(LIB_OBJS) $(LIB) test_bud_lamina test_bud_lamina_asan test_ws_native
