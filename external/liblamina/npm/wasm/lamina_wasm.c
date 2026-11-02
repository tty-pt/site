#include <stddef.h>
#include <stdint.h>
#include <math.h>

void lamina_wasm_world_to_screen(
    double wx, double wy,
    double cx, double cy,
    double zoom,
    double width, double height,
    double *out)
{
    double half_w = width / 2.0;
    double half_h = height / 2.0;
    out[0] = half_w + (wx - cx) * zoom;
    out[1] = half_h + (wy - cy) * zoom;
}

void lamina_wasm_screen_to_world(
    double sx, double sy,
    double cx, double cy,
    double zoom,
    double width, double height,
    double *out)
{
    double half_w = width / 2.0;
    double half_h = height / 2.0;
    out[0] = (sx - half_w) / zoom + cx;
    out[1] = (sy - half_h) / zoom + cy;
}
