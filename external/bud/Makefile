FOLDER := bud
all := libbud bud_test

LDLIBS-libbud :=
libbud-obj-y := src/bud_wasm_app.o
LDLIBS-bud_test := -lbud

include ../mk/include.mk

test: all
	LD_LIBRARY_PATH=./lib ./bin/bud_test

objects-set.mk: Makefile

.PHONY: all test

