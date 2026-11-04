#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ttypt/iso.h>

/*───────────────────────────────────────────────*
 *            SURFACE & 2D CANVAS               *
 *───────────────────────────────────────────────*/

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t *pixels;
} lamina_surface_t;

ISO_EXPORT(lamina_surface_t *, lamina_surface_new, uint32_t w, uint32_t h)
{
    lamina_surface_t *s = (lamina_surface_t *)malloc(sizeof(lamina_surface_t));
    if (!s) return NULL;
    s->width = w;
    s->height = h;
    s->pixels = (uint32_t *)calloc(w * h, sizeof(uint32_t));
    if (!s->pixels) {
        free(s);
        return NULL;
    }
    return s;
}

ISO_EXPORT(void, lamina_surface_free, lamina_surface_t *s)
{
    if (!s) return;
    if (s->pixels) free(s->pixels);
    free(s);
}

ISO_EXPORT(uint32_t *, lamina_surface_get_pixels, lamina_surface_t *s)
{
    return s ? s->pixels : NULL;
}

ISO_EXPORT(uint32_t, lamina_surface_get_width, lamina_surface_t *s)
{
    return s ? s->width : 0;
}

ISO_EXPORT(uint32_t, lamina_surface_get_height, lamina_surface_t *s)
{
    return s ? s->height : 0;
}

ISO_EXPORT(void, lamina_surface_clear, lamina_surface_t *s, uint32_t color)
{
    if (!s || !s->pixels) return;
    uint32_t total = s->width * s->height;
    for (uint32_t i = 0; i < total; i++) {
        s->pixels[i] = color;
    }
}

ISO_EXPORT(void, lamina_surface_fill, lamina_surface_t *s, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color)
{
    if (!s || !s->pixels || w <= 0 || h <= 0) return;
    int32_t x0 = x < 0 ? 0 : x;
    int32_t y0 = y < 0 ? 0 : y;
    int32_t x1 = (x + w) > (int32_t)s->width ? (int32_t)s->width : (x + w);
    int32_t y1 = (y + h) > (int32_t)s->height ? (int32_t)s->height : (y + h);

    for (int32_t cy = y0; cy < y1; cy++) {
        uint32_t *row = s->pixels + (cy * s->width);
        for (int32_t cx = x0; cx < x1; cx++) {
            row[cx] = color;
        }
    }
}

ISO_EXPORT(void, lamina_surface_rect, lamina_surface_t *s, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color, int32_t thickness)
{
    if (!s || !s->pixels || w <= 0 || h <= 0 || thickness <= 0) return;
    /* Top, bottom, left, right borders */
    lamina_surface_fill(s, x, y, w, thickness, color);
    lamina_surface_fill(s, x, y + h - thickness, w, thickness, color);
    lamina_surface_fill(s, x, y, thickness, h, color);
    lamina_surface_fill(s, x + w - thickness, y, thickness, h, color);
}

ISO_EXPORT(void, lamina_surface_paint, lamina_surface_t *s, int32_t x, int32_t y, uint32_t color)
{
    if (!s || !s->pixels) return;
    if (x >= 0 && x < (int32_t)s->width && y >= 0 && y < (int32_t)s->height) {
        s->pixels[y * s->width + x] = color;
    }
}

ISO_EXPORT(uint32_t, lamina_surface_pick, lamina_surface_t *s, int32_t x, int32_t y)
{
    if (!s || !s->pixels) return 0;
    if (x >= 0 && x < (int32_t)s->width && y >= 0 && y < (int32_t)s->height) {
        return s->pixels[y * s->width + x];
    }
    return 0;
}

ISO_EXPORT(void, lamina_surface_line, lamina_surface_t *s, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color, int32_t thickness)
{
    if (!s || !s->pixels) return;
    /* Bresenham's line algorithm with thickness */
    int32_t dx = abs(x1 - x0);
    int32_t dy = -abs(y1 - y0);
    int32_t sx = x0 < x1 ? 1 : -1;
    int32_t sy = y0 < y1 ? 1 : -1;
    int32_t err = dx + dy;

    int32_t ht = thickness / 2;

    while (1) {
        if (thickness <= 1) {
            lamina_surface_paint(s, x0, y0, color);
        } else {
            lamina_surface_fill(s, x0 - ht, y0 - ht, thickness, thickness, color);
        }
        if (x0 == x1 && y0 == y1) break;
        int32_t e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

/*───────────────────────────────────────────────*
 *            VIEWPORT & PROJECTIONS            *
 *───────────────────────────────────────────────*/

ISO_EXPORT(void, lamina_wasm_world_to_screen,
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

ISO_EXPORT(void, lamina_wasm_screen_to_world,
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

/*───────────────────────────────────────────────*
 *             FLEXBOX UI LAYOUT                *
 *───────────────────────────────────────────────*/

typedef struct lamina_node {
    int32_t dir;             /* 0 = row, 1 = column */
    double width, height;    /* requested size */
    double flex_grow;
    double flex_shrink;
    double pad_left, pad_top, pad_right, pad_bottom;
    int32_t align_items;     /* 0 = flex-start, 1 = center, 2 = flex-end */
    int32_t justify_content; /* 0 = flex-start, 1 = center, 2 = space-between */

    /* Computed layout results */
    double computed_x;
    double computed_y;
    double computed_w;
    double computed_h;

    /* Hierarchy */
    struct lamina_node **children;
    uint32_t child_count;
    uint32_t child_cap;
} lamina_node_t;

ISO_EXPORT(lamina_node_t *, lamina_node_new, int32_t dir)
{
    lamina_node_t *n = (lamina_node_t *)calloc(1, sizeof(lamina_node_t));
    if (!n) return NULL;
    n->dir = dir;
    n->flex_shrink = 1.0;
    return n;
}

ISO_EXPORT(void, lamina_node_set_size, lamina_node_t *n, double w, double h)
{
    if (!n) return;
    n->width = w;
    n->height = h;
}

ISO_EXPORT(void, lamina_node_set_padding, lamina_node_t *n, double l, double t, double r, double b)
{
    if (!n) return;
    n->pad_left = l;
    n->pad_top = t;
    n->pad_right = r;
    n->pad_bottom = b;
}

ISO_EXPORT(void, lamina_node_set_flex, lamina_node_t *n, double grow, double shrink)
{
    if (!n) return;
    n->flex_grow = grow;
    n->flex_shrink = shrink;
}

ISO_EXPORT(void, lamina_node_add_child, lamina_node_t *parent, lamina_node_t *child)
{
    if (!parent || !child) return;
    if (parent->child_count == parent->child_cap) {
        uint32_t new_cap = parent->child_cap == 0 ? 4 : parent->child_cap * 2;
        lamina_node_t **arr = (lamina_node_t **)realloc(parent->children, new_cap * sizeof(lamina_node_t *));
        if (!arr) return;
        parent->children = arr;
        parent->child_cap = new_cap;
    }
    parent->children[parent->child_count++] = child;
}

ISO_EXPORT(void, lamina_node_compute_layout, lamina_node_t *root, double x, double y, double avail_w, double avail_h)
{
    if (!root) return;

    root->computed_x = x;
    root->computed_y = y;
    root->computed_w = root->width > 0 ? root->width : avail_w;
    root->computed_h = root->height > 0 ? root->height : avail_h;

    double inner_x = root->computed_x + root->pad_left;
    double inner_y = root->computed_y + root->pad_top;
    double inner_w = root->computed_w - (root->pad_left + root->pad_right);
    double inner_h = root->computed_h - (root->pad_top + root->pad_bottom);
    if (inner_w < 0) inner_w = 0;
    if (inner_h < 0) inner_h = 0;

    if (root->child_count == 0) return;

    double total_grow = 0.0;
    double fixed_main = 0.0;

    for (uint32_t i = 0; i < root->child_count; i++) {
        lamina_node_t *c = root->children[i];
        double child_main = root->dir == 0 ? c->width : c->height;
        if (child_main > 0) {
            fixed_main += child_main;
        }
        total_grow += c->flex_grow;
    }

    double main_avail = root->dir == 0 ? inner_w : inner_h;
    double remaining = main_avail - fixed_main;
    if (remaining < 0) remaining = 0;

    double cur_main = root->dir == 0 ? inner_x : inner_y;

    for (uint32_t i = 0; i < root->child_count; i++) {
        lamina_node_t *c = root->children[i];
        double child_w = 0.0;
        double child_h = 0.0;

        if (root->dir == 0) { /* row */
            if (c->flex_grow > 0 && total_grow > 0) {
                child_w = (c->flex_grow / total_grow) * remaining;
            } else {
                child_w = c->width > 0 ? c->width : (inner_w / root->child_count);
            }
            child_h = c->height > 0 ? c->height : inner_h;

            lamina_node_compute_layout(c, cur_main, inner_y, child_w, child_h);
            cur_main += child_w;
        } else { /* column */
            if (c->flex_grow > 0 && total_grow > 0) {
                child_h = (c->flex_grow / total_grow) * remaining;
            } else {
                child_h = c->height > 0 ? c->height : (inner_h / root->child_count);
            }
            child_w = c->width > 0 ? c->width : inner_w;

            lamina_node_compute_layout(c, inner_x, cur_main, child_w, child_h);
            cur_main += child_h;
        }
    }
}

ISO_EXPORT(void, lamina_node_get_bounds, lamina_node_t *n, double *out)
{
    if (!n || !out) return;
    out[0] = n->computed_x;
    out[1] = n->computed_y;
    out[2] = n->computed_w;
    out[3] = n->computed_h;
}

ISO_EXPORT(void, lamina_node_free, lamina_node_t *n)
{
    if (!n) return;
    for (uint32_t i = 0; i < n->child_count; i++) {
        lamina_node_free(n->children[i]);
    }
    if (n->children) free(n->children);
    free(n);
}

/*───────────────────────────────────────────────*
 *            VERSION & METADATA                *
 *───────────────────────────────────────────────*/

static const char LAMINA_VERSION_STR[] = "1.0.1";

ISO_EXPORT(const char *, lamina_version, void)
{
    return LAMINA_VERSION_STR;
}
