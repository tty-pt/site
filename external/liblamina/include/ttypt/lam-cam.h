/**
 * @file lam-cam.h
 * @brief 3D Camera, matrix math, and perspective projection for liblamina.
 *
 * Provides a lightweight 3D pipeline for 2.5D tilted-camera and full 3D views,
 * including 4x4 matrix operations, camera setup, ray-plane unprojecting,
 * ground tiles, and billboard sprites.
 */

#ifndef LAM_CAM_H
#define LAM_CAM_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 4x4 matrix in column-major order (OpenGL compatible) */
typedef float lam_mat4_t[16];

/**
 * @brief Initialize matrix to 4x4 identity.
 */
void lam_mat4_identity(float m[16]);

/**
 * @brief Multiply two 4x4 matrices: out = a * b.
 */
void lam_mat4_mul(float out[16], const float a[16], const float b[16]);

/**
 * @brief Apply translation to matrix m.
 */
void lam_mat4_translate(float m[16], float x, float y, float z);

/**
 * @brief Apply scaling to matrix m.
 */
void lam_mat4_scale(float m[16], float sx, float sy, float sz);

/**
 * @brief Rotate matrix m around X axis by angle in radians.
 */
void lam_mat4_rotate_x(float m[16], float rad);

/**
 * @brief Rotate matrix m around Y axis by angle in radians.
 */
void lam_mat4_rotate_y(float m[16], float rad);

/**
 * @brief Rotate matrix m around Z axis by angle in radians.
 */
void lam_mat4_rotate_z(float m[16], float rad);

/**
 * @brief Construct standard perspective projection matrix.
 */
void lam_mat4_perspective(float m[16], float fov_rad, float aspect, float near_z, float far_z);

/**
 * @brief Construct standard orthographic projection matrix.
 */
void lam_mat4_ortho(float m[16], float l, float r, float b, float t, float n, float f);

/**
 * @brief Construct view matrix looking from eye towards target with given up vector.
 */
void lam_mat4_look_at(float m[16], const float eye[3], const float target[3], const float up[3]);

/**
 * @brief Invert a 4x4 matrix. Returns true on success, false if singular.
 */
bool lam_mat4_invert(float out[16], const float in[16]);

/* Camera state descriptor */
typedef struct {
	float target[3];   /* Focus point in world coordinates (X, Y, Z) */
	float distance;    /* Distance from target */
	float pitch_deg;   /* Tilt pitch angle (degrees, e.g. 50.0) */
	float yaw_deg;     /* Rotation yaw angle (degrees, 0 = facing north) */
	float fov_deg;     /* Vertical FOV (degrees, e.g. 45.0) */
	float near_z;      /* Near clipping plane (e.g. 0.1) */
	float far_z;       /* Far clipping plane (e.g. 1000.0) */
	int active;        /* 1 if 3D camera is currently enabled, 0 for 2D */

	/* Computed matrices and vectors */
	lam_mat4_t view;
	lam_mat4_t proj;
	lam_mat4_t vp;
	lam_mat4_t inv_vp;
	float eye[3];
	float right[3];
	float up[3];
} lam_cam_t;

/**
 * @brief Configure and activate the 3D camera.
 *
 * @param target_x, target_y Target world coordinates on the ground plane
 * @param distance           Camera distance / zoom
 * @param pitch_deg          Camera tilt angle in degrees (e.g. 45..60)
 * @param yaw_deg            Camera yaw rotation angle in degrees
 * @param fov_deg            Field of view in degrees (e.g. 45.0)
 */
void lam_cam_set_3d(float target_x, float target_y, float distance,
                    float pitch_deg, float yaw_deg, float fov_deg);

/**
 * @brief Restore classic 2D orthographic rendering mode.
 */
void lam_cam_set_2d(void);

/**
 * @brief Check if 3D camera mode is currently active.
 */
int lam_cam_is_3d(void);

/**
 * @brief Get pointer to active camera state.
 */
const lam_cam_t *lam_cam_get(void);

/**
 * @brief Copy the current View-Projection matrix into out.
 */
void lam_cam_get_vp(float out[16]);

/**
 * @brief Get the camera right vector in world coordinates.
 */
void lam_cam_get_right(float r[3]);

/**
 * @brief Cast a ray from screen coordinates and find intersection with ground plane Z=0.
 *
 * @param screen_x, screen_y Screen pixel coordinates
 * @param out_x, out_y       Output world coordinates on the ground plane
 * @return 1 on intersection, 0 if parallel or pointing away
 */
int lam_unproject_ground(int32_t screen_x, int32_t screen_y, float *out_x, float *out_y);

#ifdef __cplusplus
}
#endif

#endif /* LAM_CAM_H */
