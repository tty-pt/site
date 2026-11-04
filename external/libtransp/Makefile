FOLDER := transp
all := libtransp test_transp

libtransp-obj-y := src/token.o src/parse.o src/render.o src/music.o
LDLIBS-test_transp := -L./lib -ltransp

WASM_SRCS := src/token.c src/parse.c src/render.c src/music.c src/libtransp.c npm/wasm/transp_wasm.c
WASM_EXTRA_CFLAGS := -Iinclude/transp -Iinclude -Isrc

include ../mk/include.mk

CFLAGS += -I./include/transp -I./include -I./src $(EXTRA_CFLAGS)

lib/libtransp.a: src/libtransp.o src/token.o src/parse.o src/render.o src/music.o lib
	ar rcs $@ src/libtransp.o src/token.o src/parse.o src/render.o src/music.o

all: lib/libtransp.a

test: all
	LD_LIBRARY_PATH=./lib ./bin/test_transp

# Optional Node.js & WebAssembly bindings
NPM ?= npm

npm-build:
	@if command -v $(NPM) >/dev/null 2>&1; then \
		(cd npm && $(NPM) run build); \
	else \
		echo "Skipping npm build — $(NPM) not found"; \
	fi

npm-test: npm-build
	@if command -v $(NPM) >/dev/null 2>&1; then \
		(cd npm && $(NPM) test); \
	fi

objects-set.mk: Makefile

.PHONY: all test npm-build npm-test

