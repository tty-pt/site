#ifndef ND_WEBGL_H
#define ND_WEBGL_H

/*
 * nd_webgl.h — WebGL 3D hardware-accelerated scene driver for NeverDark.
 * Constructs 3D room geometry, wall/exit boundaries, dynamic lighting,
 * and entity representations from the synchronized nd_scene_t.
 */

#include <stdint.h>
#include <stddef.h>
#include "nd_scene.h"
#include <ttypt/webgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize WebGL shaders, program, and vertex buffers */
int nd_webgl_init(void);

/* Render full 3D scene from the synchronized scene graph */
void nd_webgl_render_scene(const nd_scene_t *scene);

#ifdef __cplusplus
}
#endif

#endif /* ND_WEBGL_H */
