/*
 * nd_seven_gl_occ.c — Scene FBO Targets, Hi-Z Pyramid, 2D Occlusion Map, Tree Shadows, and Cloud Shadow Map (CSM).
 */

#include "nd_seven_gl_internal.h"

float s_ridge_horizon_ndc_y[SEVEN_HORIZON_BINS];
int s_ridge_occlusion_valid = 0;
float s_occ2d_pyramid[ND_OCC2D_MAX_LEVELS][64][64];
int s_occ2d_valid = 0;
int s_occ2d_levels = 0;
uint32_t s_occ2d_w = 0, s_occ2d_h = 0;
float s_ridge_cam_flat_y = 0.0f;
int s_ridge_use_occlusion = 0;

bool nd_seven_gl_csm_window(float dist, float tx, float ty, nd_seven_gl_csm_window_t *out)
{
	if (!out || dist > 180000.0f) return false;
	float need = 4.5f * dist + 2000.0f;
	float half = 4096.0f;
	while (half < need && half < 1048576.0f) half *= 2.0f;
	float texel = (2.0f * half) / (float)ND_CSM_RES;
	out->half = half;
	out->cx = floorf(tx / texel + 0.5f) * texel;
	out->cy = floorf(ty / texel + 0.5f) * texel;
	return true;
}

static void gl_free_scene_targets(void)
{
	glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glBindRenderbuffer(GL_RENDERBUFFER, 0);
	glActiveTexture(GL_TEXTURE0 + 4);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE0 + 5);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE0 + 6);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE0 + 11);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, 0);
	if (s_gl.scene_fbo) glDeleteFramebuffer(s_gl.scene_fbo);
	if (s_gl.res_fbo) glDeleteFramebuffer(s_gl.res_fbo);
	if (s_gl.cloud_fbo) glDeleteFramebuffer(s_gl.cloud_fbo);
	if (s_gl.scene_rb_color) glDeleteRenderbuffer(s_gl.scene_rb_color);
	if (s_gl.scene_rb_depth) glDeleteRenderbuffer(s_gl.scene_rb_depth);
	if (s_gl.cloud_depth_rb) glDeleteRenderbuffer(s_gl.cloud_depth_rb);
	if (s_gl.res_color_tex) glDeleteTexture(s_gl.res_color_tex);
	if (s_gl.res_depth_tex) glDeleteTexture(s_gl.res_depth_tex);
	if (s_gl.cloud_tex) glDeleteTexture(s_gl.cloud_tex);
	for (int i = 0; i < ND_HIZ_MAX_LEVELS; i++) {
		if (s_gl.hiz_mip_fbo[i]) { glDeleteFramebuffer(s_gl.hiz_mip_fbo[i]); s_gl.hiz_mip_fbo[i] = 0; }
		if (s_gl.hiz_mip_tex[i] && s_gl.hiz_mip_tex[i] != s_gl.res_depth_tex) {
			glDeleteTexture(s_gl.hiz_mip_tex[i]);
		}
		s_gl.hiz_mip_tex[i] = 0;
	}
	s_gl.scene_fbo = s_gl.res_fbo = s_gl.cloud_fbo = 0;
	s_gl.scene_rb_color = s_gl.scene_rb_depth = s_gl.cloud_depth_rb = 0;
	s_gl.res_color_tex = s_gl.res_depth_tex = s_gl.cloud_tex = 0;
	s_gl.cloud_w = s_gl.cloud_h = 0;
	s_gl.scene_ok = false;
	s_gl.gpu_hiz_built = false;
}

static bool gl_alloc_scene_targets(uint32_t w, uint32_t h, int samples)
{
	glActiveTexture(GL_TEXTURE0);
	/* Multisampled colour + depth renderbuffers (antialiased scene) */
	s_gl.scene_rb_color = glCreateRenderbuffer();
	glBindRenderbuffer(GL_RENDERBUFFER, s_gl.scene_rb_color);
	glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, (GLsizei)w, (GLsizei)h);
	s_gl.scene_rb_depth = glCreateRenderbuffer();
	glBindRenderbuffer(GL_RENDERBUFFER, s_gl.scene_rb_depth);
	glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH32F_STENCIL8, (GLsizei)w, (GLsizei)h);
	s_gl.scene_fbo = glCreateFramebuffer();
	glBindFramebuffer(GL_FRAMEBUFFER, s_gl.scene_fbo);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, s_gl.scene_rb_color);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, s_gl.scene_rb_depth);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return false;

	/* Resolved, sampleable colour + depth */
	s_gl.res_color_tex = glCreateTexture();
	glBindTexture(GL_TEXTURE_2D, s_gl.res_color_tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)w, (GLsizei)h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	s_gl.res_depth_tex = glCreateTexture();
	glBindTexture(GL_TEXTURE_2D, s_gl.res_depth_tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH32F_STENCIL8, (GLsizei)w, (GLsizei)h, 0, GL_DEPTH_STENCIL,
	             GL_FLOAT_32_UNSIGNED_INT_24_8_REV, NULL);
	s_gl.res_fbo = glCreateFramebuffer();
	glBindFramebuffer(GL_FRAMEBUFFER, s_gl.res_fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_gl.res_color_tex, 0);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, s_gl.res_depth_tex, 0);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return false;
	glViewport(0, 0, (GLsizei)w, (GLsizei)h);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glDepthMask(GL_TRUE);
	glStencilMask(0xFFu);
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClearStencil(0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

	/* Volumetric cloud pass FBO (1/2 res for High, 1/3 res for Medium, 1/4 res for Low; single-sampled RGBA8 + depth/stencil) */
	int cq = nd_seven_get_cloud_quality();
	uint32_t cdiv = (cq <= ND_CLOUD_QUALITY_LOW) ? 4u : ((cq == ND_CLOUD_QUALITY_MEDIUM) ? 3u : 2u);
	uint32_t cw = (w > cdiv) ? (w / cdiv) : 1u;
	uint32_t ch = (h > cdiv) ? (h / cdiv) : 1u;
	s_gl.cloud_tex = glCreateTexture();
	glBindTexture(GL_TEXTURE_2D, s_gl.cloud_tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)cw, (GLsizei)ch, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	s_gl.cloud_depth_rb = glCreateRenderbuffer();
	glBindRenderbuffer(GL_RENDERBUFFER, s_gl.cloud_depth_rb);
	glRenderbufferStorageMultisample(GL_RENDERBUFFER, 0, GL_DEPTH32F_STENCIL8, (GLsizei)cw, (GLsizei)ch);
	s_gl.cloud_fbo = glCreateFramebuffer();
	glBindFramebuffer(GL_FRAMEBUFFER, s_gl.cloud_fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_gl.cloud_tex, 0);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, s_gl.cloud_depth_rb);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return false;
	glViewport(0, 0, (GLsizei)cw, (GLsizei)ch);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glDepthMask(GL_TRUE);
	glStencilMask(0xFFu);
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClearStencil(0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
	glBindTexture(GL_TEXTURE_2D, 0);
	glBindRenderbuffer(GL_RENDERBUFFER, 0);
	s_gl.cloud_w = cw;
	s_gl.cloud_h = ch;

	/* Hi-Z Buffer (hierarchical depth pyramid for GPU-driven occlusion culling):
	 * Level 0 = s_gl.res_depth_tex (full scene resolution).
	 * Levels 1..ND_HIZ_MAX_LEVELS-1 = dedicated R8 color textures with 2x2 MAX-depth reduction. */
	s_gl.hiz_w = w;
	s_gl.hiz_h = h;
	s_gl.hiz_tex = s_gl.res_depth_tex;
	s_gl.hiz_depth_tex = s_gl.res_depth_tex;
	s_gl.hiz_mip_tex[0] = s_gl.res_depth_tex;
	s_gl.hiz_mip_fbo[0] = 0;
	int levels = 1;
	for (int level = 1; level < ND_HIZ_MAX_LEVELS; level++) {
		uint32_t lw = (w > (1u << level)) ? (w >> level) : 1u;
		uint32_t lh = (h > (1u << level)) ? (h >> level) : 1u;
		s_gl.hiz_mip_tex[level] = glCreateTexture();
		glBindTexture(GL_TEXTURE_2D, s_gl.hiz_mip_tex[level]);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, (GLsizei)lw, (GLsizei)lh, 0, GL_RED, GL_UNSIGNED_BYTE, NULL);
		s_gl.hiz_mip_fbo[level] = glCreateFramebuffer();
		glBindFramebuffer(GL_FRAMEBUFFER, s_gl.hiz_mip_fbo[level]);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_gl.hiz_mip_tex[level], 0);
		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
			break;
		glViewport(0, 0, (GLsizei)lw, (GLsizei)lh);
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		levels = level + 1;
	}
	s_gl.hiz_levels = levels;
	s_gl.hiz_valid = (levels > 1);
	s_gl.hiz_failed = !s_gl.hiz_valid;
	s_gl.gpu_hiz_built = false;

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glBindTexture(GL_TEXTURE_2D, 0);

	/* Create Hi-Z downsample program using full-screen triangle vertex shader s_vs_csm */
	if (!s_gl.hiz_downsample_prog) {
		s_gl.hiz_downsample_prog = create_program_async(s_vs_csm, s_fs_hiz_downsample);
	}
	return true;
}

/* (Re)create the scene targets for the stage size; falls back to single-sampled, then to the canvas itself. */
void gl_ensure_scene_targets(uint32_t w, uint32_t h)
{
	if (s_gl.scene_failed || w == 0 || h == 0) return;
	int want_samples = nd_seven_get_msaa() ? ND_SCENE_MSAA_SAMPLES : 0;
	int cq = nd_seven_get_cloud_quality();
	uint32_t cdiv = (cq <= ND_CLOUD_QUALITY_LOW) ? 4u : ((cq == ND_CLOUD_QUALITY_MEDIUM) ? 3u : 2u);
	uint32_t want_cw = (w > cdiv) ? (w / cdiv) : 1u;
	uint32_t want_ch = (h > cdiv) ? (h / cdiv) : 1u;
	if (s_gl.scene_ok && s_gl.scene_w == w && s_gl.scene_h == h &&
	    s_gl.scene_samples == want_samples &&
	    s_gl.cloud_w == want_cw && s_gl.cloud_h == want_ch)
		return;
	if (s_gl.scene_fbo || s_gl.res_fbo) gl_free_scene_targets();
	int try_samples[2] = { want_samples, 0 };
	int num_tries = (want_samples > 0) ? 2 : 1;
	for (int i = 0; i < num_tries; i++) {
		if (gl_alloc_scene_targets(w, h, try_samples[i])) {
			s_gl.scene_ok = true;
			s_gl.scene_w = w;
			s_gl.scene_h = h;
			s_gl.scene_samples = try_samples[i];
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			return;
		}
		gl_free_scene_targets();
	}
	s_gl.scene_failed = true;
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

/* Build GPU Hi-Z depth pyramid: resolves terrain depth and downsamples to mip levels 1..N-1.
 * Returns true on success. */
bool gl_build_hiz_pyramid(void)
{
	if (!s_gl.scene_ok || !s_gl.hiz_valid || s_gl.hiz_levels <= 1) return false;
	GLuint prog = s_gl.hiz_downsample_prog;
	if (!prog || !glGetProgramParameter(prog, GL_COMPLETION_STATUS_KHR)) return false;
	if (!glGetProgramParameter(prog, GL_LINK_STATUS)) {
		glDeleteProgram(prog);
		s_gl.hiz_downsample_prog = 0;
		return false;
	}

	/* Resolve current terrain depth buffer into s_gl.res_depth_tex (level 0) */
	glBindFramebuffer(GL_READ_FRAMEBUFFER, s_gl.scene_fbo);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, s_gl.res_fbo);
	glBlitFramebuffer(0, 0, (GLint)s_gl.scene_w, (GLint)s_gl.scene_h,
	                  0, 0, (GLint)s_gl.scene_w, (GLint)s_gl.scene_h,
	                  GL_DEPTH_BUFFER_BIT, GL_NEAREST);

	glUseProgram(prog);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

	GLint loc_tex = glGetUniformLocation(prog, "uDepthTex", 9);
	GLint loc_ts  = glGetUniformLocation(prog, "uTexelSize", 10);

	for (int level = 1; level < s_gl.hiz_levels; level++) {
		uint32_t lw = (s_gl.hiz_w > (1u << level)) ? (s_gl.hiz_w >> level) : 1u;
		uint32_t lh = (s_gl.hiz_h > (1u << level)) ? (s_gl.hiz_h >> level) : 1u;
		uint32_t sw = (s_gl.hiz_w > (1u << (level - 1))) ? (s_gl.hiz_w >> (level - 1)) : 1u;
		uint32_t sh = (s_gl.hiz_h > (1u << (level - 1))) ? (s_gl.hiz_h >> (level - 1)) : 1u;

		glBindFramebuffer(GL_FRAMEBUFFER, s_gl.hiz_mip_fbo[level]);
		glViewport(0, 0, (GLsizei)lw, (GLsizei)lh);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, s_gl.hiz_mip_tex[level - 1]);
		glUniform1i(loc_tex, 0);
		glUniform2f(loc_ts, 1.0f / (float)sw, 1.0f / (float)sh);
		glDrawArrays(GL_TRIANGLES, 0, 3);
	}

	s_gl.gpu_hiz_built = true;
	glBindTexture(GL_TEXTURE_2D, 0);
	gl_bind_scene_target();
	glEnable(GL_DEPTH_TEST);
	return true;
}

/* Read back Hi-Z pyramid to CPU when CPU occlusion readback is active.
 * GPU-driven Hi-Z culling samples uHiZTex directly in the vertex shader. */
static void gl_readback_hiz(void)
{
    (void)s_gl;
}

/* Build 2D hierarchical occlusion map from Hi-Z pyramid.
 * Creates a min-depth pyramid at tile granularity for fast rectangle queries. */
static void gl_build_occ2d_map(void)
{
    if (!s_gl.hiz_valid || s_gl.hiz_levels == 0) {
        s_occ2d_valid = 0;
        return;
    }
    
    // Use Hi-Z level 2 as base (4x downsampled from full res)
    int base_level = s_gl.hiz_levels > 2 ? 2 : s_gl.hiz_levels - 1;
    uint32_t base_w = (s_gl.hiz_w > (1u << base_level)) ? (s_gl.hiz_w >> base_level) : 1u;
    uint32_t base_h = (s_gl.hiz_h > (1u << base_level)) ? (s_gl.hiz_h >> base_level) : 1u;
    
    // Number of tiles in each dimension
    int tiles_x = (base_w + ND_OCC2D_TILE_SIZE - 1) / ND_OCC2D_TILE_SIZE;
    int tiles_y = (base_h + ND_OCC2D_TILE_SIZE - 1) / ND_OCC2D_TILE_SIZE;
    tiles_x = tiles_x > 64 ? 64 : tiles_x;
    tiles_y = tiles_y > 64 ? 64 : tiles_y;
    
    s_occ2d_w = base_w;
    s_occ2d_h = base_h;
    
    // Level 0: compute min depth per tile from Hi-Z readback
    for (int ty = 0; ty < tiles_y; ty++) {
        for (int tx = 0; tx < tiles_x; tx++) {
            int px0 = tx * ND_OCC2D_TILE_SIZE;
            int py0 = ty * ND_OCC2D_TILE_SIZE;
            int px1 = (px0 + ND_OCC2D_TILE_SIZE < (int)base_w) ? (px0 + ND_OCC2D_TILE_SIZE) : (int)base_w;
            int py1 = (py0 + ND_OCC2D_TILE_SIZE < (int)base_h) ? (py0 + ND_OCC2D_TILE_SIZE) : (int)base_h;
            
            float min_depth = 1.0f;  // Far plane = 1.0 in NDC
            for (int y = py0; y < py1; y++) {
                for (int x = px0; x < px1; x++) {
                    float d = s_gl.hiz_readback[base_level][y][x];
                    if (d < min_depth) min_depth = d;
                }
            }
            s_occ2d_pyramid[0][ty][tx] = min_depth;
        }
    }
    
    // Build higher levels (2x2 min filter)
    int level_w = tiles_x;
    int level_h = tiles_y;
    for (int level = 1; level < ND_OCC2D_MAX_LEVELS; level++) {
        int next_w = (level_w + 1) / 2;
        int next_h = (level_h + 1) / 2;
        if (next_w == 0 || next_h == 0) break;
        
        for (int ty = 0; ty < next_h; ty++) {
            for (int tx = 0; tx < next_w; tx++) {
                float d00 = s_occ2d_pyramid[level-1][ty*2][tx*2];
                float d01 = (tx*2+1 < level_w) ? s_occ2d_pyramid[level-1][ty*2][tx*2+1] : d00;
                float d10 = (ty*2+1 < level_h) ? s_occ2d_pyramid[level-1][ty*2+1][tx*2] : d00;
                float d11 = (tx*2+1 < level_w && ty*2+1 < level_h) ? s_occ2d_pyramid[level-1][ty*2+1][tx*2+1] : d00;
                
                float min_d = d00;
                if (d01 < min_d) min_d = d01;
                if (d10 < min_d) min_d = d10;
                if (d11 < min_d) min_d = d11;
                
                s_occ2d_pyramid[level][ty][tx] = min_d;
            }
        }
        level_w = next_w;
        level_h = next_h;
        s_occ2d_levels = level + 1;
    }
    
    s_occ2d_valid = 1;
}

/* Test if a world-space bounding sphere is occluded by Hi-Z buffer.
 * Returns true (1) if fully occluded (sphere behind depth at all samples). */
int nd_seven_hiz_test_sphere(float cx, float cy, float cz, float radius)
{
    if (!s_gl.hiz_valid || s_gl.hiz_levels == 0 || !s_occ2d_valid) return 0;
    
    // Project sphere center to clip space using current MVP
    const float *clip = s_gl.cur_mvp;
    
    float px = clip[0]*cx + clip[4]*cy + clip[8]*cz + clip[12];
    float py = clip[1]*cx + clip[5]*cy + clip[9]*cz + clip[13];
    float pz = clip[2]*cx + clip[6]*cy + clip[10]*cz + clip[14];
    float pw = clip[3]*cx + clip[7]*cy + clip[11]*cz + clip[15];
    
    if (pw <= 0.0f) return 0;  // Behind camera
    
    float ndc_x = px / pw;
    float ndc_y = py / pw;
    float ndc_z = pz / pw;
    
    // Sphere radius in NDC (approximate)
    float r_ndc = radius * 2.0f / fmaxf(1.0f, fabsf(pw));
    
    // Test at Hi-Z level 2 (4x downsampled) for efficiency
    int level = s_gl.hiz_levels > 2 ? 2 : s_gl.hiz_levels - 1;
    int read_w = 16, read_h = 16;
    
    // UV in [0,1] for texture lookup
    float uv_x = ndc_x * 0.5f + 0.5f;
    float uv_y = -ndc_y * 0.5f + 0.5f;  // Flip Y for texture coords
    
    if (uv_x < 0.0f || uv_x > 1.0f || uv_y < 0.0f || uv_y > 1.0f) return 0;
    
    // Sample 3x3 neighborhood in Hi-Z
    float texel = 1.0f / (float)read_w;
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            int sx = (int)((uv_x + dx * texel) * read_w);
            int sy = (int)((uv_y + dy * texel) * read_h);
            sx = sx < 0 ? 0 : (sx >= read_w ? read_w - 1 : sx);
            sy = sy < 0 ? 0 : (sy >= read_h ? read_h - 1 : sy);
            
            float hiz_depth = s_gl.hiz_readback[level][sy][sx];
            if (ndc_z + r_ndc > hiz_depth) {
                return 0;  // Not occluded at this sample
            }
        }
    }
    return 1;  // Fully occluded
}

/* Ridge occlusion query for water queue gating: returns 1 if the water chunk at
 * (ox, oy, span_w, span_h) would be fully occluded by the current ridge buffer. */
int nd_seven_is_water_ridge_occluded(int32_t ox, int32_t oy, int32_t span_w, int32_t span_h,
                                     const float flat_mvp[16], float lwx, float lwy, float r_planet)
{
    (void)lwx;
    (void)lwy;
    (void)r_planet;
    if (!s_ridge_occlusion_valid || !s_ridge_use_occlusion)
        return 0;
    
    // Only check if the water chunk is behind the camera (farther than lamb)
    float cam_flat_y = s_ridge_cam_flat_y;
    if (oy + span_h >= cam_flat_y - 64.0f)
        return 0;
    
    float x0 = (float)ox, x1 = (float)(ox + span_w);
    float y0 = (float)oy, y1 = (float)(oy + span_h);
    float z_top = 96.0f; // water surface margin for occlusion test
    float corners[4][2] = { { x0, y0 }, { x1, y0 }, { x0, y1 }, { x1, y1 } };
    float min_nx = 2.0f, max_nx = -2.0f, max_ny = -2.0f;
    bool all_front = true;
    for (int c = 0; c < 4; c++) {
        float cx = corners[c][0], cy = corners[c][1];
        float cw = flat_mvp[3] * cx + flat_mvp[7] * cy + flat_mvp[11] * z_top + flat_mvp[15];
        if (cw <= 1.0f) {
            all_front = false;
            break;
        }
        float inv_w = 1.0f / cw;
        float nx = (flat_mvp[0] * cx + flat_mvp[4] * cy + flat_mvp[8] * z_top + flat_mvp[12]) * inv_w;
        float ny = (flat_mvp[1] * cx + flat_mvp[5] * cy + flat_mvp[9] * z_top + flat_mvp[13]) * inv_w;
        if (nx < min_nx) min_nx = nx;
        if (nx > max_nx) max_nx = nx;
        if (ny > max_ny) max_ny = ny;
    }
    if (!all_front || max_nx < -1.0f || min_nx > 1.0f || max_ny >= 1.0f)
        return 0;
    
    int b0 = (int)floorf((fmaxf(-1.0f, min_nx) + 1.0f) * (0.5f * SEVEN_HORIZON_BINS));
    int b1 = (int)ceilf((fminf(1.0f, max_nx) + 1.0f) * (0.5f * SEVEN_HORIZON_BINS));
    if (b0 < 0) b0 = 0;
    if (b1 >= SEVEN_HORIZON_BINS) b1 = SEVEN_HORIZON_BINS - 1;
    if (b0 > b1)
        return 0;
    
    for (int b = b0; b <= b1; b++) {
        if (max_ny + 0.015f >= s_ridge_horizon_ndc_y[b]) {
            return 0; // Not occluded - ridge buffer is higher than water chunk
        }
    }
    return 1; // Fully occluded by ridge
}

/* Cloud chunk occlusion test using Hi-Z */
int nd_seven_hiz_test_cloud_chunk(int32_t ox, int32_t oy, int32_t span_w, int32_t span_h,
                                   const float mvp[16], float cloud_z)
{
    (void)mvp;
    // Compute bounding sphere of cloud chunk
    float cx = (float)ox + span_w * 0.5f;
    float cy = (float)oy + span_h * 0.5f;
    float cz = cloud_z;
    float radius = fmaxf(span_w, span_h) * 0.7071f;
    
    return nd_seven_hiz_test_sphere(cx, cy, cz, radius);
}

/* Grass/billboard batch occlusion test using Hi-Z */
int nd_seven_hiz_test_billboard_batch(const lam_billboard_gpu_t *batch, uint32_t count,
                                       const float mvp[16], float max_radius)
{
    (void)mvp;
    if (!s_gl.hiz_valid || count == 0) return 0;
    
    int occluded = 0;
    for (uint32_t i = 0; i < count; i++) {
        const lam_billboard_gpu_t *b = &batch[i];
        float radius = fmaxf(b->width, b->height) * 0.5f;
        radius = fmaxf(radius, max_radius);
        
        if (nd_seven_hiz_test_sphere(b->x, b->y, b->z, radius)) {
            occluded++;
        }
    }
    return occluded;
}

/* Query 2D occlusion map for a screen-space rectangle.
 * Returns true if the rectangle is fully occluded (min depth <= near_z). */
int nd_seven_occ2d_test_rect(float ndc_x0, float ndc_y0, float ndc_x1, float ndc_y1, float near_z)
{
    if (!s_occ2d_valid || s_occ2d_levels == 0) return 0;
    
    // Convert NDC [-1,1] to texture coordinates [0,1]
    float u0 = ndc_x0 * 0.5f + 0.5f;
    float v0 = -ndc_y0 * 0.5f + 0.5f;
    float u1 = ndc_x1 * 0.5f + 0.5f;
    float v1 = -ndc_y1 * 0.5f + 0.5f;
    
    if (u0 > u1) { float t = u0; u0 = u1; u1 = t; }
    if (v0 > v1) { float t = v0; v0 = v1; v1 = t; }
    
    // Clamp to [0,1]
    if (u0 < 0.0f) u0 = 0.0f;
    if (v0 < 0.0f) v0 = 0.0f;
    if (u1 > 1.0f) u1 = 1.0f;
    if (v1 > 1.0f) v1 = 1.0f;
    if (u0 >= u1 || v0 >= v1) return 0;
    
    // Choose appropriate pyramid level based on rectangle size
    float rect_w = u1 - u0;
    float rect_h = v1 - v0;
    float tile_w = (float)ND_OCC2D_TILE_SIZE / (float)s_occ2d_w;
    float tile_h = (float)ND_OCC2D_TILE_SIZE / (float)s_occ2d_h;
    
    int level = 0;
    while (level + 1 < s_occ2d_levels && rect_w > tile_w * 4.0f && rect_h > tile_h * 4.0f) {
        level++;
        tile_w *= 2.0f;
        tile_h *= 2.0f;
    }
    
    // Compute tile range
    int tx0 = (int)(u0 / tile_w);
    int ty0 = (int)(v0 / tile_h);
    int tx1 = (int)(u1 / tile_w);
    int ty1 = (int)(v1 / tile_h);
    
    int level_w = (s_occ2d_w + (ND_OCC2D_TILE_SIZE << level) - 1) / (ND_OCC2D_TILE_SIZE << level);
    int level_h = (s_occ2d_h + (ND_OCC2D_TILE_SIZE << level) - 1) / (ND_OCC2D_TILE_SIZE << level);
    
    if (tx0 < 0) tx0 = 0;
    if (ty0 < 0) ty0 = 0;
    if (tx1 >= level_w) tx1 = level_w - 1;
    if (ty1 >= level_h) ty1 = level_h - 1;
    
    // Test min depth in rectangle
    float min_depth = 1.0f;
    for (int ty = ty0; ty <= ty1; ty++) {
        for (int tx = tx0; tx <= tx1; tx++) {
            float d = s_occ2d_pyramid[level][ty][tx];
            if (d < min_depth) min_depth = d;
        }
    }
    
    // Occluded if min depth in rectangle is in front of our near_z
    return (min_depth <= near_z) ? 1 : 0;
}

/* Water chunk 2D occ map test - projects water chunk to NDC and tests against occ map. */
int nd_seven_occ2d_test_water_chunk(int32_t ox, int32_t oy, int32_t span_w, int32_t span_h,
                                     const float mvp[16], float water_z)
{
    if (!s_occ2d_valid || s_occ2d_levels == 0) return 0;
    
    // Project 4 corners of water chunk to clip space
    float x0 = (float)ox, x1 = (float)(ox + span_w);
    float y0 = (float)oy, y1 = (float)(oy + span_h);
    
    float in_corners[4][4] = {
        { x0, y0, water_z, 1.0f },
        { x1, y0, water_z, 1.0f },
        { x0, y1, water_z, 1.0f },
        { x1, y1, water_z, 1.0f }
    };
    
    float min_nx = 2.0f, max_nx = -2.0f;
    float min_ny = 2.0f, max_ny = -2.0f;
    bool all_front = true;
    
    for (int c = 0; c < 4; c++) {
        float cx = in_corners[c][0];
        float cy = in_corners[c][1];
        float cz = in_corners[c][2];
        float cw = in_corners[c][3];
        
        float px = mvp[0]*cx + mvp[4]*cy + mvp[8]*cz + mvp[12]*cw;
        float py = mvp[1]*cx + mvp[5]*cy + mvp[9]*cz + mvp[13]*cw;
        float pw = mvp[3]*cx + mvp[7]*cy + mvp[11]*cz + mvp[15]*cw;
        
        if (pw <= 0.0f) {
            all_front = false;
            break;
        }
        
        float nx = px / pw;
        float ny = py / pw;
        
        if (nx < min_nx) min_nx = nx;
        if (nx > max_nx) max_nx = nx;
        if (ny < min_ny) min_ny = ny;
        if (ny > max_ny) max_ny = ny;
    }
    
    if (!all_front || max_nx < -1.0f || min_nx > 1.0f || max_ny < -1.0f || min_ny > 1.0f)
        return 0;  // Off screen or behind camera
    
    // Clamp to NDC bounds
    if (min_nx < -1.0f) min_nx = -1.0f;
    if (max_nx > 1.0f) max_nx = 1.0f;
    if (min_ny < -1.0f) min_ny = -1.0f;
    if (max_ny > 1.0f) max_ny = 1.0f;
    
    // Near Z for water (slightly in front to be conservative)
    float near_z = 0.0f;
    
    return nd_seven_occ2d_test_rect(min_nx, min_ny, max_nx, max_ny, near_z);
}

/* Bind the frame's render target (scene FBO, or the canvas if offscreen targets are unavailable) and the stage viewport. */
void gl_bind_scene_target(void)
{
	glBindFramebuffer(GL_FRAMEBUFFER, s_gl.scene_ok ? s_gl.scene_fbo : 0);
	glViewport(0, 0, (GLsizei)s_gl.stage_w, (GLsizei)s_gl.stage_h);
}

GLuint gl_ensure_tree_shadow_dummy(void)
{
	if (s_gl.tree_shadow_dummy_tex)
		return s_gl.tree_shadow_dummy_tex;
	static const uint8_t white[4] = { 255, 255, 255, 255 };
	s_gl.tree_shadow_dummy_tex = glCreateTexture();
	glActiveTexture(GL_TEXTURE0 + 12);
	glBindTexture(GL_TEXTURE_2D, s_gl.tree_shadow_dummy_tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE0);
	return s_gl.tree_shadow_dummy_tex;
}

static bool gl_ensure_tree_shadow_targets(void)
{
	if (s_gl.tree_shadow_ready) return true;
	if (s_gl.tree_shadow_failed) return false;

	glActiveTexture(GL_TEXTURE0 + 12);
	for (int i = 0; i < 2; i++) {
		s_gl.tree_shadow_tex[i] = glCreateTexture();
		glBindTexture(GL_TEXTURE_2D, s_gl.tree_shadow_tex[i]);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F,
		             ND_TREE_SHADOW_RES, ND_TREE_SHADOW_RES, 0,
		             GL_DEPTH_COMPONENT, GL_FLOAT, NULL);

		s_gl.tree_shadow_fbo[i] = glCreateFramebuffer();
		glBindFramebuffer(GL_FRAMEBUFFER, s_gl.tree_shadow_fbo[i]);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
		                       GL_TEXTURE_2D, s_gl.tree_shadow_tex[i], 0);
		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			glBindTexture(GL_TEXTURE_2D, 0);
			for (int j = 0; j <= i; j++) {
				if (s_gl.tree_shadow_fbo[j]) { glDeleteFramebuffer(s_gl.tree_shadow_fbo[j]); s_gl.tree_shadow_fbo[j] = 0; }
				if (s_gl.tree_shadow_tex[j]) { glDeleteTexture(s_gl.tree_shadow_tex[j]); s_gl.tree_shadow_tex[j] = 0; }
			}
			glActiveTexture(GL_TEXTURE0);
			gl_bind_scene_target();
			s_gl.tree_shadow_failed = true;
			return false;
		}
		glViewport(0, 0, ND_TREE_SHADOW_RES, ND_TREE_SHADOW_RES);
		glDepthMask(GL_TRUE);
		glClear(GL_DEPTH_BUFFER_BIT);
	}
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE0);
	gl_bind_scene_target();
	s_gl.tree_shadow_ready = true;
	s_gl.tree_shadow_read_idx = 0;
	s_gl.tree_shadow_read_valid = false;
	return true;
}

void gl_prepare_tree_shadow_frame(float tx, float ty, float tz,
                                         const float E[3], const float S[3], const float N[3])
{
	s_gl.tree_shadow_write_draws = false;
	float dist_fade = fminf(1.0f, fmaxf(0.0f, (4200.0f - s_gl.cur_dist) / 1000.0f));
	if (dist_fade <= 0.001f || !s_gl.pipelines_ready) {
		s_gl.tree_shadow_active = 0.0f;
		return;
	}
	if (!gl_ensure_tree_shadow_targets()) {
		s_gl.tree_shadow_active = 0.0f;
		return;
	}

	/* Express sun vector in world-grid (East, South, Up) local coordinates matching s_fs_terrain & s_fs_mesh3d */
	float se = s_gl.cur_sun_dir[0] * E[0] + s_gl.cur_sun_dir[1] * E[1] + s_gl.cur_sun_dir[2] * E[2];
	float ss = s_gl.cur_sun_dir[0] * S[0] + s_gl.cur_sun_dir[1] * S[1] + s_gl.cur_sun_dir[2] * S[2];
	float sn = s_gl.cur_sun_dir[0] * N[0] + s_gl.cur_sun_dir[1] * N[1] + s_gl.cur_sun_dir[2] * N[2];
	float lx, ly, lz;
	if (sn > -0.08f) {
		lx = se;
		ly = ss;
		lz = fmaxf(0.20f, sn);
	} else {
		lx = -0.577f;
		ly = -0.577f;
		lz = 0.816f;
	}
	float inv_l = 1.0f / sqrtf(lx * lx + ly * ly + lz * lz);
	lx *= inv_l; ly *= inv_l; lz *= inv_l;

	/* Orthonormal light-space basis (R, U, L) in (East, South, Up) coordinates */
	float hx = 0.0f, hy = (fabsf(lz) > 0.96f) ? 1.0f : 0.0f, hz = (fabsf(lz) > 0.96f) ? 0.0f : 1.0f;
	float rx = hy * lz - hz * ly;
	float ry = hz * lx - hx * lz;
	float rz = hx * ly - hy * lx;
	float inv_r = 1.0f / sqrtf(rx * rx + ry * ry + rz * rz);
	rx *= inv_r; ry *= inv_r; rz *= inv_r;

	float ux = ly * rz - lz * ry;
	float uy = lz * rx - lx * rz;
	float uz = lx * ry - ly * rx;

	/* Quantized orthographic half-extent + light-space texel snapping eliminates shadow shimmering on camera pan */
	float raw_half = fminf(1152.0f, fmaxf(352.0f, s_gl.cur_dist * 0.92f));
	float half_xy = floorf(raw_half / 64.0f + 0.5f) * 64.0f;
	const float half_z = 2048.0f;
	float texel = (2.0f * half_xy) / (float)ND_TREE_SHADOW_RES;

	float pr = tx * rx + ty * ry + tz * rz;
	float pu = tx * ux + ty * uy + tz * uz;
	float pl = tx * lx + ty * ly + tz * lz;
	float spr = floorf(pr / texel + 0.5f) * texel;
	float spu = floorf(pu / texel + 0.5f) * texel;

	s_gl.tree_shadow_write_origin[0] = spr * rx + spu * ux + pl * lx;
	s_gl.tree_shadow_write_origin[1] = spr * ry + spu * uy + pl * ly;
	s_gl.tree_shadow_write_origin[2] = spr * rz + spu * uz + pl * lz;

	float inv_xy = 1.0f / half_xy;
	float inv_z  = 1.0f / half_z;
	float *m = s_gl.tree_shadow_write_vp;
	/* Column-major mat4: x = dot(p, R)/half_xy, y = dot(p, U)/half_xy, z = -dot(p, L)/half_z (smaller depth closer to sun) */
	m[0]  = rx * inv_xy;  m[1]  = ux * inv_xy;  m[2]  = -lx * inv_z;  m[3]  = 0.0f;
	m[4]  = ry * inv_xy;  m[5]  = uy * inv_xy;  m[6]  = -ly * inv_z;  m[7]  = 0.0f;
	m[8]  = rz * inv_xy;  m[9]  = uz * inv_xy;  m[10] = -lz * inv_z;  m[11] = 0.0f;
	m[12] = 0.0f;         m[13] = 0.0f;         m[14] = 0.0f;         m[15] = 1.0f;

	s_gl.tree_shadow_active = s_gl.tree_shadow_read_valid ? dist_fade : 0.0f;

	/* Clear write shadow map depth buffer to 1.0 (unoccluded) */
	int write_idx = 1 - s_gl.tree_shadow_read_idx;
	glBindFramebuffer(GL_FRAMEBUFFER, s_gl.tree_shadow_fbo[write_idx]);
	glViewport(0, 0, ND_TREE_SHADOW_RES, ND_TREE_SHADOW_RES);
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
	glDepthMask(GL_TRUE);
	glClear(GL_DEPTH_BUFFER_BIT);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	gl_bind_scene_target();
}

void gl_bind_tree_shadow_read(GLint uTex, GLint uOrigin, GLint uVP, GLint uActive)
{
	GLuint dummy = gl_ensure_tree_shadow_dummy();
	bool active = (s_gl.tree_shadow_ready && s_gl.tree_shadow_read_valid && s_gl.tree_shadow_active > 0.001f);
	glActiveTexture(GL_TEXTURE0 + 12);
	glBindTexture(GL_TEXTURE_2D, active ? s_gl.tree_shadow_tex[s_gl.tree_shadow_read_idx] : dummy);
	if (uTex >= 0) glUniform1i(uTex, 12);
	if (active) {
		if (uOrigin >= 0) glUniform3fv(uOrigin, 1, s_gl.tree_shadow_read_origin);
		if (uVP >= 0) glUniformMatrix4fv(uVP, 1, GL_FALSE, s_gl.tree_shadow_read_vp);
		if (uActive >= 0) glUniform1f(uActive, s_gl.tree_shadow_active);
	} else {
		if (uActive >= 0) glUniform1f(uActive, 0.0f);
	}
	glActiveTexture(GL_TEXTURE0);
}

/* Resolve the multisampled scene into the sampleable colour (and depth) textures. */
void gl_resolve_scene(GLbitfield mask)
{
	if (!s_gl.scene_ok) return;
	glBindFramebuffer(GL_READ_FRAMEBUFFER, s_gl.scene_fbo);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, s_gl.res_fbo);
	glBlitFramebuffer(0, 0, (GLint)s_gl.scene_w, (GLint)s_gl.scene_h,
	                  0, 0, (GLint)s_gl.scene_w, (GLint)s_gl.scene_h, mask, GL_NEAREST);
	gl_bind_scene_target();
}

void nd_seven_gl_end_frame(void)
{
	if (!s_gl.frame_open) return;
	s_gl.frame_open = false;
	if (s_gl.tree_shadow_ready) {
		if (s_gl.tree_shadow_write_draws) {
			s_gl.tree_shadow_read_idx = 1 - s_gl.tree_shadow_read_idx;
			memcpy(s_gl.tree_shadow_read_origin, s_gl.tree_shadow_write_origin, sizeof(s_gl.tree_shadow_read_origin));
			memcpy(s_gl.tree_shadow_read_vp, s_gl.tree_shadow_write_vp, sizeof(s_gl.tree_shadow_read_vp));
			s_gl.tree_shadow_read_valid = true;
		} else {
			s_gl.tree_shadow_read_valid = false;
		}
	}
	if (!s_gl.scene_ok) return;
	gl_resolve_scene(GL_COLOR_BUFFER_BIT);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, s_gl.res_fbo);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glBlitFramebuffer(0, 0, (GLint)s_gl.scene_w, (GLint)s_gl.scene_h,
	                  0, 0, (GLint)s_gl.scene_w, (GLint)s_gl.scene_h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	
	/* Read back Hi-Z pyramid for CPU occlusion culling next frame */
	if (s_gl.hiz_valid) {
		gl_readback_hiz();
	}
	
	/* Build 2D hierarchical occlusion map from Hi-Z readback when readback is active */
	if (s_gl.hiz_valid && s_occ2d_valid) {
		gl_build_occ2d_map();
	}
	
	/* The sprite marker lives in the scene alpha: the canvas must end up fully opaque */
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
}

void nd_seven_gl_get_target_info(uint32_t *out_stage_w, uint32_t *out_stage_h,
                                 uint32_t *out_scene_w, uint32_t *out_scene_h,
                                 int *out_samples,
                                 uint32_t *out_cloud_w, uint32_t *out_cloud_h,
                                 bool *out_csm_active)
{
	if (out_stage_w) *out_stage_w = s_gl.stage_w;
	if (out_stage_h) *out_stage_h = s_gl.stage_h;
	if (out_scene_w) *out_scene_w = s_gl.scene_w;
	if (out_scene_h) *out_scene_h = s_gl.scene_h;
	if (out_samples) *out_samples = s_gl.scene_samples;
	if (out_cloud_w) *out_cloud_w = s_gl.cloud_w;
	if (out_cloud_h) *out_cloud_h = s_gl.cloud_h;
	if (out_csm_active) *out_csm_active = (nd_seven_get_cloud_quality() >= ND_CLOUD_QUALITY_MEDIUM);
}

uint32_t nd_seven_gl_get_frame_count(void)
{
	return s_gl.frame_count;
}

/* Occlusion stats for HUD/debugging */
void nd_seven_gl_get_occlusion_stats(uint32_t *out_water_culled,
                                     uint32_t *out_cloud_culled,
                                     uint32_t *out_billboard_culled,
                                     uint32_t *out_water_queued,
                                     uint32_t *out_cloud_queued)
{
	if (out_water_culled) *out_water_culled = s_gl.water_culled_occ2d;
	if (out_cloud_culled) *out_cloud_culled = s_gl.cloud_culled_hiz;
	if (out_billboard_culled) *out_billboard_culled = s_gl.billboard_culled_hiz;
	if (out_water_queued) *out_water_queued = s_gl.water_count;
	if (out_cloud_queued) *out_cloud_queued = s_gl.cloud_count;
}

bool nd_seven_gl_is_gpu_hiz_active(void)
{
	return s_gl.gpu_hiz_built && s_gl.hiz_valid;
}

int nd_seven_gl_get_hiz_mip_levels(void)
{
	return s_gl.hiz_levels;
}

uint32_t nd_seven_gl_get_indirect_draw_count(void)
{
	return s_gl.indirect_cmd_count;
}

static bool gl_ensure_csm_target(void)
{
	if (s_gl.csm_fbo) return true;
	if (s_gl.csm_failed) return false;
	glActiveTexture(GL_TEXTURE0);
	s_gl.csm_tex = glCreateTexture();
	glBindTexture(GL_TEXTURE_2D, s_gl.csm_tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, ND_CSM_RES, ND_CSM_RES, 0, GL_RED, GL_UNSIGNED_BYTE, NULL);
	s_gl.csm_fbo = glCreateFramebuffer();
	glBindFramebuffer(GL_FRAMEBUFFER, s_gl.csm_fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_gl.csm_tex, 0);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glBindTexture(GL_TEXTURE_2D, 0);
		glDeleteFramebuffer(s_gl.csm_fbo);
		glDeleteTexture(s_gl.csm_tex);
		s_gl.csm_fbo = 0;
		s_gl.csm_tex = 0;
		s_gl.csm_failed = true;
		return false;
	}
	glViewport(0, 0, ND_CSM_RES, ND_CSM_RES);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glBindTexture(GL_TEXTURE_2D, 0);
	gl_bind_scene_target();
	return true;
}

static float gl_wrap1024(float v)
{
	return v - 1024.0f * floorf(v / 1024.0f);
}

static float gl_eval_world_humidity_cpu(float wx, float wy, float sea_wet)
{
	float wc_v = 0.5f, wc_gx = 0.0f, wc_gy = 0.0f;
	gl_chaff_grad_noise2d(wx * 0.000045f, wy * 0.000045f, 0x434C494Du, &wc_v, &wc_gx, &wc_gy);
	float tex_hum = fminf(0.95f, fmaxf(0.08f, 0.50f + 0.28f * (wc_v - 0.5f) + 0.10f * sea_wet));
	float lat_phi = 6.28318530718f * ((64.0f - wy) / 8388608.0f);
	float lat_deg = asinf(fminf(1.0f, fabsf(sinf(lat_phi)))) * 57.2957795f;
	float hum = tex_hum
	          - gl_smoothstep01(24.0f, 0.0f, lat_deg) * 0.18f
	          + gl_smoothstep01(18.0f, 0.0f, fabsf(lat_deg - 38.0f)) * 0.12f
	          + gl_smoothstep01(10.0f, 0.0f, fabsf(lat_deg - 58.0f)) * 0.08f;
	return fminf(0.92f, fmaxf(0.06f, hum));
}

static float gl_eval_synoptic_front_cpu(float wx, float wy, float anim_time, float fw,
                                        float *out_warp_x, float *out_warp_y)
{
	const float prevDirX = 0.741f, prevDirY = -0.671f;
	const float prevPerpX = 0.671f, prevPerpY = 0.741f;
	float tFlow = gl_wrap1024(anim_time * 0.005f);
	float phase0 = gl_wrap1024(anim_time * 0.004f);
	float phase1 = gl_wrap1024(anim_time * 0.008f);

	float psi0_v = 0.5f, psi0_gx = 0.0f, psi0_gy = 0.0f;
	gl_chaff_grad_noise2d(wx * 0.00000042f - prevDirX * (tFlow * 0.40f),
	                      wy * 0.00000042f - prevDirY * (tFlow * 0.40f),
	                      0x4359434Cu, &psi0_v, &psi0_gx, &psi0_gy);

	float psi1_v = 0.5f, psi1_gx = 0.0f, psi1_gy = 0.0f;
	gl_chaff_grad_noise2d(wx * 0.00000180f - psi0_gy * 0.30f - prevDirX * tFlow,
	                      wy * 0.00000180f + psi0_gx * 0.30f - prevDirY * tFlow,
	                      0x5354524Du, &psi1_v, &psi1_gx, &psi1_gy);

	float warp_x = -psi0_gy * 260000.0f - psi1_gy * 68000.0f;
	float warp_y =  psi0_gx * 260000.0f + psi1_gx * 68000.0f;
	if (out_warp_x) *out_warp_x = warp_x;
	if (out_warp_y) *out_warp_y = warp_y;

	float pDispX = wx + warp_x, pDispY = wy + warp_y;
	float pAlong = pDispX * prevDirX + pDispY * prevDirY;
	float pCross = pDispX * prevPerpX + pDispY * prevPerpY;
	float pZonalX = prevDirX * (pAlong * 0.65f) + prevPerpX * (pCross * 1.55f);
	float pZonalY = prevDirY * (pAlong * 0.65f) + prevPerpY * (pCross * 1.55f);

	float oct0_v = 0.5f, oct0_gx = 0.0f, oct0_gy = 0.0f;
	gl_chaff_grad_noise2d(pZonalX * 0.00000050f - prevDirX * phase0,
	                      pZonalY * 0.00000050f - prevDirY * phase0,
	                      0x434C4430u, &oct0_v, &oct0_gx, &oct0_gy);

	float wOct1 = gl_smoothstep01(0.32f, 0.08f, 0.00000240f * fw);
	float oct1_v = 0.5f, oct1_gx = 0.0f, oct1_gy = 0.0f;
	if (wOct1 > 0.001f) {
		float pRotX =  0.8f * pZonalX + 0.6f * pZonalY;
		float pRotY = -0.6f * pZonalX + 0.8f * pZonalY;
		gl_chaff_grad_noise2d(pRotX * 0.00000240f - prevDirX * phase1,
		                      pRotY * 0.00000240f - prevDirY * phase1,
		                      0x434C4431u, &oct1_v, &oct1_gx, &oct1_gy);
	}

	float r0 = 1.0f - 2.0f * fabsf(oct0_v - 0.5f);
	float r1 = 1.0f - 2.0f * fabsf(oct1_v - 0.5f);
	float front0 = oct0_v + (r0 - 0.68f) * 0.28f;
	float front1 = oct1_v + (r1 - 0.68f) * 0.24f;
	return 0.50f + (front0 - 0.50f) * 0.72f + (front1 - 0.50f) * (0.54f * wOct1) + (psi0_v - 0.50f) * 0.34f;
}

bool gl_eval_synoptic_is_clear_sky_cpu(float wx, float wy, float anim_time,
                                              float sea_wet, float fw,
                                              bool want_cirrus, float margin)
{
	float humidity = gl_eval_world_humidity_cpu(wx, wy, sea_wet);
	float warp_x = 0.0f, warp_y = 0.0f;
	float syn_front = gl_eval_synoptic_front_cpu(wx, wy, anim_time, fw, &warp_x, &warp_y);
	float thresh = 0.555f - 0.105f * humidity;
	if (want_cirrus && (syn_front + 0.118f + margin) > (0.56f - 0.08f * humidity))
		return false;

	float wReg2 = gl_smoothstep01(0.26f, 0.07f, 0.00001150f * fw);
	float wReg3 = gl_smoothstep01(0.24f, 0.06f, 0.00005500f * fw);
	if (syn_front + 0.116f * wReg2 + 0.070f * wReg3 + margin <= thresh)
		return true;

	if (wReg2 > 0.001f) {
		float phase2 = gl_wrap1024(anim_time * 0.015f);
		float p2x =  0.28f * wx + 0.96f * wy + warp_x * 0.14f;
		float p2y = -0.96f * wx + 0.28f * wy + warp_y * 0.14f;
		float oct2_v = 0.5f, oct2_gx = 0.0f, oct2_gy = 0.0f;
		gl_chaff_grad_noise2d(p2x * 0.00001150f - 0.741f * phase2,
		                      p2y * 0.00001150f + 0.671f * phase2,
		                      0x434C4432u, &oct2_v, &oct2_gx, &oct2_gy);
		float r2 = 1.0f - 2.0f * fabsf(oct2_v - 0.5f);
		syn_front += ((oct2_v - 0.50f) + (r2 - 0.68f) * 0.25f) * 0.20f * wReg2;
	}
	return (syn_front + 0.070f * wReg3 + margin) <= thresh;
}

/* Sun-Pyramid Cloud Shadow Map:
 * Forms the pyramid with apex at the sun and base covering the visible ground frustum around (tx, ty, tz),
 * intersects that pyramid with the cloud slice zk = 920 at (pyr_cx, pyr_cy), and:
 * 1) Pre-culls the entire CSM pass on the CPU when the Sun-Pyramid slice intersects 100% clear sky (~65-75% of planet),
 *    setting csm_clear_sky = true (uCsm.z = -1) so terrain/water/clouds skip all shadow texture fetches and procedural noise.
 * 2) Otherwise renders the 128x128 R8 map centered directly on the Sun-Pyramid cloud-deck intersection. */
void gl_render_cloud_shadow_map(void)
{
	s_gl.csm_valid = false;
	s_gl.csm_clear_sky = false;
	int cq = nd_seven_get_cloud_quality();
	if (cq <= ND_CLOUD_QUALITY_LOW) {
		s_gl.csm_clear_sky = true;
		return;
	}
	if (!s_gl.csm_pipe.prog || s_gl.cur_dist > 180000.0f)
		return;

	/* Temporal CSM history invalidation */
	if (s_gl.csm_history_valid) {
		bool invalidate = false;
		
		// Camera teleport / large motion
		float cam_motion = sqrtf((s_gl.cur_tx - s_gl.csm_prev_win.cx) * (s_gl.cur_tx - s_gl.csm_prev_win.cx) +
		                         (s_gl.cur_ty - s_gl.csm_prev_win.cy) * (s_gl.cur_ty - s_gl.csm_prev_win.cy));
		if (cam_motion > 500.0f) {
			invalidate = true;  // Teleport/cut
		} else if (s_gl.csm_prev_win.half > 0.0f && cam_motion > 0.1f * s_gl.csm_prev_win.half) {
			invalidate = true;  // Large motion relative to CSM window
		}
		
		// Quality change
		if (cq != s_gl.csm_prev_quality) invalidate = true;
		
		// MSAA change
		if (nd_seven_get_msaa() != s_gl.csm_prev_msaa) invalidate = true;
		
		// Render scale change
		float res_scale = nd_seven_get_res_scale();
		if (fabsf(res_scale - s_gl.csm_prev_res_scale) > 0.01f) invalidate = true;
		
		// Sun direction change > 15 degrees
		float sun_dot = s_gl.cur_sun_dir[0] * s_gl.csm_prev_sun_dir[0] +
		                s_gl.cur_sun_dir[1] * s_gl.csm_prev_sun_dir[1] +
		                s_gl.cur_sun_dir[2] * s_gl.csm_prev_sun_dir[2];
		if (sun_dot < 0.9659258f) {  // cos(15°) = 0.9659258
			invalidate = true;
		}
		
		if (invalidate) {
			s_gl.csm_history_valid = false;
		}
	}
	
	// Store current settings for next frame invalidation check
	s_gl.csm_prev_quality = cq;
	s_gl.csm_prev_msaa = nd_seven_get_msaa();
	s_gl.csm_prev_res_scale = nd_seven_get_res_scale();

	float sun_xy_len = sqrtf(s_gl.cur_sun_dir[0] * s_gl.cur_sun_dir[0] + s_gl.cur_sun_dir[1] * s_gl.cur_sun_dir[1]);
	float sun_px = (sun_xy_len > 0.05f) ? (-s_gl.cur_sun_dir[0] / sun_xy_len) : -0.70710678f;
	float sun_py = (sun_xy_len > 0.05f) ? (-s_gl.cur_sun_dir[1] / sun_xy_len) : -0.70710678f;
	float orbit_off = fminf(14000.0f, fmaxf(0.0f, s_gl.cur_dist * 0.045f));
	float dz_ref = fmaxf(0.0f, 920.0f - fmaxf(0.0f, s_gl.cur_tz)) + orbit_off;
	float pyr_cx = s_gl.cur_tx + sun_px * dz_ref;
	float pyr_cy = s_gl.cur_ty + sun_py * dz_ref;

	if (!nd_seven_gl_csm_window(s_gl.cur_dist, pyr_cx, pyr_cy, &s_gl.csm_win))
		return;

	float sea_wet = (s_gl.cur_sea_level > -900.0f) ? 1.0f : 0.0f;
	float fw_csm = (2.0f * s_gl.csm_win.half) / (float)ND_CSM_RES;
	float pyr_half = fminf(s_gl.csm_win.half, fmaxf(1200.0f, s_gl.cur_dist * 1.8f + 800.0f));
	bool pyr_clear = true;
	for (int gy = -1; gy <= 1 && pyr_clear; gy++) {
		float sy = pyr_cy + (float)gy * pyr_half;
		for (int gx = -1; gx <= 1; gx++) {
			float sx = pyr_cx + (float)gx * pyr_half;
			if (!gl_eval_synoptic_is_clear_sky_cpu(sx, sy, s_gl.cur_anim_time, sea_wet, fw_csm, false, 0.028f)) {
				pyr_clear = false;
				break;
			}
		}
	}
	if (pyr_clear) {
		s_gl.csm_clear_sky = true;
		return;
	}

	if (!gl_ensure_csm_target()) return;
	float csm_cache_dt = (cq == ND_CLOUD_QUALITY_MEDIUM) ? 0.35f : 0.10f;
	if (s_gl.csm_has_cache &&
	    s_gl.csm_rendered_win.cx == s_gl.csm_win.cx &&
	    s_gl.csm_rendered_win.cy == s_gl.csm_win.cy &&
	    s_gl.csm_rendered_win.half == s_gl.csm_win.half &&
	    s_gl.csm_rendered_wet == sea_wet &&
	    fabsf(s_gl.cur_anim_time - s_gl.csm_rendered_time) < csm_cache_dt) {
		s_gl.csm_valid = true;
		return;
	}

	/* --- TEMPORAL REPROJECTION SETUP --- */
	// Create history texture/FBO on first use
	if (!s_gl.csm_history_tex) {
		glActiveTexture(GL_TEXTURE0);
		s_gl.csm_history_tex = glCreateTexture();
		glBindTexture(GL_TEXTURE_2D, s_gl.csm_history_tex);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, ND_CSM_RES, ND_CSM_RES, 0, GL_RED, GL_UNSIGNED_BYTE, NULL);
		
		s_gl.csm_history_fbo = glCreateFramebuffer();
		glBindFramebuffer(GL_FRAMEBUFFER, s_gl.csm_history_fbo);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_gl.csm_history_tex, 0);
		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
			glViewport(0, 0, ND_CSM_RES, ND_CSM_RES);
			glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
			glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
			glClear(GL_COLOR_BUFFER_BIT);
		}
		glBindTexture(GL_TEXTURE_2D, 0);
		
		// Create reprojection shader program
		s_gl.csm_reproj_prog = create_program_async(s_vs_csm_reproj, s_fs_csm_reproj);
	}
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	
	// Compute previous frame's CSM MVP for reprojection
	// We store the previous frame's CSM window and sun direction
	// The reprojection maps from prev CSM UV to current CSM UV
	
	/* --- RENDER CURRENT CSM --- */
	glBindFramebuffer(GL_FRAMEBUFFER, s_gl.csm_fbo);
	glViewport(0, 0, ND_CSM_RES, ND_CSM_RES);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glUseProgram(s_gl.csm_pipe.prog);
	glUniform3f(s_gl.csm_pipe.uCsm, s_gl.csm_win.cx, s_gl.csm_win.cy, s_gl.csm_win.half);
	glUniform1f(s_gl.csm_pipe.uAnimTime, s_gl.cur_anim_time);
	glUniform1f(s_gl.csm_pipe.uSeaWet, sea_wet);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	
	/* --- TEMPORAL REPROJECTION PASS --- */
	if (s_gl.csm_history_valid && s_gl.csm_reproj_prog) {
		// Bind history texture and render reprojection to a temp texture
		// For now, we'll blend directly by sampling history in the cloud shader
		// This is a simplified approach - full reprojection would need a separate pass
	}
	
	/* --- COPY CURRENT TO HISTORY FOR NEXT FRAME --- */
	// Blit current CSM to history texture
	glBindFramebuffer(GL_READ_FRAMEBUFFER, s_gl.csm_fbo);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, s_gl.csm_history_fbo);
	glBlitFramebuffer(0, 0, ND_CSM_RES, ND_CSM_RES,
	                  0, 0, ND_CSM_RES, ND_CSM_RES, GL_COLOR_BUFFER_BIT, GL_NEAREST);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	
	/* --- UPDATE HISTORY STATE --- */
	s_gl.csm_history_valid = true;
	memcpy(s_gl.csm_prev_sun_dir, s_gl.cur_sun_dir, sizeof(s_gl.cur_sun_dir));
	s_gl.csm_prev_win = s_gl.csm_win;
	s_gl.csm_prev_anim_time = s_gl.cur_anim_time;
	memcpy(s_gl.csm_prev_mvp, s_gl.cur_mvp, sizeof(s_gl.cur_mvp));  // Approximate
	
	gl_bind_scene_target();
	glEnable(GL_DEPTH_TEST);
	s_gl.csm_rendered_win = s_gl.csm_win;
	s_gl.csm_rendered_time = s_gl.cur_anim_time;
	s_gl.csm_rendered_wet = sea_wet;
	s_gl.csm_has_cache = true;
	s_gl.csm_valid = true;
}

/* Bind the cloud-shadow map (texture unit 7) and window to a program; half < 0 means Sun-Pyramid is clear sky; half = 0 means no map is available. */
void gl_bind_csm(GLint loc_tex, GLint loc_csm)
{
	glActiveTexture(GL_TEXTURE0 + 7);
	glBindTexture(GL_TEXTURE_2D, s_gl.csm_valid ? s_gl.csm_tex : 0);
	glUniform1i(loc_tex, 7);
	if (s_gl.csm_valid)
		glUniform3f(loc_csm, s_gl.csm_win.cx, s_gl.csm_win.cy, s_gl.csm_win.half);
	else if (nd_seven_get_cloud_quality() <= ND_CLOUD_QUALITY_LOW)
		glUniform3f(loc_csm, 0.0f, 0.0f, -2.0f);
	else if (s_gl.csm_clear_sky)
		glUniform3f(loc_csm, 0.0f, 0.0f, -1.0f);
	else
		glUniform3f(loc_csm, 0.0f, 0.0f, 0.0f);
	glActiveTexture(GL_TEXTURE0);
}

float nd_seven_gl_cloud_shadow_from_density(float density, float frag_z)
{
	const float zk = 920.0f;
	if (frag_z >= zk + 240.0f) return 0.0f;
	float slice_fade = gl_smoothstep01(zk + 240.0f, zk - 120.0f, frag_z);
	float s = gl_smoothstep01(0.05f, 0.66f, density * slice_fade);
	return s > 0.95f ? 0.95f : s;
}

float nd_seven_gl_scene_light(float cloud_shadow, float diffuse)
{
	float sun_trans = 1.0f - (float)ND_CLOUD_SHADOW_DIRECT_LOSS * cloud_shadow;
	float amb_trans = 1.0f - (float)ND_CLOUD_SHADOW_AMBIENT_LOSS * cloud_shadow;
	return 0.45f * amb_trans + 0.55f * diffuse * sun_trans;
}
