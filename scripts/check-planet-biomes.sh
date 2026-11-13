#!/bin/sh
# scripts/check-planet-biomes.sh — Planetary Biome, Latitude Band & Snow/Ice Diagnostic Tool
#
# Usage:
#   sh scripts/check-planet-biomes.sh [world_id] [--map]
#
# Compiles and runs a fast pure-C probe against <nd/field.h> and the Seven
# substrate/shader snow rules to report:
#   1. Land/Ocean coverage & elevation hypsometry distribution
#   2. Global terrestrial biome distribution (% of land)
#   3. Latitude-band biome breakdown (Sunlit Equator, Shaded Equator,
#      Subtropics, Temperate, Polar Caps)
#   4. Equatorial ice & alpine snowline sanity checks (CPU biome + GPU wPolar)
#   5. Compact ASCII planetary biome map (when --map is passed or by default)

set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="/tmp/nd_check_planet_biomes_$$"
SRC="/tmp/nd_check_planet_biomes_$$.c"

cleanup() {
	rm -f "$BIN" "$SRC"
}
trap cleanup EXIT INT TERM

cat > "$SRC" << 'EOF'
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <nd/field.h>

static const char *s_biome_names[BIOME_MAX] = {
	"WATER",
	"PERMANENT_ICE",
	"TUNDRA",
	"TUNDRA2",
	"TUNDRA3",
	"TUNDRA4",
	"COLD_DESERT",
	"SHRUBLAND",
	"CONIFEROUS_FOREST",
	"BOREAL_FOREST",
	"TEMPERATE_GRASSLAND",
	"WOODLAND",
	"TEMPERATE_SEASONAL_FOREST",
	"TEMPERATE_RAINFOREST",
	"DESERT",
	"SAVANNAH",
	"TROPICAL_SEASONAL_FOREST",
	"TROPICAL_RAINFOREST",
	"VOLCANIC"
};

static const char s_biome_chars[BIOME_MAX + 1] = "~ITtttCSFBGWwRdsD5V";

enum lat_band {
	BAND_EQ_SUN = 0,   /* |lat| < 15 deg, sunlit hemisphere */
	BAND_EQ_SHADE,     /* |lat| < 15 deg, night/shaded hemisphere */
	BAND_SUBTROPICAL,  /* 15..38 deg */
	BAND_TEMPERATE,    /* 38..66 deg */
	BAND_POLAR,        /* 66..90 deg (around top/bottom poles |ry| = 16384) */
	BAND_COUNT
};

static const char *s_band_names[BAND_COUNT] = {
	"Equator (Sunlit,  0-15 deg)",
	"Equator (Shaded,  0-15 deg)",
	"Subtropical     (15-38 deg)",
	"Temperate       (38-66 deg)",
	"Polar Caps      (66-90 deg)"
};

/* Distance in room units (0..16384) from nearest equator (ry=0 or ry=+-32768).
 * 0 = Equator (phi = 0 or pi), 16384 = Pole (phi = +-pi/2). */
static int32_t
equator_dist_rooms(int16_t ry)
{
	int32_t a = abs((int32_t)ry);
	return (a <= 16384) ? a : (32768 - a);
}

static enum lat_band
classify_lat_band(int16_t rx, int16_t ry, uint16_t sun_az)
{
	int32_t eq_dist = equator_dist_rooms(ry);
	if (eq_dist < 2730) { /* < 15 deg */
		int32_t inc = nd_field_torus_solar_incidence_q15(rx, ry, sun_az);
		return (inc > 4096) ? BAND_EQ_SUN : BAND_EQ_SHADE;
	} else if (eq_dist < 6918) { /* 15..38 deg */
		return BAND_SUBTROPICAL;
	} else if (eq_dist < 12015) { /* 38..66 deg */
		return BAND_TEMPERATE;
	} else { /* 66..90 deg */
		return BAND_POLAR;
	}
}

int main(int argc, char **argv)
{
	uint16_t world = 1;
	int show_map = 1;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--no-map") == 0) show_map = 0;
		else if (strcmp(argv[i], "--map") == 0) show_map = 1;
		else world = (uint16_t)atoi(argv[i]);
	}

	const int NX = 128;
	const int NY = 64;
	int total = 0, ocean = 0, land = 0;
	int bio_counts[BIOME_MAX] = { 0 };
	int band_land[BAND_COUNT] = { 0 };
	int band_bio[BAND_COUNT][BIOME_MAX];
	memset(band_bio, 0, sizeof(band_bio));

	const int n_thresh = 8;
	const int elev_thresh[8] = { 50, 150, 300, 600, 900, 1200, 1500, 1800 };
	int elev_gt[8] = { 0 };

	int eq_lowland_total = 0;
	int eq_lowland_ice = 0;
	int eq_lowland_tundra = 0;
	int eq_peak_total = 0;
	int eq_peak_ice_or_tundra = 0;
	int polar_lowland_total = 0;
	int polar_lowland_ice_or_tundra = 0;

	for (int iy = -NY / 2; iy < NY / 2; iy++) {
		for (int ix = -NX / 2; ix < NX / 2; ix++) {
			int32_t rx = ix * (65536 / NX) + 137;
			int32_t ry = iy * (65536 / NY) + 293;
			int32_t wx = rx * ND_FIELD_ROOM_W + 80;
			int32_t wy = 64 - ry * ND_FIELD_ROOM_H;

			nd_field_sample_t s = nd_field_sample(world, 3, wx, wy);
			total++;
			if (s.is_water) {
				ocean++;
				continue;
			}
			land++;
			uint8_t b = (s.bio < BIOME_MAX) ? s.bio : 0;
			bio_counts[b]++;

			int32_t elev_rel = (int32_t)s.elevation - (int32_t)s.water_level;
			for (int t = 0; t < n_thresh; t++) {
				if (elev_rel > elev_thresh[t])
					elev_gt[t]++;
			}

			enum lat_band band = classify_lat_band((int16_t)rx, (int16_t)ry, 0);
			band_land[band]++;
			band_bio[band][b]++;

			int32_t eq_dist = equator_dist_rooms((int16_t)ry);
			if (eq_dist < 2730) {
				if (elev_rel < 600) {
					eq_lowland_total++;
					if (b == BIOME_PERMANENT_ICE) eq_lowland_ice++;
					else if (b >= BIOME_TUNDRA && b <= BIOME_TUNDRA4) eq_lowland_tundra++;
				}
				if (elev_rel >= 1200) {
					eq_peak_total++;
					if (b == BIOME_PERMANENT_ICE || (b >= BIOME_TUNDRA && b <= BIOME_TUNDRA4))
						eq_peak_ice_or_tundra++;
				}
			} else if (eq_dist >= 12015 && elev_rel < 600) {
				polar_lowland_total++;
				if (b == BIOME_PERMANENT_ICE || (b >= BIOME_TUNDRA && b <= BIOME_TUNDRA4))
					polar_lowland_ice_or_tundra++;
			}
		}
	}

	printf("========================================================================\n");
	printf("  NEVERDARK PLANETARY BIOME & CLIMATE DIAGNOSTIC (world = %u)\n", world);
	printf("========================================================================\n");
	printf("Samples: %d total | Ocean: %d (%.1f%%) | Land: %d (%.1f%%)\n\n",
	       total, ocean, 100.0 * ocean / total, land, 100.0 * land / total);

	printf("--- 1. Land Elevation Hypsometry (relative to sea level) ---\n");
	for (int t = 0; t < n_thresh; t++) {
		printf("  elev > %4d wu : %4d samples (%5.1f%% of land)\n",
		       elev_thresh[t], elev_gt[t], land ? (100.0 * elev_gt[t] / land) : 0.0);
	}

	printf("\n--- 2. Global Terrestrial Biome Distribution ---\n");
	int active_biomes = 0;
	for (int b = 1; b < BIOME_MAX; b++) {
		if (bio_counts[b] > 0) active_biomes++;
		printf("  [%2d] %-27s (%c): %4d (%5.1f%%)\n",
		       b, s_biome_names[b], s_biome_chars[b],
		       bio_counts[b], land ? (100.0 * bio_counts[b] / land) : 0.0);
	}
	printf("  Active terrestrial biomes: %d / %d\n", active_biomes, BIOME_MAX - 1);

	printf("\n--- 3. Latitude Band Breakdown (grouped into macro realms) ---\n");
	printf("  %-27s | %5s | %6s | %6s | %6s | %6s | %6s | %6s\n",
	       "Latitude Band", "Land", "Ice", "Tundra", "Boreal", "Temper", "Arid", "Tropic");
	printf("  ----------------------------+-------+--------+--------+--------+--------+--------+-------\n");
	for (int bd = 0; bd < BAND_COUNT; bd++) {
		int n = band_land[bd];
		int c_ice    = band_bio[bd][BIOME_PERMANENT_ICE];
		int c_tundra = band_bio[bd][BIOME_TUNDRA] + band_bio[bd][BIOME_TUNDRA2] +
		               band_bio[bd][BIOME_TUNDRA3] + band_bio[bd][BIOME_TUNDRA4];
		int c_boreal = band_bio[bd][BIOME_CONIFEROUS_FOREST] + band_bio[bd][BIOME_BOREAL_FOREST];
		int c_temper = band_bio[bd][BIOME_TEMPERATE_GRASSLAND] + band_bio[bd][BIOME_WOODLAND] +
		               band_bio[bd][BIOME_TEMPERATE_SEASONAL_FOREST] + band_bio[bd][BIOME_TEMPERATE_RAINFOREST] +
		               band_bio[bd][BIOME_SHRUBLAND];
		int c_arid   = band_bio[bd][BIOME_DESERT] + band_bio[bd][BIOME_SAVANNAH] + band_bio[bd][BIOME_COLD_DESERT];
		int c_tropic = band_bio[bd][BIOME_TROPICAL_SEASONAL_FOREST] + band_bio[bd][BIOME_TROPICAL_RAINFOREST];
		double denom = (n > 0) ? (double)n : 1.0;
		printf("  %-27s | %5d | %5.1f%% | %5.1f%% | %5.1f%% | %5.1f%% | %5.1f%% | %5.1f%%\n",
		       s_band_names[bd], n,
		       100.0 * c_ice / denom,
		       100.0 * c_tundra / denom,
		       100.0 * c_boreal / denom,
		       100.0 * c_temper / denom,
		       100.0 * c_arid / denom,
		       100.0 * c_tropic / denom);
	}

	printf("\n--- 4. Key Realism Diagnostics ---\n");
	printf("  Equatorial lowland (<600 wu) PERMANENT_ICE : %d / %d (%.1f%%) [target: 0.0%%]\n",
	       eq_lowland_ice, eq_lowland_total,
	       eq_lowland_total ? (100.0 * eq_lowland_ice / eq_lowland_total) : 0.0);
	printf("  Equatorial lowland (<600 wu) TUNDRA        : %d / %d (%.1f%%) [target: 0.0%%]\n",
	       eq_lowland_tundra, eq_lowland_total,
	       eq_lowland_total ? (100.0 * eq_lowland_tundra / eq_lowland_total) : 0.0);
	printf("  Equatorial high peaks (>=1200 wu) snow/ice : %d / %d (%.1f%%)\n",
	       eq_peak_ice_or_tundra, eq_peak_total,
	       eq_peak_total ? (100.0 * eq_peak_ice_or_tundra / eq_peak_total) : 0.0);
	printf("  Polar lowland (<600 wu) ice/tundra         : %d / %d (%.1f%%) [target: >75%%]\n",
	       polar_lowland_ice_or_tundra, polar_lowland_total,
	       polar_lowland_total ? (100.0 * polar_lowland_ice_or_tundra / polar_lowland_total) : 0.0);

	if (show_map) {
		printf("\n--- 5. Planetary Torus ASCII Map (rx: -32768..+32767, ry: +32767..-32768) ---\n");
		printf("Legend: ~=Water I=Ice T/t=Tundra C=ColdDes S=Shrub F/B=Boreal G/W/w/R=Temp d=Desert s=Savannah D/5=Tropical V=Volcanic\n");
		for (int y = 15; y >= -16; y--) {
			int ry = y * 2048 + 1024;
			int wy = 64 - ry * ND_FIELD_ROOM_H;
			int eq_deg = (int)(equator_dist_rooms((int16_t)ry) * 90 / 16384);
			printf("ry=%6d (%2dd): ", ry, eq_deg);
			for (int x = -32; x < 32; x++) {
				int rx = x * 1024 + 512;
				int wx = rx * ND_FIELD_ROOM_W + 80;
				nd_field_sample_t s = nd_field_sample(world, 3, wx, wy);
				if (s.is_water) putchar('~');
				else putchar(s_biome_chars[s.bio < BIOME_MAX ? s.bio : 0]);
			}
			printf("\n");
		}
	}
	return 0;
}
EOF

gcc -O2 -Wall -Wextra \
	-I"$ROOT/external/nd-world/include" \
	-I"$ROOT/external/libchaff/include" \
	-I"$ROOT/external/noise/include" \
	"$SRC" \
	-L/usr/local/lib -lnoise -lm -Wl,-rpath,/usr/local/lib \
	-o "$BIN"

"$BIN" "$@"
