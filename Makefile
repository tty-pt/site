CC  ?= clang
DEV ?= 0
PROFILE ?= dev

MOD_DIRS != for f in mods/*/Makefile; do [ -f "$$f" ] && dirname "$$f"; done | sort
CLIENT_DIRS != for f in mods/*/client/Makefile; do [ -f "$$f" ] && dirname "$$f"; done | sort

all: stoma-lib hyle-lib transp-lib bud-lib hyle-bud hyle-source axil-lib axil-auth-lib axil-hyle qmap-lib xylem-lib mods clients boundary-check

mods:
	@for d in $(MOD_DIRS); do $(MAKE) -C $$d; done

clients:
	@for d in $(CLIENT_DIRS); do $(MAKE) -C $$d; done

stoma-lib:
	$(MAKE) -C external/stoma

hyle-lib:
	$(MAKE) -C external/hyle

transp-lib:
	$(MAKE) -C external/libtransp

bud-lib:
	$(MAKE) -C external/bud

hyle-bud: hyle-lib bud-lib
	$(MAKE) -C external/hyle/c/libhyle-bud

hyle-source: hyle-lib qmap-lib stoma-lib
	$(MAKE) -C external/hyle/c/libhyle-source

axil-lib:
	$(MAKE) -C external/axil

axil-auth-lib: axil-lib qmap-lib xylem-lib
	$(MAKE) -C external/axil-auth

axil-hyle: axil-lib hyle-source hyle-bud axil-auth-lib
	$(MAKE) -C external/axil-hyle

qmap-lib:
	$(MAKE) -C external/libqmap

xylem-lib:
	$(MAKE) -C external/libxylem

dev:
	@./scripts/dev.sh

run:
	$(MAKE) DEV=1 all
	./start.sh

MODS != cat mods.load

test-data-dirs:
	mkdir -p var/poem var/song var/gig var/grp var/song.types var/song.authors

unit-tests: all test-data-dirs
	@./scripts/run-with-server.sh sh -c '\
		for d in $(MODS); do \
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
	@sh tests/unit/run-auth-group-permissions.sh
	@sh tests/unit/run-site-media.sh
	@sh tests/unit/run-ordered-sync.sh
	@sh tests/unit/run-source-dataset-options.sh
	@sh tests/unit/run-viewer-prefs.sh
	@sh tests/unit/run-slugify.sh
	@sh tests/unit/run-i18n.sh

pages-test: all
	@echo "Running pages smoke tests"
	@./scripts/run-with-server.sh sh -c '\
		sh tests/pages/10-pages-render.sh && \
		sh tests/pages/20-song-search.sh && \
		sh tests/pages/25-list-metadata.sh && \
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

test-fast: boundary-check unit-c-tests standalone-unit-tests pages-test
	@echo "Fast test suite passed!"

test-e2e: test-data-dirs
	@if [ -n "$(FILE)" ]; then \
		./scripts/run-with-server.sh deno test --allow-all "tests/e2e/$(FILE)"; \
	else \
		./scripts/run-with-server.sh deno test --allow-all --parallel $(E2E_ARGS) tests/e2e/; \
	fi

e2e-tests: test-data-dirs
	AUTH_SKIP_CONFIRM=1 deno test --allow-all --parallel $(E2E_ARGS) tests/e2e/

restart:
	@pkill -f 'axil -C .* mods/core/core' || true
	@sleep 0.5
	@./start.sh &
	@sleep 1
	@curl -s --max-time 3 http://localhost:8080/ > /dev/null 2>&1 || { echo "Failed to start server"; exit 1; }
	@echo "Server restarted on :8080"

test: boundary-check unit-c-tests unit-tests pages-test integration-tests test-e2e

boundary-check:
	sh scripts/check-module-boundaries.sh && sh scripts/check-ux-purity.sh && sh scripts/check-no-site-specific-js.sh && sh scripts/check-wasm-imports.sh

watch:
	./scripts/watch.sh

format:
	find mods external/bud \( -name "*.c" -o -name "*.h" \) | xargs clang-format -i

lint:
	find mods external/bud -name "*.c" -exec clang-tidy {} -- \
		-Iexternal/axil/include -Iexternal/libqmap/include \
		-Iexternal/libxylem/include -Iexternal/bud/include \
		-Iexternal/hyle/include \;

clean:
	$(MAKE) -C external/bud clean
	@for d in $(MOD_DIRS) $(CLIENT_DIRS); do $(MAKE) -C $$d clean; done

distclean:
	$(MAKE) -C external/bud distclean
	@for d in $(MOD_DIRS) $(CLIENT_DIRS); do $(MAKE) -C $$d distclean; done

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
	$(MAKE) 2>&1 | tee -a $(BUILD_LOG_DIR)/build_$$timestamp.log; \
	echo "=== Build completed at $$(date) ===" >> $(BUILD_LOG_DIR)/build_$$timestamp.log
	@echo "Build log saved to $(BUILD_LOG_DIR)/build_$$timestamp.log"

# Capture e2e test output
test-capture: test-data-dirs
	@mkdir -p $(TEST_LOG_DIR)
	@timestamp=$$(date +%Y-%m-%d_%H-%M-%S); \
	touch $(TEST_LOG_DIR)/test_$$timestamp.log; \
	echo "=== Tests started at $$(date) ===" >> $(TEST_LOG_DIR)/test_$$timestamp.log; \
	AUTH_SKIP_CONFIRM=1 deno test --allow-all --parallel tests/e2e/ 2>&1 | tee -a $(TEST_LOG_DIR)/test_$$timestamp.log; \
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
# Run hyle workspace crate tests (core, axil, source-qmap)
hyle-tests:
	RUSTFLAGS="-l qmap -l stoma -L $$(pwd)/external/libqmap/lib -L $$(pwd)/external/stoma/lib" cargo test --workspace \
		--manifest-path external/hyle/Cargo.toml 2>&1

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
PROD_ASSETS = styles.css hyle.css bud-client.js bud-hydrate.js hyle-fragments.js list.wasm song_detail.wasm gig_detail.wasm site_chrome.wasm

deploy-wasm: clients
	scp $(PROD_ASSETS:%=htdocs/%) \
	    $(DEPLOY_HOST):$(DEPLOY_PATH)/
	scp -r htdocs/snippets/ $(DEPLOY_HOST):$(DEPLOY_PATH)/

.PHONY: all mods clients run dev clean distclean format lint test unit-c-tests unit-tests standalone-unit-tests pages-test integration-tests e2e-tests hyle-tests test-data-dirs build-capture test-capture test-single-capture debug-logs debug-clean deploy-wasm bud-lib hyle-lib transp-lib stoma-lib axil-lib qmap-lib xylem-lib boundary-check doctor compile_commands.json new-mod test-mod test-fast test-e2e
