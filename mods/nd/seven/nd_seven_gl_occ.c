/*
 * nd_seven_gl_occ.c — Scene FBO Targets, Hi-Z Pyramid, 2D Occlusion Map, Tree Shadows, and Cloud Shadow Map (CSM).
 */

#include "nd_seven_gl_internal.h"

static ar_occluder_t   s_terrain_occ;
static int             s_terrain_occ_valid = 0;
static double          s_terrain_occ_origin[3];
static nd_planet_def_t s_terrain_occ_pdef;
static uint32_t        s_terrain_occ_culled = 0;

ar_occluder_t *nd_seven_gl_begin_terrain_occluder(const float mvp[16],
                                                  const double ref_origin[3],
                                                  const nd_planet_def_t *pdef)
{
	if (mvp)
		ar_occluder_reset(&s_terrain_occ, mvp, 1.0f, 0.01f);
	else
		memset(&s_terrain_occ, 0, sizeof(s_terrain_occ));
	s_terrain_occ_valid = 0;
	s_terrain_occ_culled = 0;
	if (ref_origin) {
		s_terrain_occ_origin[0] = ref_origin[0];
		s_terrain_occ_origin[1] = ref_origin[1];
		s_terrain_occ_origin[2] = ref_origin[2];
	} else {
		s_terrain_occ_origin[0] = 0.0;
		s_terrain_occ_origin[1] = 0.0;
		s_terrain_occ_origin[2] = 0.0;
	}
	if (pdef)
		s_terrain_occ_pdef = *pdef;
	else
		memset(&s_terrain_occ_pdef, 0, sizeof(s_terrain_occ_pdef));
	return &s_terrain_occ;
}

void nd_seven_gl_end_terrain_occluder(bool valid, uint32_t culled_count)
{
	s_terrain_occ_valid = (valid && s_terrain_occ.valid) ? 1 : 0;
	s_terrain_occ_culled = culled_count;
}

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

static void gl_cloud_alloc_dims(uint32_t w, uint32_t h, int cq, uint32_t *out_w, uint32_t *out_h)
{
	/* Allocate cloud FBO at the orbital ceiling so zoom-out scales smoothly via viewport sub-rect with zero reallocation:
	 * High: 1.00x native, Medium: 0.75x native, Low: 0.67x native. */
	float max_scale = (cq <= ND_CLOUD_QUALITY_LOW) ? 0.67f : ((cq == ND_CLOUD_QUALITY_MEDIUM) ? 0.75f : 1.00f);
	uint32_t cw = (uint32_t)((float)w * max_scale + 0.5f);
	uint32_t ch = (uint32_t)((float)h * max_scale + 0.5f);
	if (cw < 1u) cw = 1u;
	if (ch < 1u) ch = 1u;
	if (cw > w) cw = w;
	if (ch > h) ch = h;
	*out_w = cw;
	*out_h = ch;
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
	glActiveTexture(GL_TEXTURE0 + 8);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE0 + 11);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE0 + 12);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, 0);
	if (s_gl.scene_fbo) glDeleteFramebuffer(s_gl.scene_fbo);
	if (s_gl.res_fbo) glDeleteFramebuffer(s_gl.res_fbo);
	if (s_gl.cloud_fbo) glDeleteFramebuffer(s_gl.cloud_fbo);
	if (s_gl.cloud_q_fbo) glDeleteFramebuffer(s_gl.cloud_q_fbo);
	if (s_gl.cloud_hist_fbo[0]) glDeleteFramebuffer(s_gl.cloud_hist_fbo[0]);
	if (s_gl.cloud_hist_fbo[1]) glDeleteFramebuffer(s_gl.cloud_hist_fbo[1]);
	if (s_gl.ssao_fbo) glDeleteFramebuffer(s_gl.ssao_fbo);
	if (s_gl.bloom_fbo[0]) glDeleteFramebuffer(s_gl.bloom_fbo[0]);
	if (s_gl.bloom_fbo[1]) glDeleteFramebuffer(s_gl.bloom_fbo[1]);
	if (s_gl.scene_rb_color) glDeleteRenderbuffer(s_gl.scene_rb_color);
	if (s_gl.scene_rb_depth) glDeleteRenderbuffer(s_gl.scene_rb_depth);
	if (s_gl.res_color_tex) glDeleteTexture(s_gl.res_color_tex);
	if (s_gl.res_depth_tex) glDeleteTexture(s_gl.res_depth_tex);
	if (s_gl.cloud_tex) glDeleteTexture(s_gl.cloud_tex);
	if (s_gl.cloud_depth_tex) glDeleteTexture(s_gl.cloud_depth_tex);
	if (s_gl.cloud_q_tex) glDeleteTexture(s_gl.cloud_q_tex);
	if (s_gl.cloud_q_depth_tex) glDeleteTexture(s_gl.cloud_q_depth_tex);
	if (s_gl.cloud_hist_tex[0]) glDeleteTexture(s_gl.cloud_hist_tex[0]);
	if (s_gl.cloud_hist_tex[1]) glDeleteTexture(s_gl.cloud_hist_tex[1]);
	if (s_gl.ssao_tex) glDeleteTexture(s_gl.ssao_tex);
	if (s_gl.bloom_tex[0]) glDeleteTexture(s_gl.bloom_tex[0]);
	if (s_gl.bloom_tex[1]) glDeleteTexture(s_gl.bloom_tex[1]);
	for (int i = 0; i < ND_HIZ_MAX_LEVELS; i++) {
		if (s_gl.hiz_mip_fbo[i]) { glDeleteFramebuffer(s_gl.hiz_mip_fbo[i]); s_gl.hiz_mip_fbo[i] = 0; }
		if (s_gl.hiz_mip_tex[i] && s_gl.hiz_mip_tex[i] != s_gl.res_depth_tex) {
			glDeleteTexture(s_gl.hiz_mip_tex[i]);
		}
		s_gl.hiz_mip_tex[i] = 0;
	}
	s_gl.scene_fbo = s_gl.res_fbo = s_gl.cloud_fbo = 0;
	s_gl.cloud_q_fbo = s_gl.cloud_hist_fbo[0] = s_gl.cloud_hist_fbo[1] = 0;
	s_gl.ssao_fbo = s_gl.bloom_fbo[0] = s_gl.bloom_fbo[1] = 0;
	s_gl.scene_rb_color = s_gl.scene_rb_depth = 0;
	s_gl.res_color_tex = s_gl.res_depth_tex = s_gl.cloud_tex = s_gl.cloud_depth_tex = 0;
	s_gl.cloud_q_tex = s_gl.cloud_q_depth_tex = 0;
	s_gl.cloud_hist_tex[0] = s_gl.cloud_hist_tex[1] = 0;
	s_gl.ssao_tex = s_gl.bloom_tex[0] = s_gl.bloom_tex[1] = 0;
	s_gl.cloud_w = s_gl.cloud_h = 0;
	s_gl.cloud_qw = s_gl.cloud_qh = 0;
	s_gl.cloud_temporal_ok = false;
	s_gl.cloud_hist_valid = false;
	s_gl.cloud_hist_idx = 0;
	s_gl.post_half_w = s_gl.post_half_h = 0;
	s_gl.scene_want_samples = -1;
	s_gl.scene_hdr = false;
	s_gl.scene_hdr_r11f = false;
	s_gl.scene_depth24 = false;
	s_gl.post_targets_ok = false;
	s_gl.scene_ok = false;
	s_gl.gpu_hiz_built = false;
}

static bool gl_alloc_scene_targets(uint32_t w, uint32_t h, int samples, int hdr_mode, bool use_d24)
{
	/* hdr_mode: 2 = packed 4 B/px GL_R11F_G11F_B10F (stencil-backed water contract, zero scene alpha),
	 *           1 = 8 B/px GL_RGBA16F fallback,
	 *           0 = 4 B/px SDR GL_RGBA8 fallback.
	 * Cloud targets always retain an alpha channel (GL_RGBA16F when hdr_mode > 0 else GL_RGBA8). */
	GLenum scene_ifmt = (hdr_mode == 2) ? GL_R11F_G11F_B10F : ((hdr_mode == 1) ? GL_RGBA16F : GL_RGBA8);
	GLenum scene_fmt  = (hdr_mode == 2) ? GL_RGB            : GL_RGBA;
	GLenum scene_type = (hdr_mode == 2) ? GL_UNSIGNED_INT_10F_11F_11F_REV
	                                    : ((hdr_mode == 1) ? GL_HALF_FLOAT : GL_UNSIGNED_BYTE);
	GLenum cloud_ifmt = (hdr_mode > 0) ? GL_RGBA16F    : GL_RGBA8;
	GLenum cloud_type = (hdr_mode > 0) ? GL_HALF_FLOAT : GL_UNSIGNED_BYTE;
	GLenum depth_ifmt = use_d24 ? GL_DEPTH24_STENCIL8 : GL_DEPTH32F_STENCIL8;
	GLenum depth_type = use_d24 ? GL_UNSIGNED_INT_24_8 : GL_FLOAT_32_UNSIGNED_INT_24_8_REV;
	s_gl.scene_hdr = (hdr_mode > 0);
	s_gl.scene_hdr_r11f = (hdr_mode == 2);
	s_gl.scene_depth24 = use_d24;

	glActiveTexture(GL_TEXTURE0);
	/* Multisampled colour + depth renderbuffers (antialiased linear HDR or fallback SDR scene) */
	s_gl.scene_rb_color = glCreateRenderbuffer();
	glBindRenderbuffer(GL_RENDERBUFFER, s_gl.scene_rb_color);
	glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, scene_ifmt, (GLsizei)w, (GLsizei)h);
	s_gl.scene_rb_depth = glCreateRenderbuffer();
	glBindRenderbuffer(GL_RENDERBUFFER, s_gl.scene_rb_depth);
	glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, depth_ifmt, (GLsizei)w, (GLsizei)h);
	s_gl.scene_fbo = glCreateFramebuffer();
	glBindFramebuffer(GL_FRAMEBUFFER, s_gl.scene_fbo);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, s_gl.scene_rb_color);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, s_gl.scene_rb_depth);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return false;

	/* Resolved, sampleable colour + depth (strictly matches scene_rb_color format for glBlitFramebuffer) */
	s_gl.res_color_tex = glCreateTexture();
	glBindTexture(GL_TEXTURE_2D, s_gl.res_color_tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, scene_ifmt, (GLsizei)w, (GLsizei)h, 0, scene_fmt, scene_type, NULL);
	s_gl.res_depth_tex = glCreateTexture();
	glBindTexture(GL_TEXTURE_2D, s_gl.res_depth_tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, depth_ifmt, (GLsizei)w, (GLsizei)h, 0, GL_DEPTH_STENCIL,
	             depth_type, NULL);
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

	/* Volumetric cloud pass FBO (HDR RGBA color + sampleable depth/stencil texture for bilateral upsampling) */
	int cq = nd_seven_get_cloud_quality();
	uint32_t cw = 1u, ch = 1u;
	gl_cloud_alloc_dims(w, h, cq, &cw, &ch);
	s_gl.cloud_tex = glCreateTexture();
	glBindTexture(GL_TEXTURE_2D, s_gl.cloud_tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, cloud_ifmt, (GLsizei)cw, (GLsizei)ch, 0, GL_RGBA, cloud_type, NULL);
	s_gl.cloud_depth_tex = glCreateTexture();
	glBindTexture(GL_TEXTURE_2D, s_gl.cloud_depth_tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, depth_ifmt, (GLsizei)cw, (GLsizei)ch, 0, GL_DEPTH_STENCIL,
	             depth_type, NULL);
	s_gl.cloud_fbo = glCreateFramebuffer();
	glBindFramebuffer(GL_FRAMEBUFFER, s_gl.cloud_fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_gl.cloud_tex, 0);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, s_gl.cloud_depth_tex, 0);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return false;
	glViewport(0, 0, (GLsizei)cw, (GLsizei)ch);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glDepthMask(GL_TRUE);
	glStencilMask(0xFFu);
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClearStencil(0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
	glBindTexture(GL_TEXTURE_2D, 0);
	s_gl.cloud_w = cw;
	s_gl.cloud_h = ch;

	/* Phase 6C Piece 2: 2x2 interleaved temporal checkerboard cloud targets
	 * (quarter-area raymarch FBO + two ping-pong full-cloud-res history color FBOs). */
	uint32_t cqw = (cw >= 2u) ? (cw >> 1) : 1u;
	uint32_t cqh = (ch >= 2u) ? (ch >> 1) : 1u;
	s_gl.cloud_qw = cqw;
	s_gl.cloud_qh = cqh;
	s_gl.cloud_temporal_ok = false;
	s_gl.cloud_hist_valid = false;
	s_gl.cloud_hist_idx = 0;
	if (cw >= 4u && ch >= 4u) {
		bool temp_ok = true;
		s_gl.cloud_q_tex = glCreateTexture();
		glBindTexture(GL_TEXTURE_2D, s_gl.cloud_q_tex);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexImage2D(GL_TEXTURE_2D, 0, cloud_ifmt, (GLsizei)cqw, (GLsizei)cqh, 0, GL_RGBA, cloud_type, NULL);
		s_gl.cloud_q_depth_tex = glCreateTexture();
		glBindTexture(GL_TEXTURE_2D, s_gl.cloud_q_depth_tex);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexImage2D(GL_TEXTURE_2D, 0, depth_ifmt, (GLsizei)cqw, (GLsizei)cqh, 0, GL_DEPTH_STENCIL,
		             depth_type, NULL);
		s_gl.cloud_q_fbo = glCreateFramebuffer();
		glBindFramebuffer(GL_FRAMEBUFFER, s_gl.cloud_q_fbo);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_gl.cloud_q_tex, 0);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, s_gl.cloud_q_depth_tex, 0);
		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
			temp_ok = false;

		for (int h_i = 0; h_i < 2; h_i++) {
			s_gl.cloud_hist_tex[h_i] = glCreateTexture();
			glBindTexture(GL_TEXTURE_2D, s_gl.cloud_hist_tex[h_i]);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glTexImage2D(GL_TEXTURE_2D, 0, cloud_ifmt, (GLsizei)cw, (GLsizei)ch, 0, GL_RGBA, cloud_type, NULL);
			s_gl.cloud_hist_fbo[h_i] = glCreateFramebuffer();
			glBindFramebuffer(GL_FRAMEBUFFER, s_gl.cloud_hist_fbo[h_i]);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_gl.cloud_hist_tex[h_i], 0);
			if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
				temp_ok = false;
		}
		if (temp_ok) {
			glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
			glDepthMask(GL_TRUE);
			glStencilMask(0xFFu);
			glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
			glClearStencil(0);
			glBindFramebuffer(GL_FRAMEBUFFER, s_gl.cloud_q_fbo);
			glViewport(0, 0, (GLsizei)cqw, (GLsizei)cqh);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
			glViewport(0, 0, (GLsizei)cw, (GLsizei)ch);
			for (int h_i = 0; h_i < 2; h_i++) {
				glBindFramebuffer(GL_FRAMEBUFFER, s_gl.cloud_hist_fbo[h_i]);
				glClear(GL_COLOR_BUFFER_BIT);
			}
			s_gl.cloud_temporal_ok = true;
		} else {
			if (s_gl.cloud_q_fbo) { glDeleteFramebuffer(s_gl.cloud_q_fbo); s_gl.cloud_q_fbo = 0; }
			if (s_gl.cloud_q_tex) { glDeleteTexture(s_gl.cloud_q_tex); s_gl.cloud_q_tex = 0; }
			if (s_gl.cloud_q_depth_tex) { glDeleteTexture(s_gl.cloud_q_depth_tex); s_gl.cloud_q_depth_tex = 0; }
			for (int h_i = 0; h_i < 2; h_i++) {
				if (s_gl.cloud_hist_fbo[h_i]) { glDeleteFramebuffer(s_gl.cloud_hist_fbo[h_i]); s_gl.cloud_hist_fbo[h_i] = 0; }
				if (s_gl.cloud_hist_tex[h_i]) { glDeleteTexture(s_gl.cloud_hist_tex[h_i]); s_gl.cloud_hist_tex[h_i] = 0; }
			}
		}
		glBindTexture(GL_TEXTURE_2D, 0);
	}

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

	/* Half-resolution SSAO + Ping-Pong Bloom targets (soft fallback if unsupported on target GPU) */
	uint32_t pw = (w > 1u) ? (w >> 1) : 1u;
	uint32_t ph = (h > 1u) ? (h >> 1) : 1u;
	s_gl.post_half_w = pw;
	s_gl.post_half_h = ph;
	s_gl.post_targets_ok = false;

	s_gl.ssao_tex = glCreateTexture();
	glBindTexture(GL_TEXTURE_2D, s_gl.ssao_tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, (GLsizei)pw, (GLsizei)ph, 0, GL_RED, GL_UNSIGNED_BYTE, NULL);
	s_gl.ssao_fbo = glCreateFramebuffer();
	glBindFramebuffer(GL_FRAMEBUFFER, s_gl.ssao_fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_gl.ssao_tex, 0);
	bool post_ok = (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);

	for (int b = 0; b < 2; b++) {
		s_gl.bloom_tex[b] = glCreateTexture();
		glBindTexture(GL_TEXTURE_2D, s_gl.bloom_tex[b]);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexImage2D(GL_TEXTURE_2D, 0, scene_ifmt, (GLsizei)pw, (GLsizei)ph, 0, scene_fmt, scene_type, NULL);
		s_gl.bloom_fbo[b] = glCreateFramebuffer();
		glBindFramebuffer(GL_FRAMEBUFFER, s_gl.bloom_fbo[b]);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_gl.bloom_tex[b], 0);
		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
			post_ok = false;
	}
	if (post_ok) {
		glViewport(0, 0, (GLsizei)pw, (GLsizei)ph);
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		glBindFramebuffer(GL_FRAMEBUFFER, s_gl.ssao_fbo);
		glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
		for (int b = 0; b < 2; b++) {
			glBindFramebuffer(GL_FRAMEBUFFER, s_gl.bloom_fbo[b]);
			glClear(GL_COLOR_BUFFER_BIT);
		}
		s_gl.post_targets_ok = true;
	} else {
		if (s_gl.ssao_fbo) { glDeleteFramebuffer(s_gl.ssao_fbo); s_gl.ssao_fbo = 0; }
		if (s_gl.ssao_tex) { glDeleteTexture(s_gl.ssao_tex); s_gl.ssao_tex = 0; }
		for (int b = 0; b < 2; b++) {
			if (s_gl.bloom_fbo[b]) { glDeleteFramebuffer(s_gl.bloom_fbo[b]); s_gl.bloom_fbo[b] = 0; }
			if (s_gl.bloom_tex[b]) { glDeleteTexture(s_gl.bloom_tex[b]); s_gl.bloom_tex[b] = 0; }
		}
	}

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glBindTexture(GL_TEXTURE_2D, 0);

	/* Create Hi-Z downsample program using full-screen triangle vertex shader s_vs_csm */
	if (!s_gl.hiz_downsample_prog) {
		s_gl.hiz_downsample_prog = create_program_async(nd_seven_gl_vs_csm(), nd_seven_gl_fs_hiz_downsample());
		s_gl.hiz_prog_ready = false;
		s_gl.hiz_uDepthTex = -1;
		s_gl.hiz_uTexelSize = -1;
		s_gl.hiz_uUvScale = -1;
	}
	return true;
}

/* (Re)create the scene targets for the stage size; tries packed HDR GL_R11F_G11F_B10F + GL_DEPTH24_STENCIL8
 * (8 B/px/sample, 50% less bandwidth than RGBA16F+D32FS8), then falls back cleanly to RGBA16F / D32FS8 / SDR / 0-MSAA. */
void gl_ensure_scene_targets(uint32_t w, uint32_t h)
{
	if (s_gl.scene_failed || w == 0 || h == 0) return;
	int want_samples = nd_seven_get_msaa_samples();
	int cq = nd_seven_get_cloud_quality();
	uint32_t want_cw = 1u, want_ch = 1u;
	gl_cloud_alloc_dims(w, h, cq, &want_cw, &want_ch);
	if (s_gl.scene_ok && s_gl.scene_w == w && s_gl.scene_h == h &&
	    s_gl.scene_want_samples == want_samples &&
	    s_gl.cloud_w == want_cw && s_gl.cloud_h == want_ch)
		return;
	if (s_gl.scene_fbo || s_gl.res_fbo) gl_free_scene_targets();
	typedef struct { int samples; int hdr_mode; bool d24; } gl_target_try_t;
	gl_target_try_t tries[10];
	int num_tries = 0;
	if (want_samples > 0) {
		tries[num_tries++] = (gl_target_try_t){ want_samples, 2, true  };
		tries[num_tries++] = (gl_target_try_t){ want_samples, 1, true  };
		tries[num_tries++] = (gl_target_try_t){ want_samples, 1, false };
		tries[num_tries++] = (gl_target_try_t){ want_samples, 0, true  };
		if (want_samples > 2) {
			tries[num_tries++] = (gl_target_try_t){ 2, 2, true  };
			tries[num_tries++] = (gl_target_try_t){ 2, 1, true  };
		}
	}
	tries[num_tries++] = (gl_target_try_t){ 0, 2, true  };
	tries[num_tries++] = (gl_target_try_t){ 0, 1, true  };
	tries[num_tries++] = (gl_target_try_t){ 0, 1, false };
	tries[num_tries++] = (gl_target_try_t){ 0, 0, true  };
	for (int i = 0; i < num_tries; i++) {
		if (gl_alloc_scene_targets(w, h, tries[i].samples, tries[i].hdr_mode, tries[i].d24)) {
			s_gl.scene_ok = true;
			s_gl.scene_w = w;
			s_gl.scene_h = h;
			s_gl.scene_samples = tries[i].samples;
			s_gl.scene_want_samples = want_samples;
			s_gl.scene_hdr = (tries[i].hdr_mode > 0);
			s_gl.scene_hdr_r11f = (tries[i].hdr_mode == 2);
			s_gl.scene_depth24 = tries[i].d24;
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
	if (!prog) return false;
	if (!s_gl.hiz_prog_ready) {
		if (!glGetProgramParameter(prog, GL_COMPLETION_STATUS_KHR)) return false;
		if (!glGetProgramParameter(prog, GL_LINK_STATUS)) {
			glDeleteProgram(prog);
			s_gl.hiz_downsample_prog = 0;
			return false;
		}
		s_gl.hiz_uDepthTex  = glGetUniformLocation(prog, "uDepthTex", 9);
		s_gl.hiz_uTexelSize = glGetUniformLocation(prog, "uTexelSize", 10);
		s_gl.hiz_uUvScale   = glGetUniformLocation(prog, "uUvScale", 8);
		s_gl.hiz_prog_ready = true;
	}

	/* Flush deferred terrain pass before resolving active depth sub-rect into s_gl.res_depth_tex (level 0) */
	gl_end_terrain_pass();
	uint32_t rw = (s_gl.render_w > 0 && s_gl.render_w <= s_gl.scene_w) ? s_gl.render_w : s_gl.scene_w;
	uint32_t rh = (s_gl.render_h > 0 && s_gl.render_h <= s_gl.scene_h) ? s_gl.render_h : s_gl.scene_h;
	gl_resolve_scene(GL_DEPTH_BUFFER_BIT);

	glTimerBegin(ND_GPU_PASS_HIZ);
	glUseProgram(prog);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

	GLint loc_tex = s_gl.hiz_uDepthTex;
	GLint loc_ts  = s_gl.hiz_uTexelSize;
	GLint loc_uvs = s_gl.hiz_uUvScale;
	glUniform1i(loc_tex, 0);

	float scene_uv_x = (s_gl.scene_w > 0) ? ((float)rw / (float)s_gl.scene_w) : 1.0f;
	float scene_uv_y = (s_gl.scene_h > 0) ? ((float)rh / (float)s_gl.scene_h) : 1.0f;

	for (int level = 1; level < s_gl.hiz_levels; level++) {
		uint32_t lw = (s_gl.hiz_w > (1u << level)) ? (s_gl.hiz_w >> level) : 1u;
		uint32_t lh = (s_gl.hiz_h > (1u << level)) ? (s_gl.hiz_h >> level) : 1u;
		uint32_t sw = (s_gl.hiz_w > (1u << (level - 1))) ? (s_gl.hiz_w >> (level - 1)) : 1u;
		uint32_t sh = (s_gl.hiz_h > (1u << (level - 1))) ? (s_gl.hiz_h >> (level - 1)) : 1u;

		glBindFramebuffer(GL_FRAMEBUFFER, s_gl.hiz_mip_fbo[level]);
		glViewport(0, 0, (GLsizei)lw, (GLsizei)lh);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, s_gl.hiz_mip_tex[level - 1]);
		glUniform2f(loc_ts, 1.0f / (float)sw, 1.0f / (float)sh);
		if (loc_uvs >= 0) {
			if (level == 1)
				glUniform2f(loc_uvs, scene_uv_x, scene_uv_y);
			else if (level == 2)
				glUniform2f(loc_uvs, 1.0f, 1.0f);
		}
		glDrawArrays(GL_TRIANGLES, 0, 3);
	}

	s_gl.gpu_hiz_built = true;
	glBindTexture(GL_TEXTURE_2D, 0);
	glTimerEnd(ND_GPU_PASS_HIZ);
	gl_bind_scene_target();
	glEnable(GL_DEPTH_TEST);
	return true;
}

/* GPU-driven Hi-Z culling samples uHiZTex directly in the vertex shader; CPU readback queries return 0. */
int nd_seven_hiz_test_sphere(float cx, float cy, float cz, float radius)
{
    (void)cx; (void)cy; (void)cz; (void)radius;
    return 0;
}

/* 3D 2D-Hi-Z terrain occlusion query for water queue gating (works at all 360-deg yaws & pitches):
 * Returns 1 if the chunk's curved-torus water surface AABB at sea_level is 100% occluded by
 * foreground terrain/planetary limb in s_terrain_occ. */
int nd_seven_is_water_occluded_at(int32_t ox, int32_t oy, int32_t span_w, int32_t span_h,
                                  float sea_level, const nd_planet_def_t *pdef)
{
	if (!s_terrain_occ_valid || !s_terrain_occ.valid || sea_level <= -900.0f)
		return 0;

	const nd_planet_def_t *eff = pdef ? pdef : &s_terrain_occ_pdef;
	if (eff->major_r <= 1.0f || eff->minor_r <= 1.0f)
		return 0;

	corolla_surface_t surf = corolla_torus((double)eff->major_r, (double)eff->minor_r);
	corolla_pose_t pose;
	memset(&pose, 0, sizeof(pose));
	for (int i = 0; i < 9; i++)
		pose.rot[i] = (double)eff->rot[i];
	pose.center[0] = (double)eff->center[0];
	pose.center[1] = (double)eff->center[1];
	pose.center[2] = (double)eff->center[2];

	double u0 = (double)ox / (double)ND_SEVEN_WORLD_DIM_W;
	double du = (double)span_w / (double)ND_SEVEN_WORLD_DIM_W;
	double v0 = ((double)oy / (double)ND_SEVEN_WORLD_DIM_H) + 0.5;
	double dv = (double)span_h / (double)ND_SEVEN_WORLD_DIM_H;

	double bmin[3], bmax[3];
	/* Pad vertically by [-8..+16] wu for waves/swash so visible water is never falsely culled */
	corolla_patch_aabb(&surf, &pose, s_terrain_occ_origin, u0, v0, du, dv,
	                   (double)sea_level - 8.0, (double)sea_level + 16.0, bmin, bmax);

	float bmin_f[3] = { (float)bmin[0], (float)bmin[1], (float)bmin[2] };
	float bmax_f[3] = { (float)bmax[0], (float)bmax[1], (float)bmax[2] };
	return ar_occluder_test_box(&s_terrain_occ, bmin_f, bmax_f) ? 1 : 0;
}

int nd_seven_is_water_ridge_occluded(int32_t ox, int32_t oy, int32_t span_w, int32_t span_h,
                                     const float flat_mvp[16], float lwx, float lwy, float r_planet)
{
	(void)flat_mvp;
	(void)lwx;
	(void)lwy;
	(void)r_planet;
	return nd_seven_is_water_occluded_at(ox, oy, span_w, span_h,
	                                     s_gl.cur_sea_level, &s_terrain_occ_pdef);
}

int nd_seven_hiz_test_cloud_chunk(int32_t ox, int32_t oy, int32_t span_w, int32_t span_h,
                                   const float mvp[16], float cloud_z)
{
    (void)ox; (void)oy; (void)span_w; (void)span_h; (void)mvp; (void)cloud_z;
    return 0;
}

int nd_seven_hiz_test_billboard_batch(const lam_billboard_gpu_t *batch, uint32_t count,
                                       const float mvp[16], float max_radius)
{
    (void)batch; (void)count; (void)mvp; (void)max_radius;
    return 0;
}

int nd_seven_occ2d_test_rect(float ndc_x0, float ndc_y0, float ndc_x1, float ndc_y1, float near_z)
{
    (void)ndc_x0; (void)ndc_y0; (void)ndc_x1; (void)ndc_y1; (void)near_z;
    return 0;
}

int nd_seven_occ2d_test_water_chunk(int32_t ox, int32_t oy, int32_t span_w, int32_t span_h,
                                     const float mvp[16], float water_z)
{
    (void)ox; (void)oy; (void)span_w; (void)span_h; (void)mvp; (void)water_z;
    return 0;
}

/* Bind the frame's render target (scene FBO, or the canvas if offscreen targets are unavailable) and active sub-rect viewport. */
void gl_bind_scene_target(void)
{
	gl_flush_cmd_ring();
	if (s_gl.scene_ok) {
		uint32_t rw = (s_gl.render_w > 0 && s_gl.render_w <= s_gl.scene_w) ? s_gl.render_w : s_gl.scene_w;
		uint32_t rh = (s_gl.render_h > 0 && s_gl.render_h <= s_gl.scene_h) ? s_gl.render_h : s_gl.scene_h;
		glBindFramebuffer(GL_FRAMEBUFFER, s_gl.scene_fbo);
		glViewport(0, 0, (GLsizei)rw, (GLsizei)rh);
	} else {
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glViewport(0, 0, (GLsizei)s_gl.stage_w, (GLsizei)s_gl.stage_h);
	}
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
	s_gl.tree_shadow_skip_write = false;
	s_gl.tree_shadow_rendered_time = -1000.0f;
	s_gl.tree_shadow_rendered_preset = -1;
	return true;
}

void gl_prepare_tree_shadow_frame(float tx, float ty, float tz,
                                         const float E[3], const float S[3], const float N[3])
{
	s_gl.tree_shadow_write_draws = false;
	s_gl.tree_shadow_skip_write = false;
	s_gl.terr_shadow_q_count = 0;
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
	float raw_half = fminf(1024.0f, fmaxf(160.0f, s_gl.cur_dist * 0.45f));
	float half_xy = floorf(raw_half / 32.0f + 0.5f) * 32.0f;
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
	s_gl.tree_shadow_write_half_xy = half_xy;

	float inv_xy = 1.0f / half_xy;
	float inv_z  = 1.0f / half_z;
	float *m = s_gl.tree_shadow_write_vp;
	/* Column-major mat4: x = dot(p, R)/half_xy, y = dot(p, U)/half_xy, z = -dot(p, L)/half_z (smaller depth closer to sun) */
	m[0]  = rx * inv_xy;  m[1]  = ux * inv_xy;  m[2]  = -lx * inv_z;  m[3]  = 0.0f;
	m[4]  = ry * inv_xy;  m[5]  = uy * inv_xy;  m[6]  = -ly * inv_z;  m[7]  = 0.0f;
	m[8]  = rz * inv_xy;  m[9]  = uz * inv_xy;  m[10] = -lz * inv_z;  m[11] = 0.0f;
	m[12] = 0.0f;         m[13] = 0.0f;         m[14] = 0.0f;         m[15] = 1.0f;

	s_gl.tree_shadow_active = s_gl.tree_shadow_read_valid ? dist_fade : 0.0f;

	/* Dirty-gate 1024x1024 shadow FBO writes: reuse cached shadow map while snapped origin,
	 * light VP, and quality preset are unchanged (time-based TTL for wind sway: 0.16s High / 0.40s Low). */
	int preset = nd_seven_get_quality_preset();
	float max_dt = (preset == ND_QUALITY_LOW) ? 0.40f : 0.16f;
	if (s_gl.tree_shadow_read_valid &&
	    s_gl.tree_shadow_rendered_preset == preset &&
	    fabsf(s_gl.cur_anim_time - s_gl.tree_shadow_rendered_time) < max_dt) {
		float d0 = fabsf(s_gl.tree_shadow_write_origin[0] - s_gl.tree_shadow_read_origin[0]);
		float d1 = fabsf(s_gl.tree_shadow_write_origin[1] - s_gl.tree_shadow_read_origin[1]);
		float d2 = fabsf(s_gl.tree_shadow_write_origin[2] - s_gl.tree_shadow_read_origin[2]);
		float vp_diff = 0.0f;
		for (int k = 0; k < 16; k++) {
			float dv = fabsf(s_gl.tree_shadow_write_vp[k] - s_gl.tree_shadow_read_vp[k]);
			if (dv > vp_diff) vp_diff = dv;
		}
		if (d0 < 0.05f && d1 < 0.05f && d2 < 0.05f && vp_diff < 1e-4f) {
			s_gl.tree_shadow_skip_write = true;
			return;
		}
	}

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

void gl_bind_tree_shadow_read(GLint uTex)
{
	GLuint dummy = gl_ensure_tree_shadow_dummy();
	bool active = (s_gl.tree_shadow_ready && s_gl.tree_shadow_read_valid && s_gl.tree_shadow_active > 0.001f);
	glActiveTexture(GL_TEXTURE0 + 12);
	glBindTexture(GL_TEXTURE_2D, active ? s_gl.tree_shadow_tex[s_gl.tree_shadow_read_idx] : dummy);
	if (uTex >= 0) glUniform1i(uTex, 12);
	glActiveTexture(GL_TEXTURE0);
}

/* Resolve the multisampled scene active sub-rect into the sampleable colour (and depth) textures,
 * skipping any attachment whose resolved copy is still valid since the last write. */
void gl_resolve_scene(GLbitfield mask)
{
	gl_flush_cmd_ring();
	if (!s_gl.scene_ok) return;
	GLbitfield eff = mask;
	if (s_gl.res_depth_valid) eff &= ~(GLbitfield)GL_DEPTH_BUFFER_BIT;
	if (s_gl.res_color_valid) eff &= ~(GLbitfield)GL_COLOR_BUFFER_BIT;
	if (!eff) return;
	uint32_t rw = (s_gl.render_w > 0 && s_gl.render_w <= s_gl.scene_w) ? s_gl.render_w : s_gl.scene_w;
	uint32_t rh = (s_gl.render_h > 0 && s_gl.render_h <= s_gl.scene_h) ? s_gl.render_h : s_gl.scene_h;
	glBindFramebuffer(GL_READ_FRAMEBUFFER, s_gl.scene_fbo);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, s_gl.res_fbo);
	glBlitFramebuffer(0, 0, (GLint)rw, (GLint)rh,
	                  0, 0, (GLint)rw, (GLint)rh, eff, GL_NEAREST);
	if (eff & GL_DEPTH_BUFFER_BIT) s_gl.res_depth_valid = true;
	if (eff & GL_COLOR_BUFFER_BIT) s_gl.res_color_valid = true;
	gl_bind_scene_target();
}

void nd_seven_gl_end_frame(void)
{
	if (!s_gl.frame_open) return;
	gl_flush_cmd_ring();
	/* Standalone test / non-macro fallback: ensure sky renders before final resolve if not yet flushed */
	if (!s_gl.sky_rendered && s_gl.pipelines_ready)
		nd_seven_gl_flush_sky();
	s_gl.frame_open = false;
	if (s_gl.tree_shadow_ready) {
		if (s_gl.tree_shadow_skip_write) {
			/* Keep cached read shadow map intact */
		} else if (s_gl.tree_shadow_write_draws) {
			s_gl.tree_shadow_read_idx = 1 - s_gl.tree_shadow_read_idx;
			memcpy(s_gl.tree_shadow_read_origin, s_gl.tree_shadow_write_origin, sizeof(s_gl.tree_shadow_read_origin));
			memcpy(s_gl.tree_shadow_read_vp, s_gl.tree_shadow_write_vp, sizeof(s_gl.tree_shadow_read_vp));
			s_gl.tree_shadow_read_valid = true;
			s_gl.tree_shadow_rendered_time = s_gl.cur_anim_time;
			s_gl.tree_shadow_rendered_preset = nd_seven_get_quality_preset();
		} else {
			s_gl.tree_shadow_read_valid = false;
			s_gl.tree_shadow_rendered_time = -1000.0f;
			s_gl.tree_shadow_rendered_preset = -1;
		}
	}
	if (!s_gl.scene_ok) return;
	uint32_t rw = (s_gl.render_w > 0 && s_gl.render_w <= s_gl.scene_w) ? s_gl.render_w : s_gl.scene_w;
	uint32_t rh = (s_gl.render_h > 0 && s_gl.render_h <= s_gl.scene_h) ? s_gl.render_h : s_gl.scene_h;
	uint32_t prw = (s_gl.post_rw > 0 && s_gl.post_rw <= s_gl.post_half_w) ? s_gl.post_rw : s_gl.post_half_w;
	uint32_t prh = (s_gl.post_rh > 0 && s_gl.post_rh <= s_gl.post_half_h) ? s_gl.post_rh : s_gl.post_half_h;
	float scene_uv_x = (s_gl.scene_w > 0) ? ((float)rw / (float)s_gl.scene_w) : 1.0f;
	float scene_uv_y = (s_gl.scene_h > 0) ? ((float)rh / (float)s_gl.scene_h) : 1.0f;
	float post_uv_x = (s_gl.post_half_w > 0) ? ((float)prw / (float)s_gl.post_half_w) : 1.0f;
	float post_uv_y = (s_gl.post_half_h > 0) ? ((float)prh / (float)s_gl.post_half_h) : 1.0f;

	bool run_post = (s_gl.pipelines_ready && s_gl.post_pipe.prog && s_gl.post_targets_ok);
	float ssao_fade = (run_post && s_gl.ssao_pipe.prog && s_gl.ssao_fbo && s_gl.ssao_tex && s_gl.cur_dist <= 2200.0f)
		? fminf(1.0f, fmaxf(0.0f, (2200.0f - s_gl.cur_dist) / 1000.0f))
		: 0.0f;
	gl_end_terrain_pass();
	GLbitfield end_resolve_mask = GL_COLOR_BUFFER_BIT | ((ssao_fade > 0.001f) ? (GLbitfield)GL_DEPTH_BUFFER_BIT : 0u);
	gl_resolve_scene(end_resolve_mask);
	if (run_post) {
		glTimerBegin(ND_GPU_PASS_POST);
		glDisableVertexAttribArray(0);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
		glDisable(GL_DEPTH_TEST);
		glDepthMask(GL_FALSE);
		glDisable(GL_STENCIL_TEST);
		glDisable(GL_CULL_FACE);
		glDisable(GL_BLEND);
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

		if (ssao_fade > 0.001f) {
			glBindFramebuffer(GL_FRAMEBUFFER, s_gl.ssao_fbo);
			glViewport(0, 0, (GLsizei)prw, (GLsizei)prh);
			glUseProgram(s_gl.ssao_pipe.prog);
			glActiveTexture(GL_TEXTURE0 + 5);
			glBindTexture(GL_TEXTURE_2D, s_gl.res_depth_tex);
			glUniform1i(s_gl.ssao_pipe.uDepthTex, 5);
			glUniform2f(s_gl.ssao_pipe.uNearFar, s_gl.cur_near, s_gl.cur_far);
			glUniform2f(s_gl.ssao_pipe.uProjScale, s_gl.cur_tan_half * s_gl.cur_aspect, s_gl.cur_tan_half);
			glUniform2f(s_gl.ssao_pipe.uViewport, (float)prw, (float)prh);
			glUniform1f(s_gl.ssao_pipe.uAnimTime, s_gl.cur_anim_time);
			if (s_gl.ssao_pipe.uSceneUvScale >= 0)
				glUniform2f(s_gl.ssao_pipe.uSceneUvScale, scene_uv_x, scene_uv_y);
			glDrawArrays(GL_TRIANGLES, 0, 3);
		}

		bool do_bloom = (nd_seven_get_quality_preset() != ND_QUALITY_LOW &&
		                 s_gl.bloom_pipe.prog && s_gl.bloom_fbo[0] && s_gl.bloom_fbo[1] &&
		                 s_gl.bloom_tex[0] && s_gl.bloom_tex[1]);
		if (do_bloom) {
			glViewport(0, 0, (GLsizei)prw, (GLsizei)prh);
			glUseProgram(s_gl.bloom_pipe.prog);

			/* Pass 0: 2x2 downsample + soft-knee highlight extraction */
			glBindFramebuffer(GL_FRAMEBUFFER, s_gl.bloom_fbo[0]);
			glActiveTexture(GL_TEXTURE0 + 4);
			glBindTexture(GL_TEXTURE_2D, s_gl.res_color_tex);
			glUniform1i(s_gl.bloom_pipe.uSrcTex, 4);
			glUniform2f(s_gl.bloom_pipe.uTexelSize, 1.0f / (float)s_gl.scene_w, 1.0f / (float)s_gl.scene_h);
			glUniform2f(s_gl.bloom_pipe.uPassDir, 0.0f, 0.0f);
			if (s_gl.bloom_pipe.uUvScale >= 0)
				glUniform2f(s_gl.bloom_pipe.uUvScale, scene_uv_x, scene_uv_y);
			glDrawArrays(GL_TRIANGLES, 0, 3);

			/* Pass 1: horizontal separable Gaussian blur */
			glBindFramebuffer(GL_FRAMEBUFFER, s_gl.bloom_fbo[1]);
			glBindTexture(GL_TEXTURE_2D, s_gl.bloom_tex[0]);
			glUniform1i(s_gl.bloom_pipe.uSrcTex, 4);
			glUniform2f(s_gl.bloom_pipe.uTexelSize, 1.0f / (float)s_gl.post_half_w, 1.0f / (float)s_gl.post_half_h);
			glUniform2f(s_gl.bloom_pipe.uPassDir, 1.0f, 0.0f);
			if (s_gl.bloom_pipe.uUvScale >= 0)
				glUniform2f(s_gl.bloom_pipe.uUvScale, post_uv_x, post_uv_y);
			glDrawArrays(GL_TRIANGLES, 0, 3);

			/* Pass 2: vertical separable Gaussian blur */
			glBindFramebuffer(GL_FRAMEBUFFER, s_gl.bloom_fbo[0]);
			glBindTexture(GL_TEXTURE_2D, s_gl.bloom_tex[1]);
			glUniform1i(s_gl.bloom_pipe.uSrcTex, 4);
			glUniform2f(s_gl.bloom_pipe.uTexelSize, 1.0f / (float)s_gl.post_half_w, 1.0f / (float)s_gl.post_half_h);
			glUniform2f(s_gl.bloom_pipe.uPassDir, 0.0f, 1.0f);
			if (s_gl.bloom_pipe.uUvScale >= 0)
				glUniform2f(s_gl.bloom_pipe.uUvScale, post_uv_x, post_uv_y);
			glDrawArrays(GL_TRIANGLES, 0, 3);
		}

		/* Final HDR ACES tone-mapping + native-res FXAA + SSAO + bloom composite to default framebuffer 0 */
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glViewport(0, 0, (GLsizei)s_gl.stage_w, (GLsizei)s_gl.stage_h);
		glUseProgram(s_gl.post_pipe.prog);
		glActiveTexture(GL_TEXTURE0 + 4);
		glBindTexture(GL_TEXTURE_2D, s_gl.res_color_tex);
		glUniform1i(s_gl.post_pipe.uSceneTex, 4);
		glActiveTexture(GL_TEXTURE0 + 6);
		glBindTexture(GL_TEXTURE_2D, s_gl.bloom_tex[0]);
		glUniform1i(s_gl.post_pipe.uBloomTex, 6);
		glActiveTexture(GL_TEXTURE0 + 8);
		glBindTexture(GL_TEXTURE_2D, s_gl.ssao_tex);
		glUniform1i(s_gl.post_pipe.uSsaoTex, 8);
		glUniform1f(s_gl.post_pipe.uSsaoActive, ssao_fade);
		glUniform1f(s_gl.post_pipe.uBloomActive, do_bloom ? 1.0f : 0.0f);
		glUniform1f(s_gl.post_pipe.uExposure, 1.12f);
		glUniform1f(s_gl.post_pipe.uCamDist, s_gl.cur_dist);
		if (s_gl.post_pipe.uSceneUvScale >= 0)
			glUniform2f(s_gl.post_pipe.uSceneUvScale, scene_uv_x, scene_uv_y);
		if (s_gl.post_pipe.uPostUvScale >= 0)
			glUniform2f(s_gl.post_pipe.uPostUvScale, post_uv_x, post_uv_y);
		if (s_gl.post_pipe.uSceneTexel >= 0)
			glUniform2f(s_gl.post_pipe.uSceneTexel, 1.0f / (float)s_gl.scene_w, 1.0f / (float)s_gl.scene_h);
		if (s_gl.post_pipe.uFxaaActive >= 0)
			glUniform1f(s_gl.post_pipe.uFxaaActive,
			            (s_gl.scene_samples == 0 || s_gl.cur_dist > 100000.0f) ? 1.0f : 0.0f);
		glDrawArrays(GL_TRIANGLES, 0, 3);

		glActiveTexture(GL_TEXTURE0 + 8);
		glBindTexture(GL_TEXTURE_2D, 0);
		glActiveTexture(GL_TEXTURE0 + 6);
		glBindTexture(GL_TEXTURE_2D, 0);
		glActiveTexture(GL_TEXTURE0 + 5);
		glBindTexture(GL_TEXTURE_2D, 0);
		glActiveTexture(GL_TEXTURE0 + 4);
		glBindTexture(GL_TEXTURE_2D, 0);
		glActiveTexture(GL_TEXTURE0);
		glEnable(GL_DEPTH_TEST);
		glDepthMask(GL_TRUE);
		glTimerEnd(ND_GPU_PASS_POST);
	} else {
		glBindFramebuffer(GL_READ_FRAMEBUFFER, s_gl.res_fbo);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		glBlitFramebuffer(0, 0, (GLint)rw, (GLint)rh,
		                  0, 0, (GLint)s_gl.stage_w, (GLint)s_gl.stage_h, GL_COLOR_BUFFER_BIT, GL_LINEAR);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}

	/* The sprite marker lives in the scene alpha: the canvas must end up fully opaque */
	if (!run_post) {
		glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
		glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	}
}

void nd_seven_gl_get_target_info(uint32_t *out_stage_w, uint32_t *out_stage_h,
                                 uint32_t *out_scene_w, uint32_t *out_scene_h,
                                 int *out_samples,
                                 uint32_t *out_cloud_w, uint32_t *out_cloud_h,
                                 bool *out_csm_active)
{
	if (out_stage_w) *out_stage_w = s_gl.stage_w;
	if (out_stage_h) *out_stage_h = s_gl.stage_h;
	if (out_scene_w) *out_scene_w = s_gl.render_w ? s_gl.render_w : s_gl.scene_w;
	if (out_scene_h) *out_scene_h = s_gl.render_h ? s_gl.render_h : s_gl.scene_h;
	if (out_samples) *out_samples = s_gl.scene_samples;
	if (out_cloud_w) *out_cloud_w = s_gl.cloud_rw ? s_gl.cloud_rw : s_gl.cloud_w;
	if (out_cloud_h) *out_cloud_h = s_gl.cloud_rh ? s_gl.cloud_rh : s_gl.cloud_h;
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

void nd_seven_gl_get_perf_counters(uint32_t *out_terrain_chunks,
                                   uint32_t *out_bb_flushes,
                                   uint32_t *out_mesh3d_count,
                                   uint32_t *out_bb_count)
{
	if (out_terrain_chunks) *out_terrain_chunks = s_gl.perf_terrain_chunks;
	if (out_bb_flushes)     *out_bb_flushes     = s_gl.perf_bb_flushes;
	if (out_mesh3d_count)   *out_mesh3d_count   = s_gl.perf_mesh3d_count;
	if (out_bb_count)       *out_bb_count       = s_gl.perf_bb_count;
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
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, ND_CSM_RES, ND_CSM_RES, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
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
	float tSyn = anim_time * 0.05f;
	float tFlow = gl_wrap1024(tSyn * 0.005f);
	float phase0 = gl_wrap1024(tSyn * 0.004f);
	float phase1 = gl_wrap1024(tSyn * 0.008f);

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
		float phase2 = gl_wrap1024(anim_time * 0.05f * 0.015f);
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

	float sun_xy_len = sqrtf(s_gl.cur_sun_dir[0] * s_gl.cur_sun_dir[0] + s_gl.cur_sun_dir[1] * s_gl.cur_sun_dir[1]);
	float sun_px = (sun_xy_len > 0.05f) ? (-s_gl.cur_sun_dir[0] / sun_xy_len) : -0.70710678f;
	float sun_py = (sun_xy_len > 0.05f) ? (-s_gl.cur_sun_dir[1] / sun_xy_len) : -0.70710678f;
	float orbit_off = fminf(14000.0f, fmaxf(0.0f, s_gl.cur_dist * 0.045f));
	float dz_ref = fmaxf(0.0f, 2200.0f - fmaxf(0.0f, s_gl.cur_tz)) + orbit_off;
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

	/* --- RENDER CURRENT CSM --- */
	gl_end_terrain_pass();
	glTimerBegin(ND_GPU_PASS_CSM);
	glBindFramebuffer(GL_FRAMEBUFFER, s_gl.csm_fbo);
	glViewport(0, 0, ND_CSM_RES, ND_CSM_RES);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glUseProgram(s_gl.csm_pipe.prog);
	glActiveTexture(GL_TEXTURE0 + 15);
	glBindTexture(GL_TEXTURE_3D, s_gl.cloud_shape3d_tex);
	glActiveTexture(GL_TEXTURE0);
	glUniform3f(s_gl.csm_pipe.uCsm, s_gl.csm_win.cx, s_gl.csm_win.cy, s_gl.csm_win.half);
	glUniform1f(s_gl.csm_pipe.uAnimTime, s_gl.cur_anim_time);
	glUniform1f(s_gl.csm_pipe.uSeaWet, sea_wet);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	glTimerEnd(ND_GPU_PASS_CSM);

	gl_bind_scene_target();
	glEnable(GL_DEPTH_TEST);
	s_gl.csm_rendered_win = s_gl.csm_win;
	s_gl.csm_rendered_time = s_gl.cur_anim_time;
	s_gl.csm_rendered_wet = sea_wet;
	s_gl.csm_has_cache = true;
	s_gl.csm_valid = true;
}

static bool gl_ensure_cloud_wth_targets(void)
{
	if (s_gl.cloud_wth_rings[0].fbo && s_gl.cloud_wth_rings[1].fbo)
		return true;
	if (s_gl.cloud_wth_failed)
		return false;

	glActiveTexture(GL_TEXTURE0);
	for (int r = 0; r < ND_CLOUD_WTH_RINGS; r++) {
		gl_cloud_wth_ring_t *ring = &s_gl.cloud_wth_rings[r];
		ring->tex = glCreateTexture();
		glBindTexture(GL_TEXTURE_2D, ring->tex);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, ND_CLOUD_WTH_RES, ND_CLOUD_WTH_RES,
		             0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		ring->fbo = glCreateFramebuffer();
		glBindFramebuffer(GL_FRAMEBUFFER, ring->fbo);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ring->tex, 0);
		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			glBindTexture(GL_TEXTURE_2D, 0);
			for (int k = 0; k <= r; k++) {
				if (s_gl.cloud_wth_rings[k].fbo) {
					glDeleteFramebuffer(s_gl.cloud_wth_rings[k].fbo);
					s_gl.cloud_wth_rings[k].fbo = 0;
				}
				if (s_gl.cloud_wth_rings[k].tex) {
					glDeleteTexture(s_gl.cloud_wth_rings[k].tex);
					s_gl.cloud_wth_rings[k].tex = 0;
				}
				s_gl.cloud_wth_rings[k].valid = false;
			}
			s_gl.cloud_wth_failed = true;
			gl_bind_scene_target();
			return false;
		}
		glViewport(0, 0, ND_CLOUD_WTH_RES, ND_CLOUD_WTH_RES);
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		glClearColor(0.0f, 0.42f, 0.0f, 0.5f);
		glClear(GL_COLOR_BUFFER_BIT);
		ring->valid = false;
	}
	glBindTexture(GL_TEXTURE_2D, 0);
	gl_bind_scene_target();
	return true;
}

/* 2-ring world-snapped 256x256 GPU cloud weather cache (decoupled from camera yaw/pitch/zoom).
 * Ring 0: +/-32,768 wu (256 wu/texel, fw=128) snapped to 8 texels (2,048 wu).
 * Ring 1: +/-262,144 wu (2,048 wu/texel, fw=1024) snapped to 8 texels (16,384 wu). */
void gl_render_cloud_weather_rings(void)
{
	int cq = nd_seven_get_cloud_quality();
	if (cq == ND_CLOUD_QUALITY_OFF || !s_gl.cloud_wth_pipe.prog)
		return;
	if (!gl_ensure_cloud_wth_targets())
		return;

	static const float k_half[ND_CLOUD_WTH_RINGS] = { ND_CLOUD_WTH_HALF_0, ND_CLOUD_WTH_HALF_1 };
	static const float k_fw[ND_CLOUD_WTH_RINGS]   = { 128.0f, 1024.0f };
	float sea_wet = (s_gl.cur_base_sea_level > -900.0f) ? 1.0f : 0.0f;

	bool need[ND_CLOUD_WTH_RINGS] = { false, false };
	float target_cx[ND_CLOUD_WTH_RINGS];
	float target_cy[ND_CLOUD_WTH_RINGS];
	bool any = false;

	for (int r = 0; r < ND_CLOUD_WTH_RINGS; r++) {
		float half = k_half[r];
		float snap = 8.0f * ((2.0f * half) / (float)ND_CLOUD_WTH_RES);
		float cx = floorf(s_gl.cur_tx / snap + 0.5f) * snap;
		float cy = floorf(s_gl.cur_ty / snap + 0.5f) * snap;
		target_cx[r] = cx;
		target_cy[r] = cy;

		const gl_cloud_wth_ring_t *ring = &s_gl.cloud_wth_rings[r];
		if (!ring->valid ||
		    ring->cx != cx || ring->cy != cy ||
		    ring->half != half ||
		    ring->rendered_wet != sea_wet ||
		    fabsf(s_gl.cur_anim_time - ring->rendered_time) >= 0.40f) {
			need[r] = true;
			any = true;
		}
	}
	if (!any)
		return;

	gl_end_terrain_pass();
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glUseProgram(s_gl.cloud_wth_pipe.prog);
	glViewport(0, 0, ND_CLOUD_WTH_RES, ND_CLOUD_WTH_RES);

	for (int r = 0; r < ND_CLOUD_WTH_RINGS; r++) {
		if (!need[r])
			continue;
		gl_cloud_wth_ring_t *ring = &s_gl.cloud_wth_rings[r];
		glBindFramebuffer(GL_FRAMEBUFFER, ring->fbo);
		if (s_gl.cloud_wth_pipe.uWthWin >= 0)
			glUniform3f(s_gl.cloud_wth_pipe.uWthWin, target_cx[r], target_cy[r], k_half[r]);
		if (s_gl.cloud_wth_pipe.uAnimTime >= 0)
			glUniform1f(s_gl.cloud_wth_pipe.uAnimTime, s_gl.cur_anim_time);
		if (s_gl.cloud_wth_pipe.uSeaWet >= 0)
			glUniform1f(s_gl.cloud_wth_pipe.uSeaWet, sea_wet);
		if (s_gl.cloud_wth_pipe.uWthFw >= 0)
			glUniform1f(s_gl.cloud_wth_pipe.uWthFw, k_fw[r]);
		glDrawArrays(GL_TRIANGLES, 0, 3);

		ring->cx = target_cx[r];
		ring->cy = target_cy[r];
		ring->half = k_half[r];
		ring->rendered_time = s_gl.cur_anim_time;
		ring->rendered_wet = sea_wet;
		ring->valid = true;
	}

	gl_bind_scene_target();
	glEnable(GL_DEPTH_TEST);
}

/* Bind the cloud-shadow map (texture unit 7); uCsm window/clear-sky flag lives in NdFrameUBO. */
void gl_bind_csm(GLint loc_tex)
{
	glActiveTexture(GL_TEXTURE0 + 7);
	glBindTexture(GL_TEXTURE_2D, s_gl.csm_valid ? s_gl.csm_tex : 0);
	if (loc_tex >= 0)
		glUniform1i(loc_tex, 7);
	glActiveTexture(GL_TEXTURE0);
}

float nd_seven_gl_cloud_shadow_from_density(float density, float frag_z)
{
	const float zk = 2200.0f;
	if (frag_z >= zk + 400.0f) return 0.0f;
	float slice_fade = gl_smoothstep01(zk + 400.0f, zk - 200.0f, frag_z);
	float s = gl_smoothstep01(0.05f, 0.66f, density * slice_fade);
	return s > 0.95f ? 0.95f : s;
}

float nd_seven_gl_scene_light(float cloud_shadow, float diffuse)
{
	float sun_trans = 1.0f - (float)ND_CLOUD_SHADOW_DIRECT_LOSS * cloud_shadow;
	float amb_trans = 1.0f - (float)ND_CLOUD_SHADOW_AMBIENT_LOSS * cloud_shadow;
	return 0.45f * amb_trans + 0.55f * diffuse * sun_trans;
}
