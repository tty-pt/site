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
	uint64_t tex_storage_3d;
	uint64_t tex_sub_image_3d;
	uint64_t generate_mipmap;
	uint64_t bind_vao;
	uint64_t buffer_data;
	uint64_t buffer_sub_data;
	uint64_t bind_buffer;
	uint64_t draws;
	uint64_t blits;
	uint64_t blits_depth;
	uint64_t blits_color;
	uint64_t instanced_tris;
	uint64_t cmd_flushes;
	uint64_t cmd_ops;
	uint32_t cur_pass;
	uint64_t pass_draws[16];
	uint64_t pass_bind_tex[16];
	uint64_t pass_ffi[16];
} gl_ffi_counts_t;

static gl_ffi_counts_t g_gl;
static GLuint g_next_id = 1;
static GLint g_next_loc = 1;

static void gl_counts_reset(void) { memset(&g_gl, 0, sizeof(g_gl)); }
static inline void count_ffi(void) { g_gl.total_calls++; g_gl.pass_ffi[g_gl.cur_pass]++; }

void lam_glViewport(GLint x, GLint y, GLsizei w, GLsizei h) { (void)x; (void)y; (void)w; (void)h; count_ffi(); }
void lam_glClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a) { (void)r; (void)g; (void)b; (void)a; count_ffi(); }
void lam_glClear(GLenum m) { (void)m; count_ffi(); }
void lam_glEnable(GLenum c) { (void)c; count_ffi(); }
void lam_glDisable(GLenum c) { (void)c; count_ffi(); }
void lam_glDepthFunc(GLenum f) { (void)f; count_ffi(); }
void lam_glDepthMask(GLboolean f) { (void)f; count_ffi(); }
void lam_glBlendFunc(GLenum s, GLenum d) { (void)s; (void)d; count_ffi(); }
void lam_glColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a) { (void)r; (void)g; (void)b; (void)a; count_ffi(); }
void lam_glPolygonOffset(GLfloat f, GLfloat u) { (void)f; (void)u; count_ffi(); }
GLuint lam_glCreateShader(GLenum t) { (void)t; count_ffi(); return g_next_id++; }
void lam_glShaderSource(GLuint s, const char *src, size_t len) { (void)s; (void)src; (void)len; count_ffi(); }
void lam_glCompileShader(GLuint s) { (void)s; count_ffi(); }
GLint lam_glGetShaderParameter(GLuint s, GLenum p) { (void)s; (void)p; count_ffi(); return 1; }
GLuint lam_glCreateProgram(void) { count_ffi(); return g_next_id++; }
void lam_glAttachShader(GLuint p, GLuint s) { (void)p; (void)s; count_ffi(); }
void lam_glLinkProgram(GLuint p) { (void)p; count_ffi(); }
GLint lam_glGetProgramParameter(GLuint p, GLenum pn) { (void)p; (void)pn; count_ffi(); g_gl.get_program_param++; return 1; }
void lam_glUseProgram(GLuint p) { (void)p; count_ffi(); g_gl.use_program++; }
GLuint lam_glCreateBuffer(void) { count_ffi(); return g_next_id++; }
void lam_glBindBuffer(GLenum t, GLuint b) { (void)t; (void)b; count_ffi(); g_gl.bind_buffer++; }
void lam_glBufferData(GLenum t, const void *d, size_t s, GLenum u) { (void)t; (void)d; (void)s; (void)u; count_ffi(); g_gl.buffer_data++; }
void lam_glBufferSubData(GLenum t, size_t o, size_t s, const void *d) { (void)t; (void)o; (void)s; (void)d; count_ffi(); g_gl.buffer_sub_data++; }
GLuint lam_glCreateTexture(void) { count_ffi(); return g_next_id++; }
void lam_glDeleteTexture(GLuint t) { (void)t; count_ffi(); }
void lam_glBindTexture(GLenum t, GLuint tex) { (void)t; (void)tex; count_ffi(); g_gl.bind_tex++; g_gl.pass_bind_tex[g_gl.cur_pass]++; }
void lam_glActiveTexture(GLenum t) { (void)t; count_ffi(); g_gl.active_tex++; }
void lam_glTexParameteri(GLenum t, GLenum p, GLint v) { (void)t; (void)p; (void)v; count_ffi(); }
void lam_glPixelStorei(GLenum p, GLint v) { (void)p; (void)v; count_ffi(); }
void lam_glTexImage2D(GLenum t, GLint l, GLint inf, GLsizei w, GLsizei h, GLint b, GLenum f, GLenum ty, const void *px)
{ (void)t; (void)l; (void)inf; (void)w; (void)h; (void)b; (void)f; (void)ty; (void)px; count_ffi(); g_gl.tex_image_2d++; }
void lam_glTexSubImage2D(GLenum t, GLint l, GLint xo, GLint yo, GLsizei w, GLsizei h, GLenum f, GLenum ty, const void *px)
{ (void)t; (void)l; (void)xo; (void)yo; (void)w; (void)h; (void)f; (void)ty; (void)px; count_ffi(); g_gl.tex_sub_image_2d++; }
void lam_glTexImage3D(GLenum t, GLint l, GLint inf, GLsizei w, GLsizei h, GLsizei d, GLint b, GLenum f, GLenum ty, const void *px)
{ (void)t; (void)l; (void)inf; (void)w; (void)h; (void)d; (void)b; (void)f; (void)ty; (void)px; count_ffi(); }
void lam_glTexSubImage3D(GLenum t, GLint l, GLint xo, GLint yo, GLint zo, GLsizei w, GLsizei h, GLsizei d, GLenum f, GLenum ty, const void *px)
{ (void)t; (void)l; (void)xo; (void)yo; (void)zo; (void)w; (void)h; (void)d; (void)f; (void)ty; (void)px; count_ffi(); g_gl.tex_sub_image_3d++; }
void lam_glTexStorage3D(GLenum t, GLsizei lv, GLenum inf, GLsizei w, GLsizei h, GLsizei d)
{ (void)t; (void)lv; (void)inf; (void)w; (void)h; (void)d; count_ffi(); g_gl.tex_storage_3d++; }
void lam_glGenerateMipmap(GLenum t) { (void)t; count_ffi(); g_gl.generate_mipmap++; }
GLuint lam_glCreateVertexArray(void) { count_ffi(); return g_next_id++; }
void lam_glDeleteVertexArray(GLuint v) { (void)v; count_ffi(); }
void lam_glBindVertexArray(GLuint v) { (void)v; count_ffi(); g_gl.bind_vao++; }
void lam_glEnableVertexAttribArray(GLuint i) { (void)i; count_ffi(); }
void lam_glDisableVertexAttribArray(GLuint i) { (void)i; count_ffi(); }
void lam_glVertexAttribPointer(GLuint i, GLint s, GLenum t, GLboolean n, GLsizei st, size_t o)
{ (void)i; (void)s; (void)t; (void)n; (void)st; (void)o; count_ffi(); }
void lam_glDrawArrays(GLenum m, GLint f, GLsizei c) { (void)m; (void)f; (void)c; count_ffi(); g_gl.draws++; g_gl.pass_draws[g_gl.cur_pass]++; }
void lam_glDrawElements(GLenum m, GLsizei c, GLenum t, size_t o) { (void)m; (void)c; (void)t; (void)o; count_ffi(); g_gl.draws++; g_gl.pass_draws[g_gl.cur_pass]++; }
void lam_glDrawElementsInstanced(GLenum m, GLsizei c, GLenum t, size_t o, GLsizei ic)
{ (void)m; (void)t; (void)o; count_ffi(); g_gl.draws++; g_gl.pass_draws[g_gl.cur_pass]++; g_gl.instanced_tris += (uint64_t)(c / 3) * (uint64_t)ic; }
void lam_glDrawArraysInstanced(GLenum m, GLint f, GLsizei c, GLsizei ic)
{ (void)m; (void)f; (void)c; (void)ic; count_ffi(); g_gl.draws++; g_gl.pass_draws[g_gl.cur_pass]++; }
void lam_glVertexAttribDivisor(GLuint i, GLuint d) { (void)i; (void)d; count_ffi(); }
GLint lam_glGetAttribLocation(GLuint p, const char *n, size_t l) { (void)p; (void)n; (void)l; count_ffi(); return 0; }
void lam_glBindAttribLocation(GLuint p, GLuint i, const char *n, size_t l) { (void)p; (void)i; (void)n; (void)l; count_ffi(); }
GLint lam_glGetUniformLocation(GLuint p, const char *n, size_t l)
{ (void)p; (void)n; (void)l; count_ffi(); g_gl.get_uniform_loc++; return g_next_loc++; }
void lam_glUniform1i(GLint l, GLint v0) { (void)l; (void)v0; count_ffi(); g_gl.uniform_calls++; }
void lam_glUniform1f(GLint l, GLfloat v0) { (void)l; (void)v0; count_ffi(); g_gl.uniform_calls++; }
void lam_glUniform2f(GLint l, GLfloat v0, GLfloat v1) { (void)l; (void)v0; (void)v1; count_ffi(); g_gl.uniform_calls++; }
void lam_glUniform3f(GLint l, GLfloat v0, GLfloat v1, GLfloat v2) { (void)l; (void)v0; (void)v1; (void)v2; count_ffi(); g_gl.uniform_calls++; }
void lam_glUniform4f(GLint l, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3) { (void)l; (void)v0; (void)v1; (void)v2; (void)v3; count_ffi(); g_gl.uniform_calls++; }
void lam_glUniform2fv(GLint l, GLsizei c, const GLfloat *v) { (void)l; (void)c; (void)v; count_ffi(); g_gl.uniform_calls++; }
void lam_glUniform3fv(GLint l, GLsizei c, const GLfloat *v) { (void)l; (void)c; (void)v; count_ffi(); g_gl.uniform_calls++; }
void lam_glUniformMatrix4fv(GLint l, GLsizei c, GLboolean t, const GLfloat *v) { (void)l; (void)c; (void)t; (void)v; count_ffi(); g_gl.uniform_calls++; }
void lam_glDeleteShader(GLuint s) { (void)s; count_ffi(); }
void lam_glDeleteProgram(GLuint p) { (void)p; count_ffi(); }
void lam_glDeleteBuffer(GLuint b) { (void)b; count_ffi(); }
GLuint lam_glCreateFramebuffer(void) { count_ffi(); return g_next_id++; }
void lam_glDeleteFramebuffer(GLuint fb) { (void)fb; count_ffi(); }
void lam_glBindFramebuffer(GLenum t, GLuint fb) { (void)t; (void)fb; count_ffi(); }
void lam_glFramebufferTexture2D(GLenum t, GLenum a, GLenum tt, GLuint tex, GLint l)
{ (void)t; (void)a; (void)tt; (void)tex; (void)l; count_ffi(); }
void lam_glFramebufferRenderbuffer(GLenum t, GLenum a, GLenum rbt, GLuint rb)
{ (void)t; (void)a; (void)rbt; (void)rb; count_ffi(); }
GLenum lam_glCheckFramebufferStatus(GLenum t) { (void)t; count_ffi(); return GL_FRAMEBUFFER_COMPLETE; }
GLuint lam_glCreateRenderbuffer(void) { count_ffi(); return g_next_id++; }
void lam_glDeleteRenderbuffer(GLuint rb) { (void)rb; count_ffi(); }
void lam_glBindRenderbuffer(GLenum t, GLuint rb) { (void)t; (void)rb; count_ffi(); }
void lam_glRenderbufferStorageMultisample(GLenum t, GLsizei s, GLenum inf, GLsizei w, GLsizei h)
{ (void)t; (void)s; (void)inf; (void)w; (void)h; count_ffi(); }
void lam_glStencilFunc(GLenum f, GLint r, GLuint m) { (void)f; (void)r; (void)m; count_ffi(); }
void lam_glStencilOp(GLenum sf, GLenum df, GLenum dp) { (void)sf; (void)df; (void)dp; count_ffi(); }
void lam_glStencilMask(GLuint m) { (void)m; count_ffi(); }
void lam_glClearStencil(GLint s) { (void)s; count_ffi(); }
void lam_glBlitFramebuffer(GLint sx0, GLint sy0, GLint sx1, GLint sy1, GLint dx0, GLint dy0, GLint dx1, GLint dy1, GLbitfield m, GLenum f)
{
	(void)sx0; (void)sy0; (void)sx1; (void)sy1; (void)dx0; (void)dy0; (void)dx1; (void)dy1; (void)f;
	count_ffi();
	g_gl.blits++;
	if (m & GL_DEPTH_BUFFER_BIT) g_gl.blits_depth++;
	if (m & GL_COLOR_BUFFER_BIT) g_gl.blits_color++;
}
void lam_glBindBufferBase(GLenum t, GLuint idx, GLuint b) { (void)t; (void)idx; (void)b; count_ffi(); g_gl.bind_buffer++; }
GLuint lam_glGetUniformBlockIndex(GLuint p, const char *n, size_t l) { (void)p; (void)n; (void)l; count_ffi(); return 0u; }
void lam_glUniformBlockBinding(GLuint p, GLuint bi, GLuint bb) { (void)p; (void)bi; (void)bb; count_ffi(); }
void lam_glTimerBegin(GLuint p) { g_gl.cur_pass = (p < 16u) ? p : 0u; count_ffi(); }
void lam_glTimerEnd(GLuint p) { (void)p; count_ffi(); g_gl.cur_pass = 0u; }
void lam_glCmdFlush(const uint32_t *buf, uint32_t word_count)
{
	count_ffi();
	g_gl.cmd_flushes++;
	uint32_t i = 0u;
	while (i < word_count) {
		uint32_t op = buf[i++];
		g_gl.cmd_ops++;
		switch (op) {
		case LAM_GL_CMD_NOP: break;
		case LAM_GL_CMD_USE_PROGRAM: g_gl.use_program++; i += 1u; break;
		case LAM_GL_CMD_ACTIVE_TEXTURE: g_gl.active_tex++; i += 1u; break;
		case LAM_GL_CMD_BIND_TEXTURE: g_gl.bind_tex++; g_gl.pass_bind_tex[g_gl.cur_pass]++; i += 2u; break;
		case LAM_GL_CMD_BIND_BUFFER: g_gl.bind_buffer++; i += 2u; break;
		case LAM_GL_CMD_ENABLE: i += 1u; break;
		case LAM_GL_CMD_DISABLE: i += 1u; break;
		case LAM_GL_CMD_DEPTH_MASK: i += 1u; break;
		case LAM_GL_CMD_BLEND_FUNC: i += 2u; break;
		case LAM_GL_CMD_UNIFORM1I: g_gl.uniform_calls++; i += 2u; break;
		case LAM_GL_CMD_UNIFORM1F: g_gl.uniform_calls++; i += 2u; break;
		case LAM_GL_CMD_UNIFORM2F: g_gl.uniform_calls++; i += 3u; break;
		case LAM_GL_CMD_UNIFORM3F: g_gl.uniform_calls++; i += 4u; break;
		case LAM_GL_CMD_UNIFORM4F: g_gl.uniform_calls++; i += 5u; break;
		case LAM_GL_CMD_UNIFORM_MAT4: g_gl.uniform_calls++; i += 18u; break;
		case LAM_GL_CMD_VERTEX_ATTRIB_PTR: i += 6u; break;
		case LAM_GL_CMD_DRAW_ELEMENTS: g_gl.draws++; g_gl.pass_draws[g_gl.cur_pass]++; i += 4u; break;
		case LAM_GL_CMD_DRAW_ELEMENTS_INSTANCED: {
			uint32_t c = buf[i + 1u];
			uint32_t ic = buf[i + 4u];
			g_gl.draws++;
			g_gl.pass_draws[g_gl.cur_pass]++;
			g_gl.instanced_tris += (uint64_t)(c / 3u) * (uint64_t)ic;
			i += 5u;
			break;
		}
		case LAM_GL_CMD_DRAW_ARRAYS: g_gl.draws++; g_gl.pass_draws[g_gl.cur_pass]++; i += 3u; break;
		case LAM_GL_CMD_ENABLE_VAA: i += 1u; break;
		case LAM_GL_CMD_DISABLE_VAA: i += 1u; break;
		case LAM_GL_CMD_VERTEX_ATTRIB_DIVISOR: i += 2u; break;
		case LAM_GL_CMD_BIND_VAO: g_gl.bind_vao++; i += 1u; break;
		default: assert(0 && "unknown lam_gl_cmd_op"); return;
		}
	}
	assert(i == word_count);
}

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

	/* Warm up shaders, FBOs, chunk cache, grass tuft VBOs, and Hi-Z cached uniform locations */
	for (int i = 0; i < 4; i++)
		nd_seven_render(1280, 720, &s_st);

	/* Steady-state High stationary (10 frames) */
	gl_counts_reset();
	uint64_t chunks_stat = 0;
	for (int i = 0; i < 10; i++) {
		nd_seven_render(1280, 720, &s_st);
		chunks_stat += nd_seven_perf_last()->terrain_chunks;
	}
	double high_draws = g_gl.draws / 10.0;
	printf("FFI High stationary (10f avg): total=%.1f unif=%.1f useProg=%.1f bindTex=%.1f bindVao=%.1f draws=%.1f blits=%.1f(d=%.1f,c=%.1f) instTris=%.0f cmdFlushes=%.1f cmdOps=%.1f getUnifLoc=%llu getProgParam=%llu texImg2D=%llu texSub2D=%.1f texStor3D=%llu genMip=%llu bufData=%llu bufSub=%.1f chunks=%.1f\n",
	       g_gl.total_calls / 10.0, g_gl.uniform_calls / 10.0, g_gl.use_program / 10.0,
	       g_gl.bind_tex / 10.0, g_gl.bind_vao / 10.0, high_draws,
	       g_gl.blits / 10.0, g_gl.blits_depth / 10.0, g_gl.blits_color / 10.0,
	       g_gl.instanced_tris / 10.0,
	       g_gl.cmd_flushes / 10.0, g_gl.cmd_ops / 10.0,
	       (unsigned long long)g_gl.get_uniform_loc,
	       (unsigned long long)g_gl.get_program_param,
	       (unsigned long long)g_gl.tex_image_2d,
	       g_gl.tex_sub_image_2d / 10.0,
	       (unsigned long long)g_gl.tex_storage_3d,
	       (unsigned long long)g_gl.generate_mipmap,
	       (unsigned long long)g_gl.buffer_data,
	       g_gl.buffer_sub_data / 10.0,
	       chunks_stat / 10.0);

	/* Hard P1/P2/P3/P6D Exit Criteria invariants (stationary) */
	assert(g_gl.get_uniform_loc == 0);   /* 0 Hi-Z / per-frame uniformLocs leak */
	assert(g_gl.get_program_param == 0); /* Hi-Z link state cached */
	assert(g_gl.tex_image_2d == 0);      /* steady-state: no full texture reallocations */
	assert(g_gl.tex_storage_3d == 0);    /* P6D: 2D array pools allocated once */
	assert(g_gl.generate_mipmap == 0);   /* P6D: 0 mipmap regenerations in steady state */
	assert(g_gl.buffer_data == 0);       /* P2/P3: no per-frame dynamic or grass VBO reallocations */
	assert(g_gl.buffer_sub_data > 0);    /* P2/P6D: dynamic VBOs updated via glBufferSubData */
	assert(g_gl.blits_depth <= 20);      /* P3: at most 2 MSAA depth resolves/frame (Hi-Z + post/water) */

	/* Steady-state Low stationary (5 frames) — verifies 3-pass Bloom is gated off & LOD grass active */
	nd_seven_apply_quality_preset(ND_QUALITY_LOW);
	for (int i = 0; i < 3; i++)
		nd_seven_render(1280, 720, &s_st);
	gl_counts_reset();
	for (int i = 0; i < 5; i++)
		nd_seven_render(1280, 720, &s_st);
	double low_draws = g_gl.draws / 5.0;
	printf("FFI Low stationary  (5f avg):  total=%.1f unif=%.1f useProg=%.1f bindTex=%.1f bindVao=%.1f draws=%.1f blits=%.1f(d=%.1f,c=%.1f) instTris=%.0f cmdFlushes=%.1f cmdOps=%.1f bufData=%llu\n",
	       g_gl.total_calls / 5.0, g_gl.uniform_calls / 5.0, g_gl.use_program / 5.0,
	       g_gl.bind_tex / 5.0, g_gl.bind_vao / 5.0, low_draws,
	       g_gl.blits / 5.0, g_gl.blits_depth / 5.0, g_gl.blits_color / 5.0,
	       g_gl.instanced_tris / 5.0,
	       g_gl.cmd_flushes / 5.0, g_gl.cmd_ops / 5.0,
	       (unsigned long long)g_gl.buffer_data);
	assert(g_gl.buffer_data == 0);
	assert(low_draws < high_draws);      /* P3: Low skips 3-pass Bloom (and/or SSAO/shadow work) */

	/* High wide-zoom steady-state (100+ terrain chunks cached, clouds visible) */
	nd_seven_apply_quality_preset(ND_QUALITY_HIGH);
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
	double draws_wide = g_gl.draws / 5.0;
	double avg_chunks_wide = chunks_wide / 5.0;
	double terr_draws_wide = g_gl.pass_draws[2] / 5.0;
	double terr_bind_wide = g_gl.pass_bind_tex[2] / 5.0;
	double terr_ffi_wide = g_gl.pass_ffi[2] / 5.0;
	printf("FFI High wide-zoom (5f avg):  total=%.1f unif=%.1f useProg=%.1f bindTex=%.1f bindVao=%.1f draws=%.1f blits=%.1f(d=%.1f,c=%.1f) cmdFlushes=%.1f cmdOps=%.1f getUnifLoc=%llu getProgParam=%llu texImg2D=%llu texSub2D=%.1f texStor3D=%llu genMip=%llu bufData=%llu bufSub=%.1f chunks=%.1f clouds=%.1f (calls/chunk=%.2f)\n",
	       total_wide, unif_wide, g_gl.use_program / 5.0,
	       g_gl.bind_tex / 5.0, g_gl.bind_vao / 5.0, draws_wide,
	       g_gl.blits / 5.0, g_gl.blits_depth / 5.0, g_gl.blits_color / 5.0,
	       g_gl.cmd_flushes / 5.0, g_gl.cmd_ops / 5.0,
	       (unsigned long long)g_gl.get_uniform_loc,
	       (unsigned long long)g_gl.get_program_param,
	       (unsigned long long)g_gl.tex_image_2d,
	       g_gl.tex_sub_image_2d / 5.0,
	       (unsigned long long)g_gl.tex_storage_3d,
	       (unsigned long long)g_gl.generate_mipmap,
	       (unsigned long long)g_gl.buffer_data,
	       g_gl.buffer_sub_data / 5.0,
	       avg_chunks_wide,
	       clouds_wide / 5.0,
	       avg_chunks_wide > 0 ? total_wide / avg_chunks_wide : 0.0);
	printf("  TERRAIN wide-zoom (5f avg): draws=%.1f bindTex=%.1f ffi=%.1f | WATER draws=%.1f ffi=%.1f | BB draws=%.1f ffi=%.1f | M3D draws=%.1f ffi=%.1f | HIZ ffi=%.1f | POST ffi=%.1f | OTHER ffi=%.1f\n",
	       terr_draws_wide, terr_bind_wide, terr_ffi_wide,
	       g_gl.pass_draws[10] / 5.0, g_gl.pass_ffi[10] / 5.0,
	       g_gl.pass_draws[8] / 5.0, g_gl.pass_ffi[8] / 5.0,
	       (g_gl.pass_draws[6] + g_gl.pass_draws[7]) / 5.0, (g_gl.pass_ffi[6] + g_gl.pass_ffi[7]) / 5.0,
	       g_gl.pass_ffi[4] / 5.0, g_gl.pass_ffi[12] / 5.0, g_gl.pass_ffi[0] / 5.0);

	assert(g_gl.get_uniform_loc == 0);
	assert(g_gl.get_program_param == 0);
	assert(g_gl.tex_image_2d == 0);
	assert(g_gl.tex_storage_3d == 0);
	assert(g_gl.generate_mipmap == 0);
	assert(g_gl.buffer_data == 0);
	if (clouds_wide > 0)
		assert(g_gl.tex_sub_image_2d >= 10); /* 2 clustered-light textures updated in-place/frame */
	/* Sub-Phase 6D: sampler2DArray instanced terrain collapses ~159 terrain draws into <= 8
	 * instanced batches and keeps terrain pass FFI <= 120 calls/frame. */
	assert(g_gl.cmd_flushes > 0);
	assert(g_gl.cmd_ops > 0);
	assert(g_gl.bind_vao > 0);
	assert(avg_chunks_wide >= 50.0);
	assert(terr_draws_wide >= 1.0 && terr_draws_wide <= 8.0);
	assert(terr_ffi_wide <= 120.0);
	assert(total_wide / avg_chunks_wide < 3.5);

	printf("PASS: Seven P1/P2/P3/P4/P6D WebGL FFI, sampler2DArray instancing, VAO, UBO, cmd-ring, blits & 0-leak invariants verified\n");
	return 0;
}
