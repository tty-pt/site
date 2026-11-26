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

	/* 5. Phase 6C Piece 3: Closed-loop Dynamic Resolution Scaling (DRS) controller */
	{
		nd_seven_drs_t drs;
		nd_seven_drs_init(&drs);
		assert(drs.enabled == 0);
		assert(fabsf(drs.cur_scale - 1.00f) < 1e-6f);

		/* 5a. Disabled DRS is strictly invariant (always 1.00x) */
		for (int i = 0; i < 30; i++) {
			float s = nd_seven_drs_update(&drs, 0.040f, (float)i * 20.0f, 0.0f, 280.0f, (float)i * 5.0f, 55.0f);
			assert(fabsf(s - 1.00f) < 1e-6f);
		}

		/* 5b. Deadband [13ms .. 18ms]: zero oscillation when stationary */
		nd_seven_drs_init(&drs);
		drs.enabled = 1;
		for (int i = 0; i < 60; i++) {
			float s = nd_seven_drs_update(&drs, 0.016f, 0.0f, 0.0f, 280.0f, 0.0f, 55.0f);
			assert(fabsf(s - 1.00f) < 1e-6f);
		}

		/* 5c. Sustained over-budget (24ms) steps down cleanly to min_scale (0.75) */
		float s_last = 1.00f;
		for (int i = 0; i < 60; i++) {
			float s = nd_seven_drs_update(&drs, 0.024f, 0.0f, 0.0f, 280.0f, 0.0f, 55.0f);
			assert(s <= s_last + 1e-6f);
			assert(s >= 0.75f - 1e-6f);
			s_last = s;
		}
		assert(fabsf(s_last - 0.75f) < 1e-4f);

		/* 5d. Sustained under-budget (10ms) recovers smoothly back to max_scale (1.00) */
		for (int i = 0; i < 80; i++) {
			float s = nd_seven_drs_update(&drs, 0.010f, 0.0f, 0.0f, 280.0f, 0.0f, 55.0f);
			assert(s >= s_last - 1e-6f);
			assert(s <= 1.00f + 1e-6f);
			s_last = s;
		}
		assert(fabsf(s_last - 1.00f) < 1e-4f);

		/* 5e. Fast camera motion triggers immediate downscale, then rapid recovery to 1.00x
		 *     within 250 ms of camera settle */
		nd_seven_drs_init(&drs);
		drs.enabled = 1;
		(void)nd_seven_drs_update(&drs, 0.016f, 0.0f, 0.0f, 280.0f, 0.0f, 55.0f);
		float s_pan = 1.00f;
		for (int i = 1; i <= 6; i++) {
			s_pan = nd_seven_drs_update(&drs, 0.022f, (float)i * 12.0f, 0.0f, 280.0f, (float)i * 2.0f, 55.0f);
		}
		assert(s_pan <= 0.80f + 1e-4f);
		assert(drs.in_motion == 1u);

		/* Camera settles at 15ms/frame: must recover to 1.00x within 250ms (17 frames @ 15ms = 255ms) */
		float s_rec = s_pan;
		for (int i = 0; i < 17; i++) {
			s_rec = nd_seven_drs_update(&drs, 0.015f, 72.0f, 0.0f, 280.0f, 12.0f, 55.0f);
		}
		assert(fabsf(s_rec - 1.00f) < 1e-4f);
		assert(drs.in_motion == 0u);

		/* 5f. Global opt-in API defaults to 0 and resets to 1.00 on disable */
		assert(nd_seven_get_drs() == 0);
		assert(fabsf(nd_seven_get_drs_scale() - 1.00f) < 1e-6f);
		nd_seven_set_drs(1);
		assert(nd_seven_get_drs() == 1);
		nd_seven_drs_feed_frame(0.016f, 0.0f, 0.0f, 280.0f, 0.0f, 55.0f);
		for (int i = 1; i <= 5; i++)
			nd_seven_drs_feed_frame(0.025f, (float)i * 10.0f, 0.0f, 280.0f, 0.0f, 55.0f);
		assert(nd_seven_get_drs_scale() < 0.99f);
		nd_seven_set_drs(0);
		assert(nd_seven_get_drs() == 0);
		assert(fabsf(nd_seven_get_drs_scale() - 1.00f) < 1e-6f);
	}

	printf("test_alt_profile: ALL PASSED\n");
	return 0;
}
