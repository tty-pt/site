CC ?= clang

MOD_DIRS := $(sort $(dir $(wildcard mods/*/Makefile)))
MODULE_DIRS := $(sort $(dir $(wildcard modules/*/Makefile)))

all: mods modules htdocs/styles.css

htdocs/styles.css: mods/ssr/input.css
	npm run build:css

mods:
	@for d in $(MOD_DIRS); do $(MAKE) -C $$d; done

modules:
	@for d in $(MODULE_DIRS); do $(MAKE) -C $$d; done

run: all
	./start.sh

MODS != cat mods.load

unit-tests: all
	@for d in $(MODS); do \
		echo "=== TESTING $$d ==="; \
		(cd mods/$$d && ./test.sh) || exit 1; \
	done

pages-test: all
	@echo "Running pages smoke tests"
	sh tests/pages/10-pages-render.sh

test:
	${MAKE} unit-tests pages-test

integration-tests: all
	@sh tests/integration/run_all.sh

e2e-tests:
	deno test --allow-all tests/e2e/

clean:
	@for d in $(MOD_DIRS) $(MODULE_DIRS); do $(MAKE) -C $$d clean; done

.PHONY: all mods modules run clean test unit-tests pages-test integration-tests e2e-tests
