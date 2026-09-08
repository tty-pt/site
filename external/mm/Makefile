REPO_ROOT := $(abspath ../..)
QMAP_DIR  := $(REPO_ROOT)/external/libqmap
STOMA_DIR := $(REPO_ROOT)/external/stoma

CC       ?= clang
CFLAGS   = -std=c11 -g -O0 -Wall -Wextra -Wpedantic -D_POSIX_C_SOURCE=200809L
CFLAGS  += -I$(QMAP_DIR)/include -I$(STOMA_DIR)/include
LDFLAGS  = -L$(QMAP_DIR)/lib -L$(STOMA_DIR)/lib
LDLIBS   = -lqmap -lstoma -lm
RPATH    = -Wl,-rpath,$(QMAP_DIR)/lib -Wl,-rpath,$(STOMA_DIR)/lib

BIN := bin/mm bin/engine_test

all: $(BIN)

bin:
	mkdir -p bin

bin/mm: src/mm.c src/engine.c src/engine.h | bin
	$(CC) $(CFLAGS) -o $@ src/mm.c src/engine.c $(LDFLAGS) $(LDLIBS) $(RPATH)

bin/engine_test: src/engine_test.c src/engine.c src/engine.h | bin
	$(CC) $(CFLAGS) -o $@ src/engine_test.c src/engine.c $(LDFLAGS) $(LDLIBS) $(RPATH)

test: all
	./test.sh

clean:
	rm -rf bin

.PHONY: all test clean