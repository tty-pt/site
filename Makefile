CC  ?= clang
V   ?= 0
ifeq ($(V),0)
  Q = @
  MAKEFLAGS += --no-print-directory -s
else
  Q =
endif
export V Q
DEV ?= 0
PROFILE ?= dev
WASI_CC ?= $(shell which clang-18 2>/dev/null || which clang 2>/dev/null || echo clang)
export WASI_CC
WASM_CFLAGS ?= --target=wasm32-wasi -fvisibility=hidden -D__wasm__
WASM_COMMON_CFLAGS ?= -Iexternal/libstele/include -Iexternal/libbud/include
WASM_LDFLAGS ?= -mexec-model=reactor -Wl,--gc-sections -Wl,--no-entry \
	-Wl,--export=malloc -Wl,--export=free -Wl,--export=_initialize \
	-Wl,--export=stl_kernel_boot -Wl,--export=stl_kernel_mount -Wl,--export=stl_kernel_mount_bin -Wl,--export=stl_kernel_unmount \
	-Wl,--export=stl_kernel_update -Wl,--export=stl_kernel_action -Wl,--export=stl_kernel_dispatch_event -Wl,--export=stl_kernel_tick -Wl,--export=stl_kernel_has_tick \
	-Wl,--export=stl_kernel_version -Wl,--export=stl_kernel_register_module \
	-Wl,--export=stele_kernel_boot -Wl,--export=stele_kernel_mount -Wl,--export=stele_kernel_unmount \
	-Wl,--export=stele_kernel_update -Wl,--export=stele_kernel_action -Wl,--export=stele_kernel_dispatch_event -Wl,--export=stele_kernel_tick -Wl,--export=stele_kernel_has_tick \
	-Wl,--export=stele_kernel_version -Wl,--export=stele_kernel_register_module \
	-Wl,--export=wasm_fetch_callback

MOD_DIRS != for f in mods/*/Makefile; do [ -f "$$f" ] && dirname "$$f"; done | sort
CLIENT_DIRS != for f in mods/*/client/Makefile; do [ -f "$$f" ] && dirname "$$f"; done | sort

all: stage-assets stele-lib corm-lib xylem-lib lemma-lib joint-lib islet-lib chaff-lib areole-lib sepal-lib hyle-lib pitch-lib bud-lib hyle-source hyle-bud axil-lib axil-auth-lib axil-hyle axil-tty-lib axil-nd-lib nd-libs mods clients site-wasm

.PHONY: check
check: boundary-check

.PHONY: $(MOD_DIRS) $(CLIENT_DIRS)
mods: $(MOD_DIRS)
$(MOD_DIRS):
	$(if $(filter 0,$(V)),@echo "  MOD     $@")
	$(Q)$(MAKE) -C $@

clients: $(CLIENT_DIRS)
$(CLIENT_DIRS):
	$(if $(filter 0,$(V)),@echo "  CLIENT  $@")
	$(Q)$(MAKE) -C $@

site-wasm: htdocs/site.wasm

SITE_WASM_INCLUDES = -Iexternal/libstele/include -Iexternal/libbud/include \
                     -Iexternal/libxylem/include -Iexternal/libpitch/include \
                     -Iexternal/libhyle-bud/include -Iexternal/libhyle/include \
                     -Iexternal/libhyle-source/include -Iexternal/libcorm/include \
                     -Iexternal/axil/include -Iexternal/axil-hyle/include \
                     -I/usr/include/json-c -Imods/common -Imods/common/ux \
                     -Imods/song/include -Imods/gig/include -Imods/index/include -Imods/chat/include

SITE_WASM_BUILD_DIR = /tmp/site_wasm_build_$(shell pwd | cksum | cut -d' ' -f1)
SITE_WASM_OBJS = kernel.o stele_event.o bud.o bud_tpl.o bud_render.o bud_runtime.o bud_json.o site_wasm_app.o site_ui.o xylem.o \
                 modules.o site_chrome.o chrome.o song.o gig.o list.o \
                 chat.o chat_ui.o music.o hyle_bud.o filter.o table.o \
                 picker.o form.o hyle_bud_cache.o url.o

$(SITE_WASM_BUILD_DIR)/kernel.o: external/libstele/src/stele_wasm_kernel.c external/libstele/include/ttypt/stele_wasm.h
	$(if $(filter 0,$(V)),@echo "  WASM    $@")
	$(Q)mkdir -p $(SITE_WASM_BUILD_DIR) && $(WASI_CC) $(WASM_CFLAGS) $(SITE_WASM_INCLUDES) -O2 -c $< -o $@
$(SITE_WASM_BUILD_DIR)/stele_event.o: external/libstele/src/stele_event.c external/libstele/include/ttypt/stele_wasm.h
	$(if $(filter 0,$(V)),@echo "  WASM    $@")
	$(Q)mkdir -p $(SITE_WASM_BUILD_DIR) && $(WASI_CC) $(WASM_CFLAGS) $(SITE_WASM_INCLUDES) -O2 -c $< -o $@
$(SITE_WASM_BUILD_DIR)/bud.o: external/libbud/src/libbud.c external/libbud/src/bud_internal.h
	$(if $(filter 0,$(V)),@echo "  WASM    $@")
	$(Q)mkdir -p $(SITE_WASM_BUILD_DIR) && $(WASI_CC) $(WASM_CFLAGS) $(SITE_WASM_INCLUDES) -O2 -c $< -o $@
$(SITE_WASM_BUILD_DIR)/bud_tpl.o: external/libbud/src/bud_tpl.c external/libbud/src/bud_internal.h
	$(if $(filter 0,$(V)),@echo "  WASM    $@")
	$(Q)mkdir -p $(SITE_WASM_BUILD_DIR) && $(WASI_CC) $(WASM_CFLAGS) $(SITE_WASM_INCLUDES) -O2 -c $< -o $@
$(SITE_WASM_BUILD_DIR)/bud_render.o: external/libbud/src/bud_render.c external/libbud/src/bud_internal.h
	$(if $(filter 0,$(V)),@echo "  WASM    $@")
	$(Q)mkdir -p $(SITE_WASM_BUILD_DIR) && $(WASI_CC) $(WASM_CFLAGS) $(SITE_WASM_INCLUDES) -O2 -c $< -o $@
$(SITE_WASM_BUILD_DIR)/bud_runtime.o: external/libbud/src/bud_runtime.c external/libbud/src/bud_internal.h
	$(if $(filter 0,$(V)),@echo "  WASM    $@")
	$(Q)mkdir -p $(SITE_WASM_BUILD_DIR) && $(WASI_CC) $(WASM_CFLAGS) $(SITE_WASM_INCLUDES) -O2 -c $< -o $@
$(SITE_WASM_BUILD_DIR)/bud_json.o: external/libbud/src/bud_json.c external/libbud/src/bud_internal.h
	$(if $(filter 0,$(V)),@echo "  WASM    $@")
	$(Q)mkdir -p $(SITE_WASM_BUILD_DIR) && $(WASI_CC) $(WASM_CFLAGS) $(SITE_WASM_INCLUDES) -O2 -c $< -o $@
$(SITE_WASM_BUILD_DIR)/site_wasm_app.o: mods/common/site_wasm_app.c mods/common/site_wasm_app.h
	$(if $(filter 0,$(V)),@echo "  WASM    $@")
	$(Q)mkdir -p $(SITE_WASM_BUILD_DIR) && $(WASI_CC) $(WASM_CFLAGS) $(SITE_WASM_INCLUDES) -O2 -c $< -o $@
$(SITE_WASM_BUILD_DIR)/site_ui.o: mods/common/ux/site_ui.c
	$(if $(filter 0,$(V)),@echo "  WASM    $@")
	$(Q)mkdir -p $(SITE_WASM_BUILD_DIR) && $(WASI_CC) $(WASM_CFLAGS) $(SITE_WASM_INCLUDES) -O2 -c $< -o $@
$(SITE_WASM_BUILD_DIR)/xylem.o: external/libxylem/src/libxylem-wasm.c
	$(if $(filter 0,$(V)),@echo "  WASM    $@")
	$(Q)mkdir -p $(SITE_WASM_BUILD_DIR) && $(WASI_CC) $(WASM_CFLAGS) $(SITE_WASM_INCLUDES) -O2 -c $< -o $@
$(SITE_WASM_BUILD_DIR)/modules.o: mods/common/ux/site_modules.c
	$(if $(filter 0,$(V)),@echo "  WASM    $@")
	$(Q)mkdir -p $(SITE_WASM_BUILD_DIR) && $(WASI_CC) $(WASM_CFLAGS) $(SITE_WASM_INCLUDES) -O2 -c $< -o $@
$(SITE_WASM_BUILD_DIR)/site_chrome.o: mods/common/ux/site_chrome.c
	$(if $(filter 0,$(V)),@echo "  WASM    $@")
	$(Q)mkdir -p $(SITE_WASM_BUILD_DIR) && $(WASI_CC) $(WASM_CFLAGS) $(SITE_WASM_INCLUDES) -O2 -c $< -o $@
$(SITE_WASM_BUILD_DIR)/chrome.o: mods/site_chrome/ux/chrome.c mods/common/site_wasm_app.h
	$(if $(filter 0,$(V)),@echo "  WASM    $@")
	$(Q)mkdir -p $(SITE_WASM_BUILD_DIR) && $(WASI_CC) $(WASM_CFLAGS) $(SITE_WASM_INCLUDES) -O2 -c $< -o $@
$(SITE_WASM_BUILD_DIR)/song.o: mods/song/ux/detail.c mods/common/site_wasm_app.h
	$(if $(filter 0,$(V)),@echo "  WASM    $@")
	$(Q)mkdir -p $(SITE_WASM_BUILD_DIR) && $(WASI_CC) $(WASM_CFLAGS) $(SITE_WASM_INCLUDES) -O2 -c $< -o $@
$(SITE_WASM_BUILD_DIR)/gig.o: mods/gig/ux/detail.c mods/common/site_wasm_app.h
	$(if $(filter 0,$(V)),@echo "  WASM    $@")
	$(Q)mkdir -p $(SITE_WASM_BUILD_DIR) && $(WASI_CC) $(WASM_CFLAGS) $(SITE_WASM_INCLUDES) -O2 -c $< -o $@
$(SITE_WASM_BUILD_DIR)/list.o: mods/index/ux/list_fe.c mods/common/site_wasm_app.h
	$(if $(filter 0,$(V)),@echo "  WASM    $@")
	$(Q)mkdir -p $(SITE_WASM_BUILD_DIR) && $(WASI_CC) $(WASM_CFLAGS) $(SITE_WASM_INCLUDES) -O2 -c $< -o $@
$(SITE_WASM_BUILD_DIR)/chat.o: mods/chat/ux/chat_fe.c mods/common/site_wasm_app.h
	$(if $(filter 0,$(V)),@echo "  WASM    $@")
	$(Q)mkdir -p $(SITE_WASM_BUILD_DIR) && $(WASI_CC) $(WASM_CFLAGS) $(SITE_WASM_INCLUDES) -O2 -c $< -o $@
$(SITE_WASM_BUILD_DIR)/chat_ui.o: mods/chat/ux/chat_ui.c
	$(if $(filter 0,$(V)),@echo "  WASM    $@")
	$(Q)mkdir -p $(SITE_WASM_BUILD_DIR) && $(WASI_CC) $(WASM_CFLAGS) $(SITE_WASM_INCLUDES) -O2 -c $< -o $@
$(SITE_WASM_BUILD_DIR)/music.o: external/libpitch/src/music.c
	$(if $(filter 0,$(V)),@echo "  WASM    $@")
	$(Q)mkdir -p $(SITE_WASM_BUILD_DIR) && $(WASI_CC) $(WASM_CFLAGS) $(SITE_WASM_INCLUDES) -O2 -c $< -o $@
$(SITE_WASM_BUILD_DIR)/hyle_bud.o: external/libhyle-bud/src/libhyle-bud.c
	$(if $(filter 0,$(V)),@echo "  WASM    $@")
	$(Q)mkdir -p $(SITE_WASM_BUILD_DIR) && $(WASI_CC) $(WASM_CFLAGS) $(SITE_WASM_INCLUDES) -O2 -c $< -o $@
$(SITE_WASM_BUILD_DIR)/filter.o: external/libhyle-bud/src/filter.c
	$(if $(filter 0,$(V)),@echo "  WASM    $@")
	$(Q)mkdir -p $(SITE_WASM_BUILD_DIR) && $(WASI_CC) $(WASM_CFLAGS) $(SITE_WASM_INCLUDES) -O2 -c $< -o $@
$(SITE_WASM_BUILD_DIR)/table.o: external/libhyle-bud/src/table.c
	$(if $(filter 0,$(V)),@echo "  WASM    $@")
	$(Q)mkdir -p $(SITE_WASM_BUILD_DIR) && $(WASI_CC) $(WASM_CFLAGS) $(SITE_WASM_INCLUDES) -O2 -c $< -o $@
$(SITE_WASM_BUILD_DIR)/picker.o: external/libhyle-bud/src/picker.c
	$(if $(filter 0,$(V)),@echo "  WASM    $@")
	$(Q)mkdir -p $(SITE_WASM_BUILD_DIR) && $(WASI_CC) $(WASM_CFLAGS) $(SITE_WASM_INCLUDES) -O2 -c $< -o $@
$(SITE_WASM_BUILD_DIR)/form.o: external/libhyle-bud/src/form.c
	$(if $(filter 0,$(V)),@echo "  WASM    $@")
	$(Q)mkdir -p $(SITE_WASM_BUILD_DIR) && $(WASI_CC) $(WASM_CFLAGS) $(SITE_WASM_INCLUDES) -O2 -c $< -o $@
$(SITE_WASM_BUILD_DIR)/hyle_bud_cache.o: external/libhyle-bud/src/hyle-bud-cache.c
	$(if $(filter 0,$(V)),@echo "  WASM    $@")
	$(Q)mkdir -p $(SITE_WASM_BUILD_DIR) && $(WASI_CC) $(WASM_CFLAGS) $(SITE_WASM_INCLUDES) -O2 -c $< -o $@
$(SITE_WASM_BUILD_DIR)/url.o: external/libhyle/src/url.c
	$(if $(filter 0,$(V)),@echo "  WASM    $@")
	$(Q)mkdir -p $(SITE_WASM_BUILD_DIR) && $(WASI_CC) $(WASM_CFLAGS) $(SITE_WASM_INCLUDES) -O2 -c $< -o $@

htdocs/site.wasm: $(addprefix $(SITE_WASM_BUILD_DIR)/,$(SITE_WASM_OBJS))
	$(if $(filter 0,$(V)),@echo "  LD.WASM $@")
	$(Q)mkdir -p htdocs
	$(Q)$(WASI_CC) $(WASM_CFLAGS) $(WASM_LDFLAGS) -o $@ $^
	$(Q)sh external/mk/scripts/wasm-audit.sh $@ || { rm -f $@; exit 1; }

# Staged htdocs deploy assets. Exported from each library and module <pkg>/htdocs/.
DEPLOY_ASSETS = styles.css hyle.css bud-cap.js hyle-cap.js axil-cap.js axil-tty-cap.js lamina-cap.js webgl-cap.js xylem-cap.js stele-client.js stele.wasm

stage-assets: htdocs/.assets-staged

htdocs/.assets-staged:
	@mkdir -p htdocs
	@rm -rf node_modules/@ttypt
	@mkdir -p node_modules/@tty-pt
	@ln -sfn ../../external/libstele/npm node_modules/@tty-pt/stele
	@ln -sfn ../../external/libcorm/npm node_modules/@tty-pt/corm
	@ln -sfn ../../external/libjoint/npm node_modules/@tty-pt/joint
	@ln -sfn ../../external/libislet/npm node_modules/@tty-pt/islet
	@ln -sfn ../../external/liblemma/npm node_modules/@tty-pt/lemma
	@ln -sfn ../../external/libsepal/npm node_modules/@tty-pt/sepal
	@ln -sfn ../../external/libhyle/npm node_modules/@tty-pt/hyle
	@ln -sfn ../../external/libbud/npm node_modules/@tty-pt/bud
	@ln -sfn ../../external/libxylem/npm node_modules/@tty-pt/xylem
	@ln -sfn ../../external/libhyle-bud/npm node_modules/@tty-pt/hyle-bud
	@ln -sfn ../../external/libhyle-source/npm node_modules/@tty-pt/hyle-source
	@ln -sfn ../../external/liblamina/npm node_modules/@tty-pt/lamina
	@ln -sfn ../../external/libpitch/npm node_modules/@tty-pt/pitch
	@ln -sfn ../../external/libpitch/npm node_modules/@tty-pt/transp
	@ln -sfn ../../external/libchaff/npm node_modules/@tty-pt/chaff
	@ln -sfn ../../external/libchaff/npm node_modules/@tty-pt/noise
	@ln -sfn ../../external/libareole/npm node_modules/@tty-pt/areole
	@ln -sfn ../../external/libareole/npm node_modules/@tty-pt/chunk
	@ln -sfn ../../external/axil/npm node_modules/@tty-pt/axil
	@if [ -d external/axil-auren/npm ]; then \
		ln -sfn ../../external/axil-auren/npm node_modules/@tty-pt/auren; \
	elif [ -d ../axil-auren/npm ]; then \
		ln -sfn ../../../axil-auren/npm node_modules/@tty-pt/auren; \
	elif [ -d "$(HOME)/.pi/agent/npm/node_modules/@tty-pt/auren" ]; then \
		ln -sfn "$(HOME)/.pi/agent/npm/node_modules/@tty-pt/auren" node_modules/@tty-pt/auren; \
	elif [ -h node_modules/@tty-pt/auren ] && [ ! -e node_modules/@tty-pt/auren ]; then \
		rm -f node_modules/@tty-pt/auren; \
	fi
	@for d in external/*/npm; do \
		if [ -d "$$d/node_modules/@tty-pt" ]; then \
			rm -rf "$$d/node_modules/@tty-pt/stele" && \
			ln -sfn "$$PWD/external/libstele/npm" "$$d/node_modules/@tty-pt/stele"; \
		fi; \
	done
	@for d in external/* mods/*; do \
		if [ -d "$$d/htdocs" ]; then \
			cp -u -R "$$d/htdocs/." htdocs/ 2>/dev/null || true; \
		fi; \
	done
	@for d in external/axil-nd external/nd-*; do \
		if [ -d "$$d/art" ]; then \
			mkdir -p htdocs/nd/art && cp -u -R "$$d/art/." htdocs/nd/art/; \
		fi; \
	done
	@missing=""; \
	for a in $(DEPLOY_ASSETS); do \
		if [ ! -f "htdocs/$$a" ]; then missing="$$missing htdocs/$$a"; fi; \
	done; \
	if [ -n "$$missing" ]; then \
		echo "ERROR: missing deploy assets:$$missing" >&2; \
		exit 1; \
	fi
	@touch $@

assets-sync: stage-assets

EXT_RPATH_LINK := $(if $(filter Linux,$(shell uname -s)),$(foreach d,$(wildcard $(CURDIR)/external/*),-Wl,-rpath-link,$(d)/lib),)
EXT_MAKE_ARGS = add-prefix-glob="../*" LDFLAGS-Unix="$(EXT_RPATH_LINK)"

lemma-lib: stele-lib corm-lib
	$(Q)$(MAKE) -C external/liblemma $(EXT_MAKE_ARGS)

joint-lib: stele-lib corm-lib
	$(Q)$(MAKE) -C external/libjoint $(EXT_MAKE_ARGS)

islet-lib: stele-lib corm-lib
	$(Q)$(MAKE) -C external/libislet $(EXT_MAKE_ARGS)

areole-lib: islet-lib
	$(Q)$(MAKE) -C external/libareole

corolla-lib: chaff-lib
	$(Q)$(MAKE) -C external/libcorolla

chunk-lib: areole-lib

chaff-lib: stele-lib
	$(Q)$(MAKE) -C external/libchaff

noise-lib: chaff-lib

sepal-lib: stele-lib corm-lib
	$(Q)$(MAKE) -C external/libsepal $(EXT_MAKE_ARGS)

pi-mm-zip:
	$(Q)$(MAKE) -C .pi/extensions/pi-mm zip

hyle-lib: lemma-lib corm-lib
	$(Q)$(MAKE) -C external/libhyle $(EXT_MAKE_ARGS)

pitch-lib: stele-lib
	$(Q)$(MAKE) -C external/libpitch $(EXT_MAKE_ARGS)

transp-lib: pitch-lib

bud-lib: stele-lib
	$(Q)$(MAKE) -C external/libbud $(EXT_MAKE_ARGS)

hyle-bud: hyle-lib bud-lib hyle-source
	$(Q)$(MAKE) -C external/libhyle-bud $(EXT_MAKE_ARGS)

hyle-source: hyle-lib corm-lib lemma-lib stele-lib
	$(Q)$(MAKE) -C external/libhyle-source $(EXT_MAKE_ARGS)

stele-lib:
	$(Q)$(MAKE) -C external/libstele $(EXT_MAKE_ARGS)

qsys-lib: stele-lib

axil-lib: stele-lib corm-lib xylem-lib
	$(Q)$(MAKE) -C external/axil $(EXT_MAKE_ARGS)

axil-auth-lib: axil-lib corm-lib xylem-lib
	$(Q)$(MAKE) -C external/axil-auth $(EXT_MAKE_ARGS)

axil-hyle: axil-lib hyle-source axil-auth-lib
	$(Q)$(MAKE) -C external/axil-hyle $(EXT_MAKE_ARGS)

axil-tty-lib: axil-lib corm-lib xylem-lib
	$(Q)$(MAKE) -C external/axil-tty $(EXT_MAKE_ARGS)

axil-nd-lib: axil-tty-lib axil-lib corm-lib xylem-lib islet-lib stele-lib
	$(Q)$(MAKE) -C external/axil-nd $(EXT_MAKE_ARGS)

nd-libs: axil-nd-lib chaff-lib areole-lib
	@$(MAKE) -C external/nd-bus $(EXT_MAKE_ARGS) || exit 1
	@for d in external/nd-*; do \
		[ "$$d" = "external/nd-bus" ] && continue; \
		if [ -f "$$d/Makefile" ]; then $(MAKE) -C "$$d" $(EXT_MAKE_ARGS) || exit 1; fi; \
	done

corm-lib: stele-lib
	$(Q)$(MAKE) -C external/libcorm $(EXT_MAKE_ARGS)

# The external packages own their install: mk/include.mk's `install` target
# copies ${LIB} (plus the bare-soname alias xy_load() addresses), headers and
# the binary into ${DESTDIR}${PREFIX}/lib + bin, with PREFIX taken from
# mk/portable.mk's table. There is no -L into the tree at link time either
# (build.mk), so ${PREFIX}/lib is the one place a library exists: the file the
# link inspects is the file the loader maps and xy_load() fetches. Run this
# with sudo/doas after any library rebuild, then restart axil.
# Keep the table in sync with external/mk/portable.mk:16-29.
UNAME_S := $(shell uname -s)
ARCH_M := $(shell uname -m)
PREFIX ?= $(if $(filter Linux,$(UNAME_S)),/usr, \
	$(if $(filter NetBSD,$(UNAME_S)),/usr/pkg, \
	$(if $(and $(filter Darwin,$(UNAME_S)),$(filter arm64,$(ARCH_M))),/opt/homebrew, \
	/usr/local)))
# Only require sudo if installing to system prefixes (/usr, /opt) and SUDO is unset
SUDO ?= $(if $(filter /usr% /opt%,$(PREFIX)),sudo,)

INSTALL_ND_CORE = nd-bus nd-core nd-obj nd-region nd-world
INSTALL_ND_OTHER = $(filter-out $(INSTALL_ND_CORE),$(patsubst external/%,%,$(wildcard external/nd-*)))

INSTALL_PKGS = libstele libcorm libxylem liblemma libjoint libislet libsepal \
	libhyle libpitch libbud libhyle-source libhyle-bud libchaff libareole \
	axil axil-auth axil-hyle axil-tty axil-nd \
	$(INSTALL_ND_CORE) $(INSTALL_ND_OTHER)

install: install-libs

install-libs:
	@for p in $(INSTALL_PKGS); do \
		$(SUDO) $(MAKE) -C external/$$p $(EXT_MAKE_ARGS) PREFIX="$(PREFIX)" install || exit 1; \
	done
	@echo "runtime libraries + axil installed under $(PREFIX)"

# --- Monorepo Turnkey Node.js & WebAssembly Targets ---
NPM_PACKAGES = external/libstele external/libcorm external/libjoint external/libislet \
               external/libhyle external/liblemma external/libsepal external/libpitch \
               external/libbud external/libxylem external/liblamina external/libhyle-bud \
               external/libhyle-source external/libhyle-react external/libchaff external/libareole external/axil-auren mods/common

npm-build-all:
	@for p in $(NPM_PACKAGES); do \
		echo "=== Building npm package in $$p ==="; \
		$(MAKE) -C $$p npm-build || exit 1; \
	done

npm-test-all: npm-build-all
	@for p in $(NPM_PACKAGES); do \
		echo "=== Testing npm package in $$p ==="; \
		$(MAKE) -C $$p npm-test || exit 1; \
	done
	@echo "=== Testing Single-Instantiation WASM Invariant ==="
	@node --test tests/unit/run-wasm-single-instantiation.mjs || exit 1
	@echo "=== Testing Unified site.wasm Kernel Multi-Slot Invariant ==="
	@node --test tests/unit/test_site_wasm_kernel.mjs || exit 1
	@echo "=== Testing In-WASM Event Bus and Cross-Slot Bridge Invariants ==="
	@node --test tests/unit/test_wasm_event_bus.mjs || exit 1
	@echo "=== Testing Optimistic Mutations & Rollback Invariants ==="
	@node --test tests/unit/test_wasm_optimistic.mjs || exit 1
	@echo "=== Testing On-Demand Lazy WASM Module Streaming Invariant ==="
	@node --test tests/unit/test_wasm_lazy.mjs || exit 1
	@echo "=== Testing Bud Binary State Hand-Off Invariant ==="
	@node --test tests/unit/test_bud_binary_state.mjs || exit 1
	@if [ -d /home/quirinpa/duna ]; then \
		echo "=== Testing consumer: ~/duna ==="; \
		npm --prefix /home/quirinpa/duna run test:unit || exit 1; \
	elif [ -d ../duna ]; then \
		echo "=== Testing consumer: ../duna ==="; \
		npm --prefix ../duna run test:unit || exit 1; \
	else \
		echo "=== Skipping ~/duna (not present in environment) ==="; \
	fi


# The `axil -C` chroot jail: seed what the engine resolves AFTER the chroot
# (it fires on_axil_post_chroot between init_pre_bind() and the first bind),
# then gate the restart on the result. JAIL is normally the site root itself
# -- on prod /var/www both holds the checkout and is the chroot, which is what
# keeps var/ and htdocs/ the same files before and after. The manifest and the
# reason for every entry: scripts/jail-manifest.sh. Run after `make` +
# `make install-libs`; check-jail.sh alone is the pre-restart gate.
JAIL ?= /var/www

install-jail:
	@sh scripts/seed-jail.sh $(JAIL)
	@sh scripts/check-jail.sh $(JAIL)

print-prefix:
	@echo $(PREFIX)

mm: ;

jarvis-lib: stele-lib corm-lib xylem-lib
	$(Q)$(MAKE) -C external/libjarvis $(EXT_MAKE_ARGS)

xylem-lib: stele-lib corm-lib
	$(Q)$(MAKE) -C external/libxylem $(EXT_MAKE_ARGS)

dev:
	@./scripts/dev.sh

run:
	$(Q)$(MAKE) DEV=1 all
	./start.sh

MODS != cat mods.load

test-data-dirs:
	mkdir -p var/poem var/song var/gig var/grp var/software var/chat/sessions var/song.types var/song.authors var/nd
	@sh scripts/seed-software.sh >/dev/null 2>&1 || true

unit-tests: all test-data-dirs
	@./scripts/run-with-server.sh sh -c '\
		for d in $(MODS); do \
			if [ ! -d mods/$$d ]; then \
				echo "=== SKIPPING $$d (no mods/$$d dir -- installed-soname or path-form entry) ==="; \
				continue; \
			fi; \
			echo "=== TESTING $$d ==="; \
			(cd mods/$$d && ./test.sh) || exit 1; \
		done'
	@$(MAKE) standalone-unit-tests

standalone-unit-tests:
	@tmpdir=$$(mktemp -d); \
	trap 'rm -rf "$$tmpdir"' EXIT HUP INT TERM; \
	for test in mpfd_contract_test caller_contract_test; do \
		echo "=== TESTING $$test ==="; \
		$(CC) -Wall -Wextra -Werror -o "$$tmpdir/$$test" \
			"tests/unit/$$test.c" || exit 1; \
		"$$tmpdir/$$test" || exit 1; \
	done
	@sh tests/unit/run-mpfd-content-length.sh
	@sh tests/unit/run-mpfd-multivalue.sh
	@sh tests/unit/run-dsv-legacy.sh
	@sh tests/unit/run-source-options.sh
	@sh tests/unit/run-bud-picker-collect.sh
	@sh tests/unit/run-bud-form.sh
	@sh tests/unit/run-bud-table.sh
	@sh tests/unit/run-source-utils.sh
	@sh tests/unit/run-auth-disk-permissions.sh
	@sh tests/unit/run-axil-auth-groups.sh
	@sh tests/unit/run-axil-auth-account.sh
	@sh tests/unit/run-xy-hook-default.sh
	@sh tests/unit/run-auth-group-permissions.sh
	@sh tests/unit/run-site-media.sh
	@sh tests/unit/run-site-ui-helpers.sh
	@sh tests/unit/run-index-helpers.sh
	@sh tests/unit/run-externals-abstractions.sh
	@sh tests/unit/run-ordered-sync.sh
	@sh tests/unit/run-source-dataset-options.sh
	@sh tests/unit/run-hyle-source-cache.sh
	@sh tests/unit/run-viewer-prefs.sh
	@sh tests/unit/run-slugify.sh
	@sh tests/unit/run-i18n.sh
	@sh tests/unit/run-nd-bcp.sh
	@sh tests/unit/run-nd-table.sh
	@sh tests/unit/run-nd-ext-state.sh
	@sh tests/unit/run-test-bcp-meta.sh
	@sh tests/unit/run-test-stone-roll.sh
	@sh tests/unit/run-test-hyle-table-bin.sh
	@sh tests/unit/run-nd-ui-slot.sh
	@sh tests/unit/run-markdown-test.sh

test-graphics:
	@sh tests/unit/run-test-scene-sync.sh
	@sh tests/unit/run-test-ssr-hydration.sh
	@sh tests/unit/run-test-seven-world.sh
	@sh tests/unit/run-test-map-render.sh
	@sh tests/unit/run-test-seven-render.sh
	@sh tests/unit/run-test-lod-zoom-consistency.sh
	@sh tests/unit/run-test-ground-zoom-lod.sh
	@sh tests/unit/run-test-viewport-registry.sh
	@sh tests/unit/run-test-seven-stage-html.sh
	@sh tests/unit/run-test-lamina-bridge.sh
	@sh tests/unit/run-test-axil-ws-multiplex.sh
	@sh tests/unit/run-test-torus-planetary-renderer.sh
	@sh tests/unit/run-test-areole-chaff-occlusion.sh
	@sh tests/unit/run-xylem-wasm.sh

pages-test: all
	@echo "Running pages smoke tests"
	@./scripts/run-with-server.sh sh -c '\
		sh tests/pages/10-pages-render.sh && \
		sh tests/pages/20-song-search.sh && \
		sh tests/pages/25-list-metadata.sh && \
		sh tests/pages/26-binary-table-streaming.sh && \
		sh tests/pages/27-column-sorting.sh && \
		sh tests/pages/30-song-multiselect.sh && \
		sh tests/pages/40-traversal.sh && \
		sh tests/pages/50-pickers.sh'

unit-c-tests:
	@sh tests/scripts/repro-matrix.sh --build

integration-tests: all
	@./scripts/run-with-server.sh sh tests/integration/run_all.sh

test-mod: all test-data-dirs
	@if [ -z "$(MOD)" ]; then \
		echo "Error: MOD is required. Example: make test-mod MOD=song"; \
		exit 1; \
	fi
	@echo "=== Running targeted tests for module: $(MOD) ==="
	@./scripts/run-with-server.sh sh -c '\
		if [ -f "mods/$(MOD)/test.sh" ]; then (cd mods/$(MOD) && ./test.sh); fi'

test-nd-harvest: test-data-dirs
	@AUTH_SKIP_CONFIRM=1 ./scripts/run-with-server.sh deno test --allow-all tests/e2e/nd-multi-room-harvest.test.ts

test-fast: boundary-check standalone-unit-tests
	@echo "Fast test suite passed!"

test-integration: unit-c-tests pages-test test-nd-harvest test-wasm test-graphics
	@echo "Integration test suite passed!"

test-wasm:
	@echo "=== Running WebAssembly and NPM test suites across external/ ==="
	@for d in stele bud corm joint islet lamina lemma sepal pitch hyle hyle-bud xylem noise chunk; do \
		if [ -d "external/lib$$d/npm" ]; then \
			echo "--> Testing @tty-pt/$$d..."; \
			timeout 30s npm --prefix "external/lib$$d/npm" test || exit 1; \
		fi; \
	done
	@if [ -d "$$HOME/duna" ]; then \
		echo "--> Verifying duna C99 & WASM unit tests..."; \
		(cd "$$HOME/duna" && node --test scripts/unit.test.mjs) || exit 1; \
	fi
	@echo "All external WebAssembly packages and duna verified successfully!"

test-e2e: test-data-dirs
	@if [ -n "$(FILE)" ]; then \
		AUTH_SKIP_CONFIRM=1 ./scripts/run-with-server.sh deno test --allow-all "tests/e2e/$(FILE)"; \
	else \
		AUTH_SKIP_CONFIRM=1 ./scripts/run-with-server.sh deno test --allow-all $(E2E_ARGS) tests/e2e/; \
	fi

e2e-tests: test-data-dirs
	AUTH_SKIP_CONFIRM=1 deno test --allow-all $(E2E_ARGS) tests/e2e/

restart:
	@pkill -f 'axil -C .* mods/core/core' || true
	@sleep 0.5
	@./start.sh &
	@sleep 1
	@curl -s --max-time 3 http://localhost:8080/ > /dev/null 2>&1 || { echo "Failed to start server"; exit 1; }
	@echo "Server restarted on :8080"

test: boundary-check unit-c-tests unit-tests pages-test integration-tests test-e2e

# depends on mods: check-no-tree-rpath inspects built artifacts, which must not
# be mid-relink when the check runs under -j
boundary-check: mods
	sh scripts/check-module-boundaries.sh && sh scripts/check-ux-purity.sh && sh scripts/check-no-site-specific-js.sh && sh scripts/check-wasm-imports.sh && sh external/mk/scripts/wasm-audit.sh htdocs/*.wasm && sh external/mk/scripts/wasm-audit.sh --check-substrate && sh scripts/check-no-tree-rpath.sh && sh scripts/check-no-impl-guards.sh && sh scripts/check-nd-encapsulation.sh && sh scripts/check-nd-no-static-load.sh && sh scripts/check-pkgconfig-versions.sh

watch:
	./scripts/watch.sh

format:
	find mods external/libbud \( -name "*.c" -o -name "*.h" \) | xargs clang-format -i

lint:
	find mods external/libbud -name "*.c" -exec clang-tidy {} -- \
		-Iexternal/axil/include -Iexternal/libcorm/include \
		-Iexternal/libxylem/include -Iexternal/libbud/include \
		-Iexternal/libhyle/include \;

clean:
	@for p in $(INSTALL_PKGS); do \
		if [ -f "external/$$p/Makefile" ]; then $(MAKE) -C external/$$p wasmtarget= clean; fi; \
	done
	@for d in $(MOD_DIRS) $(CLIENT_DIRS); do $(MAKE) -C $$d clean; done
	rm -rf /tmp/site_wasm_build htdocs/site.wasm

distclean: clean

# Debug/compilation capture targets
DEBUG_DIR := debug
BUILD_LOG_DIR := $(DEBUG_DIR)/builds
RUNTIME_LOG_DIR := $(DEBUG_DIR)/runtime
TEST_LOG_DIR := $(DEBUG_DIR)/tests

# Capture build output with timestamp
build-capture:
	@mkdir -p $(BUILD_LOG_DIR)
	@timestamp=$$(date +%Y-%m-%d_%H-%M-%S); \
	touch $(BUILD_LOG_DIR)/build_$$timestamp.log; \
	echo "=== Build started at $$(date) ===" >> $(BUILD_LOG_DIR)/build_$$timestamp.log; \
	$(Q)$(MAKE) 2>&1 | tee -a $(BUILD_LOG_DIR)/build_$$timestamp.log; \
	echo "=== Build completed at $$(date) ===" >> $(BUILD_LOG_DIR)/build_$$timestamp.log
	@echo "Build log saved to $(BUILD_LOG_DIR)/build_$$timestamp.log"

# Capture e2e test output
test-capture: test-data-dirs
	@mkdir -p $(TEST_LOG_DIR)
	@timestamp=$$(date +%Y-%m-%d_%H-%M-%S); \
	touch $(TEST_LOG_DIR)/test_$$timestamp.log; \
	echo "=== Tests started at $$(date) ===" >> $(TEST_LOG_DIR)/test_$$timestamp.log; \
	AUTH_SKIP_CONFIRM=1 DENO_JOBS=$${DENO_JOBS:-4} deno test --allow-all --parallel tests/e2e/ 2>&1 | tee -a $(TEST_LOG_DIR)/test_$$timestamp.log; \
	echo "=== Tests completed at $$(date) ===" >> $(TEST_LOG_DIR)/test_$$timestamp.log
	@echo "Test log saved to $(TEST_LOG_DIR)/test_$$timestamp.log"

# Capture single test output
test-single-capture: test-data-dirs
	@mkdir -p $(TEST_LOG_DIR)
	@timestamp=$$(date +%Y-%m-%d_%H-%M-%S); \
	test_file=${TEST}; \
	touch $(TEST_LOG_DIR)/test_$$test_file_$$timestamp.log; \
	echo "=== Test $(TEST) started at $$(date) ===" >> $(TEST_LOG_DIR)/test_$$test_file_$$timestamp.log; \
	AUTH_SKIP_CONFIRM=1 deno test --allow-all tests/e2e/$(TEST) 2>&1 | tee -a $(TEST_LOG_DIR)/test_$$test_file_$$timestamp.log; \
	echo "=== Test $(TEST) completed at $$(date) ===" >> $(TEST_LOG_DIR)/test_$$test_file_$$timestamp.log
	@echo "Test log saved to $(TEST_LOG_DIR)/test_$(TEST)_$$timestamp.log"

# Show recent debug logs
debug-logs:
	@echo "=== Recent Build Logs ==="; \
	ls -la $(BUILD_LOG_DIR)/*.log 2>/dev/null | tail -5; \
	echo ""; \
	echo "=== Recent Test Logs ==="; \
	ls -la $(TEST_LOG_DIR)/*.log 2>/dev/null | tail -5; \
	echo ""; \
	echo "=== Runtime Log (last 20 lines) ==="; \
	tail -20 $(RUNTIME_LOG_DIR)/axil.log 2>/dev/null || echo "No runtime log found"

# Clean debug logs
# Run hyle workspace crate tests (core, axil, source-corm)
hyle-tests:
	LIBRARY_PATH="$(CURDIR)/external/liblemma/lib:$${LIBRARY_PATH:-}" RUSTFLAGS="-l corm -l lemma" cargo test --workspace \
		--manifest-path external/libhyle/Cargo.toml 2>&1

debug-clean:
	rm -rf $(DEBUG_DIR)/*

doctor:
	@./scripts/doctor.sh

compile_commands.json:
	@./scripts/gen-compile-commands.sh

new-mod:
	@if [ -z "$(NAME)" ]; then \
		echo "Error: NAME is required. Example: make new-mod NAME=artist DISPLAY=Artist"; \
		exit 1; \
	fi
	@./scripts/scaffold-module.sh "$(NAME)" "$(DISPLAY)"

# Deploy JS/WASM/CSS to remote server (build wasm locally, deploy to OpenBSD)
DEPLOY_HOST ?= tty.pt
DEPLOY_PATH ?= /var/www/htdocs
PROD_ASSETS = styles.css hyle.css bud-cap.js hyle-cap.js axil-cap.js axil-tty-cap.js lamina-cap.js webgl-cap.js xylem-cap.js stele-client.js stele.wasm site.wasm

deploy-wasm: clients
	scp $(PROD_ASSETS:%=htdocs/%) \
	    $(DEPLOY_HOST):$(DEPLOY_PATH)/
	scp -r htdocs/snippets/ $(DEPLOY_HOST):$(DEPLOY_PATH)/

# NeverDark Full Universe Orchestration
ND_PORT ?= 8080
ND_PLANETS ?= 1
ND_DB ?= var/nd/universe.db

seed-universe:
	@bash scripts/seed-nd-universe.sh "$(ND_DB)" $(ND_PLANETS)

test-universe:
	@bash tests/e2e/test_full_universe.sh

run-universe:
	@if [ ! -f "$(ND_DB)" ] || [ "$${RESEED:-0}" = "1" ]; then \
		echo "Seeding NeverDark universe into $(ND_DB) ($(ND_PLANETS) planets)..."; \
		bash scripts/seed-nd-universe.sh "$(ND_DB)" $(ND_PLANETS); \
	fi
	@echo "Starting NeverDark universe on single port http://localhost:$(ND_PORT)/nd (Press Ctrl+C to stop) ..."
	@export PATH="$$PWD/external/axil/bin:$$PATH"; \
	export AXIL_ND_DB="$$PWD/$(ND_DB)"; \
	export AXIL_ND_HTDOCS="$$PWD/htdocs"; \
	. "$$PWD/scripts/nd-kernel-mods.sh"; \
	cd external/axil-nd && axil -d -A -p $(ND_PORT) -m "./lib/axil-nd:$$ND_KERNEL_MODS"

run-sector32:
	@$(MAKE) run-universe ND_PLANETS=32 ND_DB=var/nd/sector32.db

.PHONY: all mods clients run dev clean distclean format lint test unit-c-tests unit-tests standalone-unit-tests pages-test integration-tests e2e-tests hyle-tests test-data-dirs build-capture test-capture test-single-capture debug-logs debug-clean deploy-wasm bud-lib hyle-lib pitch-lib transp-lib lemma-lib joint-lib islet-lib sepal-lib areole-lib chunk-lib chaff-lib noise-lib pi-mm-zip axil-lib axil-auth-lib axil-hyle axil-tty-lib axil-nd-lib nd-libs corm-lib xylem-lib install install-libs install-jail print-prefix boundary-check doctor compile_commands.json new-mod test-mod test-fast test-e2e stage-assets assets-sync seed-universe test-universe run-universe run-sector32
