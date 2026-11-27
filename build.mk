REPO_ROOT != cd ../.. && pwd
V ?= 0
V_IS_0 != test "$(V)" = "0" && echo 0 || echo 1
Q-0 = @
Q-1 =
Q = $(Q-$(V_IS_0))
ECHO_V-0 = @echo
ECHO_V-1 = @:
ECHO_V = $(ECHO_V-$(V_IS_0))
add-prefix-glob ?= ${REPO_ROOT}/external/*
WASI_CC_DEFAULT != command -v clang-18 2>/dev/null || command -v clang 2>/dev/null || echo clang
WASI_CC ?= $(WASI_CC_DEFAULT)
-include $(REPO_ROOT)/external/mk/portable.mk

MOD_NAME != basename "$$(pwd)"
TARGET_AUTO != case "$$(pwd)" in */modules/*) printf 'be/nd_%s.%s\n' "$$(basename "$$(pwd)")" "${SO}";; *) printf '%s.%s\n' "$$(basename "$$(pwd)")" "${SO}";; esac
SRC_AUTO != case "$$(pwd)" in */modules/*) printf 'be/nd_%s.c\n' "$$(basename "$$(pwd)")";; *) printf '%s.c\n' "$$(basename "$$(pwd)")";; esac
PICFLAGS-Unix = -fPIC
PICFLAGS = ${PICFLAGS-${SYS}}

TARGET-WASM_ONLY-1 =
TARGET-WASM_ONLY-0 = $(TARGET_AUTO)
TARGET-WASM_ONLY- = $(TARGET_AUTO)
TARGET ?= ${TARGET-WASM_ONLY-${WASM_ONLY}}
SRC ?= $(SRC_AUTO)
MAKEFILE_DEPS = Makefile $(REPO_ROOT)/build.mk

CC = clang

SANITIZE ?= 0
CFLAGS-SANITIZE-1 = -fsanitize=address -fsanitize=undefined -fno-omit-frame-pointer
LDFLAGS-SANITIZE-1 = -fsanitize=address -fsanitize=undefined
CFLAGS += ${CFLAGS-SANITIZE-${SANITIZE}}
LDFLAGS += ${LDFLAGS-SANITIZE-${SANITIZE}}

PROFILE ?= dev
CFLAGS-PROFILE-release = -O2 -DNDEBUG $(PICFLAGS)
CFLAGS-PROFILE-dev = -g -O0 $(PICFLAGS)
CFLAGS-PROFILE- = -g -O0 $(PICFLAGS)
CFLAGS += ${CFLAGS-PROFILE-${PROFILE}}

CFLAGS += -I$(REPO_ROOT)/include -I$(REPO_ROOT)/mods/common -I$(REPO_ROOT)/mods/common/ux
CFLAGS += $(EXTRA_CFLAGS)

LDFLAGS += -shared
LDFLAGS-RPATH != if [ "$(uname)" = "Linux" ]; then for d in $(add-prefix-dirs); do printf '%s ' "-Wl,-rpath-link,$$d/lib"; done; fi
LDFLAGS += $(LDFLAGS-RPATH)

LDLIBS += -laxil -lcorm
LDLIBS += $(EXTRA_LDLIBS)
LDLIBS += $(EXTRA_LDLIBS-$(uname))

MOD_EXTRA_SRC-auth     = $(REPO_ROOT)/mods/common/ux/site_ui.c
MOD_EXTRA_SRC-gig      = $(REPO_ROOT)/mods/common/ux/site_ui.c
MOD_EXTRA_SRC-grp      = $(REPO_ROOT)/mods/common/ux/site_ui.c
MOD_EXTRA_SRC-index    = $(REPO_ROOT)/mods/common/ux/site_ui.c
MOD_EXTRA_SRC-nd       = $(REPO_ROOT)/mods/common/ux/site_ui.c
MOD_EXTRA_SRC-poem     = $(REPO_ROOT)/mods/common/ux/site_ui.c
MOD_EXTRA_SRC-software = $(REPO_ROOT)/mods/common/ux/site_ui.c
MOD_EXTRA_SRC-song     = $(REPO_ROOT)/mods/common/ux/site_ui.c
MODULE_EXTRA_SRC ?= $(MOD_EXTRA_SRC-$(MOD_NAME))

# Ensure `all` is the first (default) target in both GNU Make and bmake before
# included *.mk files define auxiliary targets such as check-wasm.
all:

-include $(REPO_ROOT)/external/mk/iso.mk
-include $(REPO_ROOT)/external/mk/cx.mk

SRC_C = ${SRC:.cx=.cx.c}

SITE_UI_DEPS = $(REPO_ROOT)/mods/common/ux/site_ui.c \
	$(REPO_ROOT)/mods/common/ux/site_ui.h \
	$(REPO_ROOT)/mods/common/ux/site_cx.h \
	$(REPO_ROOT)/mods/common/ux/site_paths.c \
	$(REPO_ROOT)/mods/common/ux/site_layout.cx.c \
	$(REPO_ROOT)/mods/common/ux/site_forms.cx.c \
	$(REPO_ROOT)/mods/common/ux/site_media.cx.c \
	$(REPO_ROOT)/mods/common/ux/site_page.c \
	$(REPO_ROOT)/mods/common/ux/site_chrome.cx.c \
	$(REPO_ROOT)/mods/common/ux/site_chrome.h \
	$(REPO_ROOT)/mods/common/viewer_zoom.h

LIST_UX_DEPS = $(REPO_ROOT)/mods/index/ux/list.c \
	$(REPO_ROOT)/mods/index/ux/list_state.h \
	$(REPO_ROOT)/mods/index/ux/list_query.c \
	$(REPO_ROOT)/mods/index/ux/list_filters.cx.c \
	$(REPO_ROOT)/mods/index/ux/list_layout.cx.c \
	$(REPO_ROOT)/mods/index/ux/list_json.c \
	$(REPO_ROOT)/mods/index/ux/list_render.cx.c

WASM_PATH ?= $(REPO_ROOT)/htdocs
WASI_SYSROOT ?=
WASM_CFLAGS-PROFILE-release = -O3 -fno-math-errno -ffunction-sections -fdata-sections -DNDEBUG -D__wasm__ --target=wasm32-wasi -fvisibility=hidden
WASM_CFLAGS-PROFILE-dev = -Os -fno-math-errno -ffunction-sections -fdata-sections -DNDEBUG -D__wasm__ --target=wasm32-wasi -fvisibility=hidden
WASM_CFLAGS-PROFILE- = -Os -fno-math-errno -ffunction-sections -fdata-sections -DNDEBUG -D__wasm__ --target=wasm32-wasi -fvisibility=hidden
WASM_CFLAGS-PROFILE-debug = -g -O0 -D__wasm__ --target=wasm32-wasi -fvisibility=hidden
WASM_CFLAGS  := ${WASM_CFLAGS-PROFILE-${PROFILE}}
WASM_STRIP-PROFILE-debug =
WASM_STRIP-PROFILE-dev = -Wl,--strip-debug
WASM_STRIP-PROFILE- = -Wl,--strip-debug
WASM_STRIP-PROFILE-release = -Wl,--strip-debug
WASM_LDFLAGS := -mexec-model=reactor \
                -Wl,--gc-sections \
                -Wl,--no-entry \
                ${WASM_STRIP-PROFILE-${PROFILE}} \
                -Wl,--export=malloc \
                -Wl,--export=free \
                -Wl,--export=_initialize
# No --allow-undefined: host imports must be declared with an explicit
# import_module (ISO_IMPORT); anything else is a link error instead of a
# silent `env.*` import the browser host would have to stub.
WASM_COMMON_SRC   = $(REPO_ROOT)/external/libbud/src/libbud.c $(REPO_ROOT)/external/libbud/src/bud_tpl.c $(REPO_ROOT)/external/libbud/src/bud_render.c $(REPO_ROOT)/external/libbud/src/bud_runtime.c $(REPO_ROOT)/external/libbud/src/bud_json.c $(REPO_ROOT)/external/libbud/src/bud_reconcile.c $(REPO_ROOT)/external/libstele/src/stele_wasm_kernel.c $(REPO_ROOT)/external/libstele/src/stele_event.c $(REPO_ROOT)/external/libstele/src/stele_wq.c $(REPO_ROOT)/mods/common/ux/site_ui.c
WASM_COMMON_CFLAGS = -I$(REPO_ROOT)/include -I$(REPO_ROOT)/mods/common -I$(REPO_ROOT)/mods/common/ux -I$(REPO_ROOT)/external/libstele/include -I$(REPO_ROOT)/external/libbud/include
WASM_OBJ_ROOT != printf '/tmp/site_wasm_objs_%s\n' "$$(printf '%s' '$(REPO_ROOT)' | cksum | cut -d' ' -f1)"

WASM_BUILD_TARGETS != for t in $(WASM_TARGETS); do b=$$(basename "$$t" .wasm); grep -qE "^[[:space:]]*$${b}-src[[:space:]]*[:+?]?=" Makefile && echo "$$t"; done || true
WASM_PRIMARY_NAME != set -- $(WASM_BUILD_TARGETS); [ -n "$$1" ] && basename "$$1" .wasm || true
WASM_SRC_ALL = $($(WASM_PRIMARY_NAME)-src)
WASM_CX_C != for f in $(WASM_SRC_ALL); do case "$$f" in *.cx) echo "$${f%.cx}.cx.c" ;; esac; done || true

# BSD/GNU portable: keep `all` as first target
all: dirs $(TARGET) $(WASM_TARGETS)
.MAIN: all

# asset version — generated by mods/common (portable, not default goal)
VERSION_GEN ?= $(REPO_ROOT)/mods/common/ux/version.gen.h
$(VERSION_GEN): $(REPO_ROOT)/htdocs/styles.css $(REPO_ROOT)/htdocs/hyle.css $(REPO_ROOT)/htdocs/stele-client.js $(REPO_ROOT)/htdocs/bud-cap.js $(REPO_ROOT)/htdocs/hyle-cap.js $(REPO_ROOT)/scripts/gen-asset-version.sh
	@sh $(REPO_ROOT)/scripts/gen-asset-version.sh

$(WASM_BUILD_TARGETS): $(CX_HEADERS) $(WASM_CX_C) $(WASM_COMMON_SRC) $(SITE_UI_DEPS)
	@if echo 'int main(void){}' | $(WASI_CC) $(WASM_CFLAGS) -x c - -c -o /dev/null >/dev/null 2>&1; then \
		for src in ${${@:$(WASM_PATH)/%.wasm=%}-src}; do \
			case "$$src" in *libxylem-wasm.c|*hyle-bud*|*nd-mob-gl*) continue ;; esac; \
			if [ -f "$$src" ]; then \
				bad=$$(sed -e 's,//.*,,g' -e 's,/\*.*\*/,,g' "$$src" | grep -En '\b(corm_|source_|axil_|hyle_source|XY_DECL|XY_IMPL)\b' || true); \
				if [ -n "$$bad" ]; then \
					echo "ERROR: Pre-link boundary violation in $$src:" >&2; \
					echo "$$bad" >&2; \
					exit 1; \
				fi; \
			fi; \
		done; \
		opt_cflags=""; \
		if [ "$(PROFILE)" != "debug" ]; then opt_cflags="${${@:$(WASM_PATH)/%.wasm=%}-opt-cflags}"; fi; \
		common_key=$$(printf '%s' '$(WASM_COMMON_CFLAGS) $(WASM_CFLAGS)' | cksum | cut -d' ' -f1); \
		mod_key=$$(printf '%s' '${${@:$(WASM_PATH)/%.wasm=%}-cflags} $(WASM_COMMON_CFLAGS) $(EXTRA_CFLAGS) $(WASM_CFLAGS)' " $$opt_cflags" | cksum | cut -d' ' -f1); \
		common_dir="$(WASM_OBJ_ROOT)/common_$$common_key"; \
		mod_dir="$(WASM_OBJ_ROOT)/$(MOD_NAME)_${@:$(WASM_PATH)/%.wasm=%}_$$mod_key"; \
		mkdir -p "$$common_dir" "$$mod_dir"; \
		tu_stale() { \
			src="$$1"; obj="$$2"; dep="$$3"; \
			[ ! -f "$$obj" ] || [ ! -f "$$dep" ] || [ "$$src" -nt "$$obj" ] && return 0; \
			for h in $$(sed -e 's/^[^:]*://' -e 's/\\$$//' "$$dep"); do \
				[ -z "$$h" ] && continue; \
				case "$$h" in *:) continue ;; esac; \
				if [ ! -f "$$h" ] || [ "$$h" -nt "$$obj" ]; then return 0; fi; \
			done; \
			return 1; \
		}; \
		objs=""; \
		deps_out="$@.d.tmp.$$$$"; \
		: > "$$deps_out"; \
		for src in ${${@:$(WASM_PATH)/%.wasm=%}-src:%.cx=%.cx.c}; do \
			tag=$$(printf '%s' "$$src" | tr '/. ' '___'); \
			obj="$$mod_dir/$$tag.o"; \
			dep="$$mod_dir/$$tag.d"; \
			if tu_stale "$$src" "$$obj" "$$dep"; then \
				$(WASI_CC) ${${@:$(WASM_PATH)/%.wasm=%}-cflags} $(WASM_COMMON_CFLAGS) $(EXTRA_CFLAGS) $(WASM_CFLAGS) $$opt_cflags -MMD -MP -MF "$$dep.tmp.$$$$" -MT "$$obj" -c "$$src" -o "$$obj.tmp.$$$$" || { rm -f "$$dep.tmp.$$$$" "$$obj.tmp.$$$$" "$$deps_out"; exit 1; }; \
				mv -f "$$dep.tmp.$$$$" "$$dep" && mv -f "$$obj.tmp.$$$$" "$$obj"; \
			fi; \
			sed "1s|^[^:]*:|$@:|" "$$dep" >> "$$deps_out"; \
			objs="$$objs $$obj"; \
		done; \
		for src in $(WASM_COMMON_SRC); do \
			tag=$$(printf '%s' "$$src" | tr '/. ' '___'); \
			obj="$$common_dir/$$tag.o"; \
			dep="$$common_dir/$$tag.d"; \
			if tu_stale "$$src" "$$obj" "$$dep"; then \
				$(WASI_CC) $(WASM_COMMON_CFLAGS) $(WASM_CFLAGS) -MMD -MP -MF "$$dep.tmp.$$$$" -MT "$$obj" -c "$$src" -o "$$obj.tmp.$$$$" || { rm -f "$$dep.tmp.$$$$" "$$obj.tmp.$$$$" "$$deps_out"; exit 1; }; \
				mv -f "$$dep.tmp.$$$$" "$$dep" && mv -f "$$obj.tmp.$$$$" "$$obj"; \
			fi; \
			sed "1s|^[^:]*:|$@:|" "$$dep" >> "$$deps_out"; \
			objs="$$objs $$obj"; \
		done; \
		mv -f "$$deps_out" "$@.d"; \
		extra_allowed=""; \
		for f in $(REPO_ROOT)/external/libstele/wasm-allowed-imports.lst ${${@:$(WASM_PATH)/%.wasm=%}-wasm-allowed} $(EXTRA_WASM_ALLOWED); do \
			if [ -f "$$f" ]; then extra_allowed="$$extra_allowed -Wl,--allow-undefined-file=$$f"; fi; \
		done; \
		$(WASI_CC) $(WASM_CFLAGS) $$opt_cflags $(WASM_LDFLAGS) ${${@:$(WASM_PATH)/%.wasm=%}-ldflags} $(EXTRA_WASM_LDFLAGS) $$extra_allowed -o $@ $$objs || exit 1; \
		if [ "$(PROFILE)" != "debug" ] && command -v wasm-strip >/dev/null 2>&1; then \
			wasm-strip $@; \
		fi; \
		sh $(REPO_ROOT)/external/mk/scripts/wasm-audit.sh $@ || { rm -f $@; exit 1; }; \
	else \
		echo "Skipping WASM build of $@ — install wasi-sdk or configure WASI_CC"; \
	fi

dirs:
	@for d in $(DIRS); do mkdir -p $(REPO_ROOT)/$$d 2>/dev/null || true; done

MOD_SITE_UI_DEPS-auth     = $(SITE_UI_DEPS)
MOD_SITE_UI_DEPS-chat     = $(SITE_UI_DEPS)
MOD_SITE_UI_DEPS-auren    = $(SITE_UI_DEPS)
MOD_SITE_UI_DEPS-common   = $(SITE_UI_DEPS)
MOD_SITE_UI_DEPS-gig      = $(SITE_UI_DEPS)
MOD_SITE_UI_DEPS-grp      = $(SITE_UI_DEPS)
MOD_SITE_UI_DEPS-index    = $(SITE_UI_DEPS)
MOD_SITE_UI_DEPS-nd       = $(SITE_UI_DEPS)
MOD_SITE_UI_DEPS-poem     = $(SITE_UI_DEPS)
MOD_SITE_UI_DEPS-software = $(SITE_UI_DEPS)
MOD_SITE_UI_DEPS-song     = $(SITE_UI_DEPS)
$(TARGET): $(MOD_SITE_UI_DEPS-$(MOD_NAME))

$(TARGET): $(CX_HEADERS) $(SRC_C) $(MAKEFILE_DEPS)
	@rm -f $@.d; for src in $(SRC_C); do \
		$(CC) $(CFLAGS) -MM -MP -MT $@ "$$src" >> $@.d || exit 1; \
	done
	$(ECHO_V) "  CC      $@"
	$(Q)$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SRC_C) $(MODULE_EXTRA_SRC) $(LDLIBS)

MOD_DEPS != for f in $(TARGET) $(WASM_TARGETS); do [ -n "$$f" ] && [ -f "$$f.d" ] && printf '%s.d ' "$$f"; done; printf '/dev/null'
-include $(MOD_DEPS)

wasm: $(WASM_TARGETS)

wasm-debug:
	rm -f $(WASM_TARGETS)
	$(MAKE) PROFILE=debug WASM_CFLAGS='$(WASM_CFLAGS-PROFILE-debug) -DBUD_DEBUG' wasm

clean:
	@test -z "$(TARGET)" || rm -f $(TARGET)
	rm -f $(WASM_TARGETS) *.cx.c *.cx.h ux/*.cx.c ux/*.cx.h
	@for f in $(TARGET) $(WASM_TARGETS); do [ -n "$$f" ] && rm -f "$$f.d"; done

distclean: clean

print-static-spec:
	@printf 'SRC=%s\n' "$(SRC)"
	@printf 'CFLAGS=%s\n' "$(EXTRA_CFLAGS)"

.PHONY: all clean distclean wasm wasm-debug print-static-spec
