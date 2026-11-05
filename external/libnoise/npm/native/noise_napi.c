#include <node_api.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#define XXH_INLINE_ALL 1
#include <xxhash.h>
#include <ttypt/noise.h>

static napi_value NoisePoint1D(napi_env env, napi_callback_info info) {
    size_t argc = 3;
    napi_value args[3];
    napi_get_cb_info(env, info, &argc, args, NULL, NULL);

    if (argc < 3) {
        napi_throw_type_error(env, NULL, "Expected 3 arguments: (x, seed, shift)");
        return NULL;
    }

    int32_t x = 0, shift = 0;
    uint32_t seed = 0;
    napi_get_value_int32(env, args[0], &x);
    napi_get_value_uint32(env, args[1], &seed);
    napi_get_value_int32(env, args[2], &shift);

    int val = noise_point_1d((int16_t)x, seed, (uint8_t)shift);
    napi_value res;
    napi_create_int32(env, val, &res);
    return res;
}

static napi_value NoisePoint2D(napi_env env, napi_callback_info info) {
    size_t argc = 4;
    napi_value args[4];
    napi_get_cb_info(env, info, &argc, args, NULL, NULL);

    if (argc < 4) {
        napi_throw_type_error(env, NULL, "Expected 4 arguments: (x, y, seed, shift)");
        return NULL;
    }

    int32_t x = 0, y = 0, shift = 0;
    uint32_t seed = 0;
    napi_get_value_int32(env, args[0], &x);
    napi_get_value_int32(env, args[1], &y);
    napi_get_value_uint32(env, args[2], &seed);
    napi_get_value_int32(env, args[3], &shift);

    int val = noise_point_2d((int16_t)x, (int16_t)y, seed, (uint8_t)shift);
    napi_value res;
    napi_create_int32(env, val, &res);
    return res;
}

static napi_value NoisePoint3D(napi_env env, napi_callback_info info) {
    size_t argc = 5;
    napi_value args[5];
    napi_get_cb_info(env, info, &argc, args, NULL, NULL);

    if (argc < 5) {
        napi_throw_type_error(env, NULL, "Expected 5 arguments: (x, y, z, seed, shift)");
        return NULL;
    }

    int32_t x = 0, y = 0, z = 0, shift = 0;
    uint32_t seed = 0;
    napi_get_value_int32(env, args[0], &x);
    napi_get_value_int32(env, args[1], &y);
    napi_get_value_int32(env, args[2], &z);
    napi_get_value_uint32(env, args[3], &seed);
    napi_get_value_int32(env, args[4], &shift);

    int val = noise_point_3d((int16_t)x, (int16_t)y, (int16_t)z, seed, (uint8_t)shift);
    napi_value res;
    napi_create_int32(env, val, &res);
    return res;
}

static napi_value NoisePoint4D(napi_env env, napi_callback_info info) {
    size_t argc = 6;
    napi_value args[6];
    napi_get_cb_info(env, info, &argc, args, NULL, NULL);

    if (argc < 6) {
        napi_throw_type_error(env, NULL, "Expected 6 arguments: (x, y, z, w, seed, shift)");
        return NULL;
    }

    int32_t x = 0, y = 0, z = 0, w = 0, shift = 0;
    uint32_t seed = 0;
    napi_get_value_int32(env, args[0], &x);
    napi_get_value_int32(env, args[1], &y);
    napi_get_value_int32(env, args[2], &z);
    napi_get_value_int32(env, args[3], &w);
    napi_get_value_uint32(env, args[4], &seed);
    napi_get_value_int32(env, args[5], &shift);

    int16_t p[4] = { (int16_t)x, (int16_t)y, (int16_t)z, (int16_t)w };
    int val = noise_point_n(p, seed, (uint8_t)shift, 4);
    napi_value res;
    napi_create_int32(env, val, &res);
    return res;
}

static napi_value NoisePointN(napi_env env, napi_callback_info info) {
    size_t argc = 3;
    napi_value args[3];
    napi_get_cb_info(env, info, &argc, args, NULL, NULL);

    if (argc < 3) {
        napi_throw_type_error(env, NULL, "Expected 3 arguments: (coords, seed, shift)");
        return NULL;
    }

    uint32_t dim = 0;
    napi_get_array_length(env, args[0], &dim);
    if (dim == 0 || dim > 4) {
        napi_value res;
        napi_create_int32(env, 128, &res);
        return res;
    }

    int16_t p[4];
    for (uint32_t i = 0; i < dim; i++) {
        napi_value el;
        napi_get_element(env, args[0], i, &el);
        int32_t c = 0;
        napi_get_value_int32(env, el, &c);
        p[i] = (int16_t)c;
    }

    uint32_t seed = 0;
    int32_t shift = 0;
    napi_get_value_uint32(env, args[1], &seed);
    napi_get_value_int32(env, args[2], &shift);

    int val = noise_point_n(p, seed, (uint8_t)shift, (uint8_t)dim);
    napi_value res;
    napi_create_int32(env, val, &res);
    return res;
}

static napi_value NoiseOctaves(napi_env env, napi_callback_info info) {
    size_t argc = 5;
    napi_value args[5];
    napi_get_cb_info(env, info, &argc, args, NULL, NULL);

    if (argc < 5) {
        napi_throw_type_error(env, NULL, "Expected 5 arguments: (dim, origin, octaves, seed, cy)");
        return NULL;
    }

    int32_t dim = 0, cy = 0;
    uint32_t seed = 0;
    napi_get_value_int32(env, args[0], &dim);
    napi_get_value_uint32(env, args[3], &seed);
    napi_get_value_int32(env, args[4], &cy);

    if (dim <= 0 || dim > 4 || cy <= 0 || cy > 10) {
        napi_throw_error(env, NULL, "Invalid dim or cy");
        return NULL;
    }

    uint32_t origin_len = 0;
    napi_get_array_length(env, args[1], &origin_len);
    int16_t s[4] = {0, 0, 0, 0};
    for (uint32_t i = 0; i < origin_len && i < 4; i++) {
        napi_value el;
        napi_get_element(env, args[1], i, &el);
        int32_t c = 0;
        napi_get_value_int32(env, el, &c);
        s[i] = (int16_t)c;
    }

    uint32_t oct_n = 0;
    napi_get_array_length(env, args[2], &oct_n);
    if (oct_n > 16) oct_n = 16;
    octave_t oct[16];
    for (uint32_t i = 0; i < oct_n; i++) {
        napi_value item;
        napi_get_element(env, args[2], i, &item);
        napi_value x_val, w_val;
        napi_get_named_property(env, item, "x", &x_val);
        napi_get_named_property(env, item, "w", &w_val);
        uint32_t x = 0, w = 0;
        napi_get_value_uint32(env, x_val, &x);
        napi_get_value_uint32(env, w_val, &w);
        oct[i].x = x;
        oct[i].w = w;
    }

    size_t total_elements = 1ULL << (dim * cy);
    void *data = NULL;
    napi_value arraybuffer;
    napi_create_arraybuffer(env, total_elements * sizeof(uint32_t), &data, &arraybuffer);
    if (!data) {
        napi_throw_error(env, NULL, "Failed to allocate ArrayBuffer");
        return NULL;
    }

    noise_oct((uint32_t *)data, s, oct_n, oct, seed, (unsigned)cy, (uint8_t)dim);

    napi_value typedarray;
    napi_create_typedarray(env, napi_uint32_array, total_elements, arraybuffer, 0, &typedarray);
    return typedarray;
}

NAPI_MODULE_INIT() {
    napi_property_descriptor desc[] = {
        { "point1D", NULL, NoisePoint1D, NULL, NULL, NULL, napi_default, NULL },
        { "point2D", NULL, NoisePoint2D, NULL, NULL, NULL, napi_default, NULL },
        { "point3D", NULL, NoisePoint3D, NULL, NULL, NULL, napi_default, NULL },
        { "point4D", NULL, NoisePoint4D, NULL, NULL, NULL, napi_default, NULL },
        { "pointN", NULL, NoisePointN, NULL, NULL, NULL, napi_default, NULL },
        { "octaves", NULL, NoiseOctaves, NULL, NULL, NULL, napi_default, NULL }
    };
    napi_define_properties(env, exports, sizeof(desc) / sizeof(desc[0]), desc);
    return exports;
}
