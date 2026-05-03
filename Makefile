CC ?= clang

MOD_DIRS != for f in mods/*/Makefile; do [ -f "$$f" ] && dirname "$$f"; done | sort
MODULE_DIRS != for f in modules/*/Makefile; do [ -f "$$f" ] && dirname "$$f"; done | sort

all: mods modules

mods: mods/ssr
	@for d in $(MOD_DIRS); do [ "$$d" = "mods/ssr" ] && continue; $(MAKE) -C $$d; done

mods/ssr:
	$(MAKE) -C mods/ssr

modules:
	@for d in $(MODULE_DIRS); do $(MAKE) -C $$d; done

run: all
	./start.sh

MODS != cat mods.load

test-data-dirs:
	mkdir -p items/poem/items items/song/items items/songbook/items items/choir/items

unit-tests: all test-data-dirs
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

clean:
	@for d in $(MOD_DIRS) $(MODULE_DIRS); do $(MAKE) -C $$d clean; done

distclean:
	@for d in $(MOD_DIRS) $(MODULE_DIRS); do $(MAKE) -C $$d distclean; done

.PHONY: all mods modules run clean distclean test unit-tests pages-test integration-tests e2e-tests test-data-dirs mods/ssr
