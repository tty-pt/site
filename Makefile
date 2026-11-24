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
SITE_WASM_OPT_FLAGS ?= -Os -DNDEBUG -DQSYS_UNIFIED_SITE
WASM_COMMON_CFLAGS ?= -Iexternal/libstele/include -Iexternal/libbud/include
WASM_LDFLAGS ?= -mexec-model=reactor -Wl,--gc-sections -Wl,--no-entry -Wl,--strip-debug \
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

include external/mk/cx.mk

SITE_WASM_BUILD_DIR = /tmp/site_wasm_build_$(shell pwd | cksum | cut -d' ' -f1)
SITE_WASM_SPECS := \
	kernel:external/libstele/src/stele_wasm_kernel.c \
	stele_event:external/libstele/src/stele_event.c \
	bud:external/libbud/src/libbud.c \
	bud_tpl:external/libbud/src/bud_tpl.c \
	bud_render:external/libbud/src/bud_render.c \
	bud_runtime:external/libbud/src/bud_runtime.c \
	bud_json:external/libbud/src/bud_json.c \
	bud_reconcile:external/libbud/src/bud_reconcile.c \
	site_wasm_app:mods/common/site_wasm_app.c \
	site_ui:mods/common/ux/site_ui.c \
	modules:mods/common/ux/site_modules.c \
	site_chrome:mods/common/ux/site_chrome.cx.c \
	chrome:mods/site_chrome/ux/chrome.c \
	song:mods/song/ux/detail.cx.c \
	gig:mods/gig/ux/detail.cx.c \
	list:mods/index/ux/list_fe.c \
	chat:mods/chat/ux/chat_fe.c \
	chat_ui:mods/chat/ux/chat_ui.cx.c \
	auren:mods/auren/ux/auren_fe.c \
	auren_ui:mods/auren/ux/auren_ui.cx.c \
	music:external/libpitch/src/music.c \
	hyle_bud:external/libhyle-bud/src/libhyle-bud.c \
	filter:external/libhyle-bud/src/filter.c \
	table:external/libhyle-bud/src/table.c \
	picker:external/libhyle-bud/src/picker.c \
	form:external/libhyle-bud/src/form.c \
	hyle_bud_cache:external/libhyle-bud/src/hyle-bud-cache.c \
	url:external/libhyle/src/url.c

SITE_WASM_OBJS := $(foreach s,$(SITE_WASM_SPECS),$(word 1,$(subst :, ,$(s))).o)

define SITE_WASM_RULE
$(SITE_WASM_BUILD_DIR)/$(1).o: $(2)
	$$(if $$(filter 0,$$(V)),@echo "  WASM    $$@")
	$$(Q)mkdir -p $$(SITE_WASM_BUILD_DIR) && $$(WASI_CC) $$(WASM_CFLAGS) $$(SITE_WASM_OPT_FLAGS) $$(SITE_WASM_INCLUDES) -MMD -MP -MT $$@ -MF $$@.d -c $$< -o $$@.tmp && mv -f $$@.tmp $$@
endef
$(foreach s,$(SITE_WASM_SPECS),$(eval $(call SITE_WASM_RULE,$(word 1,$(subst :, ,$(s))),$(word 2,$(subst :, ,$(s))))))

$(SITE_WASM_BUILD_DIR)/site_ui.o: mods/common/ux/site_layout.cx.c mods/common/ux/site_forms.cx.c mods/common/ux/site_media.cx.c
$(SITE_WASM_BUILD_DIR)/gig.o $(SITE_WASM_BUILD_DIR)/list.o: mods/index/ux/list_filters.cx.c mods/index/ux/list_layout.cx.c mods/index/ux/list_render.cx.c
-include $(wildcard $(SITE_WASM_BUILD_DIR)/*.o.d)

htdocs/site.wasm: $(addprefix $(SITE_WASM_BUILD_DIR)/,$(SITE_WASM_OBJS))
	$(if $(filter 0,$(V)),@echo "  LD.WASM $@")
	$(Q)mkdir -p htdocs
	$(Q)$(WASI_CC) $(WASM_CFLAGS) $(WASM_LDFLAGS) -o $@ $^
	$(Q)if command -v wasm-strip >/dev/null 2>&1; then wasm-strip $@; fi
	$(Q)sh external/mk/scripts/wasm-audit.sh $@ || { rm -f $@; exit 1; }

# Staged htdocs deploy assets. Exported from each library and module <pkg>/htdocs/.
DEPLOY_ASSETS = styles.css hyle.css bud-cap.js hyle-cap.js axil-cap.js axil-tty-cap.js lamina-cap.js webgl-cap.js xylem-cap.js stele-client.js stele.wasm
ASSET_SOURCES := $(wildcard external/*/htdocs/* mods/*/htdocs/*)
NPM_LINK_SPECS := \
	stele:libstele corm:libcorm joint:libjoint islet:libislet lemma:liblemma sepal:libsepal \
	hyle:libhyle bud:libbud xylem:libxylem hyle-bud:libhyle-bud hyle-source:libhyle-source \
	lamina:liblamina pitch:libpitch transp:libpitch chaff:libchaff noise:libchaff \
	areole:libareole chunk:libareole axil:axil

stage-assets: htdocs/.assets-staged

htdocs/.assets-staged: $(ASSET_SOURCES)
	@mkdir -p htdocs node_modules/@tty-pt
	@rm -rf node_modules/@ttypt
	@for pair in $(NPM_LINK_SPECS); do \
		pkg=$${pair%%:*}; dir=$${pair#*:}; \
		ln -sfn "../../external/$$dir/npm" "node_modules/@tty-pt/$$pkg"; \
	done
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
			cp -f -R "$$d/htdocs/." htdocs/ 2>/dev/null || true; \
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

# Keep the PREFIX table in sync with external/mk/portable.mk:16-29.
# External packages own their install into $(DESTDIR)$(PREFIX)/{lib,bin,include};
# there is no -L into the tree at link time (build.mk + check-no-tree-rpath.sh).
UNAME_S := $(shell uname -s)
ARCH_M := $(shell uname -m)
PREFIX ?= $(if $(filter Linux,$(UNAME_S)),/usr, \
	$(if $(filter NetBSD,$(UNAME_S)),/usr/pkg, \
	$(if $(and $(filter Darwin,$(UNAME_S)),$(filter arm64,$(ARCH_M))),/opt/homebrew, \
	/usr/local)))
SUDO ?= $(if $(filter /usr% /opt%,$(PREFIX)),sudo,)

EXT_RPATH_LINK := $(if $(filter Linux,$(UNAME_S)),$(foreach d,$(wildcard $(CURDIR)/external/*),-Wl,-rpath-link,$(d)/lib),)
EXT_MAKE_ARGS = add-prefix-glob="../*" LDFLAGS-Unix="$(EXT_RPATH_LINK)"
LIB_MAKE_ARGS = $(EXT_MAKE_ARGS) PREFIX="$(PREFIX)" PROFILE="$(PROFILE)" $(if $(SANITIZE),SANITIZE="$(SANITIZE)",)

define LIB_TARGET
.PHONY: $(1)
$(1): $(3)
	$$(if $$(filter 0,$$(V)),@echo "  LIB     $(2)")
	$$(Q)$$(MAKE) -C external/$(2) $$(LIB_MAKE_ARGS)
endef

$(eval $(call LIB_TARGET,stele-lib,libstele,))
$(eval $(call LIB_TARGET,corm-lib,libcorm,stele-lib))
$(eval $(call LIB_TARGET,xylem-lib,libxylem,stele-lib corm-lib))
$(eval $(call LIB_TARGET,lemma-lib,liblemma,stele-lib corm-lib))
$(eval $(call LIB_TARGET,joint-lib,libjoint,stele-lib corm-lib))
$(eval $(call LIB_TARGET,islet-lib,libislet,stele-lib corm-lib))
$(eval $(call LIB_TARGET,chaff-lib,libchaff,stele-lib))
$(eval $(call LIB_TARGET,areole-lib,libareole,islet-lib))
$(eval $(call LIB_TARGET,corolla-lib,libcorolla,chaff-lib))
$(eval $(call LIB_TARGET,sepal-lib,libsepal,stele-lib corm-lib))
$(eval $(call LIB_TARGET,hyle-lib,libhyle,lemma-lib corm-lib))
$(eval $(call LIB_TARGET,pitch-lib,libpitch,stele-lib))
$(eval $(call LIB_TARGET,bud-lib,libbud,stele-lib))
$(eval $(call LIB_TARGET,hyle-source,libhyle-source,hyle-lib corm-lib lemma-lib stele-lib))
$(eval $(call LIB_TARGET,hyle-bud,libhyle-bud,hyle-lib bud-lib hyle-source))
$(eval $(call LIB_TARGET,axil-lib,axil,stele-lib corm-lib xylem-lib))
$(eval $(call LIB_TARGET,axil-auth-lib,axil-auth,axil-lib corm-lib xylem-lib))
$(eval $(call LIB_TARGET,axil-hyle,axil-hyle,axil-lib hyle-source axil-auth-lib))
$(eval $(call LIB_TARGET,axil-tty-lib,axil-tty,axil-lib corm-lib xylem-lib))
$(eval $(call LIB_TARGET,axil-nd-lib,axil-nd,axil-tty-lib axil-lib corm-lib xylem-lib islet-lib stele-lib))
$(eval $(call LIB_TARGET,jarvis-lib,libjarvis,stele-lib corm-lib xylem-lib))

qsys-lib: stele-lib
noise-lib: chaff-lib
chunk-lib: areole-lib
transp-lib: pitch-lib

pi-mm-zip:
	$(Q)$(MAKE) -C .pi/extensions/pi-mm zip

nd-libs: axil-nd-lib chaff-lib areole-lib
	@$(MAKE) -C external/nd-bus $(LIB_MAKE_ARGS) || exit 1
	@for d in external/nd-*; do \
		[ "$$d" = "external/nd-bus" ] && continue; \
		if [ -f "$$d/Makefile" ]; then $(MAKE) -C "$$d" $(LIB_MAKE_ARGS) || exit 1; fi; \
	done

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

WASM_UNIT_TESTS = \
	run-wasm-single-instantiation.mjs test_site_wasm_kernel.mjs \
	test_wasm_event_bus.mjs test_wasm_optimistic.mjs \
	test_wasm_lazy.mjs test_bud_binary_state.mjs

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
	@for t in $(WASM_UNIT_TESTS); do \
		echo "=== Testing $$t ==="; \
		node --test "tests/unit/$$t" || exit 1; \
	done
	@if [ -d /home/quirinpa/duna ]; then \
		echo "=== Testing consumer: ~/duna ==="; \
		npm --prefix /home/quirinpa/duna run test:unit || exit 1; \
	elif [ -d ../duna ]; then \
		echo "=== Testing consumer: ../duna ==="; \
		npm --prefix ../duna run test:unit || exit 1; \
	else \
		echo "=== Skipping ~/duna (not present in environment) ==="; \
	fi

# The `axil -C` chroot jail (manifest: scripts/jail-manifest.sh).
JAIL ?= /var/www

install-jail:
	@sh scripts/seed-jail.sh $(JAIL)
	@sh scripts/check-jail.sh $(JAIL)

print-prefix:
	@echo $(PREFIX)

mm: ;

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

STANDALONE_UNIT_SCRIPTS = \
	run-mpfd-content-length.sh run-mpfd-multivalue.sh run-dsv-legacy.sh \
	run-source-options.sh run-bud-picker-collect.sh run-bud-form.sh run-bud-table.sh \
	run-source-utils.sh run-auth-disk-permissions.sh run-axil-auth-groups.sh \
	run-axil-auth-account.sh run-xy-hook-default.sh run-auth-group-permissions.sh \
	run-site-media.sh run-site-ui-helpers.sh run-index-helpers.sh \
	run-externals-abstractions.sh run-ordered-sync.sh run-source-dataset-options.sh \
	run-hyle-source-cache.sh run-viewer-prefs.sh run-slugify.sh run-i18n.sh \
	run-nd-bcp.sh run-nd-table.sh run-nd-ext-state.sh run-test-bcp-meta.sh \
	run-test-stone-roll.sh run-test-hyle-table-bin.sh run-nd-ui-slot.sh run-markdown-test.sh

GRAPHICS_UNIT_SCRIPTS = \
	run-test-scene-sync.sh run-test-ssr-hydration.sh run-test-seven-world.sh \
	run-test-map-render.sh run-test-seven-render.sh run-test-lod-zoom-consistency.sh \
	run-test-ground-zoom-lod.sh run-test-viewport-registry.sh run-test-seven-stage-html.sh \
	run-test-lamina-bridge.sh run-test-axil-ws-multiplex.sh run-test-torus-planetary-renderer.sh \
	run-test-areole-chaff-occlusion.sh run-xylem-wasm.sh

PAGES_TEST_SCRIPTS = \
	10-pages-render.sh 20-song-search.sh 25-list-metadata.sh 26-binary-table-streaming.sh \
	27-column-sorting.sh 30-song-multiselect.sh 40-traversal.sh 50-pickers.sh

standalone-unit-tests:
	@tmpdir=$$(mktemp -d); \
	trap 'rm -rf "$$tmpdir"' EXIT HUP INT TERM; \
	for test in mpfd_contract_test caller_contract_test; do \
		echo "=== TESTING $$test ==="; \
		$(CC) -Wall -Wextra -Werror -o "$$tmpdir/$$test" "tests/unit/$$test.c" || exit 1; \
		"$$tmpdir/$$test" || exit 1; \
	done
	@for s in $(STANDALONE_UNIT_SCRIPTS); do sh "tests/unit/$$s" || exit 1; done

test-graphics:
	@for s in $(GRAPHICS_UNIT_SCRIPTS); do sh "tests/unit/$$s" || exit 1; done

pages-test: all
	@echo "Running pages smoke tests"
	@./scripts/run-with-server.sh sh -c 'for s in $(PAGES_TEST_SCRIPTS); do sh "tests/pages/$$s" || exit 1; done'

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
	sh scripts/check-asset-sync.sh && sh scripts/check-module-boundaries.sh && sh scripts/check-ux-purity.sh && sh scripts/check-no-site-specific-js.sh && sh scripts/check-wasm-imports.sh && sh external/mk/scripts/wasm-audit.sh htdocs/*.wasm && sh external/mk/scripts/wasm-audit.sh --check-substrate && sh scripts/check-no-tree-rpath.sh && sh scripts/check-no-impl-guards.sh && sh scripts/check-nd-encapsulation.sh && sh scripts/check-nd-no-static-load.sh && sh scripts/check-pkgconfig-versions.sh

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
	rm -rf $(SITE_WASM_BUILD_DIR) /tmp/site_wasm_build /tmp/site_unit_cache_$(shell pwd | cksum | cut -d' ' -f1) htdocs/site.wasm

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
