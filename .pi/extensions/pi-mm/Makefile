.PHONY: all test lint complexity benchmark zip clean

ROOT_DIR := $(shell cd ../../.. && pwd)
BUNDLE := $(ROOT_DIR)/pi-mm-bundle.zip

all: test

test:
	deno test --allow-all --sloppy-imports --node-modules-dir=none tests/

lint:
	deno lint src/ tests/ scripts/

complexity:
	deno run --allow-all scripts/check-complexity.ts

benchmark:
	deno run --allow-all --sloppy-imports scripts/benchmark-mm.ts

zip:
	rm -f $(BUNDLE)
	rm -rf .stage
	mkdir -p .stage
	cp -r index.ts package.json Makefile README.md AGENTS.md IMPLEMENTATION_STATUS.md IDEA.md src skills tests scripts patches .stage/
	cd .stage && zip -r -q $(BUNDLE) .
	rm -rf .stage
	@echo "Generated $(BUNDLE)"

clean:
	rm -f $(BUNDLE)
