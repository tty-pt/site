# libnoise Makefile
CC ?= gcc
CFLAGS ?= -Wall -Wextra -Werror -O2 -Iinclude
LDLIBS ?= -lxxhash

WASM_CC ?= clang
WASM_CFLAGS ?= --target=wasm32-wasi -ffreestanding -flto -O3 -D__wasm__ -fvisibility=hidden -I./include -I./npm/include -I../mk/include -I/usr/include
WASM_LDFLAGS ?= -mexec-model=reactor -Wl,--gc-sections -Wl,--no-entry -Wl,--allow-undefined -Wl,--export=malloc -Wl,--export=free -Wl,--export=noise_wasm_point_1d -Wl,--export=noise_wasm_point_2d -Wl,--export=noise_wasm_point_3d -Wl,--export=noise_wasm_point_4d -Wl,--export=noise_wasm_point_n -Wl,--export=noise_wasm_octaves

SO_CFLAGS ?= -mgeneral-regs-only

all: test lib/libnoise.so wasm

test: bin/test_noise
	./bin/test_noise

bin/test_noise: tests/test_noise.c include/ttypt/noise.h
	@mkdir -p bin
	$(CC) $(CFLAGS) -o $@ tests/test_noise.c $(LDLIBS)

lib/libnoise.so: src/libnoise.c include/ttypt/noise.h include/ttypt/chaff.h
	@mkdir -p lib
	$(CC) $(CFLAGS) $(SO_CFLAGS) -fPIC -shared -o $@ src/libnoise.c $(LDLIBS)
	@ln -sf libnoise.so lib/libchaff.so

wasm: npm/dist/noise.wasm

npm/dist/noise.wasm: npm/wasm/noise_wasm.c include/ttypt/noise.h
	@mkdir -p npm/dist
	$(WASM_CC) $(WASM_CFLAGS) $(WASM_LDFLAGS) npm/wasm/noise_wasm.c -o $@

npm-build: wasm
	@mkdir -p npm/build/Release
	@gcc -O3 -fPIC -shared -I/usr/include/node -I./include -I/usr/include npm/native/noise_napi.c -L/usr/lib -lxxhash -o npm/build/Release/noise_node.node 2>/dev/null || true

npm-test: npm-build
	@node --test npm/tests/noise.test.mjs

clean:
	rm -rf bin lib npm/build npm/dist/*.wasm

.PHONY: all test wasm npm-build npm-test clean
