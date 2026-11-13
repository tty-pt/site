#ifndef ND_SEVEN_SHADER_H
#define ND_SEVEN_SHADER_H

/*
 * nd_seven_shader.h — Passthrough texture blit shader definition.
 * Registered dynamically with liblamina via lam_shader_register().
 * Pure-C software rasterization in nd_seven_ground.c is the authoritative engine.
 */

#include <ttypt/lam.h>
#include <ttypt/chaff_glsl.h>

static const char * const ND_WATER_FRAG_GLSL =
	"precision mediump float;\n"
	"varying vec2 v_uv;\n"
	"uniform sampler2D u_tex;\n"
	"void main() {\n"
	"	gl_FragColor = texture2D(u_tex, v_uv);\n"
	"}\n";

static inline uint32_t nd_water_shader_id(void)
{
	static uint32_t s_id = 0;
#ifdef __wasm__
	if (!s_id) {
		s_id = lam_shader_register("nd_water", ND_WATER_FRAG_GLSL);
	}
#endif
	return s_id;
}

static inline uint32_t nd_chaff_shader_id(void)
{
	static uint32_t s_id = 0;
#ifdef __wasm__
	if (!s_id) {
		s_id = lam_shader_register("chaff_noise", CHAFF_GLSL_FULLSCREEN_FRAG);
	}
#endif
	return s_id;
}

#endif /* ND_SEVEN_SHADER_H */
