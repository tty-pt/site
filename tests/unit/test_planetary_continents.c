/*
 * test_planetary_continents.c — Unit tests for planetary continents,
 * streamlined 1/f noise engine, toroidal climate bands, and ground-level relief.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <assert.h>
#include <math.h>

#include <ttypt/noise.h>
#include <ttypt/chaff.h>
#include <nd/field.h>

static void test_continental_scale_and_land_ratio(void)
{
	printf("[1. Macro Continental Landmasses & Planetary Ocean Ratio]\n");
	uint16_t world = 1; /* Citadel */
	int ocean_samples = 0;
	int total_samples = 0;

	/* Sample a global grid across the 65,536-room planet (64 x 32 samples) */
	for (int y = -16; y < 16; y++) {
		for (int x = -32; x < 32; x++) {
			int32_t wx = x * (10485760 / 64);
			int32_t wy = y * (8388608 / 32);
			nd_field_sample_t fs = nd_field_sample(world, 0, wx, wy);
			total_samples++;
			if (fs.is_water) ocean_samples++;
		}
	}

	float ocean_pct = (float)ocean_samples / (float)total_samples * 100.0f;
	float land_pct = 100.0f - ocean_pct;
	printf("  Planetary ocean coverage: %.1f%% (land: %.1f%%)\n", ocean_pct, land_pct);
	fflush(stdout);

	/* Must have realistic planetary distribution: dominant oceans (60% to 90%) and substantial continents (10% to 40%) */
	assert(ocean_pct >= 60.0f && ocean_pct <= 90.0f);
	assert(land_pct >= 10.0f && land_pct <= 40.0f);
	printf("  PASS: Prominent macro continents and realistic ocean coverage verified\n");
}

static void test_lod_continental_invariance(void)
{
	printf("[2. Bit-Exact Continental Anchor Across All LODs]\n");
	uint16_t world = 1;
	const int32_t test_points[][2] = {
		{ 0, 0 },
		{ 1048576, -524288 },
		{ -2621440, 1310720 },
		{ 4194304, -3145728 },
		{ -5000000, 4000000 }
	};
	size_t num_pts = sizeof(test_points) / sizeof(test_points[0]);

	for (size_t i = 0; i < num_pts; i++) {
		int32_t wx = test_points[i][0];
		int32_t wy = test_points[i][1];

		int32_t c0 = 0, f0 = 0;
		int32_t c1 = 0, f1 = 0;
		int32_t c2 = 0, f2 = 0;
		int32_t c3 = 0, f3 = 0;

		int32_t e0 = chaff_elevation_q12_4(world, 0, wx, wy, &c0, &f0);
		int32_t e1 = chaff_elevation_q12_4(world, 1, wx, wy, &c1, &f1);
		int32_t e2 = chaff_elevation_q12_4(world, 2, wx, wy, &c2, &f2);
		int32_t e3 = chaff_elevation_q12_4(world, 3, wx, wy, &c3, &f3);

		/* Coarse continental elevation MUST be 100% identical */
		assert(c0 == c1);
		assert(c1 == c2);
		assert(c2 == c3);

		/* LOD 0 has fine detail zeroed */
		assert(f0 == 0);
		assert(e0 == c0);

		/* Additive invariant */
		assert(e1 == c1 + f1);
		assert(e2 == c2 + f2);
		assert(e3 == c3 + f3);
	}
	printf("  PASS: Continental baseline is strictly invariant across all LODs\n");
}

static void test_torus_modular_wrapping_continuity(void)
{
	printf("[3. Torus Modular Periodic Wrapping Continuity]\n");
	uint16_t world = 1;
	const int32_t W = 10485760;
	const int32_t H = 8388608;

	/* Test across the X wrap seam: W/2 vs -W/2 and 0 vs W */
	for (int y = -10; y <= 10; y++) {
		int32_t wy = y * (H / 24);
		int32_t c_pos = 0, f_pos = 0;
		int32_t c_neg = 0, f_neg = 0;

		chaff_elevation_q12_4(world, 0, W / 2, wy, &c_pos, &f_pos);
		chaff_elevation_q12_4(world, 0, -W / 2, wy, &c_neg, &f_neg);

		int32_t diff = abs(c_pos - c_neg);
		assert(diff == 0); /* Exact identity across periodic boundary */

		/* Full field sample (including temperature and biome) wraps identically at X=0 vs X=1 and -W/2 vs +W/2 */
		nd_field_sample_t s_x0 = nd_field_sample(world, 0, 0, wy), s_x1 = nd_field_sample(world, 0, W, wy);
		assert(s_x0.elevation == s_x1.elevation && s_x0.temp == s_x1.temp && s_x0.rain == s_x1.rain && s_x0.bio == s_x1.bio);
		nd_field_sample_t s_x_pos = nd_field_sample(world, 0, W / 2, wy), s_x_neg = nd_field_sample(world, 0, -W / 2, wy);
		assert(s_x_pos.elevation == s_x_neg.elevation && s_x_pos.temp == s_x_neg.temp && s_x_pos.rain == s_x_neg.rain && s_x_pos.bio == s_x_neg.bio);
	}

	/* Test across the Y wrap seam: H/2 vs -H/2 and 0 vs H */
	for (int x = -10; x <= 10; x++) {
		int32_t wx = x * (W / 24);
		int32_t c_pos = 0, f_pos = 0;
		int32_t c_neg = 0, f_neg = 0;

		chaff_elevation_q12_4(world, 0, wx, H / 2, &c_pos, &f_pos);
		chaff_elevation_q12_4(world, 0, wx, -H / 2, &c_neg, &f_neg);

		int32_t diff = abs(c_pos - c_neg);
		assert(diff == 0); /* Exact identity across periodic boundary */

		/* Full field sample (including temperature and biome) wraps identically at Y=0 vs Y=1 and -H/2 vs +H/2 */
		nd_field_sample_t s_y0 = nd_field_sample(world, 0, wx, 0), s_y1 = nd_field_sample(world, 0, wx, H);
		assert(s_y0.elevation == s_y1.elevation && s_y0.temp == s_y1.temp && s_y0.rain == s_y1.rain && s_y0.bio == s_y1.bio);
		nd_field_sample_t s_y_pos = nd_field_sample(world, 0, wx, H / 2), s_y_neg = nd_field_sample(world, 0, wx, -H / 2);
		assert(s_y_pos.elevation == s_y_neg.elevation && s_y_pos.temp == s_y_neg.temp && s_y_pos.rain == s_y_neg.rain && s_y_pos.bio == s_y_neg.bio);
	}
	printf("  PASS: 100%% seamless torus wrapping verified along both major and minor circumferences\n");
}

static void test_toroidal_climate_and_biomes(void)
{
	printf("[4. Toroidal Latitude Climate Bands & Continental Biomes]\n");
	uint16_t world = 1;
	int biome_counts[BIOME_MAX] = { 0 };
	int samples = 0;

	/* Sample grid across the torus */
	for (int y = -16; y <= 16; y++) {
		for (int x = -16; x <= 16; x++) {
			int32_t wx = x * (10485760 / 32);
			int32_t wy = y * (8388608 / 32);

			nd_field_sample_t fs = nd_field_sample(world, 0, wx, wy);
			if (fs.bio < BIOME_MAX) {
				biome_counts[fs.bio]++;
				samples++;
			}
		}
	}

	/* Must have diversity of biomes (at least 6 different active biomes) */
	int active_biomes = 0;
	for (int b = 1; b < BIOME_MAX; b++) {
		if (biome_counts[b] > 0) active_biomes++;
	}
	printf("  Active continental biomes: %d / %d\n", active_biomes, BIOME_MAX - 1);
	assert(active_biomes >= 6);

	/* Permanent ice must exist on cold/polar inner rim or mountain peaks */
	assert(biome_counts[BIOME_PERMANENT_ICE] > 0);
	/* Temperate grassland / woodland must exist on habitable plains */
	assert(biome_counts[BIOME_TEMPERATE_GRASSLAND] > 0 || biome_counts[BIOME_WOODLAND] > 0);

	printf("  PASS: Toroidal climate zones and authentic continental biomes verified\n");
}

static void test_vertical_relief_and_storage_bounds(void)
{
	printf("[5. High Mountains, Deep Ocean Trenches & Q12.4 Storage Bounds]\n");
	uint16_t world = 1; /* Citadel */
	int16_t min_elevation = 32767;
	int16_t max_elevation = -32768;
	int16_t max_depth = 0;

	/* Sample dense planetary grid */
	for (int y = -32; y < 32; y++) {
		for (int x = -64; x < 64; x++) {
			int32_t wx = x * (10485760 / 128);
			int32_t wy = y * (8388608 / 64);
			nd_field_sample_t fs0 = nd_field_sample(world, 0, wx, wy);
			nd_field_sample_t fs3 = nd_field_sample(world, 3, wx, wy);

			/* Q12.4 fixed-point storage bounds verification (must fit in int16_t without wrap) */
			assert((int32_t)fs0.elevation_q12_4 == ((int32_t)fs0.elevation << 4));
			assert(fs3.elevation_q12_4 >= -32000 && fs3.elevation_q12_4 <= 32000);

			if (fs3.elevation < min_elevation) min_elevation = fs3.elevation;
			if (fs3.elevation > max_elevation) max_elevation = fs3.elevation;
			if (fs3.depth > max_depth) max_depth = fs3.depth;
		}
	}

	printf("  Vertical relief: lowest oceanic trench = %d units, highest mountain peak = %d units, max depth = %d units\n",
	       min_elevation, max_elevation, max_depth);
	fflush(stdout);

	/* Must reach Mount Everest benchmark (>= 1850) and abyssal trenches (<= -800, max_depth >= 800) */
	assert(max_elevation >= 1850);
	assert(min_elevation <= -800);
	assert(max_depth >= 800);
	printf("  PASS: Mount Everest alpine peaks and abyssal ocean trenches verified with zero Q12.4 overflow\n");
}

static void test_toroidal_solar_incidence_and_temperature(void)
{
	printf("[6. Toroidal Solar Incidence, Hole Illumination, Wrapping Continuity & Sun Azimuth]\n");
	const uint16_t world = 1;
	const uint32_t tm_neutral = 128u << 16;
	const int32_t W = 10485760, H = 8388608;

	nd_field_set_sun_azimuth(0);
	assert(nd_field_get_sun_azimuth() == 0);

	/* Surface normals at cardinal torus positions */
	int32_t nx, ny, nz;
	nd_field_torus_normal_q15(0, 0, &nx, &ny, &nz);
	assert(nx == 32768 && ny == 0 && nz == 0);
	nd_field_torus_normal_q15(0, -32768, &nx, &ny, &nz);
	assert(nx == -32768 && ny == 0 && nz == 0);
	nd_field_torus_normal_q15(-32768, -32768, &nx, &ny, &nz);
	assert(nx == 32768 && ny == 0 && nz == 0);
	nd_field_torus_normal_q15(0, 16384, &nx, &ny, &nz);
	assert(nx == 0 && ny == 0 && nz == 32768);
	nd_field_torus_normal_q15(0, -16384, &nx, &ny, &nz);
	assert(nx == 0 && ny == 0 && nz == -32768);

	/* 1. Temperature varies with BOTH X and Y */
	assert(nd_field_temp(world, 0, tm_neutral, 0, 0) > nd_field_temp(world, 0, tm_neutral, 8192, 0));
	assert(nd_field_temp(world, 0, tm_neutral, 8192, 0) > nd_field_temp(world, 0, tm_neutral, 16384, 0));
	assert(nd_field_temp(world, 0, tm_neutral, 0, 0) > nd_field_temp(world, 0, tm_neutral, 0, 8192));
	assert(nd_field_temp(world, 0, tm_neutral, 0, 8192) > nd_field_temp(world, 0, tm_neutral, 0, 16384));
	assert(nd_field_sample(world, 0, 80, 64).temp > nd_field_sample(world, 0, W / 2 + 80, 64).temp);
	assert(nd_field_sample(world, 0, 80, 64).temp > nd_field_sample(world, 0, 80, H / 4 + 64).temp);

	/* 2 & 3. Inner surface (phi=pi): illuminated through hole on far side, in shadow on near side */
	assert(nd_field_torus_solar_incidence_q15(-32768, -32768, 0) == 32768);
	assert(nd_field_torus_solar_incidence_q15(0, -32768, 0) == 0);
	assert(nd_field_temp(world, 0, tm_neutral, -32768, -32768) == ND_FIELD_SOLAR_TEMP_MAX);
	assert(nd_field_temp(world, 0, tm_neutral, 0, -32768) == ND_FIELD_SOLAR_TEMP_MAX - ND_FIELD_SOLAR_TEMP_SPAN);
	assert(nd_field_sample(world, 0, W / 2 + 80, H / 2 + 64).temp > nd_field_sample(world, 0, 80, H / 2 + 64).temp + 80);

	/* 4. Upper (phi=pi/2) and lower (phi=3*pi/2) tube surfaces receive zero direct illumination */
	for (int k = 0; k < 16; k++) {
		int16_t px = (int16_t)(k * 4096);
		assert(nd_field_torus_solar_incidence_q15(px, 16384, 0) == 0);
		assert(nd_field_torus_solar_incidence_q15(px, -16384, 0) == 0);
		assert(nd_field_temp(world, 0, tm_neutral, px, 16384) == ND_FIELD_SOLAR_TEMP_MAX - ND_FIELD_SOLAR_TEMP_SPAN);
		assert(nd_field_temp(world, 0, tm_neutral, px, -16384) == ND_FIELD_SOLAR_TEMP_MAX - ND_FIELD_SOLAR_TEMP_SPAN);
	}

	/* 5. Outer surface (phi=0) receives max direct illumination when facing Sun */
	int32_t inc_outer_sun = nd_field_torus_solar_incidence_q15(0, 0, 0);
	assert(inc_outer_sun == 32768 && nd_field_torus_solar_incidence_q15(-32768, 0, 0) == 0);
	int16_t t_outer_sun = nd_field_temp(world, 0, tm_neutral, 0, 0);
	assert(t_outer_sun == ND_FIELD_SOLAR_TEMP_MAX);
	for (int y = -8; y < 8; y++)
		for (int x = -8; x < 8; x++) {
			int16_t px = (int16_t)(x * 4096), py = (int16_t)(y * 4096);
			assert(nd_field_torus_solar_incidence_q15(px, py, 0) <= inc_outer_sun);
			assert(nd_field_temp(world, 0, tm_neutral, px, py) <= t_outer_sun);
		}

	/* 6. Continuity across both X and Y wrapping boundaries (32767 -> -32768 and -1 -> 0) */
	for (int k = -16; k <= 16; k++) {
		int16_t coord = (int16_t)(k * 2048);
		assert(abs(nd_field_temp(world, 0, tm_neutral, coord, 32767) - nd_field_temp(world, 0, tm_neutral, coord, -32768)) <= 1);
		assert(abs(nd_field_temp(world, 0, tm_neutral, coord, -1) - nd_field_temp(world, 0, tm_neutral, coord, 0)) <= 1);
		assert(abs(nd_field_temp(world, 0, tm_neutral, 32767, coord) - nd_field_temp(world, 0, tm_neutral, -32768, coord)) <= 1);
		assert(abs(nd_field_temp(world, 0, tm_neutral, -1, coord) - nd_field_temp(world, 0, tm_neutral, 0, coord)) <= 1);

		int32_t wx = (int32_t)coord * 160 + 80;
		nd_field_sample_t s_ymax = nd_field_sample(world, 0, wx, 64 - 32767 * 128);
		nd_field_sample_t s_ymin = nd_field_sample(world, 0, wx, 64 - (-32768) * 128);
		assert(abs(s_ymax.temp - s_ymin.temp) <= 10);
		nd_field_sample_t s_y_neg1 = nd_field_sample(world, 0, wx, 64 - (-1) * 128);
		nd_field_sample_t s_y_zero = nd_field_sample(world, 0, wx, 64);
		assert(abs(s_y_neg1.temp - s_y_zero.temp) <= 10);

		int32_t wy = 64 - (int32_t)coord * 128;
		nd_field_sample_t s_xmax = nd_field_sample(world, 0, 32767 * 160 + 80, wy);
		nd_field_sample_t s_xmin = nd_field_sample(world, 0, (-32768) * 160 + 80, wy);
		assert(abs(s_xmax.temp - s_xmin.temp) <= 10);
		nd_field_sample_t s_x_neg1 = nd_field_sample(world, 0, (-1) * 160 + 80, wy);
		nd_field_sample_t s_x_zero = nd_field_sample(world, 0, 80, wy);
		assert(abs(s_x_neg1.temp - s_x_zero.temp) <= 10);
	}

	/* 7. Changing Sun azimuth moves illuminated and shadowed regions */
	const uint16_t azimuths[4] = { 0, 16384, 32768, 49152 };
	for (int i = 0; i < 4; i++) {
		uint16_t az = azimuths[i];
		nd_field_set_sun_azimuth(az);
		assert(nd_field_get_sun_azimuth() == az);
		int16_t sun_x = (int16_t)az, anti_x = (int16_t)(az + 32768u);
		assert(nd_field_torus_solar_incidence_q15(sun_x, 0, az) == 32768);
		assert(nd_field_torus_solar_incidence_q15(anti_x, 0, az) == 0);
		assert(nd_field_temp(world, 0, tm_neutral, sun_x, 0) == ND_FIELD_SOLAR_TEMP_MAX);
		assert(nd_field_temp(world, 0, tm_neutral, anti_x, 0) == ND_FIELD_SOLAR_TEMP_MAX - ND_FIELD_SOLAR_TEMP_SPAN);
		assert(nd_field_torus_solar_incidence_q15(sun_x, -32768, az) == 0);
		assert(nd_field_torus_solar_incidence_q15(anti_x, -32768, az) == 32768);
		assert(nd_field_temp(world, 0, tm_neutral, sun_x, -32768) == ND_FIELD_SOLAR_TEMP_MAX - ND_FIELD_SOLAR_TEMP_SPAN);
		assert(nd_field_temp(world, 0, tm_neutral, anti_x, -32768) == ND_FIELD_SOLAR_TEMP_MAX);
		assert(nd_field_sun_azimuth_from_day_tick((uint64_t)i * 256ULL) == az);
	}
	nd_field_set_sun_azimuth(0);
	printf("  PASS: All 7 toroidal solar incidence and temperature invariants verified\n");
}

int main(void)
{
	printf("=== Testing Planetary Continents & Multi-Scale Terrain Engine ===\n");
	test_continental_scale_and_land_ratio();
	test_lod_continental_invariance();
	test_torus_modular_wrapping_continuity();
	test_toroidal_climate_and_biomes();
	test_vertical_relief_and_storage_bounds();
	test_toroidal_solar_incidence_and_temperature();
	printf("ALL PLANETARY CONTINENT TESTS PASSED\n");
	return 0;
}
