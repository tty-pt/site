/*
 * nd_seven_gl.c — Core WebGL2 Pipeline Lifecycle, Static Grid Buffers, Frame Dispatch, and Terrain/Water Passes.
 * Part of the NeverDark Seven 2.5D diorama platform.
 */

#include "nd_seven_gl_internal.h"

nd_seven_gl_state_t s_gl;

static GLuint compile_shader(GLenum type, const char *src)
{
	GLuint sh = glCreateShader(type);
	if (!sh) return 0;
	glShaderSource(sh, src, strlen(src));
	glCompileShader(sh);
	return sh;
}

static GLuint create_program_with_vs_async(GLuint vs, const char *fs_src)
{
	if (!vs) return 0;
	GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_src);
	if (!fs) return 0;
	GLuint prog = glCreateProgram();
	if (!prog) {
		glDeleteShader(fs);
		return 0;
	}
	glAttachShader(prog, vs);
	glAttachShader(prog, fs);
	glLinkProgram(prog);
	glDeleteShader(fs);
	return prog;
}

GLuint create_program_async(const char *vs_src, const char *fs_src)
{
	GLuint vs = compile_shader(GL_VERTEX_SHADER, vs_src);
	if (!vs) return 0;
	GLuint prog = create_program_with_vs_async(vs, fs_src);
	glDeleteShader(vs);
	return prog;
}

static void create_static_grid(int quads, float *uv_buf, uint16_t *idx_buf,
                               GLuint *out_vbo, GLuint *out_ibo)
{
	int verts = quads + 1;
	int base_verts = verts * verts;
	for (int y = 0; y < verts; y++) {
		for (int x = 0; x < verts; x++) {
			int idx = (y * verts + x) * 2;
			uv_buf[idx]     = (float)x / (float)quads;
			uv_buf[idx + 1] = (float)y / (float)quads;
		}
	}

	/* ar_mat4_lookat_lh has det = -1, which reflects right-handed world space into screen space.
	 * Wind interior quads so (+tanU, +tanV) maps CCW on screen (matching the CCW perimeter skirts),
	 * allowing default GL_CULL_FACE (GL_BACK, GL_CCW) for terrain and water passes. */
	int i_idx = 0;
	for (int y = 0; y < quads; y++) {
		for (int x = 0; x < quads; x++) {
			uint16_t i0 = (uint16_t)(y * verts + x);
			uint16_t i1 = (uint16_t)(i0 + 1);
			uint16_t i2 = (uint16_t)((y + 1) * verts + x);
			uint16_t i3 = (uint16_t)(i2 + 1);

			if ((x + y) & 1) {
				idx_buf[i_idx++] = i0;
				idx_buf[i_idx++] = i3;
				idx_buf[i_idx++] = i1;
				idx_buf[i_idx++] = i0;
				idx_buf[i_idx++] = i2;
				idx_buf[i_idx++] = i3;
			} else {
				idx_buf[i_idx++] = i0;
				idx_buf[i_idx++] = i2;
				idx_buf[i_idx++] = i1;
				idx_buf[i_idx++] = i1;
				idx_buf[i_idx++] = i2;
				idx_buf[i_idx++] = i3;
			}
		}
	}

	/* Append perimeter skirt vertices (u shifted by -2.0f) to seal any 2:1 LOD T-junctions */
	int s_vert = base_verts;
	for (int e = 0; e < 4; e++) {
		int e_base = s_vert;
		for (int k = 0; k <= quads; k++) {
			int bx = (e == 0) ? k : ((e == 1) ? quads : ((e == 2) ? (quads - k) : 0));
			int by = (e == 0) ? 0 : ((e == 1) ? k : ((e == 2) ? quads : (quads - k)));
			uv_buf[s_vert * 2]     = ((float)bx / (float)quads) - 2.0f;
			uv_buf[s_vert * 2 + 1] = (float)by / (float)quads;
			s_vert++;
		}
		for (int k = 0; k < quads; k++) {
			int bx0 = (e == 0) ? k : ((e == 1) ? quads : ((e == 2) ? (quads - k) : 0));
			int by0 = (e == 0) ? 0 : ((e == 1) ? k : ((e == 2) ? quads : (quads - k)));
			int bx1 = (e == 0) ? (k + 1) : ((e == 1) ? quads : ((e == 2) ? (quads - k - 1) : 0));
			int by1 = (e == 0) ? 0 : ((e == 1) ? (k + 1) : ((e == 2) ? quads : (quads - k - 1)));
			uint16_t t0 = (uint16_t)(by0 * verts + bx0);
			uint16_t t1 = (uint16_t)(by1 * verts + bx1);
			uint16_t b0 = (uint16_t)(e_base + k);
			uint16_t b1 = (uint16_t)(e_base + k + 1);
			idx_buf[i_idx++] = t0;
			idx_buf[i_idx++] = t1;
			idx_buf[i_idx++] = b0;
			idx_buf[i_idx++] = t1;
			idx_buf[i_idx++] = b1;
			idx_buf[i_idx++] = b0;
		}
	}

	*out_vbo = glCreateBuffer();
	glBindBuffer(GL_ARRAY_BUFFER, *out_vbo);
	glBufferData(GL_ARRAY_BUFFER, uv_buf, (GLsizei)((size_t)s_vert * 2 * sizeof(float)), GL_STATIC_DRAW);

	*out_ibo = glCreateBuffer();
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, *out_ibo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx_buf, (GLsizei)((size_t)i_idx * sizeof(uint16_t)), GL_STATIC_DRAW);
}

typedef struct {
	float pos_h[4];  /* xyz = local tuft pos, w = heightT [0..1] */
	float norm_t[4]; /* xyz = curved normal, w = bladeKind (0=outer, 0.5=inner, 1=clover) */
} gl_grass_vert_t;

static void create_grass_tuft_mesh(void)
{
	/* 5 gracefully arching tapered 3D grass blades (7 verts, 5 tris each) +
	 * 2 low broad ground-hugging clover leaves (4 verts, 2 tris each) = 43 verts, 29 tris (87 indices). */
	gl_grass_vert_t verts[48];
	uint16_t idx[96];
	int vc = 0, ic = 0;

	static const float b_ang[5]  = { 0.12f, 1.38f, 2.61f, 3.89f, 5.14f };
	static const float b_rad[5]  = { 0.30f, 0.14f, 0.34f, 0.20f, 0.28f };
	static const float b_h[5]    = { 1.00f, 0.84f, 0.94f, 0.74f, 0.88f };
	static const float b_lean[5] = { 0.44f, 0.24f, 0.48f, 0.32f, 0.40f };
	static const float b_w[5]    = { 0.145f, 0.130f, 0.150f, 0.125f, 0.138f };
	static const float b_kind[5] = { 0.0f, 0.5f, 0.0f, 0.5f, 0.0f };
	static const float seg_t[3]  = { 0.0f, 0.36f, 0.72f };
	static const float seg_w[3]  = { 1.0f, 0.76f, 0.42f };

	for (int b = 0; b < 5; b++) {
		float ca = cosf(b_ang[b]), sa = sinf(b_ang[b]);
		float px = -sa, py = ca; /* transverse width axis */
		float rx = ca * b_rad[b], ry = sa * b_rad[b];
		int base = vc;

		for (int s = 0; s < 3; s++) {
			float t = seg_t[s];
			float arch = t * t * b_lean[b];
			float z = t * b_h[b] * (1.0f - 0.14f * t * t);
			float hw = b_w[b] * seg_w[s];
			/* Tangent along blade spine -> outward normal */
			float tx = ca * (2.0f * t * b_lean[b]);
			float ty = sa * (2.0f * t * b_lean[b]);
			float tz = b_h[b] * (1.0f - 0.42f * t * t);
			float nx0 = ca * tz, ny0 = sa * tz, nz0 = -(tx * ca + ty * sa) + 0.55f;
			float nlen = sqrtf(nx0 * nx0 + ny0 * ny0 + nz0 * nz0 + 1e-6f);
			nx0 /= nlen; ny0 /= nlen; nz0 /= nlen;

			for (int side = -1; side <= 1; side += 2) {
				float sf = (float)side;
				gl_grass_vert_t *v = &verts[vc++];
				v->pos_h[0] = rx + ca * arch + px * (hw * sf);
				v->pos_h[1] = ry + sa * arch + py * (hw * sf);
				v->pos_h[2] = z;
				v->pos_h[3] = t * b_h[b];
				/* V-fold normal bevel across blade width catches grazing specular light */
				float bnx = nx0 + px * (0.28f * sf);
				float bny = ny0 + py * (0.28f * sf);
				float bnz = nz0;
				float bl = sqrtf(bnx * bnx + bny * bny + bnz * bnz + 1e-6f);
				v->norm_t[0] = bnx / bl;
				v->norm_t[1] = bny / bl;
				v->norm_t[2] = bnz / bl;
				v->norm_t[3] = b_kind[b];
			}
		}
		/* Pointed tip vertex */
		{
			gl_grass_vert_t *v = &verts[vc++];
			v->pos_h[0] = rx + ca * b_lean[b];
			v->pos_h[1] = ry + sa * b_lean[b];
			v->pos_h[2] = b_h[b] * 0.86f;
			v->pos_h[3] = b_h[b];
			float bnx = ca * 0.45f, bny = sa * 0.45f, bnz = 0.89f;
			float bl = sqrtf(bnx * bnx + bny * bny + bnz * bnz + 1e-6f);
			v->norm_t[0] = bnx / bl;
			v->norm_t[1] = bny / bl;
			v->norm_t[2] = bnz / bl;
			v->norm_t[3] = b_kind[b];
		}

		/* 2 quads (4 tris) + 1 tip cap tri = 5 tris */
		for (int s = 0; s < 2; s++) {
			uint16_t a0 = (uint16_t)(base + s * 2);
			uint16_t a1 = (uint16_t)(a0 + 1);
			uint16_t b0 = (uint16_t)(a0 + 2);
			uint16_t b1 = (uint16_t)(a0 + 3);
			idx[ic++] = a0; idx[ic++] = a1; idx[ic++] = b1;
			idx[ic++] = a0; idx[ic++] = b1; idx[ic++] = b0;
		}
		idx[ic++] = (uint16_t)(base + 4);
		idx[ic++] = (uint16_t)(base + 5);
		idx[ic++] = (uint16_t)(base + 6);
	}

	/* 2 low broad ground-hugging clover / forest-turf leaves */
	static const float c_ang[2] = { 0.82f, 3.96f };
	for (int c = 0; c < 2; c++) {
		float ca = cosf(c_ang[c]), sa = sinf(c_ang[c]);
		float px = -sa, py = ca;
		int base = vc;
		float pts[4][4] = {
			{ ca * 0.08f, sa * 0.08f, 0.10f, 0.14f },
			{ ca * 0.44f - px * 0.34f, sa * 0.44f - py * 0.34f, 0.24f, 0.26f },
			{ ca * 0.44f + px * 0.34f, sa * 0.44f + py * 0.34f, 0.24f, 0.26f },
			{ ca * 0.82f, sa * 0.82f, 0.19f, 0.30f }
		};
		for (int k = 0; k < 4; k++) {
			gl_grass_vert_t *v = &verts[vc++];
			v->pos_h[0] = pts[k][0];
			v->pos_h[1] = pts[k][1];
			v->pos_h[2] = pts[k][2];
			v->pos_h[3] = pts[k][3];
			v->norm_t[0] = ca * 0.22f;
			v->norm_t[1] = sa * 0.22f;
			v->norm_t[2] = 0.975f;
			v->norm_t[3] = 1.0f;
		}
		idx[ic++] = (uint16_t)(base + 0); idx[ic++] = (uint16_t)(base + 1); idx[ic++] = (uint16_t)(base + 2);
		idx[ic++] = (uint16_t)(base + 1); idx[ic++] = (uint16_t)(base + 3); idx[ic++] = (uint16_t)(base + 2);
	}

	s_gl.grass_vbo = glCreateBuffer();
	glBindBuffer(GL_ARRAY_BUFFER, s_gl.grass_vbo);
	glBufferData(GL_ARRAY_BUFFER, verts, (GLsizei)((size_t)vc * sizeof(gl_grass_vert_t)), GL_STATIC_DRAW);

	s_gl.grass_ibo = glCreateBuffer();
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s_gl.grass_ibo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx, (GLsizei)((size_t)ic * sizeof(uint16_t)), GL_STATIC_DRAW);
	s_gl.grass_idx_count = (GLsizei)ic;

	/* Mid-range LOD grass tuft: 3 arched blades (5 verts, 3 tris each), 0 clover = 15 verts, 9 tris (27 indices) */
	{
		gl_grass_vert_t lverts[20];
		uint16_t lidx[32];
		int lvc = 0, lic = 0;
		static const float lb_ang[3]  = { 0.12f, 2.21f, 4.30f };
		static const float lb_rad[3]  = { 0.28f, 0.30f, 0.26f };
		static const float lb_h[3]    = { 1.00f, 0.92f, 0.86f };
		static const float lb_lean[3] = { 0.44f, 0.46f, 0.38f };
		static const float lb_w[3]    = { 0.175f, 0.180f, 0.168f };
		static const float lb_kind[3] = { 0.0f, 0.5f, 0.0f };
		static const float lseg_t[2]  = { 0.0f, 0.48f };
		static const float lseg_w[2]  = { 1.0f, 0.60f };

		for (int b = 0; b < 3; b++) {
			float ca = cosf(lb_ang[b]), sa = sinf(lb_ang[b]);
			float px = -sa, py = ca;
			float rx = ca * lb_rad[b], ry = sa * lb_rad[b];
			int base = lvc;

			for (int s = 0; s < 2; s++) {
				float t = lseg_t[s];
				float arch = t * t * lb_lean[b];
				float z = t * lb_h[b] * (1.0f - 0.14f * t * t);
				float hw = lb_w[b] * lseg_w[s];
				float tx = ca * (2.0f * t * lb_lean[b]);
				float ty = sa * (2.0f * t * lb_lean[b]);
				float tz = lb_h[b] * (1.0f - 0.42f * t * t);
				float nx0 = ca * tz, ny0 = sa * tz, nz0 = -(tx * ca + ty * sa) + 0.55f;
				float nlen = sqrtf(nx0 * nx0 + ny0 * ny0 + nz0 * nz0 + 1e-6f);
				nx0 /= nlen; ny0 /= nlen; nz0 /= nlen;

				for (int side = -1; side <= 1; side += 2) {
					float sf = (float)side;
					gl_grass_vert_t *v = &lverts[lvc++];
					v->pos_h[0] = rx + ca * arch + px * (hw * sf);
					v->pos_h[1] = ry + sa * arch + py * (hw * sf);
					v->pos_h[2] = z;
					v->pos_h[3] = t * lb_h[b];
					float bnx = nx0 + px * (0.28f * sf);
					float bny = ny0 + py * (0.28f * sf);
					float bnz = nz0;
					float bl = sqrtf(bnx * bnx + bny * bny + bnz * bnz + 1e-6f);
					v->norm_t[0] = bnx / bl;
					v->norm_t[1] = bny / bl;
					v->norm_t[2] = bnz / bl;
					v->norm_t[3] = lb_kind[b];
				}
			}
			{
				gl_grass_vert_t *v = &lverts[lvc++];
				v->pos_h[0] = rx + ca * lb_lean[b];
				v->pos_h[1] = ry + sa * lb_lean[b];
				v->pos_h[2] = lb_h[b] * 0.86f;
				v->pos_h[3] = lb_h[b];
				float bnx = ca * 0.45f, bny = sa * 0.45f, bnz = 0.89f;
				float bl = sqrtf(bnx * bnx + bny * bny + bnz * bnz + 1e-6f);
				v->norm_t[0] = bnx / bl;
				v->norm_t[1] = bny / bl;
				v->norm_t[2] = bnz / bl;
				v->norm_t[3] = lb_kind[b];
			}
			uint16_t a0 = (uint16_t)(base + 0);
			uint16_t a1 = (uint16_t)(base + 1);
			uint16_t b0 = (uint16_t)(base + 2);
			uint16_t b1 = (uint16_t)(base + 3);
			lidx[lic++] = a0; lidx[lic++] = a1; lidx[lic++] = b1;
			lidx[lic++] = a0; lidx[lic++] = b1; lidx[lic++] = b0;
			lidx[lic++] = b0; lidx[lic++] = b1; lidx[lic++] = (uint16_t)(base + 4);
		}

		s_gl.grass_vbo_lod = glCreateBuffer();
		glBindBuffer(GL_ARRAY_BUFFER, s_gl.grass_vbo_lod);
		glBufferData(GL_ARRAY_BUFFER, lverts, (GLsizei)((size_t)lvc * sizeof(gl_grass_vert_t)), GL_STATIC_DRAW);

		s_gl.grass_ibo_lod = glCreateBuffer();
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s_gl.grass_ibo_lod);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, lidx, (GLsizei)((size_t)lic * sizeof(uint16_t)), GL_STATIC_DRAW);
		s_gl.grass_idx_count_lod = (GLsizei)lic;
	}
}

static void init_grid_buffers(void)
{
	static float grid_uv_scratch[(GL_TOTAL_VERTS_HI + 4 * (GL_GRID_QUADS_HI + 1)) * 2];
	static uint16_t indices_scratch[GL_TOTAL_INDICES_HI + 4 * GL_GRID_QUADS_HI * 6];

	create_static_grid(GL_GRID_QUADS_MACRO, grid_uv_scratch, indices_scratch, &s_gl.grid_vbo_macro, &s_gl.grid_ibo_macro);
	create_static_grid(GL_GRID_QUADS_LO, grid_uv_scratch, indices_scratch, &s_gl.grid_vbo, &s_gl.grid_ibo);
	create_static_grid(GL_GRID_QUADS_HI, grid_uv_scratch, indices_scratch, &s_gl.grid_vbo_hi, &s_gl.grid_ibo_hi);
	create_grass_tuft_mesh();

	s_gl.bb_vbo = glCreateBuffer();
	glBindBuffer(GL_ARRAY_BUFFER, s_gl.bb_vbo);
	glBufferData(GL_ARRAY_BUFFER, NULL, MAX_BB_INSTANCES * sizeof(nd_seven_gl_bb_inst_t), GL_DYNAMIC_DRAW);

	nd_seven_mesh3d_init();
	uint32_t m3d_vcnt = 0, m3d_icnt = 0;
	const nd_mesh3d_vert_t *m3d_verts = nd_seven_mesh3d_verts(&m3d_vcnt);
	const uint32_t *m3d_idx = nd_seven_mesh3d_indices(&m3d_icnt);
	s_gl.mesh3d_vbo = glCreateBuffer();
	glBindBuffer(GL_ARRAY_BUFFER, s_gl.mesh3d_vbo);
	glBufferData(GL_ARRAY_BUFFER, m3d_verts, (GLsizei)((size_t)m3d_vcnt * sizeof(nd_mesh3d_vert_t)), GL_STATIC_DRAW);

	s_gl.mesh3d_ibo = glCreateBuffer();
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s_gl.mesh3d_ibo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, m3d_idx, (GLsizei)((size_t)m3d_icnt * sizeof(uint32_t)), GL_STATIC_DRAW);

	s_gl.mesh3d_inst_vbo = glCreateBuffer();
	glBindBuffer(GL_ARRAY_BUFFER, s_gl.mesh3d_inst_vbo);
	glBufferData(GL_ARRAY_BUFFER, NULL, MAX_BB_INSTANCES * sizeof(gl_mesh3d_inst_t), GL_DYNAMIC_DRAW);

	/* Phase 4B.4: Upload 1x1 neutral placeholders at init; bake full procedural
	 * atlases incrementally over the first 3 frames to eliminate startup stall. */
	{
		static const uint8_t k_neut_alb[4]  = { 128, 128, 128, 255 };
		static const uint8_t k_neut_norm[4] = { 128, 128, 255, 255 };
		struct { GLuint *tex; GLenum unit; GLenum wrap; const uint8_t *px; } ph[8] = {
			{ &s_gl.fol_atlas_tex,      GL_TEXTURE0 + 3,  GL_CLAMP_TO_EDGE, k_neut_alb  },
			{ &s_gl.fol_atlas_norm_tex, GL_TEXTURE0 + 4,  GL_CLAMP_TO_EDGE, k_neut_norm },
			{ &s_gl.fol_grass_tex,      GL_TEXTURE0 + 5,  GL_CLAMP_TO_EDGE, k_neut_alb  },
			{ &s_gl.fol_grass_norm_tex, GL_TEXTURE0 + 6,  GL_CLAMP_TO_EDGE, k_neut_norm },
			{ &s_gl.fol_bark_tex,       GL_TEXTURE0 + 8,  GL_REPEAT,        k_neut_alb  },
			{ &s_gl.fol_bark_norm_tex,  GL_TEXTURE0 + 9,  GL_REPEAT,        k_neut_norm },
			{ &s_gl.terr_detail_tex,    GL_TEXTURE0 + 10, GL_REPEAT,        k_neut_alb  },
			{ &s_gl.terr_norm_tex,      GL_TEXTURE0 + 11, GL_REPEAT,        k_neut_norm }
		};
		for (int i = 0; i < 8; i++) {
			if (!*ph[i].tex) {
				*ph[i].tex = glCreateTexture();
				glActiveTexture(ph[i].unit);
				glBindTexture(GL_TEXTURE_2D, *ph[i].tex);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, (GLint)ph[i].wrap);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, (GLint)ph[i].wrap);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
				glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, ph[i].px);
				glBindTexture(GL_TEXTURE_2D, 0);
			}
		}
		glActiveTexture(GL_TEXTURE0);
		s_gl.atlas_bake_stage = 0;
	}

	/* Phase 4C.1: Persistent WebGL2 std140 Frame-Global UBO (binding 0) */
	s_gl.frame_ubo = glCreateBuffer();
	glBindBuffer(GL_UNIFORM_BUFFER, s_gl.frame_ubo);
	glBufferData(GL_UNIFORM_BUFFER, NULL, sizeof(nd_frame_ubo_t), GL_DYNAMIC_DRAW);
	glBindBufferBase(GL_UNIFORM_BUFFER, 0, s_gl.frame_ubo);
	glBindBuffer(GL_UNIFORM_BUFFER, 0);

	/* Phase 4C.2: Flat uint32 WebGL command ring buffer */
	s_gl.cmd_buf.words = s_gl.cmd_words;
	s_gl.cmd_buf.cap = (uint32_t)(sizeof(s_gl.cmd_words) / sizeof(s_gl.cmd_words[0]));
	s_gl.cmd_buf.len = 0u;
}

typedef void (*atlas_downsample_fn_t)(const uint8_t *src, int w, int h, uint8_t *dst);

static void gl_upload_atlas_mips(GLuint *tex, GLenum unit, GLenum wrap,
                                 int aniso, int w, int h, int mip_levels,
                                 uint8_t *buf_lvl0, uint8_t *buf_scratch,
                                 atlas_downsample_fn_t down_fn)
{
	if (!*tex)
		*tex = glCreateTexture();
	glActiveTexture(unit);
	glBindTexture(GL_TEXTURE_2D, *tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, (GLint)wrap);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, (GLint)wrap);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, mip_levels - 1);
	if (aniso > 1)
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, aniso);
	int cur_w = w, cur_h = h;
	const uint8_t *src = buf_lvl0;
	uint8_t *dst = buf_scratch;
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, cur_w, cur_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, src);
	for (int mip = 1; mip < mip_levels; mip++) {
		int next_w = cur_w >> 1, next_h = cur_h >> 1;
		down_fn(src, cur_w, cur_h, dst);
		glTexImage2D(GL_TEXTURE_2D, mip, GL_RGBA8, next_w, next_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, dst);
		uint8_t *prev_src = (uint8_t *)src;
		src = dst;
		dst = prev_src;
		cur_w = next_w;
		cur_h = next_h;
	}
	glBindTexture(GL_TEXTURE_2D, 0);
}

static void gl_bake_upload_terr_atlas(void)
{
	size_t tbytes = (size_t)ND_TERR_ATLAS_SIZE * (size_t)ND_TERR_ATLAS_SIZE * 4u;
	uint8_t *buf_a = (uint8_t *)malloc(tbytes);
	uint8_t *buf_n = (uint8_t *)malloc(tbytes);
	uint8_t *buf_b = (uint8_t *)malloc(tbytes >> 2);
	if (buf_a && buf_n && buf_b) {
		nd_terr_atlas_bake_level0(buf_a);
		nd_terr_atlas_bake_normals_level0(buf_a, buf_n);
		gl_upload_atlas_mips(&s_gl.terr_detail_tex, GL_TEXTURE0 + 10, GL_REPEAT, 0,
		                     ND_TERR_ATLAS_SIZE, ND_TERR_ATLAS_SIZE, ND_TERR_MIP_LEVELS,
		                     buf_a, buf_b, nd_bark_atlas_downsample);
		gl_upload_atlas_mips(&s_gl.terr_norm_tex, GL_TEXTURE0 + 11, GL_REPEAT, 0,
		                     ND_TERR_ATLAS_SIZE, ND_TERR_ATLAS_SIZE, ND_TERR_MIP_LEVELS,
		                     buf_n, buf_b, nd_bark_atlas_downsample_normals);
		glActiveTexture(GL_TEXTURE0);
	}
	free(buf_a);
	free(buf_n);
	free(buf_b);
}

static void gl_bake_upload_bark_atlas(void)
{
	size_t bbytes = (size_t)ND_BARK_ATLAS_SIZE * (size_t)ND_BARK_ATLAS_SIZE * 4u;
	uint8_t *buf_a = (uint8_t *)malloc(bbytes);
	uint8_t *buf_n = (uint8_t *)malloc(bbytes);
	uint8_t *buf_b = (uint8_t *)malloc(bbytes >> 2);
	if (buf_a && buf_n && buf_b) {
		nd_bark_atlas_bake_level0(buf_a);
		nd_bark_atlas_bake_normals_level0(buf_a, buf_n);
		gl_upload_atlas_mips(&s_gl.fol_bark_tex, GL_TEXTURE0 + 8, GL_REPEAT, 8,
		                     ND_BARK_ATLAS_SIZE, ND_BARK_ATLAS_SIZE, ND_BARK_MIP_LEVELS,
		                     buf_a, buf_b, nd_bark_atlas_downsample);
		gl_upload_atlas_mips(&s_gl.fol_bark_norm_tex, GL_TEXTURE0 + 9, GL_REPEAT, 8,
		                     ND_BARK_ATLAS_SIZE, ND_BARK_ATLAS_SIZE, ND_BARK_MIP_LEVELS,
		                     buf_n, buf_b, nd_bark_atlas_downsample_normals);
		glActiveTexture(GL_TEXTURE0);
	}
	free(buf_a);
	free(buf_n);
	free(buf_b);
}

static void gl_bake_upload_grass_atlas(void)
{
	size_t gbytes = (size_t)ND_GRASS_ATLAS_W * (size_t)ND_GRASS_ATLAS_H * 4u;
	uint8_t *buf_a = (uint8_t *)malloc(gbytes);
	uint8_t *buf_n = (uint8_t *)malloc(gbytes);
	uint8_t *buf_b = (uint8_t *)malloc(gbytes >> 2);
	if (buf_a && buf_n && buf_b) {
		nd_grass_atlas_bake_level0(buf_a);
		nd_grass_atlas_bake_normals_level0(buf_a, buf_n);
		gl_upload_atlas_mips(&s_gl.fol_grass_tex, GL_TEXTURE0 + 5, GL_CLAMP_TO_EDGE, 8,
		                     ND_GRASS_ATLAS_W, ND_GRASS_ATLAS_H, ND_GRASS_MIP_LEVELS,
		                     buf_a, buf_b, nd_grass_atlas_downsample);
		gl_upload_atlas_mips(&s_gl.fol_grass_norm_tex, GL_TEXTURE0 + 6, GL_CLAMP_TO_EDGE, 8,
		                     ND_GRASS_ATLAS_W, ND_GRASS_ATLAS_H, ND_GRASS_MIP_LEVELS,
		                     buf_n, buf_b, nd_foliage_atlas_downsample_normals);
		glActiveTexture(GL_TEXTURE0);
	}
	free(buf_a);
	free(buf_n);
	free(buf_b);
}

static void gl_bake_upload_foliage_atlas(void)
{
	size_t fbytes = (size_t)ND_FOL_ATLAS_SIZE * (size_t)ND_FOL_ATLAS_SIZE * 4u;
	uint8_t *buf_a = (uint8_t *)malloc(fbytes);
	uint8_t *buf_n = (uint8_t *)malloc(fbytes);
	uint8_t *buf_b = (uint8_t *)malloc(fbytes >> 2);
	if (buf_a && buf_n && buf_b) {
		nd_foliage_atlas_bake_level0(buf_a);
		nd_foliage_atlas_bake_normals_level0(buf_a, buf_n);
		gl_upload_atlas_mips(&s_gl.fol_atlas_tex, GL_TEXTURE0 + 3, GL_CLAMP_TO_EDGE, 8,
		                     ND_FOL_ATLAS_SIZE, ND_FOL_ATLAS_SIZE, ND_FOL_MIP_LEVELS,
		                     buf_a, buf_b, nd_foliage_atlas_downsample);
		gl_upload_atlas_mips(&s_gl.fol_atlas_norm_tex, GL_TEXTURE0 + 4, GL_CLAMP_TO_EDGE, 8,
		                     ND_FOL_ATLAS_SIZE, ND_FOL_ATLAS_SIZE, ND_FOL_MIP_LEVELS,
		                     buf_n, buf_b, nd_foliage_atlas_downsample_normals);
		glActiveTexture(GL_TEXTURE0);
	}
	free(buf_a);
	free(buf_n);
	free(buf_b);
}

static void gl_advance_atlas_bake(void)
{
	switch (s_gl.atlas_bake_stage) {
	case 0:
		gl_bake_upload_terr_atlas();
		gl_bake_upload_bark_atlas();
		s_gl.atlas_bake_stage = 1;
		break;
	case 1:
		gl_bake_upload_grass_atlas();
		s_gl.atlas_bake_stage = 2;
		break;
	case 2:
		gl_bake_upload_foliage_atlas();
		s_gl.atlas_bake_stage = 3;
		break;
	default:
		break;
	}
}

static void gl_bind_frame_ubo_prog(GLuint prog)
{
	GLuint idx = glGetUniformBlockIndex(prog, "NdFrameUBO", 10);
	if (idx != GL_INVALID_INDEX)
		glUniformBlockBinding(prog, idx, 0);
}

static void gl_upload_frame_ubo(void)
{
	if (!s_gl.frame_ubo)
		return;
	nd_frame_ubo_t ubo;
	memset(&ubo, 0, sizeof(ubo));
	memcpy(ubo.uMVP, s_gl.cur_mvp, sizeof(ubo.uMVP));

	bool ts_active = (s_gl.tree_shadow_ready && s_gl.tree_shadow_read_valid &&
	                  s_gl.tree_shadow_active > 0.001f);
	if (ts_active) {
		memcpy(ubo.uTreeShadowVP, s_gl.tree_shadow_read_vp, sizeof(ubo.uTreeShadowVP));
		ubo.uTreeShadowOrigin[0] = s_gl.tree_shadow_read_origin[0];
		ubo.uTreeShadowOrigin[1] = s_gl.tree_shadow_read_origin[1];
		ubo.uTreeShadowOrigin[2] = s_gl.tree_shadow_read_origin[2];
		ubo.uTreeShadowActive = s_gl.tree_shadow_active;
	}

	ubo.uRefTrig[0] = s_gl.cur_ref_trig[0];
	ubo.uRefTrig[1] = s_gl.cur_ref_trig[1];
	ubo.uRefTrig[2] = s_gl.cur_ref_trig[2];
	ubo.uRefTrig[3] = s_gl.cur_ref_trig[3];

	ubo.uLambLight[0] = s_gl.cur_lamb_light[0];
	ubo.uLambLight[1] = s_gl.cur_lamb_light[1];
	ubo.uLambLight[2] = s_gl.cur_lamb_light[2];
	ubo.uLambLight[3] = s_gl.cur_lamb_light[3];

	ubo.uCameraPos[0] = s_gl.cur_eye[0];
	ubo.uCameraPos[1] = s_gl.cur_eye[1];
	ubo.uCameraPos[2] = s_gl.cur_eye[2];
	ubo.uAnimTime = s_gl.cur_anim_time;

	float space_t = fminf(1.0f, fmaxf(0.0f, (s_gl.cur_dist - 12000.0f) / 40000.0f));
	float fog_scale = 1.0f + space_t * 250.0f;
	ubo.uSkyColor[0] = 0.55f * (1.0f - space_t) + 0.002f * space_t;
	ubo.uSkyColor[1] = 0.70f * (1.0f - space_t) + 0.003f * space_t;
	ubo.uSkyColor[2] = 0.85f * (1.0f - space_t) + 0.006f * space_t;
	ubo.uAtmoIntensity = fminf(1.0f, fmaxf(0.0f, (s_gl.cur_dist - 1200.0f) / 8000.0f));

	ubo.uSunDir[0] = s_gl.cur_sun_dir[0];
	ubo.uSunDir[1] = s_gl.cur_sun_dir[1];
	ubo.uSunDir[2] = s_gl.cur_sun_dir[2];
	ubo.uFogStart = s_gl.cur_dist * 1.5f * fog_scale;

	ubo.uMoonDir[0] = s_gl.cur_moon_dir[0];
	ubo.uMoonDir[1] = s_gl.cur_moon_dir[1];
	ubo.uMoonDir[2] = s_gl.cur_moon_dir[2];
	ubo.uFogEnd = s_gl.cur_dist * 4.0f * fog_scale;

	if (s_gl.csm_valid) {
		ubo.uCsm[0] = s_gl.csm_win.cx;
		ubo.uCsm[1] = s_gl.csm_win.cy;
		ubo.uCsm[2] = s_gl.csm_win.half;
	} else if (nd_seven_get_cloud_quality() <= ND_CLOUD_QUALITY_LOW) {
		ubo.uCsm[2] = -2.0f;
	} else if (s_gl.csm_clear_sky) {
		ubo.uCsm[2] = -1.0f;
	}
	ubo.uBaseSeaLevel = s_gl.cur_base_sea_level;

	ubo.uWorldDim[0] = ND_SEVEN_WORLD_DIM_W;
	ubo.uWorldDim[1] = ND_SEVEN_WORLD_DIM_H;
	ubo.uCamDist = s_gl.cur_dist;
	int q = nd_seven_get_quality_preset();
	ubo.uTerrFastDist = (q == ND_QUALITY_LOW) ? 240.0f
	                  : ((q == ND_QUALITY_MEDIUM) ? 360.0f : 520.0f);

	glBindBuffer(GL_UNIFORM_BUFFER, s_gl.frame_ubo);
	glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(ubo), &ubo);
}

static void gl_downsample_rgba8_box(const uint8_t *src, int w, int h, uint8_t *dst)
{
	int dw = w >> 1, dh = h >> 1;
	if (dw < 1) dw = 1;
	if (dh < 1) dh = 1;
	for (int y = 0; y < dh; y++) {
		int sy0 = y * 2;
		int sy1 = (sy0 + 1 < h) ? (sy0 + 1) : sy0;
		const uint8_t *r0 = src + (size_t)sy0 * (size_t)w * 4u;
		const uint8_t *r1 = src + (size_t)sy1 * (size_t)w * 4u;
		uint8_t *out = dst + (size_t)y * (size_t)dw * 4u;
		for (int x = 0; x < dw; x++) {
			int sx0 = x * 2;
			int sx1 = (sx0 + 1 < w) ? (sx0 + 1) : sx0;
			const uint8_t *p00 = r0 + (size_t)sx0 * 4u;
			const uint8_t *p10 = r0 + (size_t)sx1 * 4u;
			const uint8_t *p01 = r1 + (size_t)sx0 * 4u;
			const uint8_t *p11 = r1 + (size_t)sx1 * 4u;
			out[0] = (uint8_t)((p00[0] + p10[0] + p01[0] + p11[0] + 2u) >> 2);
			out[1] = (uint8_t)((p00[1] + p10[1] + p01[1] + p11[1] + 2u) >> 2);
			out[2] = (uint8_t)((p00[2] + p10[2] + p01[2] + p11[2] + 2u) >> 2);
			out[3] = (uint8_t)((p00[3] + p10[3] + p01[3] + p11[3] + 2u) >> 2);
			out += 4;
		}
	}
}

static void gl_ensure_noise_luts(void)
{
	if (!s_gl.noise2d_tex) {
		const int dim2 = 256;
		const int mips = 9; /* 256 -> 128 -> ... -> 1 */
		uint8_t *buf0 = (uint8_t *)malloc((size_t)dim2 * (size_t)dim2 * 4u);
		uint8_t *buf1 = (uint8_t *)malloc((size_t)(dim2 >> 1) * (size_t)(dim2 >> 1) * 4u);
		if (buf0 && buf1) {
			nd_seven_gl_bake_noise2d_lut(buf0, (uint32_t)dim2, 32u);
			s_gl.noise2d_tex = glCreateTexture();
			glActiveTexture(GL_TEXTURE0 + 13);
			glBindTexture(GL_TEXTURE_2D, s_gl.noise2d_tex);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, mips - 1);
			glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
			int cw = dim2, ch = dim2;
			uint8_t *src = buf0, *dst = buf1;
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, cw, ch, 0, GL_RGBA, GL_UNSIGNED_BYTE, src);
			for (int m = 1; m < mips; m++) {
				int nw = cw >> 1, nh = ch >> 1;
				gl_downsample_rgba8_box(src, cw, ch, dst);
				glTexImage2D(GL_TEXTURE_2D, m, GL_RGBA8, nw, nh, 0, GL_RGBA, GL_UNSIGNED_BYTE, dst);
				uint8_t *tmp = src;
				src = dst;
				dst = tmp;
				cw = nw;
				ch = nh;
			}
			/* Leave permanently bound on unit 13 (zero per-frame rebinds). */
		}
		free(buf0);
		free(buf1);
	}
	if (!s_gl.noise3d_tex) {
		const int dim3 = 64;
		uint8_t *buf3 = (uint8_t *)malloc((size_t)dim3 * (size_t)dim3 * (size_t)dim3 * 4u);
		if (buf3) {
			nd_seven_gl_bake_noise3d_lut(buf3, (uint32_t)dim3, 8u);
			s_gl.noise3d_tex = glCreateTexture();
			glActiveTexture(GL_TEXTURE0 + 14);
			glBindTexture(GL_TEXTURE_3D, s_gl.noise3d_tex);
			glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_REPEAT);
			glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_REPEAT);
			glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_REPEAT);
			glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAX_LEVEL, 0);
			glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
			glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA8, dim3, dim3, dim3, 0, GL_RGBA, GL_UNSIGNED_BYTE, buf3);
			/* Leave permanently bound on unit 14 (zero per-frame rebinds). */
		}
		free(buf3);
	}
	glActiveTexture(GL_TEXTURE0);
}

static void gl_init_mesh3d_pipe_locs(gl_mesh3d_pipe_t *pipe)
{
	if (!pipe->prog)
		return;
	if (!glGetProgramParameter(pipe->prog, GL_LINK_STATUS)) {
		glDeleteProgram(pipe->prog);
		pipe->prog = 0;
		return;
	}
	GLuint p = pipe->prog;
	gl_bind_frame_ubo_prog(p);
	pipe->uSeaLevel              = glGetUniformLocation(p, "uSeaLevel", 9);
	pipe->uCloudShadowMap        = glGetUniformLocation(p, "uCloudShadowMap", 15);
	pipe->uShadowPass            = glGetUniformLocation(p, "uShadowPass", 11);
	pipe->uBranchletTex          = glGetUniformLocation(p, "uBranchletTex", 13);
	pipe->uBranchletNorm         = glGetUniformLocation(p, "uBranchletNorm", 14);
	pipe->uGrassTex              = glGetUniformLocation(p, "uGrassTex", 9);
	pipe->uGrassNorm             = glGetUniformLocation(p, "uGrassNorm", 10);
	pipe->uBarkTex               = glGetUniformLocation(p, "uBarkTex", 8);
	pipe->uBarkNorm              = glGetUniformLocation(p, "uBarkNorm", 9);
	pipe->uTreeShadowTex         = glGetUniformLocation(p, "uTreeShadowTex", 14);
	pipe->uNoise2DTex            = glGetUniformLocation(p, "uNoise2DTex", 11);
	glUseProgram(p);
	if (pipe->uNoise2DTex >= 0) glUniform1i(pipe->uNoise2DTex, 13);
}

static void gl_init_mesh3d_shadow_pipe_locs(gl_mesh3d_shadow_pipe_t *pipe)
{
	if (!pipe->prog)
		return;
	if (!glGetProgramParameter(pipe->prog, GL_LINK_STATUS)) {
		glDeleteProgram(pipe->prog);
		pipe->prog = 0;
		return;
	}
	GLuint p = pipe->prog;
	gl_bind_frame_ubo_prog(p);
	pipe->uSeaLevel              = glGetUniformLocation(p, "uSeaLevel", 9);
	pipe->uTreeShadowWriteOrigin = glGetUniformLocation(p, "uTreeShadowWriteOrigin", 22);
	pipe->uTreeShadowWriteVP     = glGetUniformLocation(p, "uTreeShadowWriteVP", 18);
	pipe->uBranchletTex          = glGetUniformLocation(p, "uBranchletTex", 13);
	pipe->uGrassTex              = glGetUniformLocation(p, "uGrassTex", 9);
	pipe->uNoise2DTex            = glGetUniformLocation(p, "uNoise2DTex", 11);
	glUseProgram(p);
	if (pipe->uBranchletTex >= 0) glUniform1i(pipe->uBranchletTex, 3);
	if (pipe->uGrassTex >= 0)     glUniform1i(pipe->uGrassTex, 5);
	if (pipe->uNoise2DTex >= 0)   glUniform1i(pipe->uNoise2DTex, 13);
}

static void gl_link_terrain_pipe(gl_terrain_pipe_t *tp)
{
	if (!tp->prog)
		return;
	if (!glGetProgramParameter(tp->prog, GL_LINK_STATUS)) {
		glDeleteProgram(tp->prog);
		tp->prog = 0;
		return;
	}
	GLuint p = tp->prog;
	gl_bind_frame_ubo_prog(p);
	tp->uHeightTex        = glGetUniformLocation(p, "uHeightTex", 10);
	tp->uSubstrateTex     = glGetUniformLocation(p, "uSubstrateTex", 13);
	tp->uRiverTex         = glGetUniformLocation(p, "uRiverTex", 9);
	tp->uChunkOrigin      = glGetUniformLocation(p, "uChunkOrigin", 12);
	tp->uChunkRelOrigin   = glGetUniformLocation(p, "uChunkRelOrigin", 15);
	tp->uChunkSpan        = glGetUniformLocation(p, "uChunkSpan", 10);
	tp->uHeightScale      = glGetUniformLocation(p, "uHeightScale", 12);
	tp->uSeaLevel         = glGetUniformLocation(p, "uSeaLevel", 9);
	tp->uPlanetCenter     = glGetUniformLocation(p, "uPlanetCenter", 13);
	tp->uBodyRot          = glGetUniformLocation(p, "uBodyRot", 8);
	tp->uTorusRadii       = glGetUniformLocation(p, "uTorusRadii", 11);
	tp->uCloudShadowMap   = glGetUniformLocation(p, "uCloudShadowMap", 15);
	tp->uLambWakeHead     = glGetUniformLocation(p, "uLambWakeHead", 13);
	tp->uLambWakeXYT0     = glGetUniformLocation(p, "uLambWakeXYT0", 13);
	tp->uLambWakeXYT1     = glGetUniformLocation(p, "uLambWakeXYT1", 13);
	tp->uLambWakeVel0     = glGetUniformLocation(p, "uLambWakeVel0", 13);
	tp->uLambWakeVel1     = glGetUniformLocation(p, "uLambWakeVel1", 13);
	tp->uWorldId          = glGetUniformLocation(p, "uWorldId", 8);
	tp->uSeaOffsetQ12_4   = glGetUniformLocation(p, "uSeaOffsetQ12_4", 15);
	tp->uTreeShadowTex    = glGetUniformLocation(p, "uTreeShadowTex", 14);
	tp->uTerrDetailTex    = glGetUniformLocation(p, "uTerrDetailTex", 14);
	tp->uTerrNormTex      = glGetUniformLocation(p, "uTerrNormTex", 12);
	tp->uNoise2DTex       = glGetUniformLocation(p, "uNoise2DTex", 11);
	tp->uNoise3DTex       = glGetUniformLocation(p, "uNoise3DTex", 11);
	tp->aGridUV           = glGetAttribLocation(p, "a_grid_uv", 9);

	/* Bind immutable sampler units and constant height scale once per program */
	glUseProgram(p);
	if (tp->uHeightTex >= 0)      glUniform1i(tp->uHeightTex, 0);
	if (tp->uSubstrateTex >= 0)   glUniform1i(tp->uSubstrateTex, 1);
	if (tp->uRiverTex >= 0)       glUniform1i(tp->uRiverTex, 2);
	if (tp->uCloudShadowMap >= 0) glUniform1i(tp->uCloudShadowMap, 7);
	if (tp->uTerrDetailTex >= 0)  glUniform1i(tp->uTerrDetailTex, 10);
	if (tp->uTerrNormTex >= 0)    glUniform1i(tp->uTerrNormTex, 11);
	if (tp->uTreeShadowTex >= 0)  glUniform1i(tp->uTreeShadowTex, 12);
	if (tp->uNoise2DTex >= 0)     glUniform1i(tp->uNoise2DTex, 13);
	if (tp->uNoise3DTex >= 0)     glUniform1i(tp->uNoise3DTex, 14);
	if (tp->uHeightScale >= 0)    glUniform1f(tp->uHeightScale, 1.0f);
}

static void gl_link_cloud_pipe(gl_cloud_pipe_t *cp)
{
	if (!cp->prog)
		return;
	if (!glGetProgramParameter(cp->prog, GL_LINK_STATUS)) {
		glDeleteProgram(cp->prog);
		cp->prog = 0;
		return;
	}
	GLuint p = cp->prog;
	gl_bind_frame_ubo_prog(p);
	cp->uHeightTex            = glGetUniformLocation(p, "uHeightTex", 10);
	cp->uSubstrateTex         = glGetUniformLocation(p, "uSubstrateTex", 13);
	cp->uChunkOrigin          = glGetUniformLocation(p, "uChunkOrigin", 12);
	cp->uChunkRelOrigin       = glGetUniformLocation(p, "uChunkRelOrigin", 15);
	cp->uChunkSpan            = glGetUniformLocation(p, "uChunkSpan", 10);
	cp->uSeaLevel             = glGetUniformLocation(p, "uSeaLevel", 9);
	cp->uPlanetCenter         = glGetUniformLocation(p, "uPlanetCenter", 13);
	cp->uBodyRot              = glGetUniformLocation(p, "uBodyRot", 8);
	cp->uTorusRadii           = glGetUniformLocation(p, "uTorusRadii", 11);
	cp->uCloudDeck            = glGetUniformLocation(p, "uCloudDeck", 10);
	cp->uCloudShadowMap       = glGetUniformLocation(p, "uCloudShadowMap", 15);
	cp->uViewport             = glGetUniformLocation(p, "uViewport", 9);
	cp->uClusterGridDim       = glGetUniformLocation(p, "uClusterGridDim", 15);
	cp->uClusterDepthParams   = glGetUniformLocation(p, "uClusterDepthParams", 19);
	cp->uClusterLightCount    = glGetUniformLocation(p, "uClusterLightCount", 18);
	cp->uClusterLightData     = glGetUniformLocation(p, "uClusterLightData", 17);
	cp->uClusterLightListsTex = glGetUniformLocation(p, "uClusterLightListsTex", 21);
	cp->uCloudResScale        = glGetUniformLocation(p, "uCloudResScale", 14);
	cp->uNoise2DTex           = glGetUniformLocation(p, "uNoise2DTex", 11);
	cp->uNoise3DTex           = glGetUniformLocation(p, "uNoise3DTex", 11);
	cp->aGridUV               = glGetAttribLocation(p, "a_grid_uv", 9);

	/* Bind constant sampler texture units once at link time */
	glUseProgram(p);
	if (cp->uSubstrateTex >= 0)         glUniform1i(cp->uSubstrateTex, 0);
	if (cp->uHeightTex >= 0)            glUniform1i(cp->uHeightTex, 1);
	if (cp->uCloudShadowMap >= 0)       glUniform1i(cp->uCloudShadowMap, 7);
	if (cp->uClusterLightData >= 0)     glUniform1i(cp->uClusterLightData, 9);
	if (cp->uClusterLightListsTex >= 0) glUniform1i(cp->uClusterLightListsTex, 10);
	if (cp->uNoise2DTex >= 0)           glUniform1i(cp->uNoise2DTex, 13);
	if (cp->uNoise3DTex >= 0)           glUniform1i(cp->uNoise3DTex, 14);
}

static void gl_link_cloud_comp_pipe(gl_cloud_comp_pipe_t *ccp)
{
	if (!ccp->prog)
		return;
	if (!glGetProgramParameter(ccp->prog, GL_LINK_STATUS)) {
		glDeleteProgram(ccp->prog);
		ccp->prog = 0;
		return;
	}
	GLuint p = ccp->prog;
	ccp->uCloudTex        = glGetUniformLocation(p, "uCloudTex", 9);
	ccp->uCloudUvScale    = glGetUniformLocation(p, "uCloudUvScale", 13);
	ccp->uSceneUvScale    = glGetUniformLocation(p, "uSceneUvScale", 13);
	ccp->uCloudDepthTex   = glGetUniformLocation(p, "uCloudDepthTex", 14);
	ccp->uSceneDepthTex   = glGetUniformLocation(p, "uSceneDepthTex", 14);
	ccp->uBilateralActive = glGetUniformLocation(p, "uBilateralActive", 16);
}

static void gl_link_ssao_pipe(gl_ssao_pipe_t *sp)
{
	if (!sp->prog)
		return;
	if (!glGetProgramParameter(sp->prog, GL_LINK_STATUS)) {
		glDeleteProgram(sp->prog);
		sp->prog = 0;
		return;
	}
	GLuint p = sp->prog;
	sp->uDepthTex     = glGetUniformLocation(p, "uDepthTex", 9);
	sp->uNearFar      = glGetUniformLocation(p, "uNearFar", 8);
	sp->uProjScale    = glGetUniformLocation(p, "uProjScale", 10);
	sp->uViewport     = glGetUniformLocation(p, "uViewport", 9);
	sp->uAnimTime     = glGetUniformLocation(p, "uAnimTime", 9);
	sp->uSceneUvScale = glGetUniformLocation(p, "uSceneUvScale", 13);
}

static void gl_link_bloom_pipe(gl_bloom_pipe_t *bp)
{
	if (!bp->prog)
		return;
	if (!glGetProgramParameter(bp->prog, GL_LINK_STATUS)) {
		glDeleteProgram(bp->prog);
		bp->prog = 0;
		return;
	}
	GLuint p = bp->prog;
	bp->uSrcTex    = glGetUniformLocation(p, "uSrcTex", 7);
	bp->uTexelSize = glGetUniformLocation(p, "uTexelSize", 10);
	bp->uPassDir   = glGetUniformLocation(p, "uPassDir", 8);
	bp->uUvScale   = glGetUniformLocation(p, "uUvScale", 8);
}

static void init_pipelines(void)
{
	/* Probe GL context readiness before allocating buffers or queuing shaders:
	 * in WASM worker mode, the real stage canvas attaches via stele:canvas_attach
	 * a few milliseconds after the first tick. Deferring init until a real context
	 * exists avoids double-compiling shaders across a throwaway canvas. */
	GLuint probe = glCreateBuffer();
	if (!probe)
		return;
	glDeleteBuffer(probe);

	/* Upload static geometry VBOs/IBOs and 1x1 placeholder textures BEFORE
	 * queuing async shader compiles so large glBufferData uploads never
	 * stall Chrome's GPU command buffer behind pending shader links. */
	init_grid_buffers();
	gl_ensure_noise_luts();

	/* Tier A (13 boot-critical ground-level programs): share compiled VS handles across
	 * variants and unblock frame 0 as soon as near-field shaders finish linking. */
	s_gl.sky_pipe.prog = create_program_async(nd_seven_gl_vs_sky(), nd_seven_gl_fs_sky());

	GLuint vs_terr = compile_shader(GL_VERTEX_SHADER, nd_seven_gl_vs_terrain());
	if (vs_terr) {
		s_gl.terrain_pipes[ND_TERR_VAR_DRY_NEAR].prog    = create_program_with_vs_async(vs_terr, nd_seven_gl_fs_terrain_dry_near());
		s_gl.terrain_pipes[ND_TERR_VAR_SEABED_NEAR].prog = create_program_with_vs_async(vs_terr, nd_seven_gl_fs_terrain_seabed_near());
		glDeleteShader(vs_terr);
	}

	s_gl.water_pipe.prog = create_program_async(nd_seven_gl_vs_water(), nd_seven_gl_fs_water());
	s_gl.bb_pipe.prog    = create_program_async(nd_seven_gl_vs_billboard(), nd_seven_gl_fs_billboard());

	GLuint vs_m3d = compile_shader(GL_VERTEX_SHADER, nd_seven_gl_vs_mesh3d());
	if (vs_m3d) {
		s_gl.mesh3d_pipe.prog        = create_program_with_vs_async(vs_m3d, nd_seven_gl_fs_mesh3d());
		s_gl.mesh3d_opaque_pipe.prog = create_program_with_vs_async(vs_m3d, nd_seven_gl_fs_mesh3d_opaque());
		glDeleteShader(vs_m3d);
	}

	GLuint vs_m3d_sh = compile_shader(GL_VERTEX_SHADER, nd_seven_gl_vs_mesh3d_shadow());
	if (vs_m3d_sh) {
		s_gl.mesh3d_shadow_alpha_pipe.prog  = create_program_with_vs_async(vs_m3d_sh, nd_seven_gl_fs_mesh3d_shadow_alpha());
		s_gl.mesh3d_shadow_opaque_pipe.prog = create_program_with_vs_async(vs_m3d_sh, nd_seven_gl_fs_mesh3d_shadow_opaque());
		glDeleteShader(vs_m3d_sh);
	}

	s_gl.grass_pipe.prog = create_program_async(nd_seven_gl_vs_grass(), nd_seven_gl_fs_grass());

	GLuint vs_csm = compile_shader(GL_VERTEX_SHADER, nd_seven_gl_vs_csm());
	if (vs_csm) {
		s_gl.csm_pipe.prog  = create_program_with_vs_async(vs_csm, nd_seven_gl_fs_csm());
		s_gl.post_pipe.prog = create_program_with_vs_async(vs_csm, nd_seven_gl_fs_post());
		glDeleteShader(vs_csm);
	}

	s_gl.terrain_shadow_pipe.prog = create_program_async(nd_seven_gl_vs_terrain_shadow(), nd_seven_gl_fs_terrain_shadow());
	s_gl.initialized = true;
	s_gl.pipelines_ready = false;
	s_gl.tier_b_queued = false;
	s_gl.tier_b_done = false;
}

static void init_tier_b_pipelines(void)
{
	if (s_gl.tier_b_queued)
		return;
	s_gl.tier_b_queued = true;

	/* Tier B (9 active mid/macro/secondary programs): queued in the background AFTER frame 0
	 * renders so frame 0 paints immediately without waiting on mid/macro/orbital terrain,
	 * volumetric clouds, SSAO, or bloom shader compilation. Emergency monolithic fallbacks
	 * (112-spill TERRAIN_FULL / 81-spill CLOUD_FULL) are compiled lazily only if ever requested. */
	GLuint vs_terr = compile_shader(GL_VERTEX_SHADER, nd_seven_gl_vs_terrain());
	if (vs_terr) {
		s_gl.terrain_pipes[ND_TERR_VAR_DRY_CSM].pending_prog    = create_program_with_vs_async(vs_terr, nd_seven_gl_fs_terrain_dry_csm());
		s_gl.terrain_pipes[ND_TERR_VAR_SEABED_MID].pending_prog = create_program_with_vs_async(vs_terr, nd_seven_gl_fs_terrain_seabed_mid());
		s_gl.terrain_pipes[ND_TERR_VAR_MACRO].pending_prog      = create_program_with_vs_async(vs_terr, nd_seven_gl_fs_terrain_macro());
		s_gl.terrain_pipes[ND_TERR_VAR_MACRO_FAR].pending_prog  = create_program_with_vs_async(vs_terr, nd_seven_gl_fs_terrain_macro_far());
		glDeleteShader(vs_terr);
	}

	GLuint vs_cloud = compile_shader(GL_VERTEX_SHADER, nd_seven_gl_vs_cloud());
	if (vs_cloud) {
		s_gl.cloud_pipes[ND_CLOUD_VAR_WIDE_CSM].pending_prog = create_program_with_vs_async(vs_cloud, nd_seven_gl_fs_cloud_wide_csm());
		s_gl.cloud_pipes[ND_CLOUD_VAR_WIDE].pending_prog     = create_program_with_vs_async(vs_cloud, nd_seven_gl_fs_cloud_wide());
		glDeleteShader(vs_cloud);
	}

	GLuint vs_csm = compile_shader(GL_VERTEX_SHADER, nd_seven_gl_vs_csm());
	if (vs_csm) {
		s_gl.cloud_comp_pipe.pending_prog = create_program_with_vs_async(vs_csm, nd_seven_gl_fs_cloud_comp());
		s_gl.ssao_pipe.pending_prog       = create_program_with_vs_async(vs_csm, nd_seven_gl_fs_ssao());
		s_gl.bloom_pipe.pending_prog      = create_program_with_vs_async(vs_csm, nd_seven_gl_fs_bloom());
		glDeleteShader(vs_csm);
	}
}

static void poll_tier_b_pipelines(void)
{
	if (!s_gl.tier_b_queued || s_gl.tier_b_done)
		return;
	bool any_pending = false;

	static const int tier_b_terr[5] = {
		ND_TERR_VAR_DRY_CSM, ND_TERR_VAR_SEABED_MID,
		ND_TERR_VAR_MACRO, ND_TERR_VAR_MACRO_FAR, ND_TERR_VAR_FULL
	};
	for (int i = 0; i < 5; i++) {
		gl_terrain_pipe_t *tp = &s_gl.terrain_pipes[tier_b_terr[i]];
		if (tp->pending_prog) {
			if (glGetProgramParameter(tp->pending_prog, GL_COMPLETION_STATUS_KHR)) {
				tp->prog = tp->pending_prog;
				tp->pending_prog = 0;
				gl_link_terrain_pipe(tp);
			} else {
				any_pending = true;
			}
		}
	}

	for (int v = 0; v < ND_CLOUD_VAR_COUNT; v++) {
		gl_cloud_pipe_t *cp = &s_gl.cloud_pipes[v];
		if (cp->pending_prog) {
			if (glGetProgramParameter(cp->pending_prog, GL_COMPLETION_STATUS_KHR)) {
				cp->prog = cp->pending_prog;
				cp->pending_prog = 0;
				gl_link_cloud_pipe(cp);
			} else {
				any_pending = true;
			}
		}
	}

	if (s_gl.cloud_comp_pipe.pending_prog) {
		if (glGetProgramParameter(s_gl.cloud_comp_pipe.pending_prog, GL_COMPLETION_STATUS_KHR)) {
			s_gl.cloud_comp_pipe.prog = s_gl.cloud_comp_pipe.pending_prog;
			s_gl.cloud_comp_pipe.pending_prog = 0;
			gl_link_cloud_comp_pipe(&s_gl.cloud_comp_pipe);
		} else {
			any_pending = true;
		}
	}
	if (s_gl.ssao_pipe.pending_prog) {
		if (glGetProgramParameter(s_gl.ssao_pipe.pending_prog, GL_COMPLETION_STATUS_KHR)) {
			s_gl.ssao_pipe.prog = s_gl.ssao_pipe.pending_prog;
			s_gl.ssao_pipe.pending_prog = 0;
			gl_link_ssao_pipe(&s_gl.ssao_pipe);
		} else {
			any_pending = true;
		}
	}
	if (s_gl.bloom_pipe.pending_prog) {
		if (glGetProgramParameter(s_gl.bloom_pipe.pending_prog, GL_COMPLETION_STATUS_KHR)) {
			s_gl.bloom_pipe.prog = s_gl.bloom_pipe.pending_prog;
			s_gl.bloom_pipe.pending_prog = 0;
			gl_link_bloom_pipe(&s_gl.bloom_pipe);
		} else {
			any_pending = true;
		}
	}

	if (!any_pending)
		s_gl.tier_b_done = true;
}

static bool check_pipelines_ready(void)
{
	if (s_gl.pipelines_ready)
		return true;
	if (!s_gl.initialized)
		return false;

	/* Wait ONLY on Tier A (13 boot-critical ground-level programs) so frame 0 unblocks immediately. */
	static const int tier_a_terr[2] = {
		ND_TERR_VAR_DRY_NEAR, ND_TERR_VAR_SEABED_NEAR
	};
	if (s_gl.sky_pipe.prog && !glGetProgramParameter(s_gl.sky_pipe.prog, GL_COMPLETION_STATUS_KHR))
		return false;
	for (int i = 0; i < 2; i++) {
		GLuint tp = s_gl.terrain_pipes[tier_a_terr[i]].prog;
		if (tp && !glGetProgramParameter(tp, GL_COMPLETION_STATUS_KHR))
			return false;
	}
	if (s_gl.water_pipe.prog && !glGetProgramParameter(s_gl.water_pipe.prog, GL_COMPLETION_STATUS_KHR))
		return false;
	if (s_gl.bb_pipe.prog && !glGetProgramParameter(s_gl.bb_pipe.prog, GL_COMPLETION_STATUS_KHR))
		return false;
	if (s_gl.mesh3d_pipe.prog && !glGetProgramParameter(s_gl.mesh3d_pipe.prog, GL_COMPLETION_STATUS_KHR))
		return false;
	if (s_gl.mesh3d_opaque_pipe.prog && !glGetProgramParameter(s_gl.mesh3d_opaque_pipe.prog, GL_COMPLETION_STATUS_KHR))
		return false;
	if (s_gl.mesh3d_shadow_alpha_pipe.prog && !glGetProgramParameter(s_gl.mesh3d_shadow_alpha_pipe.prog, GL_COMPLETION_STATUS_KHR))
		return false;
	if (s_gl.mesh3d_shadow_opaque_pipe.prog && !glGetProgramParameter(s_gl.mesh3d_shadow_opaque_pipe.prog, GL_COMPLETION_STATUS_KHR))
		return false;
	if (s_gl.grass_pipe.prog && !glGetProgramParameter(s_gl.grass_pipe.prog, GL_COMPLETION_STATUS_KHR))
		return false;
	if (s_gl.csm_pipe.prog && !glGetProgramParameter(s_gl.csm_pipe.prog, GL_COMPLETION_STATUS_KHR))
		return false;
	if (s_gl.post_pipe.prog && !glGetProgramParameter(s_gl.post_pipe.prog, GL_COMPLETION_STATUS_KHR))
		return false;
	if (s_gl.terrain_shadow_pipe.prog && !glGetProgramParameter(s_gl.terrain_shadow_pipe.prog, GL_COMPLETION_STATUS_KHR))
		return false;

	/* Sky dome pipeline */
	if (s_gl.sky_pipe.prog) {
		if (!glGetProgramParameter(s_gl.sky_pipe.prog, GL_LINK_STATUS)) {
			glDeleteProgram(s_gl.sky_pipe.prog);
			s_gl.sky_pipe.prog = 0;
		} else {
			GLuint p = s_gl.sky_pipe.prog;
			gl_bind_frame_ubo_prog(p);
			s_gl.sky_pipe.uCamFwd        = glGetUniformLocation(p, "uCamFwd", 7);
			s_gl.sky_pipe.uCamRight      = glGetUniformLocation(p, "uCamRight", 9);
			s_gl.sky_pipe.uCamUp         = glGetUniformLocation(p, "uCamUp", 6);
			s_gl.sky_pipe.uTanHalfAspect = glGetUniformLocation(p, "uTanHalfAspect", 14);
			s_gl.sky_pipe.uEast          = glGetUniformLocation(p, "uEast", 5);
			s_gl.sky_pipe.uSouth         = glGetUniformLocation(p, "uSouth", 6);
			s_gl.sky_pipe.uZenith        = glGetUniformLocation(p, "uZenith", 7);
			s_gl.sky_pipe.uTorusEye      = glGetUniformLocation(p, "uTorusEye", 9);
			s_gl.sky_pipe.uTorusRadii    = glGetUniformLocation(p, "uTorusRadii", 11);
			s_gl.sky_pipe.uAtmoHaloVis   = glGetUniformLocation(p, "uAtmoHaloVis", 12);
		}
	}

	/* Tier A terrain pipelines (DRY_NEAR, SEABED_NEAR) */
	for (int i = 0; i < 2; i++)
		gl_link_terrain_pipe(&s_gl.terrain_pipes[tier_a_terr[i]]);

	/* Water pipeline */
	if (s_gl.water_pipe.prog) {
		if (!glGetProgramParameter(s_gl.water_pipe.prog, GL_LINK_STATUS)) {
			glDeleteProgram(s_gl.water_pipe.prog);
			s_gl.water_pipe.prog = 0;
		} else {
			GLuint p = s_gl.water_pipe.prog;
			gl_bind_frame_ubo_prog(p);
			s_gl.water_pipe.uHeightTex      = glGetUniformLocation(p, "uHeightTex", 10);
			s_gl.water_pipe.uSubstrateTex   = glGetUniformLocation(p, "uSubstrateTex", 13);
			s_gl.water_pipe.uRiverTex       = glGetUniformLocation(p, "uRiverTex", 9);
			s_gl.water_pipe.uChunkOrigin    = glGetUniformLocation(p, "uChunkOrigin", 12);
			s_gl.water_pipe.uChunkRelOrigin = glGetUniformLocation(p, "uChunkRelOrigin", 15);
			s_gl.water_pipe.uChunkSpan      = glGetUniformLocation(p, "uChunkSpan", 10);
			s_gl.water_pipe.uHeightScale    = glGetUniformLocation(p, "uHeightScale", 12);
			s_gl.water_pipe.uSeaLevel       = glGetUniformLocation(p, "uSeaLevel", 9);
			s_gl.water_pipe.uPlanetCenter   = glGetUniformLocation(p, "uPlanetCenter", 13);
			s_gl.water_pipe.uBodyRot        = glGetUniformLocation(p, "uBodyRot", 8);
			s_gl.water_pipe.uTorusRadii     = glGetUniformLocation(p, "uTorusRadii", 11);
			s_gl.water_pipe.uCloudShadowMap = glGetUniformLocation(p, "uCloudShadowMap", 15);
			s_gl.water_pipe.uLambWakeHead   = glGetUniformLocation(p, "uLambWakeHead", 13);
			s_gl.water_pipe.uLambWakeXYT0   = glGetUniformLocation(p, "uLambWakeXYT0", 13);
			s_gl.water_pipe.uLambWakeXYT1   = glGetUniformLocation(p, "uLambWakeXYT1", 13);
			s_gl.water_pipe.uLambWakeVel0   = glGetUniformLocation(p, "uLambWakeVel0", 13);
			s_gl.water_pipe.uLambWakeVel1   = glGetUniformLocation(p, "uLambWakeVel1", 13);
			s_gl.water_pipe.uSceneColor     = glGetUniformLocation(p, "uSceneColor", 11);
			s_gl.water_pipe.uSceneDepth     = glGetUniformLocation(p, "uSceneDepth", 11);
			s_gl.water_pipe.uViewport       = glGetUniformLocation(p, "uViewport", 9);
			s_gl.water_pipe.uSceneUvScale   = glGetUniformLocation(p, "uSceneUvScale", 13);
			s_gl.water_pipe.uNearFar        = glGetUniformLocation(p, "uNearFar", 8);
			s_gl.water_pipe.uProjScale      = glGetUniformLocation(p, "uProjScale", 10);
			s_gl.water_pipe.uNoise2DTex     = glGetUniformLocation(p, "uNoise2DTex", 11);
			s_gl.water_pipe.uNoise3DTex     = glGetUniformLocation(p, "uNoise3DTex", 11);
			s_gl.water_pipe.aGridUV         = glGetAttribLocation(p, "a_grid_uv", 9);
			glUseProgram(p);
			if (s_gl.water_pipe.uHeightTex >= 0)      glUniform1i(s_gl.water_pipe.uHeightTex, 0);
			if (s_gl.water_pipe.uSubstrateTex >= 0)   glUniform1i(s_gl.water_pipe.uSubstrateTex, 1);
			if (s_gl.water_pipe.uRiverTex >= 0)       glUniform1i(s_gl.water_pipe.uRiverTex, 2);
			if (s_gl.water_pipe.uSceneColor >= 0)     glUniform1i(s_gl.water_pipe.uSceneColor, 4);
			if (s_gl.water_pipe.uSceneDepth >= 0)     glUniform1i(s_gl.water_pipe.uSceneDepth, 5);
			if (s_gl.water_pipe.uCloudShadowMap >= 0) glUniform1i(s_gl.water_pipe.uCloudShadowMap, 7);
			if (s_gl.water_pipe.uNoise2DTex >= 0)     glUniform1i(s_gl.water_pipe.uNoise2DTex, 13);
			if (s_gl.water_pipe.uNoise3DTex >= 0)     glUniform1i(s_gl.water_pipe.uNoise3DTex, 14);
		}
	}

	/* Billboard pipeline */
	if (s_gl.bb_pipe.prog) {
		if (!glGetProgramParameter(s_gl.bb_pipe.prog, GL_LINK_STATUS)) {
			glDeleteProgram(s_gl.bb_pipe.prog);
			s_gl.bb_pipe.prog = 0;
		} else {
			GLuint p = s_gl.bb_pipe.prog;
			gl_bind_frame_ubo_prog(p);
			s_gl.bb_pipe.uAtlasTex       = glGetUniformLocation(p, "uAtlasTex", 9);
			s_gl.bb_pipe.uAtlasNormTex   = glGetUniformLocation(p, "uAtlasNormTex", 13);
			s_gl.bb_pipe.uSeaLevel       = glGetUniformLocation(p, "uSeaLevel", 9);
			s_gl.bb_pipe.uCloudShadowMap = glGetUniformLocation(p, "uCloudShadowMap", 15);
			s_gl.bb_pipe.uPass           = glGetUniformLocation(p, "uPass", 5);
			s_gl.bb_pipe.uCamAngles      = glGetUniformLocation(p, "uCamAngles", 10);
			s_gl.bb_pipe.uHiZTex         = glGetUniformLocation(p, "uHiZTex", 7);
			s_gl.bb_pipe.uHiZActive      = glGetUniformLocation(p, "uHiZActive", 10);
			s_gl.bb_pipe.uTreeShadowTex  = glGetUniformLocation(p, "uTreeShadowTex", 14);
			s_gl.bb_pipe.uNoise2DTex     = glGetUniformLocation(p, "uNoise2DTex", 11);
			s_gl.bb_pipe.uNoise3DTex     = glGetUniformLocation(p, "uNoise3DTex", 11);
			s_gl.bb_pipe.aPos            = glGetAttribLocation(p, "a_pos", 5);
			s_gl.bb_pipe.aUV             = glGetAttribLocation(p, "a_uv", 4);
			s_gl.bb_pipe.aTint           = glGetAttribLocation(p, "a_tint", 6);
			s_gl.bb_pipe.aLocalPos       = glGetAttribLocation(p, "a_local_pos", 11);
			s_gl.bb_pipe.aQuadUV         = glGetAttribLocation(p, "a_quad_uv", 9);
			s_gl.bb_pipe.aSpriteBounds   = glGetAttribLocation(p, "a_sprite_bounds", 15);
			glUseProgram(p);
			if (s_gl.bb_pipe.uNoise2DTex >= 0) glUniform1i(s_gl.bb_pipe.uNoise2DTex, 13);
			if (s_gl.bb_pipe.uNoise3DTex >= 0) glUniform1i(s_gl.bb_pipe.uNoise3DTex, 14);
		}
	}

	/* Depth-only terrain shadow caster pipeline (into tree_shadow_fbo) */
	if (s_gl.terrain_shadow_pipe.prog) {
		if (!glGetProgramParameter(s_gl.terrain_shadow_pipe.prog, GL_LINK_STATUS)) {
			glDeleteProgram(s_gl.terrain_shadow_pipe.prog);
			s_gl.terrain_shadow_pipe.prog = 0;
		} else {
			GLuint p = s_gl.terrain_shadow_pipe.prog;
			s_gl.terrain_shadow_pipe.uHeightTex             = glGetUniformLocation(p, "uHeightTex", 10);
			s_gl.terrain_shadow_pipe.uChunkOrigin           = glGetUniformLocation(p, "uChunkOrigin", 12);
			s_gl.terrain_shadow_pipe.uChunkSpan             = glGetUniformLocation(p, "uChunkSpan", 10);
			s_gl.terrain_shadow_pipe.uHeightScale           = glGetUniformLocation(p, "uHeightScale", 12);
			s_gl.terrain_shadow_pipe.uTreeShadowWriteOrigin = glGetUniformLocation(p, "uTreeShadowWriteOrigin", 22);
			s_gl.terrain_shadow_pipe.uTreeShadowWriteVP     = glGetUniformLocation(p, "uTreeShadowWriteVP", 18);
			s_gl.terrain_shadow_pipe.aGridUV                = glGetAttribLocation(p, "a_grid_uv", 9);
		}
	}

	/* 3D Mesh Instanced pipelines (alpha-tested + opaque zero-discard) and dedicated Pass A shadow pipelines */
	gl_init_mesh3d_pipe_locs(&s_gl.mesh3d_pipe);
	gl_init_mesh3d_pipe_locs(&s_gl.mesh3d_opaque_pipe);
	gl_init_mesh3d_shadow_pipe_locs(&s_gl.mesh3d_shadow_alpha_pipe);
	gl_init_mesh3d_shadow_pipe_locs(&s_gl.mesh3d_shadow_opaque_pipe);

	/* GPU-Instanced Chunk Grass & Clover pipeline (pre-culled per-chunk tuft VBO) */
	if (s_gl.grass_pipe.prog) {
		if (!glGetProgramParameter(s_gl.grass_pipe.prog, GL_LINK_STATUS)) {
			glDeleteProgram(s_gl.grass_pipe.prog);
			s_gl.grass_pipe.prog = 0;
		} else {
			GLuint p = s_gl.grass_pipe.prog;
			gl_bind_frame_ubo_prog(p);
			s_gl.grass_pipe.uChunkOrigin      = glGetUniformLocation(p, "uChunkOrigin", 12);
			s_gl.grass_pipe.uChunkRelOrigin   = glGetUniformLocation(p, "uChunkRelOrigin", 15);
			s_gl.grass_pipe.uChunkSpan        = glGetUniformLocation(p, "uChunkSpan", 10);
			s_gl.grass_pipe.uSeaLevel         = glGetUniformLocation(p, "uSeaLevel", 9);
			s_gl.grass_pipe.uPlanetCenter     = glGetUniformLocation(p, "uPlanetCenter", 13);
			s_gl.grass_pipe.uBodyRot          = glGetUniformLocation(p, "uBodyRot", 8);
			s_gl.grass_pipe.uTorusRadii       = glGetUniformLocation(p, "uTorusRadii", 11);
			s_gl.grass_pipe.uCloudShadowMap   = glGetUniformLocation(p, "uCloudShadowMap", 15);
			s_gl.grass_pipe.uTreeShadowTex    = glGetUniformLocation(p, "uTreeShadowTex", 14);
		}
	}

	/* Cloud-shadow map pipeline */
	if (s_gl.csm_pipe.prog) {
		if (!glGetProgramParameter(s_gl.csm_pipe.prog, GL_LINK_STATUS)) {
			glDeleteProgram(s_gl.csm_pipe.prog);
			s_gl.csm_pipe.prog = 0;
		} else {
			GLuint p = s_gl.csm_pipe.prog;
			s_gl.csm_pipe.uCsm      = glGetUniformLocation(p, "uCsm", 4);
			s_gl.csm_pipe.uAnimTime = glGetUniformLocation(p, "uAnimTime", 9);
			s_gl.csm_pipe.uSeaWet   = glGetUniformLocation(p, "uSeaWet", 7);
		}
	}

	/* HDR ACES tone-mapping + SSAO + bloom + FXAA composite pipeline */
	if (s_gl.post_pipe.prog) {
		if (!glGetProgramParameter(s_gl.post_pipe.prog, GL_LINK_STATUS)) {
			glDeleteProgram(s_gl.post_pipe.prog);
			s_gl.post_pipe.prog = 0;
		} else {
			GLuint p = s_gl.post_pipe.prog;
			s_gl.post_pipe.uSceneTex     = glGetUniformLocation(p, "uSceneTex", 9);
			s_gl.post_pipe.uBloomTex     = glGetUniformLocation(p, "uBloomTex", 9);
			s_gl.post_pipe.uSsaoTex      = glGetUniformLocation(p, "uSsaoTex", 8);
			s_gl.post_pipe.uSsaoActive   = glGetUniformLocation(p, "uSsaoActive", 11);
			s_gl.post_pipe.uBloomActive  = glGetUniformLocation(p, "uBloomActive", 12);
			s_gl.post_pipe.uExposure     = glGetUniformLocation(p, "uExposure", 9);
			s_gl.post_pipe.uCamDist      = glGetUniformLocation(p, "uCamDist", 8);
			s_gl.post_pipe.uSceneUvScale = glGetUniformLocation(p, "uSceneUvScale", 13);
			s_gl.post_pipe.uPostUvScale  = glGetUniformLocation(p, "uPostUvScale", 12);
			s_gl.post_pipe.uSceneTexel   = glGetUniformLocation(p, "uSceneTexel", 11);
			s_gl.post_pipe.uFxaaActive   = glGetUniformLocation(p, "uFxaaActive", 11);
		}
	}

	s_gl.pipelines_ready = true;
	return true;
}

static GLuint gl_ensure_dummy_river_tex(void)
{
	if (!s_gl.dummy_river_tex) {
		static const float k_zero_rgba[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		s_gl.dummy_river_tex = glCreateTexture();
		glActiveTexture(GL_TEXTURE2);
		glBindTexture(GL_TEXTURE_2D, s_gl.dummy_river_tex);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, 1, 1, 0, GL_RGBA, GL_FLOAT, k_zero_rgba);
	}
	return s_gl.dummy_river_tex;
}

/* Force driver backend (Mesa/ANGLE GEN12) shader compilation incrementally (1 linked variant per
 * frame after the first rendered frame) into the exact RGBA16F scene_fbo / cloud_fbo formats
 * with a 1x1 viewport right before glClear(), eliminating both monolithic startup freezes
 * and multi-hundred-ms first-draw hitches as Tier B shaders finish compiling. */
static void gl_prewarm_pipelines(void)
{
	if (s_gl.pipelines_prewarmed || !s_gl.pipelines_ready || !s_gl.scene_ok || !s_gl.grid_vbo_macro)
		return;

	/* Let the very first ready frame render immediately before queuing or warming Tier B variants. */
	if (!s_gl.prewarm_frame0_seen) {
		s_gl.prewarm_frame0_seen = true;
		return;
	}
	if (!s_gl.tier_b_queued)
		init_tier_b_pipelines();

	/* Prewarm only the 9 active specialized terrain/water/cloud pipelines.
	 * Never force-draw the monolithic emergency fallback shaders (112-spill TERRAIN_FULL /
	 * 81-spill CLOUD_FULL) so neither boot nor zoom_sweep triggers a multi-second Mesa JIT stall. */
	static const uint8_t warm_order[9] = {
		ND_TERR_VAR_DRY_NEAR,
		ND_TERR_VAR_SEABED_NEAR,
		ND_TERR_VAR_COUNT, /* water */
		ND_TERR_VAR_MACRO,
		ND_TERR_VAR_MACRO_FAR,
		(uint8_t)(ND_TERR_VAR_COUNT + 1 + ND_CLOUD_VAR_WIDE_CSM),
		(uint8_t)(ND_TERR_VAR_COUNT + 1 + ND_CLOUD_VAR_WIDE),
		ND_TERR_VAR_DRY_CSM,
		ND_TERR_VAR_SEABED_MID,
	};
	const uint8_t total_steps = (uint8_t)(sizeof(warm_order) / sizeof(warm_order[0]));
	int pick = -1;
	bool any_unwarmed_unlinked = false;
	for (uint8_t o = 0; o < total_steps; o++) {
		uint8_t s = warm_order[o];
		if (s_gl.prewarmed_mask & (1u << s))
			continue;
		GLuint prog = 0;
		if (s < ND_TERR_VAR_COUNT)
			prog = s_gl.terrain_pipes[s].prog;
		else if (s == ND_TERR_VAR_COUNT)
			prog = s_gl.water_pipe.prog;
		else
			prog = s_gl.cloud_pipes[s - (ND_TERR_VAR_COUNT + 1)].prog;
		if (prog) {
			pick = (int)s;
			break;
		}
		any_unwarmed_unlinked = true;
	}

	if (pick < 0) {
		if (s_gl.tier_b_done || !any_unwarmed_unlinked) {
			s_gl.pipelines_prewarmed = true;
			if (s_gl.prewarm_dummy_rgba) {
				glDeleteTexture(s_gl.prewarm_dummy_rgba);
				s_gl.prewarm_dummy_rgba = 0;
			}
		}
		return;
	}

	uint8_t idx = (uint8_t)pick;
	gl_upload_frame_ubo();
	GLuint dummy_f16 = gl_ensure_dummy_river_tex();
	if (!s_gl.prewarm_dummy_rgba) {
		uint8_t dummy_c[4] = { 128, 128, 128, 255 };
		s_gl.prewarm_dummy_rgba = glCreateTexture();
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, s_gl.prewarm_dummy_rgba);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, dummy_c);
	}

	GLuint dummy_rgba = s_gl.prewarm_dummy_rgba;

	static const GLenum rgba_units[] = {
		GL_TEXTURE0 + 7, GL_TEXTURE0 + 9, GL_TEXTURE0 + 10, GL_TEXTURE0 + 11, GL_TEXTURE0 + 12
	};
	for (size_t i = 0; i < sizeof(rgba_units) / sizeof(rgba_units[0]); i++) {
		glActiveTexture(rgba_units[i]);
		glBindTexture(GL_TEXTURE_2D, dummy_rgba);
	}

	glBindBuffer(GL_ARRAY_BUFFER, s_gl.grid_vbo_macro);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s_gl.grid_ibo_macro);

	glViewport(0, 0, 1, 1);
	glDisable(GL_CULL_FACE);
	glDisable(GL_STENCIL_TEST);

	if (idx < ND_TERR_VAR_COUNT) {
		/* 1. Prewarm one terrain variant into scene_fbo (RGBA16F MSAA) */
		const gl_terrain_pipe_t *tp = &s_gl.terrain_pipes[idx];
		if (tp->prog) {
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, dummy_f16);
			glActiveTexture(GL_TEXTURE1);
			glBindTexture(GL_TEXTURE_2D, dummy_rgba);
			glActiveTexture(GL_TEXTURE2);
			glBindTexture(GL_TEXTURE_2D, dummy_f16);
			glActiveTexture(GL_TEXTURE0);
			glEnable(GL_DEPTH_TEST);
			glDepthFunc(GL_LEQUAL);
			glDepthMask(GL_TRUE);
			glDisable(GL_BLEND);
			glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
			glUseProgram(tp->prog);
			glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, 0);
		}
	} else if (idx == ND_TERR_VAR_COUNT) {
		/* 2. Prewarm water pipeline into scene_fbo (exact color-mask & depth-mask state) */
		if (s_gl.water_pipe.prog) {
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, dummy_f16);
			glActiveTexture(GL_TEXTURE1);
			glBindTexture(GL_TEXTURE_2D, dummy_rgba);
			glActiveTexture(GL_TEXTURE2);
			glBindTexture(GL_TEXTURE_2D, dummy_f16);
			glActiveTexture(GL_TEXTURE0 + 4);
			glBindTexture(GL_TEXTURE_2D, dummy_rgba);
			glActiveTexture(GL_TEXTURE0 + 5);
			glBindTexture(GL_TEXTURE_2D, dummy_rgba);
			glActiveTexture(GL_TEXTURE0);
			glEnable(GL_DEPTH_TEST);
			glDepthFunc(GL_LEQUAL);
			glDepthMask(GL_FALSE);
			glDisable(GL_BLEND);
			glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
			glUseProgram(s_gl.water_pipe.prog);
			if (s_gl.water_pipe.uViewport >= 0)     glUniform2f(s_gl.water_pipe.uViewport, 1.0f, 1.0f);
			if (s_gl.water_pipe.uSceneUvScale >= 0) glUniform2f(s_gl.water_pipe.uSceneUvScale, 1.0f, 1.0f);
			if (s_gl.water_pipe.uNearFar >= 0)      glUniform2f(s_gl.water_pipe.uNearFar, 2.0f, 10000.0f);
			if (s_gl.water_pipe.uProjScale >= 0)    glUniform2f(s_gl.water_pipe.uProjScale, 1.0f, 1.0f);
			glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, 0);
		}
	} else {
		/* 3. Prewarm one cloud variant into cloud_fbo (RGBA16F 1x, premult alpha blend) */
		int cv = (int)(idx - (ND_TERR_VAR_COUNT + 1));
		if (cv >= 0 && cv < ND_CLOUD_VAR_COUNT && s_gl.cloud_pipes[cv].prog) {
			const gl_cloud_pipe_t *cp = &s_gl.cloud_pipes[cv];
			if (s_gl.cloud_fbo)
				glBindFramebuffer(GL_FRAMEBUFFER, s_gl.cloud_fbo);
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, dummy_rgba);
			glActiveTexture(GL_TEXTURE1);
			glBindTexture(GL_TEXTURE_2D, dummy_f16);
			glActiveTexture(GL_TEXTURE0);
			glDisable(GL_DEPTH_TEST);
			glDepthMask(GL_FALSE);
			glEnable(GL_BLEND);
			glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
			glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
			glUseProgram(cp->prog);
			if (cp->uViewport >= 0)           glUniform2f(cp->uViewport, 1.0f, 1.0f);
			if (cp->uCloudResScale >= 0)      glUniform1f(cp->uCloudResScale, 1.0f);
			if (cp->uClusterGridDim >= 0)     glUniform3f(cp->uClusterGridDim, 16.0f, 16.0f, 24.0f);
			if (cp->uClusterDepthParams >= 0) glUniform4f(cp->uClusterDepthParams, 2.0f, 10000.0f, 1.0f, 0.0f);
			glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, 0);
		}
	}

	/* Restore clean GL state and rebind scene target before frame glClear */
	glDisable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glDepthMask(GL_TRUE);
	glEnable(GL_DEPTH_TEST);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	glActiveTexture(GL_TEXTURE2);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, 0);

	s_gl.prewarmed_mask |= (1u << idx);
	if (s_gl.tier_b_done) {
		uint32_t all_linked = 0u;
		for (uint8_t o = 0; o < total_steps; o++) {
			uint8_t s = warm_order[o];
			GLuint prog = (s < ND_TERR_VAR_COUNT) ? s_gl.terrain_pipes[s].prog
			            : ((s == ND_TERR_VAR_COUNT) ? s_gl.water_pipe.prog
			                                        : s_gl.cloud_pipes[s - (ND_TERR_VAR_COUNT + 1)].prog);
			if (prog)
				all_linked |= (1u << s);
		}
		if ((s_gl.prewarmed_mask & all_linked) == all_linked) {
			s_gl.pipelines_prewarmed = true;
			if (s_gl.prewarm_dummy_rgba) {
				glDeleteTexture(s_gl.prewarm_dummy_rgba);
				s_gl.prewarm_dummy_rgba = 0;
			}
		}
	}
	gl_bind_scene_target();
}

bool nd_seven_gl_pipelines_ready(void)
{
	if (!s_gl.initialized)
		init_pipelines();
	return check_pipelines_ready();
}

void nd_seven_gl_set_focal_water(float water_level)
{
	s_gl.focal_sea_level = water_level;
	if (water_level > -900.0f) {
		if (s_gl.cur_sea_level <= -900.0f || water_level > s_gl.cur_sea_level)
			s_gl.cur_sea_level = water_level;
	}
}

void nd_seven_gl_set_render_scales(float res_scale, float cloud_scale)
{
	if (res_scale < 0.25f) res_scale = 0.25f;
	if (res_scale > 1.0f) res_scale = 1.0f;
	if (cloud_scale < 0.125f) cloud_scale = 0.125f;
	if (cloud_scale > 1.0f) cloud_scale = 1.0f;
	s_gl.cur_res_scale = res_scale;
	s_gl.cur_cloud_scale = cloud_scale;
	s_gl.render_scales_set = true;
}

void nd_seven_gl_set_lamb_light(float wx, float wy, float wz, float intensity)
{
	float cl_int = (intensity > 0.0f) ? intensity : 0.0f;
	s_gl.cur_lamb_light[0] = wx;
	s_gl.cur_lamb_light[1] = wy;
	s_gl.cur_lamb_light[2] = wz;
	s_gl.cur_lamb_light[3] = cl_int;
	s_lamb_shadow_light[0] = wx;
	s_lamb_shadow_light[1] = wy;
	s_lamb_shadow_light[2] = wz;
	s_lamb_shadow_light[3] = cl_int;
	s_grass_lamb_pos.x = wx;
	s_grass_lamb_pos.y = wy;
	if (!s_view_wind_ready)
		gl_update_view_wind(0.0f, 0.0f, s_grass_anim_time);
	gl_end_terrain_pass();
	if (s_gl.frame_open && s_gl.frame_ubo)
		gl_upload_frame_ubo();
}

float nd_seven_gl_eval_lamb_light(float frag_wx, float frag_wy, float frag_wz,
                                  float lamb_wx, float lamb_wy, float lamb_wz,
                                  float intensity)
{
	if (intensity <= 0.001f)
		return 0.0f;
	float dx = lamb_wx - frag_wx;
	float dy = lamb_wy - frag_wy;
	float dz = (lamb_wz + 10.0f) - frag_wz;
	float d2 = dx * dx + dy * dy + dz * dz;
	float r_max2 = 220.0f * 220.0f;
	float r_min2 = 28.0f * 28.0f;
	float t = (d2 - r_max2) / (r_min2 - r_max2);
	if (t < 0.0f) t = 0.0f;
	if (t > 1.0f) t = 1.0f;
	float smooth_falloff = t * t * (3.0f - 2.0f * t);
	float atten = (1.0f / (1.0f + d2 * 0.00055f)) * smooth_falloff;
	return intensity * 0.24f * atten;
}


static void gl_render_sky(float space_t)
{
	(void)space_t;
	if (!s_gl.pipelines_ready || !s_gl.sky_pipe.prog)
		return;
	gl_end_terrain_pass();
	glTimerBegin(ND_GPU_PASS_SKY);
	/* Early-Z sky dome: vertex shader emits z = 1.0, so GL_LEQUAL rejects every pixel
	 * already covered by opaque terrain without executing the sky fragment shader. */
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glDepthMask(GL_FALSE);
	glDisable(GL_BLEND);
	glDisable(GL_CULL_FACE);
	glDisable(GL_STENCIL_TEST);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glUseProgram(s_gl.sky_pipe.prog);
	glUniform3fv(s_gl.sky_pipe.uCamFwd, 1, s_gl.cur_cam_fwd);
	glUniform3fv(s_gl.sky_pipe.uCamRight, 1, s_gl.cur_cam_right);
	glUniform3fv(s_gl.sky_pipe.uCamUp, 1, s_gl.cur_cam_up);
	glUniform2f(s_gl.sky_pipe.uTanHalfAspect, s_gl.cur_tan_half, s_gl.cur_aspect);
	glUniform3fv(s_gl.sky_pipe.uEast, 1, s_gl.cur_E);
	glUniform3fv(s_gl.sky_pipe.uSouth, 1, s_gl.cur_S);
	glUniform3fv(s_gl.sky_pipe.uZenith, 1, s_gl.cur_N);
	if (s_gl.sky_pipe.uTorusEye >= 0 && s_gl.sky_pipe.uTorusRadii >= 0 && s_gl.sky_pipe.uAtmoHaloVis >= 0) {
		float eye_t[3] = {
			(float)(s_gl.cur_origin[0] + (double)s_gl.cur_eye[0] - (double)s_gl.cur_pdef.center[0]),
			(float)(s_gl.cur_origin[1] + (double)s_gl.cur_eye[1] - (double)s_gl.cur_pdef.center[1]),
			(float)(s_gl.cur_origin[2] + (double)s_gl.cur_eye[2] - (double)s_gl.cur_pdef.center[2])
		};
		float halo_vis = fminf(1.0f, fmaxf(0.0f, (s_gl.cur_dist - 20000.0f) / 100000.0f));
		glUniform3fv(s_gl.sky_pipe.uTorusEye, 1, eye_t);
		glUniform2f(s_gl.sky_pipe.uTorusRadii, s_gl.cur_pdef.major_r, s_gl.cur_pdef.minor_r);
		glUniform1f(s_gl.sky_pipe.uAtmoHaloVis, (s_gl.cur_pdef.major_r > 1.0f && s_gl.cur_pdef.minor_r > 1.0f) ? halo_vis : 0.0f);
	}
	glDrawArrays(GL_TRIANGLES, 0, 3);
	glTimerEnd(ND_GPU_PASS_SKY);
	glDepthMask(GL_TRUE);
	s_gl.res_color_valid = false;
}

void nd_seven_gl_flush_sky(void)
{
	if (s_gl.sky_rendered || !s_gl.pipelines_ready)
		return;
	s_gl.sky_rendered = true;
	gl_render_sky(s_gl.cur_space_t);
}

void nd_seven_gl_begin_frame_ex(uint32_t stage_w, uint32_t stage_h,
                                float tx, float ty, float tz,
                                float dist, float yaw_deg, float pitch_deg, float fov_deg,
                                float anim_time,
                                const nd_planet_def_t *pdef)
{
	s_gl.frame_count++;
	gl_end_terrain_pass();
	gl_reset_billboard_frame_state();
	s_gl.terrain_pass_has_body = false;
	s_gl.terrain_pass_variant = -1;
	memset(s_gl.terrain_var_body_valid, 0, sizeof(s_gl.terrain_var_body_valid));
	memset(s_gl.terrain_var_sea_valid, 0, sizeof(s_gl.terrain_var_sea_valid));
	s_gl.terrain_full_wake_valid = false;
	s_gl.terrain_pass_vbo = 0;
	s_gl.water_count = 0;
	s_gl.cloud_count = 0;
	s_gl.grass_q_count = 0;
	s_gl.sprites_in_water = false;
	s_gl.terrain_split_water_pending = false;
	s_gl.sky_rendered = false;
	s_gl.res_depth_valid = false;
	s_gl.res_color_valid = false;
	s_gl.water_culled_occ2d = 0;
	s_gl.cloud_culled_hiz = 0;
	s_gl.billboard_culled_hiz = 0;
	s_gl.perf_terrain_chunks = 0;
	s_gl.perf_bb_flushes = 0;
	s_gl.perf_mesh3d_count = 0;
	s_gl.perf_bb_count = 0;
	s_gl.local_light_count = 0;
	s_gl.gpu_hiz_built = false;
	s_gl.indirect_cmd_count = 0;
	s_gl.cur_sea_level = pdef ? (float)nd_field_water_level(pdef->world_id) : -999.0f;
	s_gl.cur_base_sea_level = s_gl.cur_sea_level;
	s_gl.focal_sea_level = -999.0f;
	if (pdef) s_gl.cur_pdef = *pdef;
	nd_seven_gl_eval_celestial_dirs(anim_time, s_gl.cur_sun_dir, s_gl.cur_moon_dir);
	s_shadow_sun_dir[0] = s_gl.cur_sun_dir[0];
	s_shadow_sun_dir[1] = s_gl.cur_sun_dir[1];
	s_shadow_sun_dir[2] = s_gl.cur_sun_dir[2];
	s_gl.cur_dist = (dist > 1.0f) ? dist : ND_SEVEN_CAM_DIST;
	s_gl.cur_yaw_deg = yaw_deg;
	s_gl.cur_pitch_deg = pitch_deg;
	s_gl.cur_anim_time = anim_time;
	s_gl.cur_tx = tx;
	s_gl.cur_ty = ty;
	s_gl.cur_tz = tz;
	s_grass_anim_time = anim_time;
	s_grass_lamb_pos.x = tx;
	s_grass_lamb_pos.y = ty;
	gl_update_view_wind(tx, ty, anim_time);

	/* Evaluate double-precision reference origin at (tx, ty, 0) to eliminate 32-bit float cancellation */
	float E[3], S[3], N[3];
	torus_frame_double((double)tx, (double)ty, 0.0, pdef, s_gl.cur_origin, E, S, N);
	for (int k = 0; k < 3; k++) {
		s_gl.cur_E[k] = E[k];
		s_gl.cur_S[k] = S[k];
		s_gl.cur_N[k] = N[k];
	}

	double u0 = 6.28318530717958647692 * ((double)tx / (double)ND_SEVEN_WORLD_DIM_W);
	double v0 = 6.28318530717958647692 * ((double)ty / (double)ND_SEVEN_WORLD_DIM_H);
	s_gl.cur_ref_trig[0] = (float)cos(u0);
	s_gl.cur_ref_trig[1] = (float)sin(u0);
	s_gl.cur_ref_trig[2] = (float)cos(v0);
	s_gl.cur_ref_trig[3] = (float)sin(v0);
	s_bb_ref_origin = s_gl.cur_origin;
	s_bb_ref_pdef = &s_gl.cur_pdef;
	s_bb_tx = tx;
	s_bb_ty = ty;
	s_bb_dist = s_gl.cur_dist;
	s_bb_ref_trig[0] = s_gl.cur_ref_trig[0];
	s_bb_ref_trig[1] = s_gl.cur_ref_trig[1];
	s_bb_ref_trig[2] = s_gl.cur_ref_trig[2];
	s_bb_ref_trig[3] = s_gl.cur_ref_trig[3];

	float yaw_rad = yaw_deg * (PI_F / 180.0f);
	float cos_y = cosf(yaw_rad);
	float sin_y = sinf(yaw_rad);
	float pitch_rad = pitch_deg * (PI_F / 180.0f);
	float cos_p = cosf(pitch_rad);
	float sin_p = sinf(pitch_rad);
	float d = s_gl.cur_dist;

	s_gl.cur_cos_y = cos_y;
	s_gl.cur_sin_y = sin_y;
	s_gl.cur_cos_p = cos_p;
	s_gl.cur_sin_p = sin_p;

	float horiz[3] = {
		sin_y * E[0] + cos_y * S[0],
		sin_y * E[1] + cos_y * S[1],
		sin_y * E[2] + cos_y * S[2]
	};
	float right[3] = {
		cos_y * E[0] - sin_y * S[0],
		cos_y * E[1] - sin_y * S[1],
		cos_y * E[2] - sin_y * S[2]
	};

	s_gl.cur_target[0] = tz * N[0];
	s_gl.cur_target[1] = tz * N[1];
	s_gl.cur_target[2] = tz * N[2];

	float view_to_eye[3] = {
		cos_p * horiz[0] + sin_p * N[0],
		cos_p * horiz[1] + sin_p * N[1],
		cos_p * horiz[2] + sin_p * N[2]
	};

	s_gl.cur_eye[0] = s_gl.cur_target[0] + d * view_to_eye[0];
	s_gl.cur_eye[1] = s_gl.cur_target[1] + d * view_to_eye[1];
	s_gl.cur_eye[2] = s_gl.cur_target[2] + d * view_to_eye[2];

	s_gl.cur_up[0] = -sin_p * horiz[0] + cos_p * N[0];
	s_gl.cur_up[1] = -sin_p * horiz[1] + cos_p * N[1];
	s_gl.cur_up[2] = -sin_p * horiz[2] + cos_p * N[2];

	for (int k = 0; k < 3; k++) {
		s_gl.cur_cam_right[k] = right[k];
		s_gl.cur_cam_up[k]    = s_gl.cur_up[k];
		s_gl.cur_cam_fwd[k]   = -view_to_eye[k];
	}

	float modelview[16], proj[16];
	ar_mat4_lookat_lh(modelview, s_gl.cur_eye, s_gl.cur_target, s_gl.cur_up);
	float fov_rad = fov_deg * (PI_F / 180.0f);
	float aspect = (stage_h > 0) ? ((float)stage_w / (float)stage_h) : 1.333333f;
	float near_z = (d * 0.005f > 2.0f) ? (d * 0.005f) : 2.0f;
	float far_z = (d * 20.0f > 80000000.0f) ? (d * 20.0f) : 80000000.0f;
	ar_mat4_perspective(proj, fov_rad, aspect, near_z, far_z);

	s_gl.cur_near = near_z;
	s_gl.cur_far = far_z;
	s_gl.cur_tan_half = tanf(fov_rad * 0.5f);
	s_gl.cur_aspect = aspect;
	ar_mat4_multiply(s_gl.cur_mvp, proj, modelview);
	ar_frustum_from_matrix(&s_gl.cur_frustum, s_gl.cur_mvp);

	if (!s_gl.initialized) init_pipelines();
	if (check_pipelines_ready())
		poll_tier_b_pipelines();
	if (s_gl.pipelines_ready && s_gl.atlas_bake_stage < 3) gl_advance_atlas_bake();

	s_gl.stage_w = stage_w;
	s_gl.stage_h = stage_h;
	s_gl.frame_open = true;
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(0, 0, (GLsizei)stage_w, (GLsizei)stage_h);
	if (s_gl.pipelines_ready)
		gl_ensure_scene_targets(stage_w, stage_h);

	/* Compute active sub-rect dimensions inside persistent native-sized FBOs (zero reallocation on zoom) */
	float res_sc = s_gl.render_scales_set ? s_gl.cur_res_scale : 1.0f;
	uint32_t rw = (uint32_t)((float)stage_w * res_sc + 0.5f);
	uint32_t rh = (uint32_t)((float)stage_h * res_sc + 0.5f);
	if (rw < 1u) rw = 1u;
	if (rh < 1u) rh = 1u;
	if (s_gl.scene_w > 0 && rw > s_gl.scene_w) rw = s_gl.scene_w;
	if (s_gl.scene_h > 0 && rh > s_gl.scene_h) rh = s_gl.scene_h;
	s_gl.render_w = rw;
	s_gl.render_h = rh;

	uint32_t prw = (rw > 1u) ? (rw >> 1) : 1u;
	uint32_t prh = (rh > 1u) ? (rh >> 1) : 1u;
	if (s_gl.post_half_w > 0 && prw > s_gl.post_half_w) prw = s_gl.post_half_w;
	if (s_gl.post_half_h > 0 && prh > s_gl.post_half_h) prh = s_gl.post_half_h;
	s_gl.post_rw = prw;
	s_gl.post_rh = prh;

	if (s_gl.render_scales_set) {
		uint32_t crw = (uint32_t)((float)stage_w * s_gl.cur_cloud_scale + 0.5f);
		uint32_t crh = (uint32_t)((float)stage_h * s_gl.cur_cloud_scale + 0.5f);
		if (crw < 1u) crw = 1u;
		if (crh < 1u) crh = 1u;
		if (s_gl.cloud_w > 0 && crw > s_gl.cloud_w) crw = s_gl.cloud_w;
		if (s_gl.cloud_h > 0 && crh > s_gl.cloud_h) crh = s_gl.cloud_h;
		s_gl.cloud_rw = crw;
		s_gl.cloud_rh = crh;
	} else {
		s_gl.cloud_rw = s_gl.cloud_w;
		s_gl.cloud_rh = s_gl.cloud_h;
	}
	s_gl.render_scales_set = false;

	gl_bind_scene_target();
	if (s_gl.pipelines_ready && s_gl.scene_ok && !s_gl.pipelines_prewarmed)
		gl_prewarm_pipelines();
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glDepthMask(GL_TRUE);
	glDisable(GL_CULL_FACE);

	float space_t = fminf(1.0f, fmaxf(0.0f, (s_gl.cur_dist - 12000.0f) / 40000.0f));
	s_gl.cur_space_t = space_t;
	float sky_r = 0.55f * (1.0f - space_t) + 0.002f * space_t;
	float sky_g = 0.70f * (1.0f - space_t) + 0.003f * space_t;
	float sky_b = 0.85f * (1.0f - space_t) + 0.006f * space_t;

	glClearColor(sky_r, sky_g, sky_b, 1.0f);
	glClearStencil(0);
	glStencilMask(0xFF);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

	if (s_gl.pipelines_ready) {
		gl_render_cloud_shadow_map();
		gl_prepare_tree_shadow_frame(tx, ty, tz, E, S, N);
	} else {
		s_gl.csm_valid = false;
		s_gl.csm_clear_sky = false;
		s_gl.tree_shadow_active = 0.0f;
	}
	gl_upload_frame_ubo();
}

void nd_seven_gl_begin_frame(uint32_t stage_w, uint32_t stage_h,
                             float tx, float ty, float tz,
                             float dist, float pitch_deg, float fov_deg,
                             float anim_time,
                             const nd_planet_def_t *pdef)
{
	nd_seven_gl_begin_frame_ex(stage_w, stage_h, tx, ty, tz,
	                           dist, 0.0f, pitch_deg, fov_deg,
	                           anim_time, pdef);
}

static void select_grid_buffers(uint8_t level, int32_t ox, int32_t oy, int32_t span_w, int32_t span_h,
                                GLuint *out_vbo, GLuint *out_ibo, GLsizei *out_indices)
{
	/* Station-aligned terrain mesh LOD:
	 * - 48x48 HI: immediate L0 foreground and curved orbital chunks (L11+ or dist >= 200k)
	 * - 24x24 LO: near-field L1 hills (and close L2, exact 1:2 vertex station alignment with 48x48)
	 * - 16x16 MACRO: wide-zoom L2+ and distant horizon chunks (L3..L10, exact 1:3 station alignment) */
	(void)ox;
	(void)oy;
	(void)span_w;
	(void)span_h;
	if (level == 0u || level >= 11u || s_gl.cur_dist >= 200000.0f) {
		*out_vbo = s_gl.grid_vbo_hi;
		*out_ibo = s_gl.grid_ibo_hi;
		*out_indices = GL_TOTAL_INDICES_HI;
	} else if (level == 1u || (level == 2u && s_gl.cur_dist < 2200.0f)) {
		*out_vbo = s_gl.grid_vbo;
		*out_ibo = s_gl.grid_ibo;
		*out_indices = GL_TOTAL_INDICES;
	} else {
		*out_vbo = s_gl.grid_vbo_macro;
		*out_ibo = s_gl.grid_ibo_macro;
		*out_indices = GL_TOTAL_INDICES_MACRO;
	}
}

static inline uint32_t chunk_cache_hash(uint16_t world, uint8_t level,
                                        int32_t ox, int32_t oy,
                                        int32_t span_w, int32_t span_h)
{
	uint32_t h = ((uint32_t)world << 8) ^ (uint32_t)level;
	h ^= (uint32_t)ox * 0x9E3779B1u;
	h = (h ^ (h >> 16)) * 0x85EBCA6Bu;
	h ^= (uint32_t)oy * 0xC2B2AE35u;
	h = (h ^ (h >> 13)) * 0xC2B2AE35u;
	h ^= ((uint32_t)span_w * 0x27D4EB2Du) ^ ((uint32_t)span_h * 0x165667B1u);
	h ^= h >> 16;
	return h & (CHUNK_CACHE_HASH_SIZE - 1u);
}

static int chunk_hash_lookup(uint16_t world, uint8_t level,
                             int32_t ox, int32_t oy,
                             int32_t span_w, int32_t span_h)
{
	const uint32_t mask = CHUNK_CACHE_HASH_SIZE - 1u;
	uint32_t b = chunk_cache_hash(world, level, ox, oy, span_w, span_h);
	for (uint32_t step = 0; step < CHUNK_CACHE_HASH_SIZE; step++) {
		uint16_t v = s_gl.chunk_hash[b];
		if (v == 0) return -1;
		int idx = (int)v - 1;
		const gl_chunk_tex_t *c = &s_gl.chunk_cache[idx];
		if (c->active && c->world == world && c->level == level &&
		    c->ox == ox && c->oy == oy &&
		    c->span_w == span_w && c->span_h == span_h) {
			return idx;
		}
		b = (b + 1u) & mask;
	}
	return -1;
}

static void chunk_hash_insert(int idx)
{
	const uint32_t mask = CHUNK_CACHE_HASH_SIZE - 1u;
	const gl_chunk_tex_t *c = &s_gl.chunk_cache[idx];
	uint32_t b = chunk_cache_hash(c->world, c->level, c->ox, c->oy, c->span_w, c->span_h);
	for (uint32_t step = 0; step < CHUNK_CACHE_HASH_SIZE; step++) {
		if (s_gl.chunk_hash[b] == 0) {
			s_gl.chunk_hash[b] = (uint16_t)(idx + 1);
			return;
		}
		b = (b + 1u) & mask;
	}
}

static void chunk_hash_remove(int idx)
{
	const uint32_t mask = CHUNK_CACHE_HASH_SIZE - 1u;
	const gl_chunk_tex_t *c = &s_gl.chunk_cache[idx];
	uint16_t target = (uint16_t)(idx + 1);
	uint32_t b = chunk_cache_hash(c->world, c->level, c->ox, c->oy, c->span_w, c->span_h);
	for (uint32_t step = 0; step < CHUNK_CACHE_HASH_SIZE; step++) {
		uint16_t v = s_gl.chunk_hash[b];
		if (v == 0) return;
		if (v == target) break;
		b = (b + 1u) & mask;
	}
	if (s_gl.chunk_hash[b] != target) return;
	uint32_t hole = b;
	uint32_t cur = (hole + 1u) & mask;
	for (;;) {
		uint16_t v = s_gl.chunk_hash[cur];
		if (v == 0) {
			s_gl.chunk_hash[hole] = 0;
			return;
		}
		const gl_chunk_tex_t *e = &s_gl.chunk_cache[(int)v - 1];
		uint32_t home = chunk_cache_hash(e->world, e->level, e->ox, e->oy, e->span_w, e->span_h);
		if (((hole - home) & mask) < ((cur - home) & mask)) {
			s_gl.chunk_hash[hole] = v;
			hole = cur;
		}
		cur = (cur + 1u) & mask;
	}
}

static void gl_begin_terrain_pass(void)
{
	glTimerBegin(ND_GPU_PASS_TERRAIN);

	if (s_gl.terr_detail_tex) {
		glActiveTexture(GL_TEXTURE0 + 10);
		glBindTexture(GL_TEXTURE_2D, s_gl.terr_detail_tex);
	}
	if (s_gl.terr_norm_tex) {
		glActiveTexture(GL_TEXTURE0 + 11);
		glBindTexture(GL_TEXTURE_2D, s_gl.terr_norm_tex);
	}
	gl_bind_csm(-1);
	gl_bind_tree_shadow_read(-1);
	(void)gl_ensure_dummy_river_tex();
	glActiveTexture(GL_TEXTURE0);

	glEnable(GL_CULL_FACE);
	glEnable(GL_STENCIL_TEST);
	glStencilMask(0x08);
	glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
	glEnableVertexAttribArray(0);
	s_gl.terrain_pass_active = true;
	s_gl.terrain_pass_has_body = false;
	s_gl.terrain_pass_variant = -1;
	s_gl.terrain_pass_seabed_stencil = -1;
	memset(s_gl.terrain_var_body_valid, 0, sizeof(s_gl.terrain_var_body_valid));
	memset(s_gl.terrain_var_sea_valid, 0, sizeof(s_gl.terrain_var_sea_valid));
	s_gl.terrain_full_wake_valid = false;
	s_gl.terrain_pass_vbo = 0;
	s_gl.terrain_pass_river_tex = 0;
}

static gl_grass_vbo_slot_t *gl_grass_get_or_build_chunk(
	uint16_t world, uint8_t level, uint32_t rev,
	int32_t ox, int32_t oy, int32_t span_w, int32_t span_h,
	int32_t grid_n, float sea_level,
	const int16_t *height_i16, const uint32_t *color_rgba,
	uint32_t chunk_w, uint32_t chunk_h, bool *out_touched_vbo)
{
	if (grid_n < 1) grid_n = 1;
	if (grid_n > 68) grid_n = 68;
	int32_t sea_q = (sea_level > -900.0f) ? (int32_t)floorf(sea_level * 16.0f + 0.5f) : -16000;

	int free_idx = -1;
	int oldest_idx = 0;
	uint32_t oldest_touch = 0xFFFFFFFFu;
	for (int i = 0; i < MAX_GRASS_VBO_CACHE; i++) {
		gl_grass_vbo_slot_t *s = &s_gl.grass_cache[i];
		if (s->active &&
		    s->world == world && s->level == level && s->rev == rev &&
		    s->ox == ox && s->oy == oy &&
		    s->span_w == span_w && s->span_h == span_h &&
		    s->grid_n == grid_n && s->sea_level_q == sea_q) {
			s->last_touch = s_gl.frame_count;
			return s;
		}
		if (!s->active && free_idx < 0)
			free_idx = i;
		if (s->last_touch < oldest_touch) {
			oldest_touch = s->last_touch;
			oldest_idx = i;
		}
	}

	gl_grass_vbo_slot_t *slot = &s_gl.grass_cache[(free_idx >= 0) ? free_idx : oldest_idx];
	static gl_grass_tuft_t s_grass_build_scratch[MAX_GRASS_TUFTS_PER_CHUNK];
	uint32_t count = 0;

	if (height_i16 && color_rgba && chunk_w > 0 && chunk_h > 0) {
		float max_cx = (chunk_w > 1u) ? (float)(chunk_w - 1u) : 1.0f;
		float max_cy = (chunk_h > 1u) ? (float)(chunk_h - 1u) : 1.0f;
		int32_t ic_max_x = (int32_t)(chunk_w - 1u);
		int32_t ic_max_y = (int32_t)(chunk_h - 1u);
		float tex_to_world_x = max_cx / ((span_w > 1) ? (float)span_w : 1.0f);
		float tex_to_world_y = max_cy / ((span_h > 1) ? (float)span_h : 1.0f);
		float inv_gn = 1.0f / (float)grid_n;
		int32_t oxI = (int32_t)floorf((float)ox * 0.25f);
		int32_t oyI = (int32_t)floorf((float)oy * 0.25f);

		for (int iy = 0; iy < grid_n; iy++) {
			for (int ix = 0; ix < grid_n; ix++) {
				uint32_t h0 = gl_chaff_hash3(ix + oxI * 17, iy + oyI * 29, oxI ^ (oyI * 31), 0x47524153u);
				uint32_t h1 = gl_chaff_hash3(ix * 3 + oxI * 13, iy * 5 + oyI * 19, (int32_t)(h0 & 0xFFFFu), 0x434C4F56u);
				float jx = 0.08f + 0.84f * ((float)(h0 & 0xFFFFu) * (1.0f / 65535.0f));
				float jy = 0.08f + 0.84f * ((float)((h0 >> 16u) & 0xFFFFu) * (1.0f / 65535.0f));
				float rYaw  = 6.28318530718f * ((float)(h1 & 0xFFFFu) * (1.0f / 65535.0f));
				float rSize = (float)((h1 >> 16u) & 0xFFu) * (1.0f / 255.0f);
				float rHue  = (float)((h1 >> 24u) & 0xFFu) * (1.0f / 255.0f);

				float uvx = ((float)ix + jx) * inv_gn;
				float uvy = ((float)iy + jy) * inv_gn;
				if (uvx < 0.002f) uvx = 0.002f; else if (uvx > 0.998f) uvx = 0.998f;
				if (uvy < 0.002f) uvy = 0.002f; else if (uvy > 0.998f) uvy = 0.998f;

				float px = uvx * max_cx;
				float py = uvy * max_cy;
				int32_t i0x = (int32_t)floorf(px);
				int32_t i0y = (int32_t)floorf(py);
				if (i0x < 0) i0x = 0; else if (i0x > ic_max_x) i0x = ic_max_x;
				if (i0y < 0) i0y = 0; else if (i0y > ic_max_y) i0y = ic_max_y;
				int32_t i1x = (i0x < ic_max_x) ? (i0x + 1) : ic_max_x;
				int32_t i1y = (i0y < ic_max_y) ? (i0y + 1) : ic_max_y;
				float fx = px - (float)i0x;
				float fy = py - (float)i0y;
				if (fx < 0.0f) fx = 0.0f; else if (fx > 1.0f) fx = 1.0f;
				if (fy < 0.0f) fy = 0.0f; else if (fy > 1.0f) fy = 1.0f;

				size_t row0 = (size_t)i0y * (size_t)chunk_w;
				size_t row1 = (size_t)i1y * (size_t)chunk_w;
				float h00 = (float)height_i16[row0 + (size_t)i0x];
				float h10 = (float)height_i16[row0 + (size_t)i1x];
				float h01 = (float)height_i16[row1 + (size_t)i0x];
				float h11 = (float)height_i16[row1 + (size_t)i1x];
				float hm0 = h00 + (h10 - h00) * fx;
				float hm1 = h01 + (h11 - h01) * fx;
				float wz  = (hm0 + (hm1 - hm0) * fy) * 0.0625f;

				float shoreEnv = (sea_level > -900.0f) ? gl_smoothstep01(1.3f, 4.4f, wz - sea_level) : 1.0f;
				if (shoreEnv <= 0.015f)
					continue;

				float gx = ((h10 - h00) + ((h11 - h01) - (h10 - h00)) * fy) * tex_to_world_x * 0.0625f;
				float gy = ((h01 - h00) + ((h11 - h10) - (h01 - h00)) * fx) * tex_to_world_y * 0.0625f;
				float slopeMag = sqrtf(gx * gx + gy * gy);
				float slopeEnv = 1.0f - gl_smoothstep01(0.24f, 0.46f, slopeMag);
				if (shoreEnv * slopeEnv <= 0.015f)
					continue;

				uint32_t c00 = color_rgba[row0 + (size_t)i0x];
				uint32_t c10 = color_rgba[row0 + (size_t)i1x];
				uint32_t c01 = color_rgba[row1 + (size_t)i0x];
				uint32_t c11 = color_rgba[row1 + (size_t)i1x];
				float w00 = (1.0f - fx) * (1.0f - fy);
				float w10 = fx * (1.0f - fy);
				float w01 = (1.0f - fx) * fy;
				float w11 = fx * fy;
				float sr = ((float)(c00 & 0xFFu) * w00 + (float)(c10 & 0xFFu) * w10 +
				            (float)(c01 & 0xFFu) * w01 + (float)(c11 & 0xFFu) * w11) * (1.0f / 255.0f);
				float sg = ((float)((c00 >> 8u) & 0xFFu) * w00 + (float)((c10 >> 8u) & 0xFFu) * w10 +
				            (float)((c01 >> 8u) & 0xFFu) * w01 + (float)((c11 >> 8u) & 0xFFu) * w11) * (1.0f / 255.0f);
				float sb = ((float)((c00 >> 16u) & 0xFFu) * w00 + (float)((c10 >> 16u) & 0xFFu) * w10 +
				            (float)((c01 >> 16u) & 0xFFu) * w01 + (float)((c11 >> 16u) & 0xFFu) * w11) * (1.0f / 255.0f);
				float sa = ((float)((c00 >> 24u) & 0xFFu) * w00 + (float)((c10 >> 24u) & 0xFFu) * w10 +
				            (float)((c01 >> 24u) & 0xFFu) * w01 + (float)((c11 >> 24u) & 0xFFu) * w11) * (1.0f / 255.0f);

				float riverCarve = (1.0f - sa) * 15.9375f;
				float riverEnv = 1.0f - gl_smoothstep01(0.08f, 0.35f, riverCarve);
				if (shoreEnv * slopeEnv * riverEnv <= 0.015f)
					continue;

				float maxRG = (sr > sg) ? sr : sg;
				float maxBio = (maxRG > sb) ? maxRG : sb;
				float minRG = (sr < sg) ? sr : sg;
				float minBio = (minRG < sb) ? minRG : sb;
				float wVolc   = 1.0f - gl_smoothstep01(0.18f, 0.32f, maxBio);
				float wPolar  = gl_smoothstep01(0.65f, 0.85f, minBio) * (1.0f - gl_smoothstep01(0.04f, 0.14f, maxRG - sb));
				float wDesert = gl_smoothstep01(0.62f, 0.72f, sr) * gl_smoothstep01(0.58f, 0.68f, sg) *
				                gl_smoothstep01(0.42f, 0.50f, sb) * (1.0f - wPolar);
				float wAlpine = gl_smoothstep01(0.44f, 0.62f, minBio) *
				                (1.0f - gl_smoothstep01(0.03f, 0.09f, maxBio - minBio)) * (1.0f - wPolar);
				float bioDensity = 1.0f - wVolc * 0.99f - wPolar * 0.96f - wDesert * 0.95f - wAlpine * 0.55f;
				if (bioDensity < 0.0f) bioDensity = 0.0f; else if (bioDensity > 1.0f) bioDensity = 1.0f;
				if (rSize > bioDensity * 1.10f)
					continue;

				float wx = (float)ox + uvx * (float)span_w;
				float wy = (float)oy + uvy * (float)span_h;
				float patchN = 0.0f, pgx = 0.0f, pgy = 0.0f;
				gl_chaff_grad_noise2d(wx * 0.024f, wy * 0.024f, 0x47525041u, &patchN, &pgx, &pgy);
				if (rSize > bioDensity * (0.68f + 0.42f * patchN))
					continue;

				float trailEnv = 1.0f - gl_smoothstep01(0.18f, 0.62f, gl_eval_trail_weight(wx, wy, slopeMag));
				float staticEnv = shoreEnv * riverEnv * slopeEnv * trailEnv;
				if (staticEnv <= 0.015f)
					continue;

				float lushN = 0.0f, lgx = 0.0f, lgy = 0.0f;
				gl_chaff_grad_noise2d(wx * 0.0085f, wy * 0.0085f, 0x4C555348u, &lushN, &lgx, &lgy);
				float lp_mix = lushN * 0.65f + patchN * 0.35f;
				if (lp_mix < 0.0f) lp_mix = 0.0f; else if (lp_mix > 1.0f) lp_mix = 1.0f;
				float lushBoost = (0.82f + (1.38f - 0.82f) * lp_mix) * (1.0f - wAlpine * 0.42f);
				float tierH;
				if (rHue > 0.68f)
					tierH = 18.0f + (31.0f - 18.0f) * ((rHue - 0.68f) / 0.32f);
				else if (rHue < 0.28f)
					tierH = 8.0f + (14.0f - 8.0f) * (rHue / 0.28f);
				else
					tierH = 13.0f + (21.0f - 13.0f) * ((rHue - 0.28f) / 0.40f);
				float tuftHBase = tierH * lushBoost;
				float tuftWBase = (4.8f + 4.0f * rSize) * ((rHue < 0.28f) ? 1.25f : 1.0f);

				float nInv = 1.0f / sqrtf(gx * gx + gy * gy + 1.0f);
				float ntx = -gx * nInv;
				float nty = -gy * nInv;
				float ntz =  1.0f * nInv;

				gl_grass_tuft_t *t = &s_grass_build_scratch[count++];
				t->tuft0[0] = uvx;
				t->tuft0[1] = uvy;
				t->tuft0[2] = wz;
				t->tuft0[3] = rYaw;
				t->tuft1[0] = tuftWBase;
				t->tuft1[1] = tuftHBase;
				t->tuft1[2] = staticEnv;
				t->tuft1[3] = rHue;
				t->sub_rgba[0] = (uint8_t)(sr * 255.0f + 0.5f);
				t->sub_rgba[1] = (uint8_t)(sg * 255.0f + 0.5f);
				t->sub_rgba[2] = (uint8_t)(sb * 255.0f + 0.5f);
				t->sub_rgba[3] = 255u;
				t->nterr_rgba[0] = (uint8_t)((ntx * 0.5f + 0.5f) * 255.0f + 0.5f);
				t->nterr_rgba[1] = (uint8_t)((nty * 0.5f + 0.5f) * 255.0f + 0.5f);
				t->nterr_rgba[2] = (uint8_t)((ntz * 0.5f + 0.5f) * 255.0f + 0.5f);
				t->nterr_rgba[3] = 255u;
			}
		}
	}

	if (count > 0) {
		gl_flush_cmd_ring();
		if (!slot->vbo)
			slot->vbo = glCreateBuffer();
		glBindBuffer(GL_ARRAY_BUFFER, slot->vbo);
		if (out_touched_vbo)
			*out_touched_vbo = true;
		if (!slot->vbo_allocated) {
			glBufferData(GL_ARRAY_BUFFER, NULL,
			             (GLsizei)(MAX_GRASS_TUFTS_PER_CHUNK * sizeof(gl_grass_tuft_t)),
			             GL_STATIC_DRAW);
			slot->vbo_allocated = true;
		}
		glBufferSubData(GL_ARRAY_BUFFER, 0,
		                (GLsizei)(count * sizeof(gl_grass_tuft_t)),
		                s_grass_build_scratch);
	}

	slot->active = 1;
	slot->world = world;
	slot->level = level;
	slot->rev = rev;
	slot->ox = ox;
	slot->oy = oy;
	slot->span_w = span_w;
	slot->span_h = span_h;
	slot->grid_n = grid_n;
	slot->sea_level_q = sea_q;
	slot->tuft_count = count;
	slot->last_touch = s_gl.frame_count;
	return slot;
}

void nd_seven_gl_flush_terrain_chunk(uint16_t world, uint8_t level, uint32_t rev,
                                     int32_t ox, int32_t oy, int32_t span_w, int32_t span_h,
                                     const uint32_t *color_rgba, const int16_t *height_i16,
                                     uint32_t chunk_w, uint32_t chunk_h,
                                     uint32_t stage_w, uint32_t stage_h,
                                     float sea_level,
                                     const nd_planet_def_t *pdef)
{
	(void)stage_w; (void)stage_h;
	if (!s_gl.initialized || !s_gl.pipelines_ready) return;
	if (!s_gl.terrain_pipes[ND_TERR_VAR_DRY_NEAR].prog &&
	    !s_gl.terrain_pipes[ND_TERR_VAR_DRY_CSM].prog &&
	    !s_gl.terrain_pipes[ND_TERR_VAR_SEABED_MID].prog &&
	    !s_gl.terrain_pipes[ND_TERR_VAR_FULL].prog) return;
	s_gl.perf_terrain_chunks++;
	const nd_planet_def_t *eff_pdef = pdef ? pdef : &s_gl.cur_pdef;

	float terrain_sea_lvl = sea_level;
	if (world == s_gl.cur_pdef.world_id && terrain_sea_lvl > -900.0f) {
		if (s_gl.focal_sea_level > -900.0f) {
			s_gl.cur_sea_level = s_gl.focal_sea_level;
		} else {
			bool contains_focal = (s_gl.cur_tx >= (float)ox && s_gl.cur_tx <= (float)(ox + span_w) &&
			                       s_gl.cur_ty >= (float)oy && s_gl.cur_ty <= (float)(oy + span_h));
			if (contains_focal && level == 0) {
				s_gl.cur_sea_level = terrain_sea_lvl;
			} else if (s_gl.cur_sea_level <= -900.0f) {
				s_gl.cur_sea_level = terrain_sea_lvl;
			}
		}
	}

	/* O(1) chunk texture lookup in open-addressed hash table; LRU eviction only on miss */
	int slot_idx = chunk_hash_lookup(world, level, ox, oy, span_w, span_h);
	bool was_miss = (slot_idx < 0);
	if (was_miss) {
		if (s_gl.chunk_cache_used < MAX_CHUNK_TEXTURES) {
			slot_idx = (int)(s_gl.chunk_cache_used++);
		} else {
			int oldest_idx = 0;
			uint32_t oldest_touch = 0xFFFFFFFFu;
			for (int i = 0; i < MAX_CHUNK_TEXTURES; i++) {
				if (s_gl.chunk_cache[i].last_touch < oldest_touch) {
					oldest_touch = s_gl.chunk_cache[i].last_touch;
					oldest_idx = i;
				}
			}
			if (s_gl.chunk_cache[oldest_idx].active)
				chunk_hash_remove(oldest_idx);
			slot_idx = oldest_idx;
		}
	}
	gl_chunk_tex_t *slot = &s_gl.chunk_cache[slot_idx];

	bool needs_upload = (was_miss || !slot->active || slot->rev != rev ||
	                     slot->tex_w != chunk_w || slot->tex_h != chunk_h);
	bool needs_river_only = (!needs_upload && slot->has_river && span_w <= 40960 &&
	                         terrain_sea_lvl > -900.0f &&
	                         (!slot->river_tex || fabsf(terrain_sea_lvl - slot->river_sea_level) > 0.01f));

	if (!slot->height_tex || !slot->substrate_tex || needs_upload || needs_river_only)
		gl_flush_cmd_ring();

	if (!slot->height_tex) {
		glActiveTexture(GL_TEXTURE0);
		slot->height_tex = glCreateTexture();
		glBindTexture(GL_TEXTURE_2D, slot->height_tex);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	}
	if (!slot->substrate_tex) {
		glActiveTexture(GL_TEXTURE1);
		slot->substrate_tex = glCreateTexture();
		glBindTexture(GL_TEXTURE_2D, slot->substrate_tex);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 4);
	}

	static float s_chunk_bake_rgba[128 * 128 * 4];
	if (needs_upload) {
		slot->active = 1;
		slot->world = world;
		slot->level = level;
		slot->rev = rev;
		slot->ox = ox;
		slot->oy = oy;
		slot->span_w = span_w;
		slot->span_h = span_h;
		slot->tex_w = chunk_w;
		slot->tex_h = chunk_h;
		if (was_miss)
			chunk_hash_insert(slot_idx);

		/* Record conservative per-chunk elevation bounds and river presence once per upload
		 * so zero-visual-diff terrain shader variants can skip dead water/swash/macro branches. */
		if (height_i16 && chunk_w > 0 && chunk_h > 0) {
			size_t n_px = (size_t)chunk_w * (size_t)chunk_h;
			int16_t mn = height_i16[0];
			int16_t mx = height_i16[0];
			for (size_t i = 1; i < n_px; i++) {
				int16_t h = height_i16[i];
				if (h < mn) mn = h;
				if (h > mx) mx = h;
			}
			slot->min_h_q12_4 = mn;
			slot->max_h_q12_4 = mx;
		} else {
			slot->min_h_q12_4 = -32768;
			slot->max_h_q12_4 = 32767;
		}
		if (color_rgba && chunk_w > 0 && chunk_h > 0) {
			size_t n_px = (size_t)chunk_w * (size_t)chunk_h;
			const uint8_t *rgba_b = (const uint8_t *)color_rgba;
			uint8_t hr = 0;
			for (size_t i = 0; i < n_px; i++) {
				if (rgba_b[i * 4u + 3u] < 254u) {
					hr = 1;
					break;
				}
			}
			slot->has_river = hr;
		} else {
			slot->has_river = 1;
		}

		bool can_sub = (slot->alloc_w == chunk_w && slot->alloc_h == chunk_h &&
		                height_i16 != NULL && color_rgba != NULL);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, slot->height_tex);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
		if (chunk_w <= 128u && chunk_h <= 128u) {
			nd_seven_gl_bake_chunk_height_derived(height_i16, chunk_w, chunk_h,
			                                      (float)span_w, (float)span_h,
			                                      s_chunk_bake_rgba);
			if (can_sub)
				glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, chunk_w, chunk_h,
				                GL_RGBA, GL_FLOAT, s_chunk_bake_rgba);
			else
				glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, chunk_w, chunk_h, 0,
				             GL_RGBA, GL_FLOAT, s_chunk_bake_rgba);
		}

		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, slot->substrate_tex);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
		if (can_sub)
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, chunk_w, chunk_h, GL_RGBA, GL_UNSIGNED_BYTE, color_rgba);
		else
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, chunk_w, chunk_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, color_rgba);
		{
			/* Build full CPU mipmap chain (box 2x2 downsample) for smooth orbital/oblique filtering */
			static uint8_t s_mip_a[128 * 128 * 4];
			static uint8_t s_mip_b[128 * 128 * 4];
			const uint8_t *src_px = (const uint8_t *)color_rgba;
			uint32_t mw = chunk_w;
			uint32_t mh = chunk_h;
			int mip = 0;
			if (mw <= 256u && mh <= 256u && src_px != NULL) {
				while (mw > 1u || mh > 1u) {
					uint32_t nw = (mw > 1u) ? (mw >> 1) : 1u;
					uint32_t nh = (mh > 1u) ? (mh >> 1) : 1u;
					uint8_t *dst_px = (mip & 1) ? s_mip_b : s_mip_a;
					for (uint32_t y = 0; y < nh; y++) {
						uint32_t sy0 = (y * 2u < mh) ? (y * 2u) : (mh - 1u);
						uint32_t sy1 = (sy0 + 1u < mh) ? (sy0 + 1u) : (mh - 1u);
						for (uint32_t x = 0; x < nw; x++) {
							uint32_t sx0 = (x * 2u < mw) ? (x * 2u) : (mw - 1u);
							uint32_t sx1 = (sx0 + 1u < mw) ? (sx0 + 1u) : (mw - 1u);
							const uint8_t *p00 = src_px + ((size_t)sy0 * mw + sx0) * 4u;
							const uint8_t *p10 = src_px + ((size_t)sy0 * mw + sx1) * 4u;
							const uint8_t *p01 = src_px + ((size_t)sy1 * mw + sx0) * 4u;
							const uint8_t *p11 = src_px + ((size_t)sy1 * mw + sx1) * 4u;
							uint8_t *out = dst_px + ((size_t)y * nw + x) * 4u;
							out[0] = (uint8_t)((p00[0] + p10[0] + p01[0] + p11[0] + 2u) >> 2);
							out[1] = (uint8_t)((p00[1] + p10[1] + p01[1] + p11[1] + 2u) >> 2);
							out[2] = (uint8_t)((p00[2] + p10[2] + p01[2] + p11[2] + 2u) >> 2);
							out[3] = (uint8_t)((p00[3] + p10[3] + p01[3] + p11[3] + 2u) >> 2);
						}
					}
					mip++;
					if (can_sub && mip <= slot->alloc_mips)
						glTexSubImage2D(GL_TEXTURE_2D, mip, 0, 0, (GLsizei)nw, (GLsizei)nh, GL_RGBA, GL_UNSIGNED_BYTE, dst_px);
					else
						glTexImage2D(GL_TEXTURE_2D, mip, GL_RGBA8, (GLsizei)nw, (GLsizei)nh, 0, GL_RGBA, GL_UNSIGNED_BYTE, dst_px);
					src_px = dst_px;
					mw = nw;
					mh = nh;
				}
			}
			if (!can_sub || mip != slot->alloc_mips) {
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, mip);
				slot->alloc_w = chunk_w;
				slot->alloc_h = chunk_h;
				slot->alloc_mips = mip;
			}
		}
	}
	if ((needs_upload || needs_river_only) && slot->has_river && span_w <= 40960 &&
	    terrain_sea_lvl > -900.0f && chunk_w <= 128u && chunk_h <= 128u) {
		glActiveTexture(GL_TEXTURE2);
		if (!slot->river_tex) {
			slot->river_tex = glCreateTexture();
			glBindTexture(GL_TEXTURE_2D, slot->river_tex);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		} else {
			glBindTexture(GL_TEXTURE_2D, slot->river_tex);
		}
		nd_seven_gl_bake_chunk_river(height_i16, color_rgba, chunk_w, chunk_h,
		                             terrain_sea_lvl, s_chunk_bake_rgba);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
		if (slot->river_alloc_w == chunk_w && slot->river_alloc_h == chunk_h) {
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, chunk_w, chunk_h,
			                GL_RGBA, GL_FLOAT, s_chunk_bake_rgba);
		} else {
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, chunk_w, chunk_h, 0,
			             GL_RGBA, GL_FLOAT, s_chunk_bake_rgba);
			slot->river_alloc_w = chunk_w;
			slot->river_alloc_h = chunk_h;
		}
		slot->river_sea_level = terrain_sea_lvl;
		s_gl.terrain_pass_river_tex = slot->river_tex;
	}
	slot->last_touch = s_gl.frame_count;

	if (!s_gl.terrain_pass_active)
		gl_begin_terrain_pass();

	/* Select the leanest zero-visual-diff compiled terrain fragment shader for this chunk */
	float min_z = (float)slot->min_h_q12_4 * 0.0625f;
	float max_z = (float)slot->max_h_q12_4 * 0.0625f;
	double cpos[3];
	torus_frame_double((double)ox + 0.5 * (double)span_w,
	                   (double)oy + 0.5 * (double)span_h,
	                   0.5 * (double)(min_z + max_z),
	                   eff_pdef, cpos, NULL, NULL, NULL);
	float cdx = (float)(cpos[0] - s_gl.cur_origin[0]) - s_gl.cur_eye[0];
	float cdy = (float)(cpos[1] - s_gl.cur_origin[1]) - s_gl.cur_eye[1];
	float cdz = (float)(cpos[2] - s_gl.cur_origin[2]) - s_gl.cur_eye[2];
	float d_ctr = sqrtf(cdx * cdx + cdy * cdy + cdz * cdz);
	float r_chunk = 0.5f * sqrtf((float)span_w * (float)span_w + (float)span_h * (float)span_h)
	                + 0.5f * (max_z - min_z) + 32.0f;
	float min_d = fmaxf(0.0f, d_ctr - r_chunk);
	float max_d = d_ctr + r_chunk;

	bool is_dry = (!slot->has_river && (terrain_sea_lvl <= -900.0f || min_z > terrain_sea_lvl + 16.0f));
	bool is_near = (level == 0 && span_w <= 240 && max_d < 1040.0f);
	bool is_macro = (level >= 2 && min_d > 3860.0f);
	bool csm_safe;
	if (!s_gl.csm_valid) {
		csm_safe = (s_gl.csm_clear_sky && max_d < 180000.0f);
	} else {
		float max_dz = fmaxf(0.0f, 920.0f - min_z) + fminf(14000.0f, max_d * 0.045f);
		float rx = fmaxf(fabsf((float)ox - s_gl.csm_win.cx),
		                 fabsf((float)(ox + span_w) - s_gl.csm_win.cx)) + max_dz;
		float ry = fmaxf(fabsf((float)oy - s_gl.csm_win.cy),
		                 fabsf((float)(oy + span_h) - s_gl.csm_win.cy)) + max_dz;
		csm_safe = (max_d <= 140000.0f &&
		            rx <= 0.90f * s_gl.csm_win.half &&
		            ry <= 0.90f * s_gl.csm_win.half);
	}

	uint32_t max_water_q = (uint32_t)nd_seven_gl_max_water_queue();
	bool water_eligible = (!is_dry && terrain_sea_lvl > -900.0f && s_gl.water_count < max_water_q);
	bool water_occluded = false;
	if (water_eligible && nd_seven_is_water_occluded_at(ox, oy, span_w, span_h, terrain_sea_lvl, eff_pdef)) {
		water_occluded = true;
		s_gl.water_culled_occ2d++;
	}
	bool split_water_range = (s_gl.scene_ok && s_gl.water_pipe.prog && s_gl.cur_dist <= 5500.0f);
	bool can_split_water = (water_eligible && !water_occluded && split_water_range);
	/* Occluded-water chunks do not need inline water rendering either — shade with SEABED_NEAR /
	 * SEABED_MID (without setting seabed stencil 0x08 or queueing a water quad) so they never fall
	 * back to the 112-spill monolithic ND_TERR_VAR_FULL shader. */
	bool use_seabed_shader = (can_split_water || (water_eligible && water_occluded && split_water_range));

	int want_var = ND_TERR_VAR_FULL;
	if (is_dry && is_near && csm_safe && s_gl.terrain_pipes[ND_TERR_VAR_DRY_NEAR].prog)
		want_var = ND_TERR_VAR_DRY_NEAR;
	else if (is_macro && csm_safe && s_gl.terrain_pipes[ND_TERR_VAR_MACRO].prog)
		want_var = ND_TERR_VAR_MACRO;
	else if (is_macro && s_gl.terrain_pipes[ND_TERR_VAR_MACRO_FAR].prog)
		want_var = ND_TERR_VAR_MACRO_FAR;
	else if (is_dry && csm_safe && s_gl.terrain_pipes[ND_TERR_VAR_DRY_CSM].prog)
		want_var = ND_TERR_VAR_DRY_CSM;
	else if (use_seabed_shader && is_near && csm_safe && s_gl.terrain_pipes[ND_TERR_VAR_SEABED_NEAR].prog) {
		want_var = ND_TERR_VAR_SEABED_NEAR;
		if (can_split_water)
			s_gl.terrain_split_water_pending = true;
	} else if (use_seabed_shader && csm_safe && s_gl.terrain_pipes[ND_TERR_VAR_SEABED_MID].prog) {
		want_var = ND_TERR_VAR_SEABED_MID;
		if (can_split_water)
			s_gl.terrain_split_water_pending = true;
	}

	/* Graceful Tier A fallback while Tier B FULL / MACRO / MACRO_FAR finish compiling in background */
	if (!s_gl.terrain_pipes[want_var].prog) {
		if (use_seabed_shader && s_gl.terrain_pipes[ND_TERR_VAR_SEABED_MID].prog) {
			want_var = ND_TERR_VAR_SEABED_MID;
			if (can_split_water)
				s_gl.terrain_split_water_pending = true;
		} else if (use_seabed_shader && s_gl.terrain_pipes[ND_TERR_VAR_SEABED_NEAR].prog) {
			want_var = ND_TERR_VAR_SEABED_NEAR;
			if (can_split_water)
				s_gl.terrain_split_water_pending = true;
		} else if (s_gl.terrain_pipes[ND_TERR_VAR_DRY_CSM].prog) {
			want_var = ND_TERR_VAR_DRY_CSM;
		} else if (s_gl.terrain_pipes[ND_TERR_VAR_DRY_NEAR].prog) {
			want_var = ND_TERR_VAR_DRY_NEAR;
		} else if (s_gl.terrain_pipes[ND_TERR_VAR_SEABED_MID].prog) {
			want_var = ND_TERR_VAR_SEABED_MID;
			if (can_split_water)
				s_gl.terrain_split_water_pending = true;
		} else {
			return;
		}
	}

	/* Write stencil bit 0x08 on wet-candidate (SEABED_*) chunks and clear it on dry/occluded/macro chunks
	 * so hardware early-stencil rejects non-water pixels in nd_seven_gl_flush_water(). */
	int want_seabed_st = (can_split_water &&
	                      (want_var == ND_TERR_VAR_SEABED_NEAR || want_var == ND_TERR_VAR_SEABED_MID)) ? 1 : 0;
	if (want_seabed_st != s_gl.terrain_pass_seabed_stencil) {
		gl_flush_cmd_ring();
		glStencilFunc(GL_ALWAYS, want_seabed_st ? 0x08 : 0x00, 0x08);
		s_gl.terrain_pass_seabed_stencil = want_seabed_st;
	}

	const gl_terrain_pipe_t *tp = &s_gl.terrain_pipes[want_var];
	if (want_var != s_gl.terrain_pass_variant) {
		lam_gl_cmd_use_program(&s_gl.cmd_buf, tp->prog);
		s_gl.terrain_pass_variant = want_var;
	}

	if (want_var == ND_TERR_VAR_FULL && !s_gl.terrain_full_wake_valid) {
		float wake_m[16];
		lam_gl_cmd_uniform4f(&s_gl.cmd_buf, tp->uLambWakeHead,
		                     s_gl.cur_wake_head[0], s_gl.cur_wake_head[1],
		                     s_gl.cur_wake_head[2], s_gl.cur_wake_head[3]);
		memcpy(wake_m, &s_gl.cur_wake_xyt[0], sizeof(wake_m));
		lam_gl_cmd_uniform_mat4(&s_gl.cmd_buf, tp->uLambWakeXYT0, GL_FALSE, wake_m);
		memcpy(wake_m, &s_gl.cur_wake_xyt[4], sizeof(wake_m));
		lam_gl_cmd_uniform_mat4(&s_gl.cmd_buf, tp->uLambWakeXYT1, GL_FALSE, wake_m);
		memcpy(wake_m, &s_gl.cur_wake_vel[0], sizeof(wake_m));
		lam_gl_cmd_uniform_mat4(&s_gl.cmd_buf, tp->uLambWakeVel0, GL_FALSE, wake_m);
		memcpy(wake_m, &s_gl.cur_wake_vel[4], sizeof(wake_m));
		lam_gl_cmd_uniform_mat4(&s_gl.cmd_buf, tp->uLambWakeVel1, GL_FALSE, wake_m);
		s_gl.terrain_full_wake_valid = true;
	}

	/* Hoist per-celestial-body transform & world ID uniforms across chunks of the same planet */
	bool same_body = s_gl.terrain_pass_has_body &&
	                 s_gl.terrain_pass_world == world &&
	                 s_gl.terrain_pass_pdef.major_r == eff_pdef->major_r &&
	                 s_gl.terrain_pass_pdef.minor_r == eff_pdef->minor_r &&
	                 memcmp(s_gl.terrain_pass_pdef.center, eff_pdef->center, sizeof(eff_pdef->center)) == 0 &&
	                 memcmp(s_gl.terrain_pass_pdef.rot, eff_pdef->rot, sizeof(eff_pdef->rot)) == 0;
	if (!same_body) {
		double p0_chunk[3];
		torus_frame_double((double)s_gl.cur_tx, (double)s_gl.cur_ty, 0.0, eff_pdef, p0_chunk, NULL, NULL, NULL);
		s_gl.terrain_pass_rel_center[0] = (float)(p0_chunk[0] - s_gl.cur_origin[0]);
		s_gl.terrain_pass_rel_center[1] = (float)(p0_chunk[1] - s_gl.cur_origin[1]);
		s_gl.terrain_pass_rel_center[2] = (float)(p0_chunk[2] - s_gl.cur_origin[2]);
		bool has_rot = (eff_pdef->rot[0] != 0.0f || eff_pdef->rot[4] != 0.0f || eff_pdef->rot[8] != 0.0f);
		float rot_mat4[16] = {
			has_rot ? eff_pdef->rot[0] : 1.0f, has_rot ? eff_pdef->rot[3] : 0.0f, has_rot ? eff_pdef->rot[6] : 0.0f, 0.0f,
			has_rot ? eff_pdef->rot[1] : 0.0f, has_rot ? eff_pdef->rot[4] : 1.0f, has_rot ? eff_pdef->rot[7] : 0.0f, 0.0f,
			has_rot ? eff_pdef->rot[2] : 0.0f, has_rot ? eff_pdef->rot[5] : 0.0f, has_rot ? eff_pdef->rot[8] : 1.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f
		};
		memcpy(s_gl.terrain_pass_rot_mat4, rot_mat4, sizeof(rot_mat4));
		s_gl.terrain_pass_world = world;
		s_gl.terrain_pass_pdef = *eff_pdef;
		s_gl.terrain_pass_has_body = true;

		/* Pass canonical world ID and sea_offset_q12_4 for GPU canonical field evaluation */
		static uint16_t s_cached_gl_world = 0xFFFFu;
		static int32_t s_cached_gl_sea_offset = 0;
		if (world != s_cached_gl_world) {
			uint32_t w_seed = 0x48454947u ^ ((uint32_t)world * 0x9E3779B1u);
			int32_t origin_bias = nd_field_eval_coarse_unwrapped_q12_4(w_seed, 0, 0) >> 2;
			uint32_t w_noise = (((world >> 2) + 2) & 3) * 65536u;
			s_cached_gl_sea_offset = (int32_t)((((int64_t)w_noise * 200 / 262144 - 100)) << 4) +
			                         ND_FIELD_ORIGIN_SEA_OFFSET_Q12_4 + origin_bias;
			s_cached_gl_world = world;
		}
		s_gl.terrain_pass_sea_offset_q12_4 = s_cached_gl_sea_offset;
		memset(s_gl.terrain_var_body_valid, 0, sizeof(s_gl.terrain_var_body_valid));
	}
	if (!s_gl.terrain_var_body_valid[want_var]) {
		lam_gl_cmd_uniform3f(&s_gl.cmd_buf, tp->uPlanetCenter,
		                     s_gl.terrain_pass_rel_center[0],
		                     s_gl.terrain_pass_rel_center[1],
		                     s_gl.terrain_pass_rel_center[2]);
		lam_gl_cmd_uniform_mat4(&s_gl.cmd_buf, tp->uBodyRot, GL_FALSE, s_gl.terrain_pass_rot_mat4);
		lam_gl_cmd_uniform2f(&s_gl.cmd_buf, tp->uTorusRadii, eff_pdef->major_r, eff_pdef->minor_r);
		lam_gl_cmd_uniform1i(&s_gl.cmd_buf, tp->uWorldId, (GLint)world);
		lam_gl_cmd_uniform1i(&s_gl.cmd_buf, tp->uSeaOffsetQ12_4, (GLint)s_gl.terrain_pass_sea_offset_q12_4);
		s_gl.terrain_var_body_valid[want_var] = true;
	}
	const float *rel_center = s_gl.terrain_pass_rel_center;
	const float *rot_mat4 = s_gl.terrain_pass_rot_mat4;

	/* Per-chunk texture binds & uniforms (batched into flat command ring) */
	lam_gl_cmd_active_texture(&s_gl.cmd_buf, GL_TEXTURE0);
	lam_gl_cmd_bind_texture(&s_gl.cmd_buf, GL_TEXTURE_2D, slot->height_tex);
	lam_gl_cmd_active_texture(&s_gl.cmd_buf, GL_TEXTURE1);
	lam_gl_cmd_bind_texture(&s_gl.cmd_buf, GL_TEXTURE_2D, slot->substrate_tex);
	if (want_var == ND_TERR_VAR_SEABED_NEAR || want_var == ND_TERR_VAR_SEABED_MID ||
	    want_var == ND_TERR_VAR_FULL) {
		GLuint want_river_tex = (slot->has_river && slot->river_tex)
		                        ? slot->river_tex : s_gl.dummy_river_tex;
		if (want_river_tex != s_gl.terrain_pass_river_tex) {
			lam_gl_cmd_active_texture(&s_gl.cmd_buf, GL_TEXTURE2);
			lam_gl_cmd_bind_texture(&s_gl.cmd_buf, GL_TEXTURE_2D, want_river_tex);
			s_gl.terrain_pass_river_tex = want_river_tex;
		}
	}

	lam_gl_cmd_uniform2f(&s_gl.cmd_buf, tp->uChunkOrigin, (float)ox, (float)oy);
	lam_gl_cmd_uniform2f(&s_gl.cmd_buf, tp->uChunkRelOrigin,
	                     (float)((double)ox - (double)s_gl.cur_tx),
	                     (float)((double)oy - (double)s_gl.cur_ty));
	lam_gl_cmd_uniform2f(&s_gl.cmd_buf, tp->uChunkSpan, (float)span_w, (float)span_h);
	if (!s_gl.terrain_var_sea_valid[want_var] || terrain_sea_lvl != s_gl.terrain_var_sea_level[want_var]) {
		lam_gl_cmd_uniform1f(&s_gl.cmd_buf, tp->uSeaLevel, terrain_sea_lvl);
		s_gl.terrain_var_sea_level[want_var] = terrain_sea_lvl;
		s_gl.terrain_var_sea_valid[want_var] = true;
	}

	GLuint active_vbo = 0, active_ibo = 0;
	GLsizei active_indices = 0;
	select_grid_buffers(level, ox, oy, span_w, span_h, &active_vbo, &active_ibo, &active_indices);
	GLsizei skirt_quads = (active_indices == GL_TOTAL_INDICES_HI) ? GL_GRID_QUADS_HI
	                    : ((active_indices == GL_TOTAL_INDICES) ? GL_GRID_QUADS_LO : GL_GRID_QUADS_MACRO);
	GLsizei draw_indices = active_indices + (4 * skirt_quads * 6);

	if (active_vbo != s_gl.terrain_pass_vbo) {
		lam_gl_cmd_bind_buffer(&s_gl.cmd_buf, GL_ARRAY_BUFFER, active_vbo);
		lam_gl_cmd_vertex_attrib_ptr(&s_gl.cmd_buf, 0, 2, GL_FLOAT, GL_FALSE, 8, 0);
		lam_gl_cmd_bind_buffer(&s_gl.cmd_buf, GL_ELEMENT_ARRAY_BUFFER, active_ibo);
		s_gl.terrain_pass_vbo = active_vbo;
	}
	lam_gl_cmd_draw_elements(&s_gl.cmd_buf, GL_TRIANGLES, draw_indices, GL_UNSIGNED_SHORT, 0);
	s_gl.res_depth_valid = false;
	s_gl.res_color_valid = false;
	if (s_gl.indirect_cmd_count < ND_GPU_INDIRECT_MAX) {
		nd_gpu_draw_elements_indirect_cmd_t *cmd = &s_gl.indirect_cmds[s_gl.indirect_cmd_count++];
		cmd->count = (uint32_t)draw_indices;
		cmd->instance_count = 1u;
		cmd->first_index = 0u;
		cmd->base_vertex = 0;
		cmd->base_instance = s_gl.indirect_cmd_count - 1u;
	}

	/* Enqueue primary-world near-field chunks (Level <= 1) for depth-only terrain shadow casting
	 * into tree_shadow_fbo so riverbanks, cliffs, and hills cast directional shadows onto lowlands. */
	if (world == s_gl.cur_pdef.world_id && level <= 1 &&
	    s_gl.tree_shadow_ready && !s_gl.tree_shadow_skip_write && s_gl.terrain_shadow_pipe.prog &&
	    s_gl.cur_dist < 2300.0f && s_gl.terr_shadow_q_count < MAX_TERR_SHADOW_QUEUE) {
		float sh_pad = s_gl.tree_shadow_write_half_xy + (float)span_w;
		float cx = (float)ox + (float)span_w * 0.5f;
		float cy = (float)oy + (float)span_h * 0.5f;
		if (fabsf(cx - s_gl.tree_shadow_write_origin[0]) <= sh_pad &&
		    fabsf(cy - s_gl.tree_shadow_write_origin[1]) <= sh_pad) {
			gl_terr_shadow_chunk_t *sq = &s_gl.terr_shadow_q[s_gl.terr_shadow_q_count++];
			sq->height_tex = slot->height_tex;
			sq->ox = (float)ox;
			sq->oy = (float)oy;
			sq->span_w = (float)span_w;
			sq->span_h = (float)span_h;
		}
	}

	/* Enqueue primary-world near-field chunks (Level <= 1) for GPU-instanced 3D grass & clover carpet
	 * using CPU-pre-culled per-chunk tuft VBOs (eliminates underwater/cliff/barren GPU vertex waste). */
	if (world == s_gl.cur_pdef.world_id && level <= 1 &&
	    s_gl.grass_pipe.prog && s_gl.grass_idx_count > 0 &&
	    s_gl.cur_dist < 1450.0f && s_gl.grass_q_count < MAX_GRASS_CHUNKS) {
		float g_pad = 1180.0f + (float)span_w * 0.5f;
		float cx = (float)ox + (float)span_w * 0.5f;
		float cy = (float)oy + (float)span_h * 0.5f;
		if (fabsf(cx - s_gl.cur_tx) <= g_pad && fabsf(cy - s_gl.cur_ty) <= g_pad) {
			float det_scale = nd_seven_get_detail_scale();
			int l0_n = (det_scale < 0.75f) ? 36 : ((det_scale < 0.90f) ? 44 : 54);
			int l1_n = (det_scale < 0.75f) ? 24 : ((det_scale < 0.90f) ? 28 : 34);
			int grid_n = (level == 0) ? l0_n : l1_n;
			bool touched_vbo = false;
			gl_grass_vbo_slot_t *gslot = gl_grass_get_or_build_chunk(
				world, level, rev, ox, oy, span_w, span_h,
				grid_n, terrain_sea_lvl, height_i16, color_rgba,
				chunk_w, chunk_h, &touched_vbo);
			if (touched_vbo)
				s_gl.terrain_pass_vbo = 0;
			if (gslot && gslot->tuft_count > 0) {
				gl_grass_chunk_t *gq = &s_gl.grass_q[s_gl.grass_q_count++];
				gq->tuft_vbo = gslot->vbo;
				gq->tuft_count = gslot->tuft_count;
				gq->ox = ox;
				gq->oy = oy;
				gq->span_w = span_w;
				gq->span_h = span_h;
				gq->level = level;
				gq->sea_level = terrain_sea_lvl;
				gq->center_x = cx;
				gq->center_y = cy;
				gq->rel_center[0] = rel_center[0];
				gq->rel_center[1] = rel_center[1];
				gq->rel_center[2] = rel_center[2];
				for (int k = 0; k < 16; k++) gq->rot_mat4[k] = rot_mat4[k];
				gq->major_r = eff_pdef->major_r;
				gq->minor_r = eff_pdef->minor_r;
			}
		}
	}

	/* Queue volumetric cloud shell pass for this chunk (gated by 3D cloud-shell frustum,
	 * planetary limb visibility, zoom ramp, and synoptic clear-sky pre-cull) */
	uint32_t max_cloud_q = (s_gl.cur_dist > 20000.0f) ? (uint32_t)MAX_WATER_QUEUE_BASE : (uint32_t)nd_seven_gl_max_water_queue();
	if (s_gl.cloud_count < max_cloud_q &&
	    nd_seven_gl_should_queue_cloud_chunk(ox, oy, span_w, span_h, eff_pdef)) {
		gl_water_chunk_t *cq = &s_gl.cloud_queue[s_gl.cloud_count++];
		cq->ox = ox;
		cq->oy = oy;
		cq->span_w = span_w;
		cq->span_h = span_h;
		cq->level = level;
		cq->sea_level = terrain_sea_lvl;
		cq->height_tex = slot->height_tex;
		cq->substrate_tex = slot->substrate_tex;
		cq->rel_center[0] = rel_center[0];
		cq->rel_center[1] = rel_center[1];
		cq->rel_center[2] = rel_center[2];
		for (int k = 0; k < 16; k++) cq->rot_mat4[k] = rot_mat4[k];
		cq->major_r = eff_pdef->major_r;
		cq->minor_r = eff_pdef->minor_r;
	}

	/* Queue the water surface of this wet chunk for Stage 8 seabed optics & submerged-object pass */
	if (water_eligible && !water_occluded) {
		gl_water_chunk_t *wq = &s_gl.water_queue[s_gl.water_count++];
		wq->ox = ox;
		wq->oy = oy;
		wq->span_w = span_w;
		wq->span_h = span_h;
		wq->level = level;
		wq->sea_level = terrain_sea_lvl;
		wq->height_tex = slot->height_tex;
		wq->substrate_tex = slot->substrate_tex;
		wq->river_tex = (slot->has_river && slot->river_tex) ? slot->river_tex : s_gl.dummy_river_tex;
		wq->rel_center[0] = rel_center[0];
		wq->rel_center[1] = rel_center[1];
		wq->rel_center[2] = rel_center[2];
		for (int k = 0; k < 16; k++) wq->rot_mat4[k] = rot_mat4[k];
		wq->major_r = eff_pdef->major_r;
		wq->minor_r = eff_pdef->minor_r;
	}
}

void nd_seven_gl_flush_grass(void)
{
	if (!s_gl.initialized || !s_gl.pipelines_ready || !s_gl.grass_pipe.prog ||
	    s_gl.grass_q_count == 0 || s_gl.grass_idx_count <= 0 || s_gl.cur_dist >= 1450.0f) {
		s_gl.grass_q_count = 0;
		return;
	}

	int quality = nd_seven_get_quality_preset();
	bool has_lod = (s_gl.grass_vbo_lod != 0 && s_gl.grass_ibo_lod != 0 && s_gl.grass_idx_count_lod > 0);

	gl_end_terrain_pass();
	glTimerBegin(ND_GPU_PASS_GRASS);
	glUseProgram(s_gl.grass_pipe.prog);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
	glDisable(GL_CULL_FACE);
	glEnable(GL_STENCIL_TEST);
	glStencilMask(0x08);
	glStencilFunc(GL_ALWAYS, 0x00, 0x08);
	glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

	gl_bind_csm(s_gl.grass_pipe.uCloudShadowMap);
	gl_bind_tree_shadow_read(s_gl.grass_pipe.uTreeShadowTex);

	glEnableVertexAttribArray(0);
	glVertexAttribDivisor(0, 0);
	glEnableVertexAttribArray(1);
	glVertexAttribDivisor(1, 0);
	glEnableVertexAttribArray(2);
	glVertexAttribDivisor(2, 1);
	glEnableVertexAttribArray(3);
	glVertexAttribDivisor(3, 1);
	glEnableVertexAttribArray(4);
	glVertexAttribDivisor(4, 1);
	glEnableVertexAttribArray(5);
	glVertexAttribDivisor(5, 1);

	int bound_lod = -1;
	GLsizei active_idx_count = s_gl.grass_idx_count;
	for (uint32_t i = 0; i < s_gl.grass_q_count; i++) {
		const gl_grass_chunk_t *gq = &s_gl.grass_q[i];
		if (!gq->tuft_vbo || gq->tuft_count == 0)
			continue;

		float dx = gq->center_x - s_gl.cur_tx;
		float dy = gq->center_y - s_gl.cur_ty;
		int want_lod = (has_lod && (quality == ND_QUALITY_LOW || (dx * dx + dy * dy) > (220.0f * 220.0f))) ? 1 : 0;
		if (want_lod != bound_lod) {
			GLuint mvbo = want_lod ? s_gl.grass_vbo_lod : s_gl.grass_vbo;
			GLuint mibo = want_lod ? s_gl.grass_ibo_lod : s_gl.grass_ibo;
			active_idx_count = want_lod ? s_gl.grass_idx_count_lod : s_gl.grass_idx_count;
			lam_gl_cmd_bind_buffer(&s_gl.cmd_buf, GL_ARRAY_BUFFER, mvbo);
			lam_gl_cmd_vertex_attrib_ptr(&s_gl.cmd_buf, 0, 4, GL_FLOAT, GL_FALSE, sizeof(gl_grass_vert_t), 0);
			lam_gl_cmd_vertex_attrib_ptr(&s_gl.cmd_buf, 1, 4, GL_FLOAT, GL_FALSE, sizeof(gl_grass_vert_t), 16);
			lam_gl_cmd_bind_buffer(&s_gl.cmd_buf, GL_ELEMENT_ARRAY_BUFFER, mibo);
			bound_lod = want_lod;
		}

		lam_gl_cmd_bind_buffer(&s_gl.cmd_buf, GL_ARRAY_BUFFER, gq->tuft_vbo);
		lam_gl_cmd_vertex_attrib_ptr(&s_gl.cmd_buf, 2, 4, GL_FLOAT, GL_FALSE, sizeof(gl_grass_tuft_t), 0);
		lam_gl_cmd_vertex_attrib_ptr(&s_gl.cmd_buf, 3, 4, GL_FLOAT, GL_FALSE, sizeof(gl_grass_tuft_t), 16);
		lam_gl_cmd_vertex_attrib_ptr(&s_gl.cmd_buf, 4, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(gl_grass_tuft_t), 32);
		lam_gl_cmd_vertex_attrib_ptr(&s_gl.cmd_buf, 5, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(gl_grass_tuft_t), 36);

		lam_gl_cmd_uniform3f(&s_gl.cmd_buf, s_gl.grass_pipe.uPlanetCenter,
		                     gq->rel_center[0], gq->rel_center[1], gq->rel_center[2]);
		lam_gl_cmd_uniform_mat4(&s_gl.cmd_buf, s_gl.grass_pipe.uBodyRot, GL_FALSE, gq->rot_mat4);
		lam_gl_cmd_uniform2f(&s_gl.cmd_buf, s_gl.grass_pipe.uTorusRadii, gq->major_r, gq->minor_r);
		lam_gl_cmd_uniform2f(&s_gl.cmd_buf, s_gl.grass_pipe.uChunkOrigin, (float)gq->ox, (float)gq->oy);
		lam_gl_cmd_uniform2f(&s_gl.cmd_buf, s_gl.grass_pipe.uChunkRelOrigin,
		                     (float)((double)gq->ox - (double)s_gl.cur_tx),
		                     (float)((double)gq->oy - (double)s_gl.cur_ty));
		lam_gl_cmd_uniform2f(&s_gl.cmd_buf, s_gl.grass_pipe.uChunkSpan, (float)gq->span_w, (float)gq->span_h);
		lam_gl_cmd_uniform1f(&s_gl.cmd_buf, s_gl.grass_pipe.uSeaLevel, gq->sea_level);

		lam_gl_cmd_draw_elements_instanced(&s_gl.cmd_buf, GL_TRIANGLES, active_idx_count, GL_UNSIGNED_SHORT, 0,
		                                   (GLsizei)gq->tuft_count);
	}
	gl_flush_cmd_ring();
	glTimerEnd(ND_GPU_PASS_GRASS);

	glStencilMask(0xFF);
	glDisable(GL_STENCIL_TEST);
	glVertexAttribDivisor(2, 0);
	glVertexAttribDivisor(3, 0);
	glVertexAttribDivisor(4, 0);
	glVertexAttribDivisor(5, 0);
	glDisableVertexAttribArray(5);
	glDisableVertexAttribArray(4);
	glDisableVertexAttribArray(3);
	glDisableVertexAttribArray(2);
	glDisableVertexAttribArray(1);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	glActiveTexture(GL_TEXTURE0);
	s_gl.res_depth_valid = false;
	s_gl.res_color_valid = false;
	s_gl.grass_q_count = 0;
}

void nd_seven_gl_flush_water(void)
{
	/* Composites Stage 8 ocean optics over SEABED_ONLY terrain chunks (alpha 0.5) and refracted
	 * absorption over submerged sprites/meshes (alpha 0.0, stencil bit 0). Dry land / grass / sky
	 * (alpha 1.0) are discarded immediately. Skipped when neither seabed split nor submerged sprites
	 * are pending. */
	if (!s_gl.initialized || !s_gl.pipelines_ready || !s_gl.water_pipe.prog || s_gl.water_count == 0 ||
	    (!s_gl.sprites_in_water && !s_gl.terrain_split_water_pending) ||
	    !s_gl.scene_ok || s_gl.cur_dist > 5500.0f) {
		s_gl.water_count = 0;
		s_gl.terrain_split_water_pending = false;
		return;
	}

	gl_end_terrain_pass();
	gl_resolve_scene(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glTimerBegin(ND_GPU_PASS_WATER);

	glUseProgram(s_gl.water_pipe.prog);
	gl_bind_csm(s_gl.water_pipe.uCloudShadowMap);
	(void)gl_ensure_dummy_river_tex();

	glActiveTexture(GL_TEXTURE0 + 4);
	glBindTexture(GL_TEXTURE_2D, s_gl.res_color_tex);
	glUniform1i(s_gl.water_pipe.uSceneColor, 4);
	glActiveTexture(GL_TEXTURE0 + 5);
	glBindTexture(GL_TEXTURE_2D, s_gl.res_depth_tex);
	glUniform1i(s_gl.water_pipe.uSceneDepth, 5);
	glActiveTexture(GL_TEXTURE0);
	glUniform1i(s_gl.water_pipe.uHeightTex, 0);
	glUniform1i(s_gl.water_pipe.uSubstrateTex, 1);
	if (s_gl.water_pipe.uRiverTex >= 0)
		glUniform1i(s_gl.water_pipe.uRiverTex, 2);
	uint32_t rw = s_gl.render_w ? s_gl.render_w : s_gl.scene_w;
	uint32_t rh = s_gl.render_h ? s_gl.render_h : s_gl.scene_h;
	glUniform2f(s_gl.water_pipe.uViewport, (float)s_gl.scene_w, (float)s_gl.scene_h);
	if (s_gl.water_pipe.uSceneUvScale >= 0) {
		float sx = (s_gl.scene_w > 0) ? ((float)rw / (float)s_gl.scene_w) : 1.0f;
		float sy = (s_gl.scene_h > 0) ? ((float)rh / (float)s_gl.scene_h) : 1.0f;
		glUniform2f(s_gl.water_pipe.uSceneUvScale, sx, sy);
	}
	glUniform2f(s_gl.water_pipe.uNearFar, s_gl.cur_near, s_gl.cur_far);
	glUniform2f(s_gl.water_pipe.uProjScale, s_gl.cur_tan_half * s_gl.cur_aspect, s_gl.cur_tan_half);

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glDepthMask(GL_FALSE);
	glDisable(GL_BLEND);
	glEnable(GL_CULL_FACE);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
	/* Hardware early-stencil: bit 0x08 = SEABED_* terrain chunk, bit 0x01 = mesh3d/billboard core.
	 * Dry terrain, grass, and sky have (stencil & 0x09) == 0 and are rejected before FS execution. */
	glEnable(GL_STENCIL_TEST);
	glStencilMask(0);
	glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
	if (s_gl.terrain_split_water_pending && s_gl.sprites_in_water)
		glStencilFunc(GL_LESS, 0, 0x09);     /* 0 < (stencil & 0x09)  <=>  bit 0x08 or bit 0x01 set */
	else if (s_gl.terrain_split_water_pending)
		glStencilFunc(GL_EQUAL, 0x08, 0x08); /* seabed-only: skip all meshes/sprites/grass/dry/sky */
	else
		glStencilFunc(GL_EQUAL, 0x01, 0x01); /* submerged sprites only */

	float zero_center[3] = { 0.0f, 0.0f, 0.0f };
	glUniform1f(s_gl.water_pipe.uHeightScale, 1.0f);
	glUniform3fv(s_gl.water_pipe.uPlanetCenter, 1, zero_center);
	glUniform2f(s_gl.water_pipe.uTorusRadii, s_gl.cur_pdef.major_r, s_gl.cur_pdef.minor_r);
	glUniform4f(s_gl.water_pipe.uLambWakeHead,
	            s_gl.cur_wake_head[0], s_gl.cur_wake_head[1],
	            s_gl.cur_wake_head[2], s_gl.cur_wake_head[3]);
	glUniformMatrix4fv(s_gl.water_pipe.uLambWakeXYT0, 1, GL_FALSE, &s_gl.cur_wake_xyt[0][0]);
	glUniformMatrix4fv(s_gl.water_pipe.uLambWakeXYT1, 1, GL_FALSE, &s_gl.cur_wake_xyt[4][0]);
	glUniformMatrix4fv(s_gl.water_pipe.uLambWakeVel0, 1, GL_FALSE, &s_gl.cur_wake_vel[0][0]);
	glUniformMatrix4fv(s_gl.water_pipe.uLambWakeVel1, 1, GL_FALSE, &s_gl.cur_wake_vel[4][0]);
	glEnableVertexAttribArray(0);

	GLuint bound_w_vbo = 0;
	GLuint bound_river_tex = 0;
	for (uint32_t i = 0; i < s_gl.water_count; i++) {
		const gl_water_chunk_t *chk = &s_gl.water_queue[i];
		GLuint w_vbo = 0, w_ibo = 0;
		GLsizei w_indices = 0;
		select_grid_buffers(chk->level, chk->ox, chk->oy, chk->span_w, chk->span_h, &w_vbo, &w_ibo, &w_indices);

		if (w_vbo != bound_w_vbo) {
			lam_gl_cmd_bind_buffer(&s_gl.cmd_buf, GL_ARRAY_BUFFER, w_vbo);
			lam_gl_cmd_vertex_attrib_ptr(&s_gl.cmd_buf, 0, 2, GL_FLOAT, GL_FALSE, 8, 0);
			lam_gl_cmd_bind_buffer(&s_gl.cmd_buf, GL_ELEMENT_ARRAY_BUFFER, w_ibo);
			bound_w_vbo = w_vbo;
		}

		lam_gl_cmd_active_texture(&s_gl.cmd_buf, GL_TEXTURE0);
		lam_gl_cmd_bind_texture(&s_gl.cmd_buf, GL_TEXTURE_2D, chk->height_tex);

		lam_gl_cmd_active_texture(&s_gl.cmd_buf, GL_TEXTURE1);
		lam_gl_cmd_bind_texture(&s_gl.cmd_buf, GL_TEXTURE_2D, chk->substrate_tex);

		GLuint want_rv = chk->river_tex ? chk->river_tex : s_gl.dummy_river_tex;
		if (want_rv != bound_river_tex) {
			lam_gl_cmd_active_texture(&s_gl.cmd_buf, GL_TEXTURE2);
			lam_gl_cmd_bind_texture(&s_gl.cmd_buf, GL_TEXTURE_2D, want_rv);
			bound_river_tex = want_rv;
		}

		lam_gl_cmd_uniform3f(&s_gl.cmd_buf, s_gl.water_pipe.uPlanetCenter,
		                     chk->rel_center[0], chk->rel_center[1], chk->rel_center[2]);
		lam_gl_cmd_uniform_mat4(&s_gl.cmd_buf, s_gl.water_pipe.uBodyRot, GL_FALSE, chk->rot_mat4);
		lam_gl_cmd_uniform2f(&s_gl.cmd_buf, s_gl.water_pipe.uTorusRadii, chk->major_r, chk->minor_r);
		lam_gl_cmd_uniform2f(&s_gl.cmd_buf, s_gl.water_pipe.uChunkOrigin, (float)chk->ox, (float)chk->oy);
		lam_gl_cmd_uniform2f(&s_gl.cmd_buf, s_gl.water_pipe.uChunkRelOrigin,
		                     (float)((double)chk->ox - (double)s_gl.cur_tx),
		                     (float)((double)chk->oy - (double)s_gl.cur_ty));
		lam_gl_cmd_uniform2f(&s_gl.cmd_buf, s_gl.water_pipe.uChunkSpan, (float)chk->span_w, (float)chk->span_h);
		lam_gl_cmd_uniform1f(&s_gl.cmd_buf, s_gl.water_pipe.uSeaLevel, chk->sea_level);

		lam_gl_cmd_draw_elements(&s_gl.cmd_buf, GL_TRIANGLES, w_indices, GL_UNSIGNED_SHORT, 0);
		if (s_gl.indirect_cmd_count < ND_GPU_INDIRECT_MAX) {
			nd_gpu_draw_elements_indirect_cmd_t *cmd = &s_gl.indirect_cmds[s_gl.indirect_cmd_count++];
			cmd->count = (uint32_t)w_indices;
			cmd->instance_count = 1u;
			cmd->first_index = 0u;
			cmd->base_vertex = 0;
			cmd->base_instance = s_gl.indirect_cmd_count - 1u;
		}
	}
	gl_flush_cmd_ring();
	glTimerEnd(ND_GPU_PASS_WATER);

	glDisable(GL_CULL_FACE);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glDepthMask(GL_TRUE);
	glDisable(GL_STENCIL_TEST);
	glStencilMask(0xFF);
	glEnable(GL_BLEND);
	glActiveTexture(GL_TEXTURE0);
	/* Water pass writes color only (glDepthMask(GL_FALSE)), so res_depth_valid remains true */
	s_gl.res_color_valid = false;
	s_gl.water_count = 0;
	s_gl.terrain_split_water_pending = false;
}

void gl_flush_terrain_shadows_to_fbo(void)
{
	if (!s_gl.terrain_shadow_pipe.prog || s_gl.terr_shadow_q_count == 0)
		return;
	gl_end_terrain_pass();
	glUseProgram(s_gl.terrain_shadow_pipe.prog);
	glUniform1f(s_gl.terrain_shadow_pipe.uHeightScale, 1.0f);
	if (s_gl.terrain_shadow_pipe.uTreeShadowWriteOrigin >= 0)
		glUniform3fv(s_gl.terrain_shadow_pipe.uTreeShadowWriteOrigin, 1, s_gl.tree_shadow_write_origin);
	if (s_gl.terrain_shadow_pipe.uTreeShadowWriteVP >= 0)
		glUniformMatrix4fv(s_gl.terrain_shadow_pipe.uTreeShadowWriteVP, 1, GL_FALSE, s_gl.tree_shadow_write_vp);

	glBindBuffer(GL_ARRAY_BUFFER, s_gl.grid_vbo_hi);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s_gl.grid_ibo_hi);

	glEnable(GL_POLYGON_OFFSET_FILL);
	glPolygonOffset(2.0f, 3.0f);
	glActiveTexture(GL_TEXTURE0);
	glUniform1i(s_gl.terrain_shadow_pipe.uHeightTex, 0);

	for (uint32_t i = 0; i < s_gl.terr_shadow_q_count; i++) {
		const gl_terr_shadow_chunk_t *sq = &s_gl.terr_shadow_q[i];
		lam_gl_cmd_bind_texture(&s_gl.cmd_buf, GL_TEXTURE_2D, sq->height_tex);
		lam_gl_cmd_uniform2f(&s_gl.cmd_buf, s_gl.terrain_shadow_pipe.uChunkOrigin, sq->ox, sq->oy);
		lam_gl_cmd_uniform2f(&s_gl.cmd_buf, s_gl.terrain_shadow_pipe.uChunkSpan, sq->span_w, sq->span_h);
		lam_gl_cmd_draw_elements(&s_gl.cmd_buf, GL_TRIANGLES, GL_TOTAL_INDICES_HI, GL_UNSIGNED_SHORT, 0);
	}
	gl_flush_cmd_ring();

	glDisable(GL_POLYGON_OFFSET_FILL);
	s_gl.terr_shadow_q_count = 0;
}

