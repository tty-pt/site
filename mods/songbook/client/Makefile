OUT_DIR = ../../../htdocs
OUT_NAME = songbook_client
TARGET = ../../../target/wasm32-unknown-unknown/release/ndc_songbook_client.wasm
OUTPUTS = ${OUT_DIR}/${OUT_NAME}.js ${OUT_DIR}/${OUT_NAME}_bg.wasm

all: ${OUTPUTS}

${TARGET}: Cargo.toml src/lib.rs
	cargo build --manifest-path Cargo.toml --target wasm32-unknown-unknown --release --target-dir ../../../target

${OUTPUTS}: ${TARGET}
	wasm-bindgen --target web --no-typescript --out-dir ${OUT_DIR} --out-name ${OUT_NAME} ${TARGET}

clean:
	@true

distclean:
	cargo clean --manifest-path Cargo.toml
	rm -f ${OUT_DIR}/${OUT_NAME}.js ${OUT_DIR}/${OUT_NAME}_bg.wasm

.PHONY: all clean distclean
