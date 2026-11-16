# libcorolla Makefile — Generic Parametric 2-Manifold, Self-Occlusion & Orbital Irradiance Library
PREFIX ?= /usr/local
DESTDIR ?=

CC ?= gcc
CFLAGS ?= -Wall -Wextra -Werror -O2 -Iinclude -I../libchaff/include
LDLIBS ?= -lm

all: lib/libcorolla.so test

lib/libcorolla.so: src/corolla.c include/ttypt/corolla.h
	@mkdir -p lib
	$(CC) $(CFLAGS) -fPIC -shared -o $@ src/corolla.c $(LDLIBS)

test: bin/test_corolla
	./bin/test_corolla

bin/test_corolla: tests/test_corolla.c src/corolla.c include/ttypt/corolla.h
	@mkdir -p bin
	$(CC) $(CFLAGS) -o $@ tests/test_corolla.c src/corolla.c $(LDLIBS)

install: lib/libcorolla.so corolla.pc
	install -d $(DESTDIR)$(PREFIX)/include/ttypt
	install -m 644 include/ttypt/corolla.h $(DESTDIR)$(PREFIX)/include/ttypt/
	install -d $(DESTDIR)$(PREFIX)/lib
	install -m 755 lib/libcorolla.so $(DESTDIR)$(PREFIX)/lib/
	install -d $(DESTDIR)$(PREFIX)/lib/pkgconfig
	install -m 644 corolla.pc $(DESTDIR)$(PREFIX)/lib/pkgconfig/

clean:
	rm -rf bin lib

.PHONY: all test install clean
