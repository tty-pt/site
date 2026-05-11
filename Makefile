CC  ?= clang
DEV ?= 0

MOD_DIRS != for f in mods/*/Makefile; do [ -f "$$f" ] && dirname "$$f"; done | sort
MODULE_DIRS != for f in modules/*/Makefile; do [ -f "$$f" ] && dirname "$$f"; done | sort
CLIENT_DIRS != for f in mods/*/client/Makefile; do [ -f "$$f" ] && dirname "$$f"; done | sort

all: hyle-lib bud-lib hyle-bud mods modules clients

mods:
	@for d in $(MOD_DIRS); do $(MAKE) -C $$d; done

modules:
	@for d in $(MODULE_DIRS); do $(MAKE) -C $$d; done

clients:
	@for d in $(CLIENT_DIRS); do $(MAKE) -C $$d; done

hyle-lib:
	$(MAKE) -C external/hyle

bud-lib:
	$(MAKE) -C external/bud

hyle-bud: hyle-lib bud-lib
	$(MAKE) -C external/hyle/c/libhyle-bud

run:
	$(MAKE) DEV=1 all
	./start.sh

MODS != cat mods.load

test-data-dirs:
	mkdir -p items/poem/items items/song/items items/songbook/items items/choir/items

unit-tests: all test-data-dirs
	@curl -s --max-time 2 http://localhost:8080/ > /dev/null 2>&1 || \
		{ echo "ERROR: axil not running on :8080 — start the server before running unit-tests"; exit 1; }
	@for d in $(MODS); do \
		echo "=== TESTING $$d ==="; \
		(cd mods/$$d && ./test.sh) || exit 1; \
	done

pages-test: all
	@echo "Running pages smoke tests"
	sh tests/pages/10-pages-render.sh

integration-tests: all
	@sh tests/integration/run_all.sh

e2e-tests: test-data-dirs
	AUTH_SKIP_CONFIRM=1 deno test --allow-all tests/e2e/

test: unit-tests pages-test e2e-tests

watch:
	./scripts/watch.sh

format:
	find mods external/bud \( -name "*.c" -o -name "*.h" \) | xargs clang-format -i

lint:
	find mods external/bud -name "*.c" -exec clang-tidy {} -- \;

clean:
	$(MAKE) -C external/bud clean
	@for d in $(MOD_DIRS) $(MODULE_DIRS) $(CLIENT_DIRS); do $(MAKE) -C $$d clean; done

distclean:
	$(MAKE) -C external/bud distclean
	@for d in $(MOD_DIRS) $(MODULE_DIRS) $(CLIENT_DIRS); do $(MAKE) -C $$d distclean; done

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
	AUTH_SKIP_CONFIRM=1 deno test --allow-all tests/e2e/ 2>&1 | tee -a $(TEST_LOG_DIR)/test_$$timestamp.log; \
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
	RUSTFLAGS="-l qmap" cargo test --workspace \
		--manifest-path external/hyle/Cargo.toml 2>&1

debug-clean:
	rm -rf $(DEBUG_DIR)/*

# Deploy JS/WASM/CSS to remote server (build wasm locally, deploy to OpenBSD)
DEPLOY_HOST ?= tty.pt
DEPLOY_PATH ?= /var/www/htdocs

deploy-wasm: clients
	scp htdocs/*.js htdocs/*.wasm htdocs/*.css \
	    $(DEPLOY_HOST):$(DEPLOY_PATH)/
	scp -r htdocs/snippets/ $(DEPLOY_HOST):$(DEPLOY_PATH)/

.PHONY: all mods modules clients run clean distclean format lint test unit-tests pages-test integration-tests e2e-tests hyle-tests test-data-dirs build-capture test-capture test-single-capture debug-logs debug-clean deploy-wasm bud-lib hyle-lib
