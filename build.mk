include $(HOME)/mk/portable.mk

REPO_ROOT != cd ../.. && pwd
MOD_NAME != basename "$$(pwd)"
TARGET_AUTO != case "$$(pwd)" in */modules/*) printf 'be/nd_%s.%s\n' "$$(basename "$$(pwd)")" "${SO}";; *) printf '%s.%s\n' "$$(basename "$$(pwd)")" "${SO}";; esac
SRC_AUTO != case "$$(pwd)" in */modules/*) printf 'be/nd_%s.c\n' "$$(basename "$$(pwd)")";; *) printf '%s.c\n' "$$(basename "$$(pwd)")";; esac
PICFLAGS-Unix = -fPIC
PICFLAGS = ${PICFLAGS-${SYS}}

TARGET ?= $(TARGET_AUTO)
SRC ?= $(SRC_AUTO)
MAKEFILE_DEPS = Makefile $(REPO_ROOT)/build.mk $(REPO_ROOT)/mods/ssr/ssr_ffi.h

CC ?= clang

CFLAGS += -g -O0 $(PICFLAGS)
CFLAGS += $(EXTRA_CFLAGS)

LDFLAGS += -shared

LDLIBS += -lndc -lqmap
LDLIBS += $(EXTRA_LDLIBS)
LDLIBS += $(EXTRA_LDLIBS-$(uname))

all: dirs $(TARGET)

dirs:
	@for d in $(DIRS); do mkdir -p $(REPO_ROOT)/$$d 2>/dev/null || true; done

$(TARGET): $(SRC) $(MAKEFILE_DEPS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SRC) $(LDLIBS)

clean:
	rm -f $(TARGET)

distclean: clean

.PHONY: all clean distclean
