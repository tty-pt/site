CC ?= clang

MOD_DIRS := $(sort $(dir $(wildcard mods/*/Makefile)))
MOD_JSONS := $(sort $(wildcard mods/*/mod.json))
MODULE_DIRS := $(sort $(dir $(wildcard modules/*/Makefile)))

all: core.so mods modules module.db

core.so: src/core/main.c
	$(CC) -fPIC -shared -g -o $@ $<

mods:
	@for d in $(MOD_DIRS); do $(MAKE) -C $$d; done

modules:
	@for d in $(MODULE_DIRS); do $(MAKE) -C $$d; done

module.db: $(MOD_JSONS) scripts/gen_mods.py
	@rm -f $@ mods.load
	@python3 scripts/gen_mods.py --mods-dir mods --mods-load mods.load | \
		while read -r line; do qmap -p "$$line" $@; done

run:
	./start.sh

TEST_DIRS := $(sort $(dir $(wildcard mods/*/test.sh)))

test: all
	@for d in $(TEST_DIRS); do \
		echo "=== Running tests in $$d ==="; \
		(cd $$d && ./test.sh) || exit 1; \
	done

pages-test:
	@echo "Starting site in background for pages smoke tests..."
	@./start.sh > /tmp/start_sh.log 2>&1 &
	@sleep 1
	@echo "Running pages smoke tests against ${NDC_HOST:=127.0.0.1}:${NDC_PORT:=8080}"
	@NDC_HOST=${NDC_HOST:=127.0.0.1} NDC_PORT=${NDC_PORT:=8080} \
		sh tests/pages/10-pages-render.sh
	@echo "Stopping background services"
	@pkill -f "ndc -C" || true
	@pkill -f "deno" || true

integration-tests: all
	@sh tests/integration/run_all.sh

clean:
	@for d in $(MOD_DIRS) $(MODULE_DIRS); do $(MAKE) -C $$d clean; done
	rm -f core.so module.db mods.load

.PHONY: all mods modules run clean test
