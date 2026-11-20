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

GLuint create_program_async(const char *vs_src, const char *fs_src)
{
	GLuint vs = compile_shader(GL_VERTEX_SHADER, vs_src);
	GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_src);
	if (!vs || !fs) {
		if (vs) glDeleteShader(vs);
		if (fs) glDeleteShader(fs);
		return 0;
	}
	GLuint prog = glCreateProgram();
	if (!prog) {
		glDeleteShader(vs);
		glDeleteShader(fs);
		return 0;
	}
	glAttachShader(prog, vs);
	glAttachShader(prog, fs);
	glLinkProgram(prog);
	glDeleteShader(vs);
	glDeleteShader(fs);
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

	int i_idx = 0;
	for (int y = 0; y < quads; y++) {
		for (int x = 0; x < quads; x++) {
			uint16_t i0 = (uint16_t)(y * verts + x);
			uint16_t i1 = (uint16_t)(i0 + 1);
			uint16_t i2 = (uint16_t)((y + 1) * verts + x);
			uint16_t i3 = (uint16_t)(i2 + 1);

			if ((x + y) & 1) {
				idx_buf[i_idx++] = i0;
				idx_buf[i_idx++] = i1;
				idx_buf[i_idx++] = i3;
				idx_buf[i_idx++] = i0;
				idx_buf[i_idx++] = i3;
				idx_buf[i_idx++] = i2;
			} else {
				idx_buf[i_idx++] = i0;
				idx_buf[i_idx++] = i1;
				idx_buf[i_idx++] = i2;
				idx_buf[i_idx++] = i1;
				idx_buf[i_idx++] = i3;
				idx_buf[i_idx++] = i2;
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

static void init_grid_buffers(void)
{
	static float grid_uv_scratch[(GL_TOTAL_VERTS_HI + 4 * (GL_GRID_QUADS_HI + 1)) * 2];
	static uint16_t indices_scratch[GL_TOTAL_INDICES_HI + 4 * GL_GRID_QUADS_HI * 6];

	create_static_grid(GL_GRID_QUADS_MACRO, grid_uv_scratch, indices_scratch, &s_gl.grid_vbo_macro, &s_gl.grid_ibo_macro);
	create_static_grid(GL_GRID_QUADS_LO, grid_uv_scratch, indices_scratch, &s_gl.grid_vbo, &s_gl.grid_ibo);
	create_static_grid(GL_GRID_QUADS_HI, grid_uv_scratch, indices_scratch, &s_gl.grid_vbo_hi, &s_gl.grid_ibo_hi);

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
	glBufferData(GL_ARRAY_BUFFER, NULL, MAX_BB_INSTANCES * sizeof(nd_seven_gl_mesh3d_inst_t), GL_DYNAMIC_DRAW);

	/* Bake and upload 1024x1024 RGBA8 botanical branchlet albedo + companion normal/depth/translucency atlases with CPU mip levels */
	if (!s_gl.fol_atlas_tex || !s_gl.fol_atlas_norm_tex) {
		uint8_t *buf_a = (uint8_t *)malloc((size_t)ND_FOL_ATLAS_SIZE * (size_t)ND_FOL_ATLAS_SIZE * 4u);
		uint8_t *buf_n = (uint8_t *)malloc((size_t)ND_FOL_ATLAS_SIZE * (size_t)ND_FOL_ATLAS_SIZE * 4u);
		uint8_t *buf_b = (uint8_t *)malloc((size_t)(ND_FOL_ATLAS_SIZE / 2) * (size_t)(ND_FOL_ATLAS_SIZE / 2) * 4u);
		if (buf_a && buf_n && buf_b) {
			nd_foliage_atlas_bake_level0(buf_a);
			nd_foliage_atlas_bake_normals_level0(buf_a, buf_n);

			if (!s_gl.fol_atlas_tex)
				s_gl.fol_atlas_tex = glCreateTexture();
			glActiveTexture(GL_TEXTURE0 + 3);
			glBindTexture(GL_TEXTURE_2D, s_gl.fol_atlas_tex);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, ND_FOL_MIP_LEVELS - 1);
			int cur_w = ND_FOL_ATLAS_SIZE;
			const uint8_t *src = buf_a;
			uint8_t *dst = buf_b;
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, cur_w, cur_w, 0, GL_RGBA, GL_UNSIGNED_BYTE, src);
			for (int mip = 1; mip < ND_FOL_MIP_LEVELS; mip++) {
				int next_w = cur_w >> 1;
				nd_foliage_atlas_downsample(src, cur_w, cur_w, dst);
				glTexImage2D(GL_TEXTURE_2D, mip, GL_RGBA8, next_w, next_w, 0, GL_RGBA, GL_UNSIGNED_BYTE, dst);
				uint8_t *prev_src = (uint8_t *)src;
				src = dst;
				dst = prev_src;
				cur_w = next_w;
			}
			glBindTexture(GL_TEXTURE_2D, 0);

			if (!s_gl.fol_atlas_norm_tex)
				s_gl.fol_atlas_norm_tex = glCreateTexture();
			glActiveTexture(GL_TEXTURE0 + 4);
			glBindTexture(GL_TEXTURE_2D, s_gl.fol_atlas_norm_tex);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, ND_FOL_MIP_LEVELS - 1);
			cur_w = ND_FOL_ATLAS_SIZE;
			src = buf_n;
			dst = buf_b;
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, cur_w, cur_w, 0, GL_RGBA, GL_UNSIGNED_BYTE, src);
			for (int mip = 1; mip < ND_FOL_MIP_LEVELS; mip++) {
				int next_w = cur_w >> 1;
				nd_foliage_atlas_downsample_normals(src, cur_w, cur_w, dst);
				glTexImage2D(GL_TEXTURE_2D, mip, GL_RGBA8, next_w, next_w, 0, GL_RGBA, GL_UNSIGNED_BYTE, dst);
				uint8_t *prev_src = (uint8_t *)src;
				src = dst;
				dst = prev_src;
				cur_w = next_w;
			}
			glBindTexture(GL_TEXTURE_2D, 0);
			glActiveTexture(GL_TEXTURE0);
		}
		free(buf_a);
		free(buf_n);
		free(buf_b);
	}
}

static void init_pipelines(void)
{
	s_gl.terrain_pipe.prog    = create_program_async(s_vs_terrain, s_fs_terrain);
	s_gl.water_pipe.prog      = create_program_async(s_vs_water, s_fs_water);
	s_gl.bb_pipe.prog         = create_program_async(s_vs_billboard, s_fs_billboard);
	s_gl.mesh3d_pipe.prog     = create_program_async(s_vs_mesh3d, s_fs_mesh3d);
	s_gl.cloud_pipe.prog      = create_program_async(s_vs_cloud, s_fs_cloud);
	s_gl.cloud_comp_pipe.prog = create_program_async(s_vs_csm, s_fs_cloud_comp);
	s_gl.csm_pipe.prog        = create_program_async(s_vs_csm, s_fs_csm);
	init_grid_buffers();
	s_gl.initialized = true;
	s_gl.pipelines_ready = false;
}

static bool check_pipelines_ready(void)
{
	if (s_gl.pipelines_ready)
		return true;
	if (!s_gl.initialized)
		return false;

	if (s_gl.terrain_pipe.prog && !glGetProgramParameter(s_gl.terrain_pipe.prog, GL_COMPLETION_STATUS_KHR))
		return false;
	if (s_gl.water_pipe.prog && !glGetProgramParameter(s_gl.water_pipe.prog, GL_COMPLETION_STATUS_KHR))
		return false;
	if (s_gl.bb_pipe.prog && !glGetProgramParameter(s_gl.bb_pipe.prog, GL_COMPLETION_STATUS_KHR))
		return false;
	if (s_gl.mesh3d_pipe.prog && !glGetProgramParameter(s_gl.mesh3d_pipe.prog, GL_COMPLETION_STATUS_KHR))
		return false;
	if (s_gl.cloud_pipe.prog && !glGetProgramParameter(s_gl.cloud_pipe.prog, GL_COMPLETION_STATUS_KHR))
		return false;
	if (s_gl.cloud_comp_pipe.prog && !glGetProgramParameter(s_gl.cloud_comp_pipe.prog, GL_COMPLETION_STATUS_KHR))
		return false;
	if (s_gl.csm_pipe.prog && !glGetProgramParameter(s_gl.csm_pipe.prog, GL_COMPLETION_STATUS_KHR))
		return false;

	/* Terrain pipeline */
	if (s_gl.terrain_pipe.prog) {
		if (!glGetProgramParameter(s_gl.terrain_pipe.prog, GL_LINK_STATUS)) {
			glDeleteProgram(s_gl.terrain_pipe.prog);
			s_gl.terrain_pipe.prog = 0;
		} else {
			GLuint p = s_gl.terrain_pipe.prog;
			s_gl.terrain_pipe.uMVP              = glGetUniformLocation(p, "uMVP", 4);
			s_gl.terrain_pipe.uHeightTex        = glGetUniformLocation(p, "uHeightTex", 10);
			s_gl.terrain_pipe.uSubstrateTex     = glGetUniformLocation(p, "uSubstrateTex", 13);
			s_gl.terrain_pipe.uChunkOrigin      = glGetUniformLocation(p, "uChunkOrigin", 12);
			s_gl.terrain_pipe.uChunkRelOrigin   = glGetUniformLocation(p, "uChunkRelOrigin", 15);
			s_gl.terrain_pipe.uChunkSpan        = glGetUniformLocation(p, "uChunkSpan", 10);
			s_gl.terrain_pipe.uRefTrig          = glGetUniformLocation(p, "uRefTrig", 8);
			s_gl.terrain_pipe.uHeightScale      = glGetUniformLocation(p, "uHeightScale", 12);
			s_gl.terrain_pipe.uSeaLevel         = glGetUniformLocation(p, "uSeaLevel", 9);
			s_gl.terrain_pipe.uCameraPos        = glGetUniformLocation(p, "uCameraPos", 10);
			s_gl.terrain_pipe.uPlanetCenter     = glGetUniformLocation(p, "uPlanetCenter", 13);
			s_gl.terrain_pipe.uBodyRot          = glGetUniformLocation(p, "uBodyRot", 8);
			s_gl.terrain_pipe.uTorusRadii       = glGetUniformLocation(p, "uTorusRadii", 11);
			s_gl.terrain_pipe.uWorldDim         = glGetUniformLocation(p, "uWorldDim", 9);
			s_gl.terrain_pipe.uFogStart         = glGetUniformLocation(p, "uFogStart", 9);
			s_gl.terrain_pipe.uFogEnd           = glGetUniformLocation(p, "uFogEnd", 7);
			s_gl.terrain_pipe.uAnimTime         = glGetUniformLocation(p, "uAnimTime", 9);
			s_gl.terrain_pipe.uSkyColor         = glGetUniformLocation(p, "uSkyColor", 9);
			s_gl.terrain_pipe.uAtmoIntensity    = glGetUniformLocation(p, "uAtmoIntensity", 14);
			s_gl.terrain_pipe.uLambLight        = glGetUniformLocation(p, "uLambLight", 10);
			s_gl.terrain_pipe.uCloudShadowMap   = glGetUniformLocation(p, "uCloudShadowMap", 15);
			s_gl.terrain_pipe.uCsm              = glGetUniformLocation(p, "uCsm", 4);
			s_gl.terrain_pipe.uBaseSeaLevel     = glGetUniformLocation(p, "uBaseSeaLevel", 13);
			s_gl.terrain_pipe.uSunDir           = glGetUniformLocation(p, "uSunDir", 7);
			s_gl.terrain_pipe.uMoonDir          = glGetUniformLocation(p, "uMoonDir", 8);
			s_gl.terrain_pipe.uLambWakeHead     = glGetUniformLocation(p, "uLambWakeHead", 13);
			s_gl.terrain_pipe.uLambWakeXYT0     = glGetUniformLocation(p, "uLambWakeXYT0", 13);
			s_gl.terrain_pipe.uLambWakeXYT1     = glGetUniformLocation(p, "uLambWakeXYT1", 13);
			s_gl.terrain_pipe.uLambWakeVel0     = glGetUniformLocation(p, "uLambWakeVel0", 13);
			s_gl.terrain_pipe.uLambWakeVel1     = glGetUniformLocation(p, "uLambWakeVel1", 13);
			s_gl.terrain_pipe.uWorldId          = glGetUniformLocation(p, "uWorldId", 8);
			s_gl.terrain_pipe.uSeaOffsetQ12_4   = glGetUniformLocation(p, "uSeaOffsetQ12_4", 15);
			s_gl.terrain_pipe.uTreeShadowTex    = glGetUniformLocation(p, "uTreeShadowTex", 14);
			s_gl.terrain_pipe.uTreeShadowOrigin = glGetUniformLocation(p, "uTreeShadowOrigin", 17);
			s_gl.terrain_pipe.uTreeShadowVP     = glGetUniformLocation(p, "uTreeShadowVP", 13);
			s_gl.terrain_pipe.uTreeShadowActive = glGetUniformLocation(p, "uTreeShadowActive", 17);
			s_gl.terrain_pipe.aGridUV           = glGetAttribLocation(p, "a_grid_uv", 9);
		}
	}

	/* Water pipeline */
	if (s_gl.water_pipe.prog) {
		if (!glGetProgramParameter(s_gl.water_pipe.prog, GL_LINK_STATUS)) {
			glDeleteProgram(s_gl.water_pipe.prog);
			s_gl.water_pipe.prog = 0;
		} else {
			GLuint p = s_gl.water_pipe.prog;
			s_gl.water_pipe.uMVP            = glGetUniformLocation(p, "uMVP", 4);
			s_gl.water_pipe.uHeightTex      = glGetUniformLocation(p, "uHeightTex", 10);
			s_gl.water_pipe.uSubstrateTex   = glGetUniformLocation(p, "uSubstrateTex", 13);
			s_gl.water_pipe.uChunkOrigin    = glGetUniformLocation(p, "uChunkOrigin", 12);
			s_gl.water_pipe.uChunkRelOrigin = glGetUniformLocation(p, "uChunkRelOrigin", 15);
			s_gl.water_pipe.uChunkSpan      = glGetUniformLocation(p, "uChunkSpan", 10);
			s_gl.water_pipe.uRefTrig        = glGetUniformLocation(p, "uRefTrig", 8);
			s_gl.water_pipe.uHeightScale    = glGetUniformLocation(p, "uHeightScale", 12);
			s_gl.water_pipe.uSeaLevel       = glGetUniformLocation(p, "uSeaLevel", 9);
			s_gl.water_pipe.uCameraPos      = glGetUniformLocation(p, "uCameraPos", 10);
			s_gl.water_pipe.uPlanetCenter   = glGetUniformLocation(p, "uPlanetCenter", 13);
			s_gl.water_pipe.uBodyRot        = glGetUniformLocation(p, "uBodyRot", 8);
			s_gl.water_pipe.uTorusRadii     = glGetUniformLocation(p, "uTorusRadii", 11);
			s_gl.water_pipe.uWorldDim       = glGetUniformLocation(p, "uWorldDim", 9);
			s_gl.water_pipe.uFogStart       = glGetUniformLocation(p, "uFogStart", 9);
			s_gl.water_pipe.uFogEnd         = glGetUniformLocation(p, "uFogEnd", 7);
			s_gl.water_pipe.uAnimTime       = glGetUniformLocation(p, "uAnimTime", 9);
			s_gl.water_pipe.uSkyColor       = glGetUniformLocation(p, "uSkyColor", 9);
			s_gl.water_pipe.uAtmoIntensity  = glGetUniformLocation(p, "uAtmoIntensity", 14);
			s_gl.water_pipe.uLambLight      = glGetUniformLocation(p, "uLambLight", 10);
			s_gl.water_pipe.uCloudShadowMap = glGetUniformLocation(p, "uCloudShadowMap", 15);
			s_gl.water_pipe.uCsm            = glGetUniformLocation(p, "uCsm", 4);
			s_gl.water_pipe.uBaseSeaLevel   = glGetUniformLocation(p, "uBaseSeaLevel", 13);
			s_gl.water_pipe.uSunDir         = glGetUniformLocation(p, "uSunDir", 7);
			s_gl.water_pipe.uMoonDir        = glGetUniformLocation(p, "uMoonDir", 8);
			s_gl.water_pipe.uLambWakeHead   = glGetUniformLocation(p, "uLambWakeHead", 13);
			s_gl.water_pipe.uLambWakeXYT0   = glGetUniformLocation(p, "uLambWakeXYT0", 13);
			s_gl.water_pipe.uLambWakeXYT1   = glGetUniformLocation(p, "uLambWakeXYT1", 13);
			s_gl.water_pipe.uLambWakeVel0   = glGetUniformLocation(p, "uLambWakeVel0", 13);
			s_gl.water_pipe.uLambWakeVel1   = glGetUniformLocation(p, "uLambWakeVel1", 13);
			s_gl.water_pipe.uSceneColor     = glGetUniformLocation(p, "uSceneColor", 11);
			s_gl.water_pipe.uSceneDepth     = glGetUniformLocation(p, "uSceneDepth", 11);
			s_gl.water_pipe.uViewport       = glGetUniformLocation(p, "uViewport", 9);
			s_gl.water_pipe.uNearFar        = glGetUniformLocation(p, "uNearFar", 8);
			s_gl.water_pipe.uProjScale      = glGetUniformLocation(p, "uProjScale", 10);
			s_gl.water_pipe.aGridUV         = glGetAttribLocation(p, "a_grid_uv", 9);
		}
	}

	/* Billboard pipeline */
	if (s_gl.bb_pipe.prog) {
		if (!glGetProgramParameter(s_gl.bb_pipe.prog, GL_LINK_STATUS)) {
			glDeleteProgram(s_gl.bb_pipe.prog);
			s_gl.bb_pipe.prog = 0;
		} else {
			GLuint p = s_gl.bb_pipe.prog;
			s_gl.bb_pipe.uMVP           = glGetUniformLocation(p, "uMVP", 4);
			s_gl.bb_pipe.uAtlasTex      = glGetUniformLocation(p, "uAtlasTex", 9);
			s_gl.bb_pipe.uAtlasNormTex  = glGetUniformLocation(p, "uAtlasNormTex", 13);
			s_gl.bb_pipe.uSeaLevel      = glGetUniformLocation(p, "uSeaLevel", 9);
			s_gl.bb_pipe.uAnimTime      = glGetUniformLocation(p, "uAnimTime", 9);
			s_gl.bb_pipe.uCameraPos     = glGetUniformLocation(p, "uCameraPos", 10);
			s_gl.bb_pipe.uFogStart      = glGetUniformLocation(p, "uFogStart", 9);
			s_gl.bb_pipe.uFogEnd        = glGetUniformLocation(p, "uFogEnd", 7);
			s_gl.bb_pipe.uSkyColor      = glGetUniformLocation(p, "uSkyColor", 9);
			s_gl.bb_pipe.uAtmoIntensity = glGetUniformLocation(p, "uAtmoIntensity", 14);
			s_gl.bb_pipe.uLambLight     = glGetUniformLocation(p, "uLambLight", 10);
			s_gl.bb_pipe.uSunDir        = glGetUniformLocation(p, "uSunDir", 7);
			s_gl.bb_pipe.uCloudShadowMap = glGetUniformLocation(p, "uCloudShadowMap", 15);
			s_gl.bb_pipe.uCsm           = glGetUniformLocation(p, "uCsm", 4);
			s_gl.bb_pipe.uPass          = glGetUniformLocation(p, "uPass", 5);
			s_gl.bb_pipe.uHiZTex        = glGetUniformLocation(p, "uHiZTex", 7);
			s_gl.bb_pipe.uHiZActive     = glGetUniformLocation(p, "uHiZActive", 10);
			s_gl.bb_pipe.aPos           = glGetAttribLocation(p, "a_pos", 5);
			s_gl.bb_pipe.aUV            = glGetAttribLocation(p, "a_uv", 4);
			s_gl.bb_pipe.aTint          = glGetAttribLocation(p, "a_tint", 6);
			s_gl.bb_pipe.aLocalPos      = glGetAttribLocation(p, "a_local_pos", 11);
			s_gl.bb_pipe.aQuadUV        = glGetAttribLocation(p, "a_quad_uv", 9);
			s_gl.bb_pipe.aSpriteBounds  = glGetAttribLocation(p, "a_sprite_bounds", 15);
		}
	}

	/* 3D Mesh Instanced pipeline */
	if (s_gl.mesh3d_pipe.prog) {
		if (!glGetProgramParameter(s_gl.mesh3d_pipe.prog, GL_LINK_STATUS)) {
			glDeleteProgram(s_gl.mesh3d_pipe.prog);
			s_gl.mesh3d_pipe.prog = 0;
		} else {
			GLuint p = s_gl.mesh3d_pipe.prog;
			s_gl.mesh3d_pipe.uMVP                   = glGetUniformLocation(p, "uMVP", 4);
			s_gl.mesh3d_pipe.uSeaLevel              = glGetUniformLocation(p, "uSeaLevel", 9);
			s_gl.mesh3d_pipe.uAnimTime              = glGetUniformLocation(p, "uAnimTime", 9);
			s_gl.mesh3d_pipe.uCameraPos             = glGetUniformLocation(p, "uCameraPos", 10);
			s_gl.mesh3d_pipe.uFogStart              = glGetUniformLocation(p, "uFogStart", 9);
			s_gl.mesh3d_pipe.uFogEnd                = glGetUniformLocation(p, "uFogEnd", 7);
			s_gl.mesh3d_pipe.uSkyColor              = glGetUniformLocation(p, "uSkyColor", 9);
			s_gl.mesh3d_pipe.uAtmoIntensity         = glGetUniformLocation(p, "uAtmoIntensity", 14);
			s_gl.mesh3d_pipe.uLambLight             = glGetUniformLocation(p, "uLambLight", 10);
			s_gl.mesh3d_pipe.uSunDir                = glGetUniformLocation(p, "uSunDir", 7);
			s_gl.mesh3d_pipe.uCloudShadowMap        = glGetUniformLocation(p, "uCloudShadowMap", 15);
			s_gl.mesh3d_pipe.uCsm                   = glGetUniformLocation(p, "uCsm", 4);
			s_gl.mesh3d_pipe.uShadowPass            = glGetUniformLocation(p, "uShadowPass", 11);
			s_gl.mesh3d_pipe.uBranchletTex          = glGetUniformLocation(p, "uBranchletTex", 13);
		s_gl.mesh3d_pipe.uBranchletNorm          = glGetUniformLocation(p, "uBranchletNorm", 14);
			s_gl.mesh3d_pipe.uTreeShadowTex         = glGetUniformLocation(p, "uTreeShadowTex", 14);
			s_gl.mesh3d_pipe.uTreeShadowOrigin      = glGetUniformLocation(p, "uTreeShadowOrigin", 17);
			s_gl.mesh3d_pipe.uTreeShadowVP          = glGetUniformLocation(p, "uTreeShadowVP", 13);
			s_gl.mesh3d_pipe.uTreeShadowActive      = glGetUniformLocation(p, "uTreeShadowActive", 17);
			s_gl.mesh3d_pipe.uTreeShadowWriteOrigin = glGetUniformLocation(p, "uTreeShadowWriteOrigin", 22);
			s_gl.mesh3d_pipe.uTreeShadowWriteVP     = glGetUniformLocation(p, "uTreeShadowWriteVP", 18);
		}
	}

	/* Cloud pipeline */
	if (s_gl.cloud_pipe.prog) {
		if (!glGetProgramParameter(s_gl.cloud_pipe.prog, GL_LINK_STATUS)) {
			glDeleteProgram(s_gl.cloud_pipe.prog);
			s_gl.cloud_pipe.prog = 0;
		} else {
			GLuint p = s_gl.cloud_pipe.prog;
			s_gl.cloud_pipe.uMVP            = glGetUniformLocation(p, "uMVP", 4);
			s_gl.cloud_pipe.uHeightTex      = glGetUniformLocation(p, "uHeightTex", 10);
			s_gl.cloud_pipe.uSubstrateTex   = glGetUniformLocation(p, "uSubstrateTex", 13);
			s_gl.cloud_pipe.uChunkOrigin    = glGetUniformLocation(p, "uChunkOrigin", 12);
			s_gl.cloud_pipe.uChunkRelOrigin = glGetUniformLocation(p, "uChunkRelOrigin", 15);
			s_gl.cloud_pipe.uChunkSpan      = glGetUniformLocation(p, "uChunkSpan", 10);
			s_gl.cloud_pipe.uRefTrig        = glGetUniformLocation(p, "uRefTrig", 8);
			s_gl.cloud_pipe.uSeaLevel       = glGetUniformLocation(p, "uSeaLevel", 9);
			s_gl.cloud_pipe.uCameraPos      = glGetUniformLocation(p, "uCameraPos", 10);
			s_gl.cloud_pipe.uPlanetCenter   = glGetUniformLocation(p, "uPlanetCenter", 13);
			s_gl.cloud_pipe.uBodyRot        = glGetUniformLocation(p, "uBodyRot", 8);
			s_gl.cloud_pipe.uTorusRadii     = glGetUniformLocation(p, "uTorusRadii", 11);
			s_gl.cloud_pipe.uWorldDim       = glGetUniformLocation(p, "uWorldDim", 9);
			s_gl.cloud_pipe.uFogStart       = glGetUniformLocation(p, "uFogStart", 9);
			s_gl.cloud_pipe.uFogEnd         = glGetUniformLocation(p, "uFogEnd", 7);
			s_gl.cloud_pipe.uAnimTime       = glGetUniformLocation(p, "uAnimTime", 9);
			s_gl.cloud_pipe.uSkyColor       = glGetUniformLocation(p, "uSkyColor", 9);
			s_gl.cloud_pipe.uAtmoIntensity  = glGetUniformLocation(p, "uAtmoIntensity", 14);
			s_gl.cloud_pipe.uCamDist        = glGetUniformLocation(p, "uCamDist", 8);
			s_gl.cloud_pipe.uCloudDeck      = glGetUniformLocation(p, "uCloudDeck", 10);
			s_gl.cloud_pipe.uSunDir               = glGetUniformLocation(p, "uSunDir", 7);
			s_gl.cloud_pipe.uCloudShadowMap       = glGetUniformLocation(p, "uCloudShadowMap", 15);
			s_gl.cloud_pipe.uCloudShadowMapHistory = glGetUniformLocation(p, "uCloudShadowMapHistory", 22);
			s_gl.cloud_pipe.uCsm                  = glGetUniformLocation(p, "uCsm", 4);
			s_gl.cloud_pipe.uPrevCsm              = glGetUniformLocation(p, "uPrevCsm", 8);
			s_gl.cloud_pipe.uPrevSunDir           = glGetUniformLocation(p, "uPrevSunDir", 11);
			s_gl.cloud_pipe.uCsmConfidence        = glGetUniformLocation(p, "uCsmConfidence", 14);
			s_gl.cloud_pipe.uCsmJitterDelta       = glGetUniformLocation(p, "uCsmJitterDelta", 15);
			s_gl.cloud_pipe.uMoonDir              = glGetUniformLocation(p, "uMoonDir", 8);
			s_gl.cloud_pipe.uViewport             = glGetUniformLocation(p, "uViewport", 9);
			s_gl.cloud_pipe.uClusterGridDim       = glGetUniformLocation(p, "uClusterGridDim", 15);
			s_gl.cloud_pipe.uClusterDepthParams   = glGetUniformLocation(p, "uClusterDepthParams", 19);
			s_gl.cloud_pipe.uClusterLightCount    = glGetUniformLocation(p, "uClusterLightCount", 18);
			s_gl.cloud_pipe.uClusterLightData     = glGetUniformLocation(p, "uClusterLightData", 17);
			s_gl.cloud_pipe.uClusterLightListsTex = glGetUniformLocation(p, "uClusterLightListsTex", 21);
			s_gl.cloud_pipe.aGridUV               = glGetAttribLocation(p, "a_grid_uv", 9);
		}
	}

	/* Cloud half-res composite pipeline */
	if (s_gl.cloud_comp_pipe.prog) {
		if (!glGetProgramParameter(s_gl.cloud_comp_pipe.prog, GL_LINK_STATUS)) {
			glDeleteProgram(s_gl.cloud_comp_pipe.prog);
			s_gl.cloud_comp_pipe.prog = 0;
		} else {
			GLuint p = s_gl.cloud_comp_pipe.prog;
			s_gl.cloud_comp_pipe.uCloudTex = glGetUniformLocation(p, "uCloudTex", 9);
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

	s_gl.pipelines_ready = true;
	return true;
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


void nd_seven_gl_begin_frame(uint32_t stage_w, uint32_t stage_h,
                             float tx, float ty, float tz,
                             float dist, float pitch_deg, float fov_deg,
                             float anim_time,
                             const nd_planet_def_t *pdef)
{
	s_gl.frame_count++;
	s_gl.water_count = 0;
	s_gl.cloud_count = 0;
	s_gl.sprites_in_water = false;
	s_gl.water_culled_occ2d = 0;
	s_gl.cloud_culled_hiz = 0;
	s_gl.billboard_culled_hiz = 0;
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

	float pitch_rad = pitch_deg * (PI_F / 180.0f);
	float cos_p = cosf(pitch_rad);
	float sin_p = sinf(pitch_rad);
	float d = s_gl.cur_dist;

	s_gl.cur_target[0] = tz * N[0];
	s_gl.cur_target[1] = tz * N[1];
	s_gl.cur_target[2] = tz * N[2];

	s_gl.cur_eye[0] = s_gl.cur_target[0] + d * (cos_p * S[0] + sin_p * N[0]);
	s_gl.cur_eye[1] = s_gl.cur_target[1] + d * (cos_p * S[1] + sin_p * N[1]);
	s_gl.cur_eye[2] = s_gl.cur_target[2] + d * (cos_p * S[2] + sin_p * N[2]);

	s_gl.cur_up[0] = -sin_p * S[0] + cos_p * N[0];
	s_gl.cur_up[1] = -sin_p * S[1] + cos_p * N[1];
	s_gl.cur_up[2] = -sin_p * S[2] + cos_p * N[2];

	float modelview[16], proj[16];
	ar_mat4_lookat_lh(modelview, s_gl.cur_eye, s_gl.cur_target, s_gl.cur_up);
	float fov_rad = fov_deg * (PI_F / 180.0f);
	float aspect = (stage_h > 0) ? ((float)stage_w / (float)stage_h) : 1.333333f;
	float near_z = (d * 0.005f > 2.0f) ? (d * 0.005f) : 2.0f;
	float far_z = (d * 20.0f > 80000000.0f) ? (d * 20.0f) : 80000000.0f;
	ar_mat4_perspective(proj, fov_rad, aspect, near_z, far_z);

	s_gl.csm_prev_jitter_x = s_gl.csm_jitter_x;
	s_gl.csm_prev_jitter_y = s_gl.csm_jitter_y;
	s_gl.csm_jitter_x = 0.0f;
	s_gl.csm_jitter_y = 0.0f;

	s_gl.cur_near = near_z;
	s_gl.cur_far = far_z;
	s_gl.cur_tan_half = tanf(fov_rad * 0.5f);
	s_gl.cur_aspect = aspect;
	ar_mat4_multiply(s_gl.cur_mvp, proj, modelview);
	ar_frustum_from_matrix(&s_gl.cur_frustum, s_gl.cur_mvp);

	if (!s_gl.initialized) init_pipelines();
	(void)check_pipelines_ready();

	s_gl.stage_w = stage_w;
	s_gl.stage_h = stage_h;
	s_gl.frame_open = true;
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(0, 0, (GLsizei)stage_w, (GLsizei)stage_h);
	if (s_gl.pipelines_ready)
		gl_ensure_scene_targets(stage_w, stage_h);
	gl_bind_scene_target();
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glDepthMask(GL_TRUE);
	glDisable(GL_CULL_FACE);

	float space_t = fminf(1.0f, fmaxf(0.0f, (s_gl.cur_dist - 12000.0f) / 40000.0f));
	float sky_r = 0.55f * (1.0f - space_t) + 0.012f * space_t;
	float sky_g = 0.70f * (1.0f - space_t) + 0.016f * space_t;
	float sky_b = 0.85f * (1.0f - space_t) + 0.035f * space_t;

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
}

static void select_grid_buffers(uint8_t level, int32_t ox, int32_t oy, int32_t span_w, int32_t span_h,
                                GLuint *out_vbo, GLuint *out_ibo, GLsizei *out_indices)
{
	/* Compute distance from camera focal point to nearest point on chunk box */
	float min_x = (float)ox, max_x = (float)(ox + span_w);
	float min_y = (float)oy, max_y = (float)(oy + span_h);
	float dx = 0.0f, dy = 0.0f;
	if (s_gl.cur_tx < min_x) dx = min_x - s_gl.cur_tx;
	else if (s_gl.cur_tx > max_x) dx = s_gl.cur_tx - max_x;
	if (s_gl.cur_ty < min_y) dy = min_y - s_gl.cur_ty;
	else if (s_gl.cur_ty > max_y) dy = s_gl.cur_ty - max_y;
	float eff_dist = sqrtf(s_gl.cur_dist * s_gl.cur_dist + dx * dx + dy * dy);

	uint32_t quads = nd_seven_gl_grid_quads_for_zoom(level, eff_dist);
	if (quads >= GL_GRID_QUADS_LO) {
		/* Always use the 48x48 grid (exact 1:1 texel alignment with 49x49 chunk heightmaps)
		 * for all near/mid chunks (Level 0..2), so 2:1 LOD borders share exact vertex stations. */
		*out_vbo = s_gl.grid_vbo_hi;
		*out_ibo = s_gl.grid_ibo_hi;
		*out_indices = GL_TOTAL_INDICES_HI;
	} else {
		*out_vbo = s_gl.grid_vbo_macro;
		*out_ibo = s_gl.grid_ibo_macro;
		*out_indices = GL_TOTAL_INDICES_MACRO;
	}
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
	if (!s_gl.initialized || !s_gl.pipelines_ready || !s_gl.terrain_pipe.prog) return;
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

	/* Find or allocate chunk textures in LRU cache */
	int slot_idx = -1;
	int oldest_idx = 0;
	uint32_t oldest_touch = 0xFFFFFFFFu;

	for (int i = 0; i < MAX_CHUNK_TEXTURES; i++) {
		if (s_gl.chunk_cache[i].active &&
		    s_gl.chunk_cache[i].world == world &&
		    s_gl.chunk_cache[i].level == level &&
		    s_gl.chunk_cache[i].ox == ox &&
		    s_gl.chunk_cache[i].oy == oy &&
		    s_gl.chunk_cache[i].span_w == span_w &&
		    s_gl.chunk_cache[i].span_h == span_h) {
			slot_idx = i;
			break;
		}
		if (!s_gl.chunk_cache[i].active && slot_idx < 0) {
			slot_idx = i;
		}
		if (s_gl.chunk_cache[i].last_touch < oldest_touch) {
			oldest_touch = s_gl.chunk_cache[i].last_touch;
			oldest_idx = i;
		}
	}

	if (slot_idx < 0) slot_idx = oldest_idx;
	gl_chunk_tex_t *slot = &s_gl.chunk_cache[slot_idx];

	if (!slot->height_tex) {
		glActiveTexture(GL_TEXTURE0);
		slot->height_tex = glCreateTexture();
		glBindTexture(GL_TEXTURE_2D, slot->height_tex);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	}
	if (!slot->substrate_tex) {
		glActiveTexture(GL_TEXTURE1);
		slot->substrate_tex = glCreateTexture();
		glBindTexture(GL_TEXTURE_2D, slot->substrate_tex);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	}

	bool needs_upload = (!slot->active || slot->rev != rev ||
	                     slot->world != world || slot->level != level ||
	                     slot->ox != ox || slot->oy != oy ||
	                     slot->span_w != span_w || slot->span_h != span_h ||
	                     slot->tex_w != chunk_w || slot->tex_h != chunk_h);

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

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, slot->height_tex);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_R16I, chunk_w, chunk_h, 0, GL_RED_INTEGER, GL_SHORT, height_i16);

		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, slot->substrate_tex);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, chunk_w, chunk_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, color_rgba);
	}
	slot->last_touch = s_gl.frame_count;

	double p0_chunk[3];
	torus_frame_double((double)s_gl.cur_tx, (double)s_gl.cur_ty, 0.0, eff_pdef, p0_chunk, NULL, NULL, NULL);
	float rel_center[3] = {
		(float)(p0_chunk[0] - s_gl.cur_origin[0]),
		(float)(p0_chunk[1] - s_gl.cur_origin[1]),
		(float)(p0_chunk[2] - s_gl.cur_origin[2])
	};
	bool has_rot = (eff_pdef->rot[0] != 0.0f || eff_pdef->rot[4] != 0.0f || eff_pdef->rot[8] != 0.0f);
	float rot_mat4[16] = {
		has_rot ? eff_pdef->rot[0] : 1.0f, has_rot ? eff_pdef->rot[3] : 0.0f, has_rot ? eff_pdef->rot[6] : 0.0f, 0.0f,
		has_rot ? eff_pdef->rot[1] : 0.0f, has_rot ? eff_pdef->rot[4] : 1.0f, has_rot ? eff_pdef->rot[7] : 0.0f, 0.0f,
		has_rot ? eff_pdef->rot[2] : 0.0f, has_rot ? eff_pdef->rot[5] : 0.0f, has_rot ? eff_pdef->rot[8] : 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f
	};

	/* Draw terrain chunk mesh */
	glUseProgram(s_gl.terrain_pipe.prog);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, slot->height_tex);
	glUniform1i(s_gl.terrain_pipe.uHeightTex, 0);

	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, slot->substrate_tex);
	glUniform1i(s_gl.terrain_pipe.uSubstrateTex, 1);
	gl_bind_csm(s_gl.terrain_pipe.uCloudShadowMap, s_gl.terrain_pipe.uCsm);
	gl_bind_tree_shadow_read(s_gl.terrain_pipe.uTreeShadowTex,
	                         s_gl.terrain_pipe.uTreeShadowOrigin,
	                         s_gl.terrain_pipe.uTreeShadowVP,
	                         s_gl.terrain_pipe.uTreeShadowActive);

	glUniformMatrix4fv(s_gl.terrain_pipe.uMVP, 1, GL_FALSE, s_gl.cur_mvp);
	glUniform2f(s_gl.terrain_pipe.uChunkOrigin, (float)ox, (float)oy);
	glUniform2f(s_gl.terrain_pipe.uChunkRelOrigin,
	            (float)((double)ox - (double)s_gl.cur_tx),
	            (float)((double)oy - (double)s_gl.cur_ty));
	glUniform2f(s_gl.terrain_pipe.uChunkSpan, (float)span_w, (float)span_h);
	glUniform4f(s_gl.terrain_pipe.uRefTrig,
	            s_gl.cur_ref_trig[0], s_gl.cur_ref_trig[1],
	            s_gl.cur_ref_trig[2], s_gl.cur_ref_trig[3]);
	glUniform1f(s_gl.terrain_pipe.uHeightScale, 1.0f);
	glUniform1f(s_gl.terrain_pipe.uSeaLevel, terrain_sea_lvl);
	glUniform3fv(s_gl.terrain_pipe.uCameraPos, 1, s_gl.cur_eye);
	glUniform3fv(s_gl.terrain_pipe.uPlanetCenter, 1, rel_center);
	glUniformMatrix4fv(s_gl.terrain_pipe.uBodyRot, 1, GL_FALSE, rot_mat4);
	glUniform2f(s_gl.terrain_pipe.uTorusRadii, eff_pdef->major_r, eff_pdef->minor_r);
	glUniform2f(s_gl.terrain_pipe.uWorldDim, ND_SEVEN_WORLD_DIM_W, ND_SEVEN_WORLD_DIM_H);

	float space_t = fminf(1.0f, fmaxf(0.0f, (s_gl.cur_dist - 12000.0f) / 40000.0f));
	/* In deep space (space_t -> 1), push distance fog out to infinity so distant celestial bodies
	 * (like the Moon behind the main planet) remain crisp and un-fogged against space */
	float fog_scale = 1.0f + space_t * 250.0f;
	glUniform1f(s_gl.terrain_pipe.uFogStart, s_gl.cur_dist * 1.5f * fog_scale);
	glUniform1f(s_gl.terrain_pipe.uFogEnd, s_gl.cur_dist * 4.0f * fog_scale);
	glUniform1f(s_gl.terrain_pipe.uAnimTime, s_gl.cur_anim_time);

	glUniform3f(s_gl.terrain_pipe.uSkyColor,
	            0.55f * (1.0f - space_t) + 0.012f * space_t,
	            0.70f * (1.0f - space_t) + 0.016f * space_t,
	            0.85f * (1.0f - space_t) + 0.035f * space_t);
	glUniform1f(s_gl.terrain_pipe.uAtmoIntensity, fminf(1.0f, fmaxf(0.0f, (s_gl.cur_dist - 1200.0f) / 8000.0f)));
	glUniform4f(s_gl.terrain_pipe.uLambLight,
	            s_gl.cur_lamb_light[0], s_gl.cur_lamb_light[1],
	            s_gl.cur_lamb_light[2], s_gl.cur_lamb_light[3]);
	glUniform1f(s_gl.terrain_pipe.uBaseSeaLevel, s_gl.cur_base_sea_level);
	glUniform3fv(s_gl.terrain_pipe.uSunDir, 1, s_gl.cur_sun_dir);
	glUniform3fv(s_gl.terrain_pipe.uMoonDir, 1, s_gl.cur_moon_dir);
	glUniform4f(s_gl.terrain_pipe.uLambWakeHead,
	            s_gl.cur_wake_head[0], s_gl.cur_wake_head[1],
	            s_gl.cur_wake_head[2], s_gl.cur_wake_head[3]);
	glUniformMatrix4fv(s_gl.terrain_pipe.uLambWakeXYT0, 1, GL_FALSE, &s_gl.cur_wake_xyt[0][0]);
	glUniformMatrix4fv(s_gl.terrain_pipe.uLambWakeXYT1, 1, GL_FALSE, &s_gl.cur_wake_xyt[4][0]);
	glUniformMatrix4fv(s_gl.terrain_pipe.uLambWakeVel0, 1, GL_FALSE, &s_gl.cur_wake_vel[0][0]);
	glUniformMatrix4fv(s_gl.terrain_pipe.uLambWakeVel1, 1, GL_FALSE, &s_gl.cur_wake_vel[4][0]);

	/* Pass canonical world ID and sea_offset_q12_4 for GPU canonical field evaluation */
	{
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
		glUniform1i(s_gl.terrain_pipe.uWorldId, (GLint)world);
		glUniform1i(s_gl.terrain_pipe.uSeaOffsetQ12_4, (GLint)s_cached_gl_sea_offset);
	}

	GLuint active_vbo = 0, active_ibo = 0;
	GLsizei active_indices = 0;
	select_grid_buffers(level, ox, oy, span_w, span_h, &active_vbo, &active_ibo, &active_indices);
	GLsizei draw_indices = active_indices + ((active_indices == GL_TOTAL_INDICES_HI)
	                                         ? (4 * GL_GRID_QUADS_HI * 6)
	                                         : (4 * GL_GRID_QUADS_MACRO * 6));

	glBindBuffer(GL_ARRAY_BUFFER, active_vbo);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8, 0);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, active_ibo);
	glDrawElements(GL_TRIANGLES, draw_indices, GL_UNSIGNED_SHORT, 0);
	if (s_gl.indirect_cmd_count < ND_GPU_INDIRECT_MAX) {
		nd_gpu_draw_elements_indirect_cmd_t *cmd = &s_gl.indirect_cmds[s_gl.indirect_cmd_count++];
		cmd->count = (uint32_t)draw_indices;
		cmd->instance_count = 1u;
		cmd->first_index = 0u;
		cmd->base_vertex = 0;
		cmd->base_instance = s_gl.indirect_cmd_count - 1u;
	}

	/* Queue volumetric cloud shell pass for this chunk (gated by 3D cloud-shell frustum,
	 * planetary limb visibility, zoom ramp, and synoptic clear-sky pre-cull) */
	if (s_gl.cloud_count < (uint32_t)nd_seven_gl_max_water_queue() &&
	    nd_seven_gl_should_queue_cloud_chunk(ox, oy, span_w, span_h, eff_pdef)) {
		
		/* Hi-Z occlusion test for cloud chunk (when CPU readback is populated) */
		if (s_gl.hiz_valid && s_occ2d_valid &&
		    nd_seven_hiz_test_cloud_chunk(ox, oy, span_w, span_h, s_gl.cur_mvp, 920.0f)) {
			s_gl.cloud_culled_hiz++;
		} else {
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
	}

	/* Queue the water surface of this chunk for the submerged-object pass */
	if (terrain_sea_lvl > -900.0f && s_gl.water_count < (uint32_t)nd_seven_gl_max_water_queue()) {
		/* Skip water chunks that are occluded (2D hierarchical occlusion map or 1D ridge test) */
		if (s_occ2d_valid && s_gl.hiz_valid) {
			if (nd_seven_occ2d_test_water_chunk(ox, oy, span_w, span_h, s_gl.cur_mvp, terrain_sea_lvl)) {
				s_gl.water_culled_occ2d++;
				return; /* Don't queue this water chunk */
			}
		} else if (nd_seven_is_water_ridge_occluded(ox, oy, span_w, span_h,
		                                            s_gl.cur_mvp, s_gl.cur_lamb_light[0], s_gl.cur_lamb_light[1],
		                                            s_gl.cur_pdef.minor_r)) {
			return; /* Don't queue this water chunk */
		}
		gl_water_chunk_t *wq = &s_gl.water_queue[s_gl.water_count++];
		wq->ox = ox;
		wq->oy = oy;
		wq->span_w = span_w;
		wq->span_h = span_h;
		wq->level = level;
		wq->sea_level = terrain_sea_lvl;
		wq->height_tex = slot->height_tex;
		wq->substrate_tex = slot->substrate_tex;
		wq->rel_center[0] = rel_center[0];
		wq->rel_center[1] = rel_center[1];
		wq->rel_center[2] = rel_center[2];
		for (int k = 0; k < 16; k++) wq->rot_mat4[k] = rot_mat4[k];
		wq->major_r = eff_pdef->major_r;
		wq->minor_r = eff_pdef->minor_r;
	}
}

void nd_seven_gl_flush_water(void)
{
	/* The terrain pass already shades water over the seabed. This pass composites only what stands BELOW the surface on top
	 * of that (sprites, marked in the stencil buffer): refraction of the resolved scene, true view-ray absorption from the
	 * depth difference, then the same air-water surface layer as the terrain pass. Skipped when nothing is submerged. */
	if (!s_gl.initialized || !s_gl.pipelines_ready || !s_gl.water_pipe.prog || s_gl.water_count == 0 ||
	    !s_gl.sprites_in_water || !s_gl.scene_ok || s_gl.cur_dist > 2000.0f) {
		s_gl.water_count = 0;
		return;
	}

	gl_resolve_scene(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glUseProgram(s_gl.water_pipe.prog);
	gl_bind_csm(s_gl.water_pipe.uCloudShadowMap, s_gl.water_pipe.uCsm);

	glActiveTexture(GL_TEXTURE0 + 4);
	glBindTexture(GL_TEXTURE_2D, s_gl.res_color_tex);
	glUniform1i(s_gl.water_pipe.uSceneColor, 4);
	glActiveTexture(GL_TEXTURE0 + 5);
	glBindTexture(GL_TEXTURE_2D, s_gl.res_depth_tex);
	glUniform1i(s_gl.water_pipe.uSceneDepth, 5);
	glActiveTexture(GL_TEXTURE0);
	glUniform2f(s_gl.water_pipe.uViewport, (float)s_gl.scene_w, (float)s_gl.scene_h);
	glUniform2f(s_gl.water_pipe.uNearFar, s_gl.cur_near, s_gl.cur_far);
	glUniform2f(s_gl.water_pipe.uProjScale, s_gl.cur_tan_half * s_gl.cur_aspect, s_gl.cur_tan_half);

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glDepthMask(GL_FALSE);
	glDisable(GL_BLEND);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
	glEnable(GL_STENCIL_TEST);
	glStencilMask(0);
	glStencilFunc(GL_EQUAL, 1, 0x01);
	glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);

	float zero_center[3] = { 0.0f, 0.0f, 0.0f };
	glUniformMatrix4fv(s_gl.water_pipe.uMVP, 1, GL_FALSE, s_gl.cur_mvp);
	glUniform4f(s_gl.water_pipe.uRefTrig,
	            s_gl.cur_ref_trig[0], s_gl.cur_ref_trig[1],
	            s_gl.cur_ref_trig[2], s_gl.cur_ref_trig[3]);
	glUniform1f(s_gl.water_pipe.uHeightScale, 1.0f);
	glUniform3fv(s_gl.water_pipe.uCameraPos, 1, s_gl.cur_eye);
	glUniform3fv(s_gl.water_pipe.uPlanetCenter, 1, zero_center);
	glUniform2f(s_gl.water_pipe.uTorusRadii, s_gl.cur_pdef.major_r, s_gl.cur_pdef.minor_r);
	glUniform2f(s_gl.water_pipe.uWorldDim, ND_SEVEN_WORLD_DIM_W, ND_SEVEN_WORLD_DIM_H);

	glUniform1f(s_gl.water_pipe.uFogStart, s_gl.cur_dist * 1.5f);
	glUniform1f(s_gl.water_pipe.uFogEnd, s_gl.cur_dist * 4.0f);
	glUniform1f(s_gl.water_pipe.uAnimTime, s_gl.cur_anim_time);

	float space_t = fminf(1.0f, fmaxf(0.0f, (s_gl.cur_dist - 12000.0f) / 40000.0f));
	glUniform3f(s_gl.water_pipe.uSkyColor,
	            0.55f * (1.0f - space_t) + 0.012f * space_t,
	            0.70f * (1.0f - space_t) + 0.016f * space_t,
	            0.85f * (1.0f - space_t) + 0.035f * space_t);
	glUniform1f(s_gl.water_pipe.uAtmoIntensity, fminf(1.0f, fmaxf(0.0f, (s_gl.cur_dist - 1200.0f) / 8000.0f)));
	glUniform4f(s_gl.water_pipe.uLambLight,
	            s_gl.cur_lamb_light[0], s_gl.cur_lamb_light[1],
	            s_gl.cur_lamb_light[2], s_gl.cur_lamb_light[3]);
	glUniform1f(s_gl.water_pipe.uBaseSeaLevel, s_gl.cur_base_sea_level);
	glUniform3fv(s_gl.water_pipe.uSunDir, 1, s_gl.cur_sun_dir);
	glUniform3fv(s_gl.water_pipe.uMoonDir, 1, s_gl.cur_moon_dir);
	glUniform4f(s_gl.water_pipe.uLambWakeHead,
	            s_gl.cur_wake_head[0], s_gl.cur_wake_head[1],
	            s_gl.cur_wake_head[2], s_gl.cur_wake_head[3]);
	glUniformMatrix4fv(s_gl.water_pipe.uLambWakeXYT0, 1, GL_FALSE, &s_gl.cur_wake_xyt[0][0]);
	glUniformMatrix4fv(s_gl.water_pipe.uLambWakeXYT1, 1, GL_FALSE, &s_gl.cur_wake_xyt[4][0]);
	glUniformMatrix4fv(s_gl.water_pipe.uLambWakeVel0, 1, GL_FALSE, &s_gl.cur_wake_vel[0][0]);
	glUniformMatrix4fv(s_gl.water_pipe.uLambWakeVel1, 1, GL_FALSE, &s_gl.cur_wake_vel[4][0]);

	for (uint32_t i = 0; i < s_gl.water_count; i++) {
		const gl_water_chunk_t *chk = &s_gl.water_queue[i];
		GLuint w_vbo = 0, w_ibo = 0;
		GLsizei w_indices = 0;
		select_grid_buffers(chk->level, chk->ox, chk->oy, chk->span_w, chk->span_h, &w_vbo, &w_ibo, &w_indices);

		glBindBuffer(GL_ARRAY_BUFFER, w_vbo);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8, 0);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, w_ibo);

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, chk->height_tex);
		glUniform1i(s_gl.water_pipe.uHeightTex, 0);

		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, chk->substrate_tex);
		glUniform1i(s_gl.water_pipe.uSubstrateTex, 1);

		glUniform3fv(s_gl.water_pipe.uPlanetCenter, 1, chk->rel_center);
		glUniformMatrix4fv(s_gl.water_pipe.uBodyRot, 1, GL_FALSE, chk->rot_mat4);
		glUniform2f(s_gl.water_pipe.uTorusRadii, chk->major_r, chk->minor_r);
		glUniform2f(s_gl.water_pipe.uChunkOrigin, (float)chk->ox, (float)chk->oy);
		glUniform2f(s_gl.water_pipe.uChunkRelOrigin,
		            (float)((double)chk->ox - (double)s_gl.cur_tx),
		            (float)((double)chk->oy - (double)s_gl.cur_ty));
		glUniform2f(s_gl.water_pipe.uChunkSpan, (float)chk->span_w, (float)chk->span_h);
		glUniform1f(s_gl.water_pipe.uSeaLevel, chk->sea_level);

		glDrawElements(GL_TRIANGLES, w_indices, GL_UNSIGNED_SHORT, 0);
		if (s_gl.indirect_cmd_count < ND_GPU_INDIRECT_MAX) {
			nd_gpu_draw_elements_indirect_cmd_t *cmd = &s_gl.indirect_cmds[s_gl.indirect_cmd_count++];
			cmd->count = (uint32_t)w_indices;
			cmd->instance_count = 1u;
			cmd->first_index = 0u;
			cmd->base_vertex = 0;
			cmd->base_instance = s_gl.indirect_cmd_count - 1u;
		}
	}

	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glDepthMask(GL_TRUE);
	glDisable(GL_STENCIL_TEST);
	glStencilMask(0xFF);
	glEnable(GL_BLEND);
	glActiveTexture(GL_TEXTURE0);
	s_gl.water_count = 0;
}
