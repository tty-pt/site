OUT_DIR = ../../../htdocs
OUT_NAME = song-client
TARGET = target/wasm32-unknown-unknown/release/ndc_song_client.wasm
OUTPUTS = ${OUT_DIR}/${OUT_NAME}.js ${OUT_DIR}/${OUT_NAME}_bg.wasm

all:
	@if command -v wasm-bindgen >/dev/null 2>&1 && rustup target list --installed | grep -qx wasm32-unknown-unknown; then \
		${MAKE} ${OUTPUTS}; \
	else \
		echo "Skipping song client wasm build (need wasm-bindgen and rustup target wasm32-unknown-unknown)"; \
	fi

${TARGET}: Cargo.toml src/lib.rs
	cargo build --manifest-path Cargo.toml --target wasm32-unknown-unknown --release

${OUTPUTS}: ${TARGET}
	wasm-bindgen --target web --no-typescript --out-dir ${OUT_DIR} --out-name ${OUT_NAME} ${TARGET}
	printf '\nawait __wbg_init();\n' >> ${OUT_DIR}/${OUT_NAME}.js

clean:
	@true

distclean:
	cargo clean --manifest-path Cargo.toml
	rm -f ${OUT_DIR}/${OUT_NAME}.js ${OUT_DIR}/${OUT_NAME}_bg.wasm

.PHONY: all clean distclean
