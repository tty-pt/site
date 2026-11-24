#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "nd_seven_ground.h"

int main(void)
{
	nd_seven_alt_profile_t p_close, p_mid, p_orbit, prev, cur;

	/* 1. High preset: U-shaped target_quad_px (crisp close -> lean mid -> crisp orbit) */
	nd_seven_eval_alt_profile(280.0f, 1.0f, 1.0f, ND_CLOUD_QUALITY_HIGH, &p_close);
	nd_seven_eval_alt_profile(8000.0f, 1.0f, 1.0f, ND_CLOUD_QUALITY_HIGH, &p_mid);
	nd_seven_eval_alt_profile(8000000.0f, 1.0f, 1.0f, ND_CLOUD_QUALITY_HIGH, &p_orbit);

	assert(fabsf(p_close.target_quad_px - (2.0f * 32.0f / 48.0f)) < 1e-3f);
	assert(p_mid.target_quad_px > p_close.target_quad_px + 1.0f);
	assert(p_orbit.target_quad_px < p_mid.target_quad_px - 0.4f);
	assert(p_orbit.target_quad_px <= 2.45f);
	assert(p_close.min_level == 0u && p_mid.min_level == 0u && p_orbit.min_level == 0u);
	assert(p_orbit.eff_cloud_scale >= 0.99f);
	assert(p_mid.eff_cloud_scale <= 0.52f);

	/* 2. Low preset: res_scale relaxes to 1.0 in orbit, cloud scale jumps to 0.67 */
	nd_seven_eval_alt_profile(1800.0f, 0.55f, 0.67f, ND_CLOUD_QUALITY_LOW, &p_mid);
	nd_seven_eval_alt_profile(8000000.0f, 0.55f, 0.67f, ND_CLOUD_QUALITY_LOW, &p_orbit);
	assert(fabsf(p_mid.eff_res_scale - 0.67f) < 1e-3f);
	assert(fabsf(p_orbit.eff_res_scale - 1.00f) < 1e-3f);
	assert(p_orbit.eff_cloud_scale >= 0.66f);
	assert(p_orbit.target_quad_px <= 3.40f);

	/* 3. Continuity check across 1000 log-spaced samples [35 .. 28,000,000]: zero step cliffs */
	float log_min = log2f(35.0f);
	float log_max = log2f(28000000.0f);
	nd_seven_eval_alt_profile(35.0f, 0.75f, 0.75f, ND_CLOUD_QUALITY_MEDIUM, &prev);
	for (int i = 1; i <= 1000; i++) {
		float d = exp2f(log_min + (log_max - log_min) * ((float)i / 1000.0f));
		nd_seven_eval_alt_profile(d, 0.75f, 0.75f, ND_CLOUD_QUALITY_MEDIUM, &cur);
		assert(fabsf(cur.target_quad_px - prev.target_quad_px) < 0.05f);
		assert(fabsf(cur.eff_res_scale - prev.eff_res_scale) < 0.02f);
		assert(fabsf(cur.eff_cloud_scale - prev.eff_cloud_scale) < 0.02f);
		assert(cur.grass_max_dist == 1450.0f);
		assert(cur.ssao_max_dist == 2200.0f);
		assert(cur.chunk_budget >= 4u && cur.chunk_budget <= 16u);
		prev = cur;
	}

	/* 4. Progressive meadow lattice stepping */
	assert(nd_seven_meadow_lattice_step(200.0f, 1800.0f) == 16);
	assert(nd_seven_meadow_lattice_step(500.0f, 1800.0f) == 24);
	assert(nd_seven_meadow_lattice_step(900.0f, 1800.0f) == 36);
	assert(nd_seven_meadow_lattice_step(1300.0f, 1800.0f) == 56);
	assert(nd_seven_meadow_lattice_step(1850.0f, 1800.0f) == 0);

	printf("test_alt_profile: ALL PASSED\n");
	return 0;
}
