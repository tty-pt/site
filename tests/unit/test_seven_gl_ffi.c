/*
 * test_seven_gl_ffi.c — Deterministic WebGL FFI call-count & steady-state
 * leak verification for NeverDark Seven (compiled with -DLAM_WEBGL_NATIVE).
 */
#define LAM_WEBGL_NATIVE 1
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <ttypt/webgl.h>
#include <nd/bcp-types.h>
#include <nd/seven-world.h>
#include "nd_seven.h"
#include "nd_seven_motion.h"

typedef struct {
	uint64_t total_calls;
	uint64_t get_uniform_loc;
	uint64_t get_program_param;
	uint64_t use_program;
	uint64_t uniform_calls;
	uint64_t bind_tex;
	uint64_t active_tex;
	uint64_t tex_image_2d;
	uint64_t tex_sub_image_2d;
	uint64_t buffer_data;
	uint64_t bind_buffer;
	uint64_t draws;
	uint64_t blits;
} gl_ffi_counts_t;

static gl_ffi_counts_t g_gl;
static GLuint g_next_id = 1;
static GLint g_next_loc = 1;

static void gl_counts_reset(void) { memset(&g_gl, 0, sizeof(g_gl)); }

void lam_glViewport(GLint x, GLint y, GLsizei w, GLsizei h) { (void)x; (void)y; (void)w; (void)h; g_gl.total_calls++; }
void lam_glClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a) { (void)r; (void)g; (void)b; (void)a; g_gl.total_calls++; }
void lam_glClear(GLenum m) { (void)m; g_gl.total_calls++; }
void lam_glEnable(GLenum c) { (void)c; g_gl.total_calls++; }
void lam_glDisable(GLenum c) { (void)c; g_gl.total_calls++; }
void lam_glDepthFunc(GLenum f) { (void)f; g_gl.total_calls++; }
void lam_glDepthMask(GLboolean f) { (void)f; g_gl.total_calls++; }
void lam_glBlendFunc(GLenum s, GLenum d) { (void)s; (void)d; g_gl.total_calls++; }
void lam_glColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a) { (void)r; (void)g; (void)b; (void)a; g_gl.total_calls++; }
void lam_glPolygonOffset(GLfloat f, GLfloat u) { (void)f; (void)u; g_gl.total_calls++; }
GLuint lam_glCreateShader(GLenum t) { (void)t; g_gl.total_calls++; return g_next_id++; }
void lam_glShaderSource(GLuint s, const char *src, size_t len) { (void)s; (void)src; (void)len; g_gl.total_calls++; }
void lam_glCompileShader(GLuint s) { (void)s; g_gl.total_calls++; }
GLint lam_glGetShaderParameter(GLuint s, GLenum p) { (void)s; (void)p; g_gl.total_calls++; return 1; }
GLuint lam_glCreateProgram(void) { g_gl.total_calls++; return g_next_id++; }
void lam_glAttachShader(GLuint p, GLuint s) { (void)p; (void)s; g_gl.total_calls++; }
void lam_glLinkProgram(GLuint p) { (void)p; g_gl.total_calls++; }
GLint lam_glGetProgramParameter(GLuint p, GLenum pn) { (void)p; (void)pn; g_gl.total_calls++; g_gl.get_program_param++; return 1; }
void lam_glUseProgram(GLuint p) { (void)p; g_gl.total_calls++; g_gl.use_program++; }
GLuint lam_glCreateBuffer(void) { g_gl.total_calls++; return g_next_id++; }
void lam_glBindBuffer(GLenum t, GLuint b) { (void)t; (void)b; g_gl.total_calls++; g_gl.bind_buffer++; }
void lam_glBufferData(GLenum t, const void *d, size_t s, GLenum u) { (void)t; (void)d; (void)s; (void)u; g_gl.total_calls++; g_gl.buffer_data++; }
GLuint lam_glCreateTexture(void) { g_gl.total_calls++; return g_next_id++; }
void lam_glDeleteTexture(GLuint t) { (void)t; g_gl.total_calls++; }
void lam_glBindTexture(GLenum t, GLuint tex) { (void)t; (void)tex; g_gl.total_calls++; g_gl.bind_tex++; }
void lam_glActiveTexture(GLenum t) { (void)t; g_gl.total_calls++; g_gl.active_tex++; }
void lam_glTexParameteri(GLenum t, GLenum p, GLint v) { (void)t; (void)p; (void)v; g_gl.total_calls++; }
void lam_glPixelStorei(GLenum p, GLint v) { (void)p; (void)v; g_gl.total_calls++; }
void lam_glTexImage2D(GLenum t, GLint l, GLint inf, GLsizei w, GLsizei h, GLint b, GLenum f, GLenum ty, const void *px)
{ (void)t; (void)l; (void)inf; (void)w; (void)h; (void)b; (void)f; (void)ty; (void)px; g_gl.total_calls++; g_gl.tex_image_2d++; }
void lam_glTexSubImage2D(GLenum t, GLint l, GLint xo, GLint yo, GLsizei w, GLsizei h, GLenum f, GLenum ty, const void *px)
{ (void)t; (void)l; (void)xo; (void)yo; (void)w; (void)h; (void)f; (void)ty; (void)px; g_gl.total_calls++; g_gl.tex_sub_image_2d++; }
void lam_glTexImage3D(GLenum t, GLint l, GLint inf, GLsizei w, GLsizei h, GLsizei d, GLint b, GLenum f, GLenum ty, const void *px)
{ (void)t; (void)l; (void)inf; (void)w; (void)h; (void)d; (void)b; (void)f; (void)ty; (void)px; g_gl.total_calls++; }
void lam_glTexSubImage3D(GLenum t, GLint l, GLint xo, GLint yo, GLint zo, GLsizei w, GLsizei h, GLsizei d, GLenum f, GLenum ty, const void *px)
{ (void)t; (void)l; (void)xo; (void)yo; (void)zo; (void)w; (void)h; (void)d; (void)f; (void)ty; (void)px; g_gl.total_calls++; }
void lam_glEnableVertexAttribArray(GLuint i) { (void)i; g_gl.total_calls++; }
void lam_glDisableVertexAttribArray(GLuint i) { (void)i; g_gl.total_calls++; }
void lam_glVertexAttribPointer(GLuint i, GLint s, GLenum t, GLboolean n, GLsizei st, size_t o)
{ (void)i; (void)s; (void)t; (void)n; (void)st; (void)o; g_gl.total_calls++; }
void lam_glDrawArrays(GLenum m, GLint f, GLsizei c) { (void)m; (void)f; (void)c; g_gl.total_calls++; g_gl.draws++; }
void lam_glDrawElements(GLenum m, GLsizei c, GLenum t, size_t o) { (void)m; (void)c; (void)t; (void)o; g_gl.total_calls++; g_gl.draws++; }
void lam_glDrawElementsInstanced(GLenum m, GLsizei c, GLenum t, size_t o, GLsizei ic)
{ (void)m; (void)c; (void)t; (void)o; (void)ic; g_gl.total_calls++; g_gl.draws++; }
void lam_glDrawArraysInstanced(GLenum m, GLint f, GLsizei c, GLsizei ic)
{ (void)m; (void)f; (void)c; (void)ic; g_gl.total_calls++; g_gl.draws++; }
void lam_glVertexAttribDivisor(GLuint i, GLuint d) { (void)i; (void)d; g_gl.total_calls++; }
GLint lam_glGetAttribLocation(GLuint p, const char *n, size_t l) { (void)p; (void)n; (void)l; g_gl.total_calls++; return 0; }
void lam_glBindAttribLocation(GLuint p, GLuint i, const char *n, size_t l) { (void)p; (void)i; (void)n; (void)l; g_gl.total_calls++; }
GLint lam_glGetUniformLocation(GLuint p, const char *n, size_t l)
{ (void)p; (void)n; (void)l; g_gl.total_calls++; g_gl.get_uniform_loc++; return g_next_loc++; }
void lam_glUniform1i(GLint l, GLint v0) { (void)l; (void)v0; g_gl.total_calls++; g_gl.uniform_calls++; }
void lam_glUniform1f(GLint l, GLfloat v0) { (void)l; (void)v0; g_gl.total_calls++; g_gl.uniform_calls++; }
void lam_glUniform2f(GLint l, GLfloat v0, GLfloat v1) { (void)l; (void)v0; (void)v1; g_gl.total_calls++; g_gl.uniform_calls++; }
void lam_glUniform3f(GLint l, GLfloat v0, GLfloat v1, GLfloat v2) { (void)l; (void)v0; (void)v1; (void)v2; g_gl.total_calls++; g_gl.uniform_calls++; }
void lam_glUniform4f(GLint l, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3) { (void)l; (void)v0; (void)v1; (void)v2; (void)v3; g_gl.total_calls++; g_gl.uniform_calls++; }
void lam_glUniform2fv(GLint l, GLsizei c, const GLfloat *v) { (void)l; (void)c; (void)v; g_gl.total_calls++; g_gl.uniform_calls++; }
void lam_glUniform3fv(GLint l, GLsizei c, const GLfloat *v) { (void)l; (void)c; (void)v; g_gl.total_calls++; g_gl.uniform_calls++; }
void lam_glUniformMatrix4fv(GLint l, GLsizei c, GLboolean t, const GLfloat *v) { (void)l; (void)c; (void)t; (void)v; g_gl.total_calls++; g_gl.uniform_calls++; }
void lam_glDeleteShader(GLuint s) { (void)s; g_gl.total_calls++; }
void lam_glDeleteProgram(GLuint p) { (void)p; g_gl.total_calls++; }
void lam_glDeleteBuffer(GLuint b) { (void)b; g_gl.total_calls++; }
GLuint lam_glCreateFramebuffer(void) { g_gl.total_calls++; return g_next_id++; }
void lam_glDeleteFramebuffer(GLuint fb) { (void)fb; g_gl.total_calls++; }
void lam_glBindFramebuffer(GLenum t, GLuint fb) { (void)t; (void)fb; g_gl.total_calls++; }
void lam_glFramebufferTexture2D(GLenum t, GLenum a, GLenum tt, GLuint tex, GLint l)
{ (void)t; (void)a; (void)tt; (void)tex; (void)l; g_gl.total_calls++; }
void lam_glFramebufferRenderbuffer(GLenum t, GLenum a, GLenum rbt, GLuint rb)
{ (void)t; (void)a; (void)rbt; (void)rb; g_gl.total_calls++; }
GLenum lam_glCheckFramebufferStatus(GLenum t) { (void)t; g_gl.total_calls++; return GL_FRAMEBUFFER_COMPLETE; }
GLuint lam_glCreateRenderbuffer(void) { g_gl.total_calls++; return g_next_id++; }
void lam_glDeleteRenderbuffer(GLuint rb) { (void)rb; g_gl.total_calls++; }
void lam_glBindRenderbuffer(GLenum t, GLuint rb) { (void)t; (void)rb; g_gl.total_calls++; }
void lam_glRenderbufferStorageMultisample(GLenum t, GLsizei s, GLenum inf, GLsizei w, GLsizei h)
{ (void)t; (void)s; (void)inf; (void)w; (void)h; g_gl.total_calls++; }
void lam_glStencilFunc(GLenum f, GLint r, GLuint m) { (void)f; (void)r; (void)m; g_gl.total_calls++; }
void lam_glStencilOp(GLenum sf, GLenum df, GLenum dp) { (void)sf; (void)df; (void)dp; g_gl.total_calls++; }
void lam_glStencilMask(GLuint m) { (void)m; g_gl.total_calls++; }
void lam_glClearStencil(GLint s) { (void)s; g_gl.total_calls++; }
void lam_glBlitFramebuffer(GLint sx0, GLint sy0, GLint sx1, GLint sy1, GLint dx0, GLint dy0, GLint dx1, GLint dy1, GLbitfield m, GLenum f)
{ (void)sx0; (void)sy0; (void)sx1; (void)sy1; (void)dx0; (void)dy0; (void)dx1; (void)dy1; (void)m; (void)f; g_gl.total_calls++; g_gl.blits++; }

static nd_client_state_t s_st;

static void setup_map(int x, int y, int biome)
{
	nd_seven_forget();
	memset(&s_st, 0, sizeof(s_st));
	strcpy(s_st.preferred_driver, "lamina-seven");
	s_st.map_data.width = s_st.map_data.height = 7;
	s_st.map_data.player_x = s_st.map_data.player_y = 3;
	s_st.map_data.pos[0] = (int16_t)x;
	s_st.map_data.pos[1] = (int16_t)y;
	for (int i = 0; i < 49; i++) {
		nd_map_tile_t *t = &s_st.map_data.tiles[i];
		t->bio_idx = (uint8_t)biome;
		t->exits = 0x1B;
		t->fg_l = t->fg = t->fg_r = 10;
	}
	s_st.map_data.tiles[3 * 7 + 3].flags = ND_MAP_FLAG_PLAYER;
	s_st.map_data.tiles[3 * 7 + 3].symbol = '!';
	nd_seven_set_lamb(NDW_CENTER_X, NDW_CENTER_Y);
	nd_seven_sync_origin(&s_st);
}

int main(void)
{
	setup_map(10, 10, 10);
	nd_seven_apply_quality_preset(ND_QUALITY_HIGH);
	nd_seven_reset_cam();
	for (int s = 0; s < 35; s++) nd_seven_step_cam(0.05f);
	nd_seven_set_zoom(1.0f);
	nd_seven_perf_set_enabled(1);

	/* Warm up shaders, FBOs, chunk cache, and Hi-Z cached uniform locations */
	for (int i = 0; i < 4; i++)
		nd_seven_render(1280, 720, &s_st);

	/* Steady-state High stationary (10 frames) */
	gl_counts_reset();
	uint64_t chunks_stat = 0;
	for (int i = 0; i < 10; i++) {
		nd_seven_render(1280, 720, &s_st);
		chunks_stat += nd_seven_perf_last()->terrain_chunks;
	}
	printf("FFI High stationary (10f avg): total=%.1f unif=%.1f useProg=%.1f bindTex=%.1f draws=%.1f getUnifLoc=%llu getProgParam=%llu texImg2D=%llu texSub2D=%.1f chunks=%.1f\n",
	       g_gl.total_calls / 10.0, g_gl.uniform_calls / 10.0, g_gl.use_program / 10.0,
	       g_gl.bind_tex / 10.0, g_gl.draws / 10.0,
	       (unsigned long long)g_gl.get_uniform_loc,
	       (unsigned long long)g_gl.get_program_param,
	       (unsigned long long)g_gl.tex_image_2d,
	       g_gl.tex_sub_image_2d / 10.0,
	       chunks_stat / 10.0);

	/* Hard P1 Exit Criteria invariants (stationary) */
	assert(g_gl.get_uniform_loc == 0);   /* 0 Hi-Z / per-frame uniformLocs leak */
	assert(g_gl.get_program_param == 0); /* Hi-Z link state cached */
	assert(g_gl.tex_image_2d == 0);      /* steady-state: no full texture reallocations */

	/* High wide-zoom steady-state (100+ terrain chunks cached, clouds visible) */
	nd_seven_set_zoom(0.10f);
	for (int i = 0; i < 3; i++)
		nd_seven_render(1280, 720, &s_st);
	gl_counts_reset();
	uint64_t chunks_wide = 0, clouds_wide = 0;
	for (int i = 0; i < 5; i++) {
		nd_seven_render(1280, 720, &s_st);
		chunks_wide += nd_seven_perf_last()->terrain_chunks;
		clouds_wide += nd_seven_perf_last()->cloud_queued;
	}
	double total_wide = g_gl.total_calls / 5.0;
	double unif_wide = g_gl.uniform_calls / 5.0;
	double avg_chunks_wide = chunks_wide / 5.0;
	printf("FFI High wide-zoom (5f avg):  total=%.1f unif=%.1f useProg=%.1f bindTex=%.1f draws=%.1f getUnifLoc=%llu getProgParam=%llu texImg2D=%llu texSub2D=%.1f chunks=%.1f clouds=%.1f (calls/chunk=%.1f)\n",
	       total_wide, unif_wide, g_gl.use_program / 5.0,
	       g_gl.bind_tex / 5.0, g_gl.draws / 5.0,
	       (unsigned long long)g_gl.get_uniform_loc,
	       (unsigned long long)g_gl.get_program_param,
	       (unsigned long long)g_gl.tex_image_2d,
	       g_gl.tex_sub_image_2d / 5.0,
	       avg_chunks_wide,
	       clouds_wide / 5.0,
	       avg_chunks_wide > 0 ? total_wide / avg_chunks_wide : 0.0);

	assert(g_gl.get_uniform_loc == 0);
	assert(g_gl.get_program_param == 0);
	assert(g_gl.tex_image_2d == 0);
	if (clouds_wide > 0)
		assert(g_gl.tex_sub_image_2d >= 10); /* 2 clustered-light textures updated in-place/frame */
	/* Pre-P1 terrain pass alone emitted ~45+ GL calls/chunk (~6,500+/frame at wide zoom).
	 * With P1 pass hoisting, total frame calls/chunk must stay well under 25. */
	assert(avg_chunks_wide >= 50.0);
	assert(total_wide / avg_chunks_wide < 25.0);

	printf("PASS: Seven P1 WebGL FFI hoisting & 0-leak invariants verified\n");
	return 0;
}
