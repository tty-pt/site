FOLDER := transp
all := libtransp test_transp

libtransp-obj-y := src/token.o src/parse.o src/render.o src/music.o
LDLIBS-test_transp := -L./lib -ltransp

include ../mk/include.mk

CFLAGS += -I./include/transp -I./include -I./src $(EXTRA_CFLAGS)

lib/libtransp.a: src/libtransp.o src/token.o src/parse.o src/render.o src/music.o | lib
	ar rcs $@ $^

all: lib/libtransp.a

test: all
	LD_LIBRARY_PATH=./lib ./bin/test_transp

objects-set.mk: Makefile

.PHONY: all test
