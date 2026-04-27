EXTRA_CFLAGS += -I./lib/transp
EXTRA_LDLIBS += ./lib/transp/libtransp.a

all: transp.so client-wasm song.so

include ../../build.mk

transp.so:
	${MAKE} -C lib/transp

client-wasm:
	@if command -v wasm-bindgen >/dev/null 2>&1 && rustup target list --installed | grep -qx wasm32-unknown-unknown; then \
		${MAKE} -C client; \
	else \
		echo "Skipping song wasm client build (need wasm-bindgen and rustup target wasm32-unknown-unknown)"; \
	fi
