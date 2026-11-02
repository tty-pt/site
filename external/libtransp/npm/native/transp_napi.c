#include <node_api.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include <transp/transp.h>
#include <transp/transp_flags.h>

static napi_value TranspTranspose(napi_env env, napi_callback_info info) {
    size_t argc = 3;
    napi_value args[3];
    napi_get_cb_info(env, info, &argc, args, NULL, NULL);

    if (argc < 2) {
        napi_throw_type_error(env, NULL, "Expected (text, semitones, optionalFlags)");
        return NULL;
    }

    size_t in_len = 0;
    napi_get_value_string_utf8(env, args[0], NULL, 0, &in_len);

    char *in_buf = (char *)malloc(in_len + 1);
    if (!in_buf) {
        napi_throw_error(env, NULL, "Out of memory");
        return NULL;
    }
    napi_get_value_string_utf8(env, args[0], in_buf, in_len + 1, &in_len);

    int32_t semitones = 0;
    napi_get_value_int32(env, args[1], &semitones);

    uint32_t flags = 0;
    if (argc >= 3) {
        napi_get_value_uint32(env, args[2], &flags);
    }

    transp_ctx_t *ctx = transp_init();
    if (!ctx) {
        free(in_buf);
        napi_throw_error(env, NULL, "Failed to initialize transp context");
        return NULL;
    }

    char *result = transp_buffer(ctx, in_buf, semitones, (int)flags);
    transp_free(ctx);
    free(in_buf);

    if (!result) {
        napi_value empty_str;
        napi_create_string_utf8(env, "", 0, &empty_str);
        return empty_str;
    }

    napi_value res;
    napi_create_string_utf8(env, result, NAPI_AUTO_LENGTH, &res);
    free(result);

    return res;
}

static napi_value Init(napi_env env, napi_value exports) {
    napi_property_descriptor desc[] = {
        {"transpose", NULL, TranspTranspose, NULL, NULL, NULL, napi_default, NULL}
    };
    napi_define_properties(env, exports, sizeof(desc) / sizeof(desc[0]), desc);
    return exports;
}

NAPI_MODULE(NODE_GYP_MODULE_NAME, Init)
