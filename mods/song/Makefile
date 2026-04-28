EXTRA_CFLAGS += -I./lib/transp
EXTRA_LDLIBS += ./lib/transp/libtransp.a

include ../../build.mk

all: transp.so client-wasm $(TARGET)

transp.so:
	${MAKE} -C lib/transp

client-wasm:
	@if command -v wasm-bindgen >/dev/null 2>&1 && rustup target list --installed | grep -qx wasm32-unknown-unknown; then \
		${MAKE} -C client; \
	else \
		echo "Skipping song wasm client build (need wasm-bindgen and rustup target wasm32-unknown-unknown)"; \
	fi
