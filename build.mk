include $(HOME)/mk/portable.mk

REPO_ROOT != cd ../.. && pwd
MOD_NAME != basename "$$(pwd)"
TARGET_AUTO != case "$$(pwd)" in */modules/*) printf 'be/nd_%s.%s\n' "$$(basename "$$(pwd)")" "${SO}";; *) printf '%s.%s\n' "$$(basename "$$(pwd)")" "${SO}";; esac
SRC_AUTO != case "$$(pwd)" in */modules/*) printf 'be/nd_%s.c\n' "$$(basename "$$(pwd)")";; *) printf '%s.c\n' "$$(basename "$$(pwd)")";; esac
PICFLAGS-Unix = -fPIC
PICFLAGS = ${PICFLAGS-${SYS}}

TARGET ?= $(TARGET_AUTO)
SRC ?= $(SRC_AUTO)
MAKEFILE_DEPS = Makefile $(REPO_ROOT)/build.mk

CC ?= clang

CFLAGS += -g -O0 $(PICFLAGS)
CFLAGS += -I$(REPO_ROOT)/external/axil/include -I$(REPO_ROOT)/external/qmap/include
CFLAGS += $(EXTRA_CFLAGS)

LDFLAGS += -shared
LDFLAGS += -L$(REPO_ROOT)/external/axil/lib -L$(REPO_ROOT)/external/qmap/lib
LDFLAGS += -L$(REPO_ROOT)/external/axil-auth/lib

LDLIBS += -laxil -lqmap
LDLIBS += $(EXTRA_LDLIBS)
LDLIBS += $(EXTRA_LDLIBS-$(uname))

WASM_PATH ?= $(REPO_ROOT)/htdocs
WASI_CC      ?= clang
WASI_SYSROOT ?=
WASM_CFLAGS  ?= -O2 -D__wasm__ --target=wasm32-wasi
WASM_LDFLAGS ?= -mexec-model=reactor -Wl,--export-all -Wl,--allow-undefined
WASM_COMMON_SRC   = $(REPO_ROOT)/external/bud/src/libbud.c $(REPO_ROOT)/external/bud/src/bud_wasm_app.c
WASM_COMMON_CFLAGS = -I$(REPO_ROOT)/external/bud/include

all: dirs $(TARGET) $(WASM_TARGETS)

$(WASM_PATH)/%.wasm:
	@if echo 'int main(void){}' | $(WASI_CC) $(WASM_CFLAGS) -x c - -c -o /dev/null >/dev/null 2>&1; then \
		$(WASI_CC) $($*-cflags) $(WASM_COMMON_CFLAGS) $(WASM_CFLAGS) $(WASM_LDFLAGS) -o $@ $($*-src) $(WASM_COMMON_SRC); \
	else \
		echo "Skipping WASM build of $@ — install wasi-sdk or configure WASI_CC"; \
	fi

dirs:
	@for d in $(DIRS); do mkdir -p $(REPO_ROOT)/$$d 2>/dev/null || true; done

$(TARGET): $(SRC) $(MAKEFILE_DEPS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SRC) $(LDLIBS)

wasm: $(WASM_TARGETS)

clean:
	rm -f $(TARGET) $(WASM_TARGETS)

distclean: clean

.PHONY: all clean distclean wasm
