#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <transp/transp.h>
#include <ttypt/iso.h>

static transp_ctx_t *s_ctx = NULL;

ISO_EXPORT(char *, transp_wasm_transpose, const char *input, int semitones, int flags) {
    if (!s_ctx) {
        s_ctx = transp_init();
    }
    if (!s_ctx || !input) return NULL;
    return transp_buffer(s_ctx, input, semitones, flags);
}

ISO_EXPORT(int, transp_wasm_get_key, void) {
    if (!s_ctx) return -1;
    return transp_get_key(s_ctx);
}

ISO_EXPORT(void, transp_wasm_reset_key, void) {
    if (s_ctx) transp_reset_key(s_ctx);
}

ISO_EXPORT(char *, transp_wasm_shift_table, int latin) {
    if (!s_ctx) s_ctx = transp_init();
    if (!s_ctx) return NULL;
    return transp_shift_table(s_ctx, latin);
}
