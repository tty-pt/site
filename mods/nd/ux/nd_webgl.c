#include <stdio.h>
#include <string.h>
#include <math.h>
#include "nd_webgl.h"

#define MAX_GL_VERTICES 2048

typedef struct {
	float x, y, z;
	float r, g, b, a;
} nd_gl_vertex_t;

static GLuint g_prog = 0;
static GLuint g_vbo = 0;
static GLint g_loc_mvp = -1;
static int g_webgl_ready = 0;

static nd_gl_vertex_t s_vert_buf[MAX_GL_VERTICES];
static size_t s_vert_count = 0;

static const char *s_vs_src =
	"attribute vec3 a_pos;\n"
	"attribute vec4 a_col;\n"
	"uniform mat4 u_mvp;\n"
	"varying vec4 v_col;\n"
	"void main() {\n"
	"  gl_Position = u_mvp * vec4(a_pos, 1.0);\n"
	"  v_col = a_col;\n"
	"}\n";

static const char *s_fs_src =
	"precision mediump float;\n"
	"varying vec4 v_col;\n"
	"void main() {\n"
	"  gl_FragColor = v_col;\n"
	"}\n";

static void add_vertex(float x, float y, float z, float r, float g, float b, float a)
{
	if (s_vert_count < MAX_GL_VERTICES) {
		s_vert_buf[s_vert_count].x = x;
		s_vert_buf[s_vert_count].y = y;
		s_vert_buf[s_vert_count].z = z;
		s_vert_buf[s_vert_count].r = r;
		s_vert_buf[s_vert_count].g = g;
		s_vert_buf[s_vert_count].b = b;
		s_vert_buf[s_vert_count].a = a;
		s_vert_count++;
	}
}

static void add_quad_xz(float x0, float z0, float x1, float z1, float y, float r, float g, float b, float a)
{
	/* Triangle 1 */
	add_vertex(x0, y, z0, r, g, b, a);
	add_vertex(x1, y, z0, r, g, b, a);
	add_vertex(x1, y, z1, r, g, b, a);
	/* Triangle 2 */
	add_vertex(x0, y, z0, r, g, b, a);
	add_vertex(x1, y, z1, r, g, b, a);
	add_vertex(x0, y, z1, r, g, b, a);
}

static void add_quad_xy(float x0, float y0, float x1, float y1, float z, float r, float g, float b, float a)
{
	add_vertex(x0, y0, z, r, g, b, a);
	add_vertex(x1, y0, z, r, g, b, a);
	add_vertex(x1, y1, z, r, g, b, a);
	add_vertex(x0, y0, z, r, g, b, a);
	add_vertex(x1, y1, z, r, g, b, a);
	add_vertex(x0, y1, z, r, g, b, a);
}

static void add_quad_zy(float z0, float y0, float z1, float y1, float x, float r, float g, float b, float a)
{
	add_vertex(x, y0, z0, r, g, b, a);
	add_vertex(x, y0, z1, r, g, b, a);
	add_vertex(x, y1, z1, r, g, b, a);
	add_vertex(x, y0, z0, r, g, b, a);
	add_vertex(x, y1, z1, r, g, b, a);
	add_vertex(x, y1, z0, r, g, b, a);
}

static void add_cube(float cx, float cy, float cz, float size, float r, float g, float b, float a)
{
	float h = size * 0.5f;
	/* Top / Bottom */
	add_quad_xz(cx - h, cz - h, cx + h, cz + h, cy + h, r * 1.1f, g * 1.1f, b * 1.1f, a);
	add_quad_xz(cx - h, cz - h, cx + h, cz + h, cy - h, r * 0.6f, g * 0.6f, b * 0.6f, a);
	/* North / South walls */
	add_quad_xy(cx - h, cy - h, cx + h, cy + h, cz - h, r * 0.9f, g * 0.9f, b * 0.9f, a);
	add_quad_xy(cx - h, cy - h, cx + h, cy + h, cz + h, r * 0.8f, g * 0.8f, b * 0.8f, a);
	/* East / West walls */
	add_quad_zy(cz - h, cy - h, cz + h, cy + h, cx - h, r * 0.7f, g * 0.7f, b * 0.7f, a);
	add_quad_zy(cz - h, cy - h, cz + h, cy + h, cx + h, r * 0.85f, g * 0.85f, b * 0.85f, a);
}

/* 4x4 Matrix multiplication and projection math */
static void mat4_multiply(float *out, const float *a, const float *b)
{
	float tmp[16];
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			tmp[i * 4 + j] =
				a[i * 4 + 0] * b[0 * 4 + j] +
				a[i * 4 + 1] * b[1 * 4 + j] +
				a[i * 4 + 2] * b[2 * 4 + j] +
				a[i * 4 + 3] * b[3 * 4 + j];
		}
	}
	memcpy(out, tmp, 16 * sizeof(float));
}

static void mat4_perspective(float *m, float fov_rad, float aspect, float near_z, float far_z)
{
	memset(m, 0, 16 * sizeof(float));
	float tan_half = tanf(fov_rad * 0.5f);
	m[0] = 1.0f / (aspect * tan_half);
	m[5] = 1.0f / tan_half;
	m[10] = -(far_z + near_z) / (far_z - near_z);
	m[11] = -1.0f;
	m[14] = -(2.0f * far_z * near_z) / (far_z - near_z);
}

static void mat4_lookat(float *m, float eye_x, float eye_y, float eye_z,
                        float at_x, float at_y, float at_z,
                        float up_x, float up_y, float up_z)
{
	(void)up_x; (void)up_y; (void)up_z;
	float zx = eye_x - at_x, zy = eye_y - at_y, zz = eye_z - at_z;
	float zlen = sqrtf(zx*zx + zy*zy + zz*zz);
	if (zlen > 0.0001f) { zx /= zlen; zy /= zlen; zz /= zlen; }

	float xx = zz, xy = 0.0f, xz = -zx;
	float xlen = sqrtf(xx*xx + xz*xz);
	if (xlen > 0.0001f) { xx /= xlen; xz /= xlen; }

	float yx = zy * xz - zz * xy;
	float yy = zz * xx - zx * xz;
	float yz = zx * xy - zy * xx;

	memset(m, 0, 16 * sizeof(float));
	m[0] = xx; m[1] = yx; m[2] = zx;
	m[4] = xy; m[5] = yy; m[6] = zy;
	m[8] = xz; m[9] = yz; m[10] = zz;
	m[12] = -(xx * eye_x + xy * eye_y + xz * eye_z);
	m[13] = -(yx * eye_x + yy * eye_y + yz * eye_z);
	m[14] = -(zx * eye_x + zy * eye_y + zz * eye_z);
	m[15] = 1.0f;
}

int nd_webgl_init(void)
{
	if (g_webgl_ready)
		return 0;

	glViewport(0, 0, 640, 480);

	GLuint vs = glCreateShader(GL_VERTEX_SHADER);
	if (vs) {
		glShaderSource(vs, s_vs_src, strlen(s_vs_src));
		glCompileShader(vs);
	}

	GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
	if (fs) {
		glShaderSource(fs, s_fs_src, strlen(s_fs_src));
		glCompileShader(fs);
	}

	g_prog = glCreateProgram();
	if (g_prog && vs && fs) {
		glAttachShader(g_prog, vs);
		glAttachShader(g_prog, fs);
		glLinkProgram(g_prog);
		glUseProgram(g_prog);

		g_loc_mvp = glGetUniformLocation(g_prog, "u_mvp", 5);
	}

	g_vbo = glCreateBuffer();
	g_webgl_ready = 1;
	return 0;
}

void nd_webgl_render_scene(const nd_scene_t *scene)
{
	if (!scene)
		return;

	if (!g_webgl_ready) {
		nd_webgl_init();
	}

	/* 1. Dynamic Lighting & Sky Clear based on Time of Day */
	float tod = scene->time_of_day;
	int is_night = (tod < 0.25f || tod > 0.75f);
	float sky_r = is_night ? 0.02f : (0.05f + 0.12f * tod);
	float sky_g = is_night ? 0.02f : (0.06f + 0.14f * tod);
	float sky_b = is_night ? 0.05f : (0.10f + 0.18f * tod);
	glClearColor(sky_r, sky_g, sky_b, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	/* 2. Build 3D Mesh Geometry into Vertex Buffer */
	s_vert_count = 0;

	float rw = scene->room.width ? (float)scene->room.width : 16.0f;
	float rh = scene->room.height ? (float)scene->room.height : 12.0f;
	float hw = rw * 0.5f;
	float hh = rh * 0.5f;
	float wall_y = 2.0f;

	/* Lighting modulation */
	float light = is_night ? 0.45f : 1.0f;

	/* Floor: quad on XZ plane */
	add_quad_xz(-hw, -hh, hw, hh, 0.0f, 0.18f * light, 0.18f * light, 0.22f * light, 1.0f);

	/* North Wall (z = -hh) with optional center doorway cutout (exit bit 1) */
	float nw_r = 0.35f * light, nw_g = 0.35f * light, nw_b = 0.42f * light;
	if (scene->room.exits & 1) {
		add_quad_xy(-hw, 0.0f, -1.0f, wall_y, -hh, nw_r, nw_g, nw_b, 1.0f);
		add_quad_xy(1.0f, 0.0f, hw, wall_y, -hh, nw_r, nw_g, nw_b, 1.0f);
		add_quad_xy(-1.0f, 1.4f, 1.0f, wall_y, -hh, nw_r, nw_g, nw_b, 1.0f); /* Arch lintel */
	} else {
		add_quad_xy(-hw, 0.0f, hw, wall_y, -hh, nw_r, nw_g, nw_b, 1.0f);
	}

	/* South Wall (z = +hh) with exit bit 2 */
	if (scene->room.exits & 2) {
		add_quad_xy(-hw, 0.0f, -1.0f, wall_y, hh, nw_r, nw_g, nw_b, 1.0f);
		add_quad_xy(1.0f, 0.0f, hw, wall_y, hh, nw_r, nw_g, nw_b, 1.0f);
		add_quad_xy(-1.0f, 1.4f, 1.0f, wall_y, hh, nw_r, nw_g, nw_b, 1.0f);
	} else {
		add_quad_xy(-hw, 0.0f, hw, wall_y, hh, nw_r, nw_g, nw_b, 1.0f);
	}

	/* West Wall (x = -hw) with exit bit 8 */
	if (scene->room.exits & 8) {
		add_quad_zy(-hh, 0.0f, -1.0f, wall_y, -hw, nw_r * 0.9f, nw_g * 0.9f, nw_b * 0.9f, 1.0f);
		add_quad_zy(1.0f, 0.0f, hh, wall_y, -hw, nw_r * 0.9f, nw_g * 0.9f, nw_b * 0.9f, 1.0f);
		add_quad_zy(-1.0f, 1.4f, 1.0f, wall_y, -hw, nw_r * 0.9f, nw_g * 0.9f, nw_b * 0.9f, 1.0f);
	} else {
		add_quad_zy(-hh, 0.0f, hh, wall_y, -hw, nw_r * 0.9f, nw_g * 0.9f, nw_b * 0.9f, 1.0f);
	}

	/* East Wall (x = +hw) with exit bit 4 */
	if (scene->room.exits & 4) {
		add_quad_zy(-hh, 0.0f, -1.0f, wall_y, hw, nw_r * 0.9f, nw_g * 0.9f, nw_b * 0.9f, 1.0f);
		add_quad_zy(1.0f, 0.0f, hh, wall_y, hw, nw_r * 0.9f, nw_g * 0.9f, nw_b * 0.9f, 1.0f);
		add_quad_zy(-1.0f, 1.4f, 1.0f, wall_y, hw, nw_r * 0.9f, nw_g * 0.9f, nw_b * 0.9f, 1.0f);
	} else {
		add_quad_zy(-hh, 0.0f, hh, wall_y, hw, nw_r * 0.9f, nw_g * 0.9f, nw_b * 0.9f, 1.0f);
	}

	/* 3. Render Entities as 3D Prisms / Avatars */
	float player_x = 0.0f;
	float player_z = 0.0f;

	for (size_t i = 0; i < scene->entity_count; i++) {
		const nd_scene_entity_t *se = &scene->entities[i];
		float ex = se->pos.x - hw;
		float ez = se->pos.y - hh;
		float ey = 0.4f;

		if (se->is_player) {
			player_x = ex;
			player_z = ez;
			add_cube(ex, ey, ez, 0.7f, 0.25f, 0.65f, 1.0f, 1.0f); /* Player Avatar */
		} else if (se->is_target) {
			add_cube(ex, ey, ez, 0.7f, 0.95f, 0.25f, 0.25f, 1.0f); /* Target Mob */
			/* Gold target marker ring / halo above head */
			add_cube(ex, ey + 0.65f, ez, 0.35f, 1.0f, 0.85f, 0.1f, 1.0f);
		} else {
			add_cube(ex, ey, ez, 0.6f, 0.9f, 0.65f, 0.2f, 1.0f); /* NPC / Mob */
		}
	}

	/* 3b. Attachments at skeleton sockets, then spell bursts at their target */
	for (size_t i = 0; i < scene->entity_count; i++) {
		const nd_scene_entity_t *se = &scene->entities[i];
		float ex = se->pos.x - hw, ez = se->pos.y - hh;
		for (size_t a = 0; a < se->attach_count; a++) {
			const char *k = se->attach[a].socket;
			float ox = 0.0f, oy = 0.4f, oz = 0.0f;
			if (!strcmp(k, "head"))        { oy = 0.95f; }
			else if (!strcmp(k, "neck"))   { oy = 0.8f; }
			else if (!strcmp(k, "chest"))  { oy = 0.5f; oz = 0.38f; }
			else if (!strcmp(k, "back"))   { oy = 0.5f; oz = -0.38f; }
			else if (!strcmp(k, "pants"))  { oy = 0.1f; oz = 0.38f; }
			else if (!strcmp(k, "rhand"))  { ox = 0.45f; oy = 0.4f; }
			else if (!strcmp(k, "lhand"))  { ox = -0.45f; oy = 0.4f; }
			else if (!strcmp(k, "rfinger")) { ox = 0.45f; oy = 0.2f; }
			else if (!strcmp(k, "lfinger")) { ox = -0.45f; oy = 0.2f; }
			float r = 0.3f + 0.7f * (float)((se->attach[a].ref * 37u) % 100u) / 100.0f;
			add_cube(ex + ox, oy, ez + oz, 0.22f, r, 0.8f, 1.0f - r, 1.0f);
		}
	}
	{
		static uint32_t seen_serial;
		static int frames_left;
		for (size_t i = 0; i < scene->fx_count; i++) {
			const nd_scene_fx_t *fx = &scene->fx[i];
			if (fx->serial != seen_serial) {
				seen_serial = fx->serial;
				frames_left = 45;
			}
			if (frames_left <= 0)
				continue;
			float cr = 1.0f, cg = 1.0f, cb = 1.0f;
			if (!strcmp(fx->kind, "fire"))       { cr = 1.0f; cg = 0.35f; cb = 0.1f; }
			else if (!strcmp(fx->kind, "water")) { cr = 0.2f; cg = 0.5f; cb = 1.0f; }
			else if (!strcmp(fx->kind, "earth")) { cr = 0.6f; cg = 0.45f; cb = 0.2f; }
			else if (!strcmp(fx->kind, "air"))   { cr = 0.85f; cg = 0.9f; cb = 0.95f; }
			for (size_t j = 0; j < scene->entity_count; j++) {
				const nd_scene_entity_t *se = &scene->entities[j];
				if (se->ref != fx->target_ref && !(se->is_player && !fx->target_ref))
					continue;
				float t = (float)(45 - frames_left) / 45.0f;
				for (int p = 0; p < 6; p++) {
					float ang = (float)p * 1.0472f;
					float rad = 0.4f + 1.2f * t;
					add_cube(se->pos.x - hw + rad * __builtin_cosf(ang), 0.5f + t,
						se->pos.y - hh + rad * __builtin_sinf(ang), 0.15f, cr, cg, cb, 1.0f);
				}
			}
		}
		if (frames_left > 0)
			frames_left--;
	}

	/* 4. Camera Matrix: Orbit & Perspective Tracking Player */
	float mat_proj[16], mat_view[16], mat_mvp[16];
	mat4_perspective(mat_proj, 60.0f * 3.14159f / 180.0f, 640.0f / 480.0f, 0.1f, 100.0f);

	float cam_dist = 11.0f;
	float eye_x = player_x;
	float eye_y = 9.0f;
	float eye_z = player_z + cam_dist;
	mat4_lookat(mat_view, eye_x, eye_y, eye_z, player_x, 0.4f, player_z, 0.0f, 1.0f, 0.0f);

	mat4_multiply(mat_mvp, mat_proj, mat_view);

	/* 5. Upload Buffers and Issue Draw Call */
	if (g_prog && g_vbo && s_vert_count > 0) {
		glUseProgram(g_prog);
		if (g_loc_mvp >= 0) {
			glUniformMatrix4fv(g_loc_mvp, 1, GL_FALSE, mat_mvp);
		}

		glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
		glBufferData(GL_ARRAY_BUFFER, s_vert_buf, s_vert_count * sizeof(nd_gl_vertex_t), GL_DYNAMIC_DRAW);

		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(nd_gl_vertex_t), 0);

		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(nd_gl_vertex_t), 3 * sizeof(float));

		glDrawArrays(GL_TRIANGLES, 0, (GLsizei)s_vert_count);
	}
}
