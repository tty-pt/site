#include <node_api.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

static napi_value LaminaWorldToScreen(napi_env env, napi_callback_info info) {
    size_t argc = 7;
    napi_value args[7];
    napi_get_cb_info(env, info, &argc, args, NULL, NULL);

    if (argc < 7) {
        napi_throw_type_error(env, NULL, "Expected (worldX, worldY, cx, cy, zoom, width, height)");
        return NULL;
    }

    double wx = 0, wy = 0, cx = 0, cy = 0, zoom = 1.0, width = 640, height = 480;
    napi_get_value_double(env, args[0], &wx);
    napi_get_value_double(env, args[1], &wy);
    napi_get_value_double(env, args[2], &cx);
    napi_get_value_double(env, args[3], &cy);
    napi_get_value_double(env, args[4], &zoom);
    napi_get_value_double(env, args[5], &width);
    napi_get_value_double(env, args[6], &height);

    double half_w = width / 2.0;
    double half_h = height / 2.0;
    double sx = half_w + (wx - cx) * zoom;
    double sy = half_h + (wy - cy) * zoom;

    napi_value res;
    napi_create_object(env, &res);

    napi_value x_val, y_val;
    napi_create_double(env, round(sx), &x_val);
    napi_create_double(env, round(sy), &y_val);

    napi_set_named_property(env, res, "x", x_val);
    napi_set_named_property(env, res, "y", y_val);
    return res;
}

static napi_value LaminaScreenToWorld(napi_env env, napi_callback_info info) {
    size_t argc = 7;
    napi_value args[7];
    napi_get_cb_info(env, info, &argc, args, NULL, NULL);

    if (argc < 7) {
        napi_throw_type_error(env, NULL, "Expected (screenX, screenY, cx, cy, zoom, width, height)");
        return NULL;
    }

    double sx = 0, sy = 0, cx = 0, cy = 0, zoom = 1.0, width = 640, height = 480;
    napi_get_value_double(env, args[0], &sx);
    napi_get_value_double(env, args[1], &sy);
    napi_get_value_double(env, args[2], &cx);
    napi_get_value_double(env, args[3], &cy);
    napi_get_value_double(env, args[4], &zoom);
    napi_get_value_double(env, args[5], &width);
    napi_get_value_double(env, args[6], &height);

    double half_w = width / 2.0;
    double half_h = height / 2.0;
    double wx = (sx - half_w) / zoom + cx;
    double wy = (sy - half_h) / zoom + cy;

    napi_value res;
    napi_create_object(env, &res);

    napi_value x_val, y_val;
    napi_create_double(env, round(wx), &x_val);
    napi_create_double(env, round(wy), &y_val);

    napi_set_named_property(env, res, "x", x_val);
    napi_set_named_property(env, res, "y", y_val);
    return res;
}

static napi_value Init(napi_env env, napi_value exports) {
    napi_property_descriptor desc[] = {
        {"worldToScreen", NULL, LaminaWorldToScreen, NULL, NULL, NULL, napi_default, NULL},
        {"screenToWorld", NULL, LaminaScreenToWorld, NULL, NULL, NULL, napi_default, NULL}
    };
    napi_define_properties(env, exports, sizeof(desc) / sizeof(desc[0]), desc);
    return exports;
}

NAPI_MODULE(NODE_GYP_MODULE_NAME, Init)
