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

clean:
	@for d in $(MOD_DIRS) $(MODULE_DIRS); do $(MAKE) -C $$d clean; done
	rm -f core.so module.db mods.load

.PHONY: all mods modules run clean
