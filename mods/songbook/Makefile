include ../../build.mk

all: client-wasm $(TARGET)

client-wasm:
	@if command -v wasm-bindgen >/dev/null 2>&1 && rustup target list --installed | grep -qx wasm32-unknown-unknown; then \
		${MAKE} -C client; \
	else \
		echo "Skipping songbook wasm client build (need wasm-bindgen and rustup target wasm32-unknown-unknown)"; \
	fi
