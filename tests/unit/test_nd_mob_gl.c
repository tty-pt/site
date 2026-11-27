/* test_nd_mob_gl.c — Standalone unit test for external/nd-mob-gl.
 *
 * Verifies:
 *   1. XY hook registration via nd_mob_gl_register_mem() and XY_DECL dispatch.
 *   2. Non-blocking async shader compilation matching webgl-cap.js Firefox worker
 *      (GL_COMPLETION_STATUS_KHR == 0 and GL_LINK_STATUS == 0 while compiling;
 *      zero premature glDeleteProgram calls, zero program handle leaks).
 *   3. Mixed-herd species template caching & instanced draw-call batching
 *      (10 actors across 4 species -> 4 cached species, 4 shadow draws, 4 color draws).
 *   4. Context-loss reset & automatic async GL rebuild.
 *   5. All 14 external/nd-mob/data/mobs.dsv species + ungulates synced cleanly.
 *   6. Optional shader dump (--dump-shaders <dir>) for glslangValidator compile + link check.
 */

#include <nd/mob-gl.h>
#include <nd/mob_dna.h>
#include <ttypt/webgl.h>

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef LAM_WEBGL_NATIVE
/* =========================================================================
 * Minimal WebGL2 mock simulating Firefox shaderWorker async compilation
 * ========================================================================= */

typedef struct {
	int used;
	int linked;
	int polls_left;
} mock_prog_t;

static mock_prog_t s_mock_progs[64];
static uint32_t    s_mock_prog_creates = 0;
static uint32_t    s_mock_prog_deletes = 0;
static uint32_t    s_mock_draw_calls   = 0;
static uint32_t    s_mock_cull_disables = 0;
static int         s_mock_unpack_align = 1; /* intentionally start at 1 to verify 4 is set */
static uint32_t    s_mock_tex_uploads_aligned = 0;
static int         s_mock_async_delay  = 0;
static GLuint      s_mock_next_id      = 1;

void lam_glDisable(GLenum cap)
{
	if (cap == GL_CULL_FACE)
		s_mock_cull_disables++;
}

GLuint lam_glCreateShader(GLenum type) { (void)type; return s_mock_next_id++; }
void   lam_glShaderSource(GLuint s, const char *src, size_t len) { (void)s; (void)src; (void)len; }
void   lam_glCompileShader(GLuint s) { (void)s; }
void   lam_glDeleteShader(GLuint s) { (void)s; }

GLuint lam_glCreateProgram(void)
{
	s_mock_prog_creates++;
	for (uint32_t i = 1; i < 64u; i++) {
		if (!s_mock_progs[i].used) {
			memset(&s_mock_progs[i], 0, sizeof(s_mock_progs[i]));
			s_mock_progs[i].used = 1;
			return i;
		}
	}
	return 0;
}

void lam_glAttachShader(GLuint p, GLuint s) { (void)p; (void)s; }

void lam_glLinkProgram(GLuint p)
{
	if (p > 0 && p < 64u && s_mock_progs[p].used) {
		s_mock_progs[p].linked = 1;
		s_mock_progs[p].polls_left = s_mock_async_delay;
	}
}

GLint lam_glGetProgramParameter(GLuint p, GLenum param)
{
	if (p == 0 || p >= 64u || !s_mock_progs[p].used || !s_mock_progs[p].linked)
		return 0;
	if (param == GL_COMPLETION_STATUS_KHR) {
		if (s_mock_progs[p].polls_left > 0) {
			s_mock_progs[p].polls_left--;
			return 0;
		}
		return 1;
	}
	if (param == GL_LINK_STATUS) {
		/* Matches webgl-cap.js:1024: returns 0 while worker is compiling! */
		return (s_mock_progs[p].polls_left == 0) ? 1 : 0;
	}
	return 1;
}

void lam_glDeleteProgram(GLuint p)
{
	s_mock_prog_deletes++;
	if (p > 0 && p < 64u)
		s_mock_progs[p].used = 0;
}

void   lam_glUseProgram(GLuint p) { (void)p; }
GLuint lam_glCreateBuffer(void) { return s_mock_next_id++; }
void   lam_glBindBuffer(GLenum t, GLuint b) { (void)t; (void)b; }
void   lam_glBufferData(GLenum t, const void *d, size_t s, GLenum u) { (void)t; (void)d; (void)s; (void)u; }
void   lam_glBufferSubData(GLenum t, size_t o, size_t s, const void *d) { (void)t; (void)o; (void)s; (void)d; }
GLuint lam_glCreateTexture(void) { return s_mock_next_id++; }
void   lam_glBindTexture(GLenum t, GLuint tex) { (void)t; (void)tex; }
void   lam_glActiveTexture(GLenum t) { (void)t; }
void   lam_glTexParameteri(GLenum t, GLenum p, GLint v) { (void)t; (void)p; (void)v; }

void lam_glPixelStorei(GLenum p, GLint v)
{
	if (p == GL_UNPACK_ALIGNMENT)
		s_mock_unpack_align = v;
}

void lam_glTexImage2D(GLenum t, GLint l, GLint inf, GLsizei w, GLsizei h, GLint b, GLenum f, GLenum ty, const void *px)
{
	(void)t; (void)l; (void)inf; (void)h; (void)b; (void)f; (void)ty; (void)px;
	assert(s_mock_unpack_align == 4);
	assert(w == (GLsizei)ND_MOB_GL_ACTOR_TEX_W);
	s_mock_tex_uploads_aligned++;
}

void lam_glTexSubImage2D(GLenum t, GLint l, GLint xo, GLint yo, GLsizei w, GLsizei h, GLenum f, GLenum ty, const void *px)
{
	(void)t; (void)l; (void)xo; (void)yo; (void)h; (void)f; (void)ty; (void)px;
	assert(s_mock_unpack_align == 4);
	assert(w == (GLsizei)ND_MOB_GL_ACTOR_TEX_W);
	s_mock_tex_uploads_aligned++;
}

void   lam_glEnableVertexAttribArray(GLuint i) { (void)i; }
void   lam_glDisableVertexAttribArray(GLuint i) { (void)i; }
void   lam_glVertexAttribPointer(GLuint i, GLint s, GLenum t, GLboolean n, GLsizei st, size_t o)
{ (void)i; (void)s; (void)t; (void)n; (void)st; (void)o; }
void   lam_glDrawElementsInstanced(GLenum m, GLsizei c, GLenum t, size_t o, GLsizei ic)
{ (void)m; (void)c; (void)t; (void)o; (void)ic; s_mock_draw_calls++; }
void   lam_glVertexAttribDivisor(GLuint i, GLuint d) { (void)i; (void)d; }
GLint  lam_glGetUniformLocation(GLuint p, const char *n, size_t l) { (void)p; (void)n; (void)l; return 1; }
void   lam_glUniform1i(GLint l, GLint v0) { (void)l; (void)v0; }
void   lam_glUniform1f(GLint l, GLfloat v0) { (void)l; (void)v0; }
void   lam_glUniform3fv(GLint l, GLsizei c, const GLfloat *v) { (void)l; (void)c; (void)v; }
void   lam_glUniform4f(GLint l, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3)
{ (void)l; (void)v0; (void)v1; (void)v2; (void)v3; }
void   lam_glUniformMatrix4fv(GLint l, GLsizei c, GLboolean t, const GLfloat *v) { (void)l; (void)c; (void)t; (void)v; }
GLuint lam_glCreateVertexArray(void) { return s_mock_next_id++; }
void   lam_glBindVertexArray(GLuint v) { (void)v; }
#endif

static int
write_text_file(const char *path, const char *text)
{
	FILE *f = fopen(path, "wb");
	if (!f) return -1;
	size_t n = strlen(text);
	size_t w = fwrite(text, 1, n, f);
	fclose(f);
	return (w == n) ? 0 : -1;
}

static int
dump_shaders(const char *dir)
{
	char path[512];
	snprintf(path, sizeof(path), "%s/mob_shadow.vert", dir);
	if (write_text_file(path, nd_mob_gl_shadow_vs_src()) != 0) return -1;
	snprintf(path, sizeof(path), "%s/mob_shadow.frag", dir);
	if (write_text_file(path, nd_mob_gl_shadow_fs_src()) != 0) return -1;
	snprintf(path, sizeof(path), "%s/mob_color.vert", dir);
	if (write_text_file(path, nd_mob_gl_color_vs_src()) != 0) return -1;
	snprintf(path, sizeof(path), "%s/mob_color.frag", dir);
	if (write_text_file(path, nd_mob_gl_color_fs_src()) != 0) return -1;
	return 0;
}

static void
make_identity_mat4(float m[16])
{
	memset(m, 0, 16 * sizeof(float));
	m[0] = m[5] = m[10] = m[15] = 1.0f;
}

int
main(int argc, char **argv)
{
	if (argc == 3 && strcmp(argv[1], "--dump-shaders") == 0) {
		if (dump_shaders(argv[2]) != 0) {
			fprintf(stderr, "FAIL: unable to dump shaders to %s\n", argv[2]);
			return 1;
		}
		return 0;
	}

	/* 1. Register in-memory XY hooks and initialize */
	int rc = nd_mob_gl_register_mem();
	assert(rc == XY_OK);
	assert(nd_mob_gl_init(0) == 0);

	/* 2. Mixed herd: 1 lamb + 5 salmon + 3 parrots + 1 shark = 10 actors, 4 species */
	nd_mob_actor_in_t herd[10];
	memset(herd, 0, sizeof(herd));

	/* Actor 0: local player lamb with explicit anim params + owner glow */
	herd[0].ref = 0;
	herd[0].dna = creature_dna_lamb_default(42u);
	herd[0].state.age = 0.25f;
	herd[0].state.weight = 0.5f;
	herd[0].scale_wu_per_m = 45.0f;
	herd[0].alpha = 1.0f;
	herd[0].flags = ND_MOB_ACT_LOCAL_PLAYER | ND_MOB_ACT_OWNER_GLOW | ND_MOB_ACT_HAS_ANIM_PARAMS;
	herd[0].anim.speed_norm = 0.5f;
	herd[0].anim.gait_phase = 1.2f;

	/* Actors 1..5: school of 5 salmon underwater */
	for (int i = 0; i < 5; i++) {
		int k = 1 + i;
		herd[k].ref = (uint64_t)(100 + i);
		assert(creature_dna_from_name("Salmon", (uint32_t)(100 + i), &herd[k].dna, &herd[k].state) == 1);
		herd[k].world_x = 10.0f * (float)i;
		herd[k].world_y = 20.0f;
		herd[k].world_z = -4.0f;
		herd[k].rel_x   = herd[k].world_x;
		herd[k].rel_y   = herd[k].world_y;
		herd[k].rel_z   = herd[k].world_z;
		herd[k].water_z = 0.0f;
		herd[k].scale_wu_per_m = 45.0f;
		herd[k].alpha   = 1.0f;
		herd[k].flags   = ND_MOB_ACT_IN_WATER;
	}

	/* Actors 6..8: flock of 3 airborne parrots */
	for (int i = 0; i < 3; i++) {
		int k = 6 + i;
		herd[k].ref = (uint64_t)(200 + i);
		assert(creature_dna_from_name("Parrot", (uint32_t)(200 + i), &herd[k].dna, &herd[k].state) == 1);
		herd[k].world_x = -15.0f + 5.0f * (float)i;
		herd[k].world_y = 30.0f;
		herd[k].world_z = 22.0f;
		herd[k].rel_x   = herd[k].world_x;
		herd[k].rel_y   = herd[k].world_y;
		herd[k].rel_z   = herd[k].world_z;
		herd[k].water_z = -1000.0f;
		herd[k].scale_wu_per_m = 45.0f;
		herd[k].alpha   = 1.0f;
		herd[k].flags   = ND_MOB_ACT_AIRBORNE;
	}

	/* Actor 9: 1 shark */
	herd[9].ref = 300;
	assert(creature_dna_from_name("Shark", 300u, &herd[9].dna, &herd[9].state) == 1);
	herd[9].world_x = 60.0f;
	herd[9].world_y = -20.0f;
	herd[9].world_z = -8.0f;
	herd[9].water_z = 0.0f;
	herd[9].scale_wu_per_m = 45.0f;
	herd[9].alpha   = 1.0f;
	herd[9].flags   = ND_MOB_ACT_IN_WATER;

	nd_mob_shadow_pass_t spass;
	memset(&spass, 0, sizeof(spass));
	make_identity_mat4(spass.light_vp);
	spass.sea_level = 0.0f;

	nd_mob_frame_t frame;
	memset(&frame, 0, sizeof(frame));
	make_identity_mat4(frame.mvp);
	make_identity_mat4(frame.shadow_vp);
	frame.east[0] = 1.0f;
	frame.south[1] = 1.0f;
	frame.up[2] = 1.0f;
	frame.sun_dir[2] = 1.0f;
	frame.sky_col[0] = 0.55f;
	frame.sky_col[1] = 0.70f;
	frame.sky_col[2] = 0.85f;
	frame.fog_start = 500.0f;
	frame.fog_end = 2500.0f;
	frame.sea_level = 0.0f;
	frame.atmo_intensity = 1.0f;
	frame.shadow_active = 1u;
	frame.shadow_tex_unit = 12u;

	nd_mob_gl_stats_t st;

#ifdef LAM_WEBGL_NATIVE
	/* 2a. Simulate Firefox WebGL shaderWorker: 4 polls pending before completion.
	 * Must NOT delete programs or re-create them on every frame. */
	s_mock_async_delay = 4;
	assert(nd_mob_gl_sync(herd, 10u, 0.016f) == 10);
	/* Frame 0 (poll 1 in shadow, poll 2 in color) and Frame 1 (polls 3 & 4): still pending */
	for (int f = 0; f < 2; f++) {
		assert(nd_mob_gl_draw_shadow(&spass) == 0);
		assert(nd_mob_gl_draw_color(&frame) == 0);
		assert(s_mock_prog_creates == 2u);
		assert(s_mock_prog_deletes == 0u);
		assert(nd_mob_gl_get_stats(&st) == 0);
		assert(st.gl_ready == 0u);
	}
	/* Next frame: polls_left == 0 -> both pipelines complete cleanly without re-creating */
	assert(nd_mob_gl_sync(herd, 10u, 0.016f) == 10);
	assert(nd_mob_gl_draw_shadow(&spass) == 4);
	assert(nd_mob_gl_draw_color(&frame) == 4);
	assert(s_mock_prog_creates == 2u);
	assert(s_mock_prog_deletes == 0u);
	assert(s_mock_cull_disables >= 2u);
	assert(s_mock_tex_uploads_aligned >= 2u);
	s_mock_async_delay = 0;
#else
	int synced = nd_mob_gl_sync(herd, 10u, 0.016f);
	assert(synced == 10);

	int sh_draws = nd_mob_gl_draw_shadow(&spass);
	assert(sh_draws == 4);

	int col_draws = nd_mob_gl_draw_color(&frame);
	assert(col_draws == 4);
#endif

	memset(&st, 0, sizeof(st));
	assert(nd_mob_gl_get_stats(&st) == 0);
	assert(st.active_actors == 10u);
	assert(st.cached_species == 4u);
	assert(st.draw_calls_shadow == 4u);
	assert(st.draw_calls_color == 4u);
	assert(st.total_verts > 2000u);
	assert(st.total_indices > 9000u);
	assert(st.gl_ready == 1u);

	/* 3. Context loss reset preserves CPU species cache and re-initializes GL asynchronously */
	assert(nd_mob_gl_reset_context(0) == 0);
	assert(nd_mob_gl_get_stats(&st) == 0);
	assert(st.gl_ready == 0u);
	assert(st.cached_species == 4u);

#ifdef LAM_WEBGL_NATIVE
	s_mock_async_delay = 1;
	s_mock_unpack_align = 1;
	assert(nd_mob_gl_draw_color(&frame) == 0);
	assert(nd_mob_gl_draw_color(&frame) == 4);
	assert(s_mock_prog_creates == 4u);
	assert(s_mock_prog_deletes == 0u);
	s_mock_async_delay = 0;
#else
	assert(nd_mob_gl_draw_color(&frame) == 4);
#endif
	assert(nd_mob_gl_get_stats(&st) == 0);
	assert(st.gl_ready == 1u);

	/* 4. Sync all 14 mobs.dsv species + ungulates + human simultaneously */
	static const char *const k_names[] = {
		"Goldfish", "Salmon", "Tuna", "Koifish",
		"Dolphin", "Shark", "Moonfish", "Rainbowfish",
		"Icebird", "Parrot", "Swallow", "Woodpecker",
		"Sparrow", "Owl", "Lamb", "Sheep", "Horse", "Cow", "Pig", "Human"
	};
	enum { N_ALL = (int)(sizeof(k_names) / sizeof(k_names[0])) };
	nd_mob_actor_in_t zoo[N_ALL];
	memset(zoo, 0, sizeof(zoo));
	for (int i = 0; i < N_ALL; i++) {
		zoo[i].ref = (uint64_t)(1000 + i);
		assert(creature_dna_from_name(k_names[i], (uint32_t)(1000 + i), &zoo[i].dna, &zoo[i].state) == 1);
		zoo[i].world_x = 12.0f * (float)i;
		zoo[i].scale_wu_per_m = 45.0f;
		zoo[i].alpha = 1.0f;
		if (zoo[i].dna.archetype == CREATURE_ARCH_PISCINE)
			zoo[i].flags |= ND_MOB_ACT_IN_WATER;
		else if (zoo[i].dna.archetype == CREATURE_ARCH_AVIAN)
			zoo[i].flags |= ND_MOB_ACT_AIRBORNE;
	}

	/* Advance 5 frames to exercise actor motion tracking & ambient swimming/flight */
	for (int f = 0; f < 5; f++) {
		for (int i = 0; i < N_ALL; i++)
			zoo[i].world_y += 0.8f;
		assert(nd_mob_gl_sync(zoo, (unsigned)N_ALL, 0.033f) == N_ALL);
	}
	assert(nd_mob_gl_draw_shadow(&spass) == N_ALL);
	assert(nd_mob_gl_draw_color(&frame) == N_ALL);
	assert(nd_mob_gl_get_stats(&st) == 0);
	assert(st.active_actors == (uint32_t)N_ALL);
	assert(st.cached_species == (uint32_t)N_ALL);

	/* 5. Multi-palette single-VBO instancing + attachment socket query (nd_mob_gl_get_bone_matrix) */
	uint32_t species_before_crowd = st.cached_species;
	nd_mob_actor_in_t crowd[3];
	memset(crowd, 0, sizeof(crowd));
	creature_dna_t base_human = creature_dna_human_default(500u);
	base_human.arch.biped.garment_top    = 0;
	base_human.arch.biped.garment_bottom = 0;
	base_human.arch.biped.headgear       = 1; /* distinct from zoo human so +1 cache entry */
	base_human.arch.biped.cloak_len      = 1;
	static const uint32_t k_palettes[3][4] = {
		{ 0xE6B89CFFu, 0x2B1E16FFu, 0xB83228FFu, 0x2E4A7DFFu },
		{ 0x8D5524FFu, 0x1A1410FFu, 0x2F7A48FFu, 0x6E3B20FFu },
		{ 0xF5D0B5FFu, 0xD4A84BFFu, 0x4A3A8CFFu, 0x222630FFu }
	};
	for (int i = 0; i < 3; i++) {
		crowd[i].ref = (uint64_t)(5000 + i);
		crowd[i].dna = base_human;
		crowd[i].dna.seed = (uint32_t)(700 + i);
		for (int c = 0; c < 4; c++)
			crowd[i].dna.palette[c] = k_palettes[i][c];
		crowd[i].state.age = 0.55f;
		crowd[i].state.weight = 0.5f;
		crowd[i].world_x = 10.0f * (float)i;
		crowd[i].scale_wu_per_m = 45.0f;
		crowd[i].alpha = 1.0f;
		crowd[i].flags = ND_MOB_ACT_HAS_ANIM_PARAMS;
		crowd[i].anim.action_id = (i == 0) ? MOB_ACT_SWING : MOB_ACT_NONE;
		crowd[i].anim.action_phase = 0.35f;
		crowd[i].anim.anim_sec = 1.2f;
	}
	assert(nd_mob_gl_sync(crowd, 3u, 0.016f) == 3);
	assert(nd_mob_gl_draw_color(&frame) == 1); /* 3 differently-colored humans -> 1 instanced draw call! */
	assert(nd_mob_gl_get_stats(&st) == 0);
	assert(st.cached_species == species_before_crowd + 1u);

	/* Query right-hand socket bone matrix for crowd[0] */
	float hand_m[12];
	assert(nd_mob_gl_get_bone_matrix(5000u, MOB_BIPED_HAND_R, hand_m) == 0);
	for (int k = 0; k < 12; k++)
		assert(isfinite(hand_m[k]));
	float det = hand_m[0] * (hand_m[5] * hand_m[10] - hand_m[6] * hand_m[9])
	          - hand_m[1] * (hand_m[4] * hand_m[10] - hand_m[6] * hand_m[8])
	          + hand_m[2] * (hand_m[4] * hand_m[9]  - hand_m[5] * hand_m[8]);
	assert(fabsf(det - 1.0f) < 1e-4f);
	/* Invalid ref or out-of-range bone returns -1 */
	assert(nd_mob_gl_get_bone_matrix(999999u, MOB_BIPED_HAND_R, hand_m) == -1);
	assert(nd_mob_gl_get_bone_matrix(5000u, MOB_BIPED_BONE_COUNT, hand_m) == -1);

	/* 6. Dynamic species cache growth past legacy 32-slot cap (zero silent fallback) */
	nd_mob_actor_in_t variants[24];
	memset(variants, 0, sizeof(variants));
	for (int i = 0; i < 24; i++) {
		variants[i].ref = (uint64_t)(8000 + i);
		variants[i].dna = creature_dna_human_default((uint32_t)(8000 + i));
		variants[i].dna.arch.biped.garment_top    = (uint8_t)(i % 3);
		variants[i].dna.arch.biped.garment_bottom = (uint8_t)((i / 3) % 2);
		variants[i].dna.arch.biped.headgear       = (uint8_t)((i / 6) % 3);
		variants[i].dna.arch.biped.cloak_len      = (uint8_t)((i / 18) % 3);
		variants[i].dna.arch.biped.torso_v_taper  = 0.15f + 0.03f * (float)i;
		variants[i].state.age = 0.55f;
		variants[i].state.weight = 0.50f;
		variants[i].scale_wu_per_m = 45.0f;
		variants[i].alpha = 1.0f;
	}
	assert(nd_mob_gl_sync(variants, 24u, 0.016f) == 24);
	assert(nd_mob_gl_draw_color(&frame) == 24);
	assert(nd_mob_gl_get_stats(&st) == 0);
	assert(st.cached_species > 32u);

	printf("PASS test_nd_mob_gl (actors=%u, cached_species=%u, total_verts=%u, total_indices=%u)\n",
	       st.active_actors, st.cached_species, st.total_verts, st.total_indices);
	return 0;
}
