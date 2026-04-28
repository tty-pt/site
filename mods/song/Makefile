EXTRA_CFLAGS += -I./lib/transp
EXTRA_LDLIBS += ./lib/transp/libtransp.a

include ../../build.mk

all: transp.so client-wasm $(TARGET)

transp.so:
	${MAKE} -C lib/transp

client-wasm:
	@if command -v wasm-bindgen >/dev/null 2>&1 && command -v rustc >/dev/null 2>&1 && rustc --print target-list | grep -q wasm32-unknown-unknown; then \
		${MAKE} -C client; \
	else \
		echo "Skipping song wasm client build (missing wasm-bindgen or target wasm32-unknown-unknown)"; \
	fi
