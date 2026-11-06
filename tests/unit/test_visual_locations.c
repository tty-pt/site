#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <nd/seven-world.h>
#include "nd_seven.h"
#include "nd_seven_ground.h"
#include "nd_seven_atlas.h"
#include "nd_seven_objects.h"
#include "nd_seven_motion.h"
#include "nd_lamina.h"
#include <nd/field.h>
#include <nd/terrain.h>

#define STAGE_W 1280
#define STAGE_H 720
#define MAP_W 224
#define MAP_H 224

static uint32_t s_stage_fb[STAGE_W * STAGE_H];
static uint32_t s_map_fb[MAP_W * MAP_H];

static void dump_ppm(const char *path, const uint32_t *p, int w, int h)
{
	FILE *f = fopen(path, "wb");
	if (!f) {
		fprintf(stderr, "Cannot open %s for write\n", path);
		return;
	}
	fprintf(f, "P6\n%d %d\n255\n", w, h);
	for (int i = 0; i < w * h; i++) {
		uint32_t c = p[i];
		fputc(c & 0xFF, f);
		fputc((c >> 8) & 0xFF, f);
		fputc((c >> 16) & 0xFF, f);
	}
	fclose(f);
}

typedef struct {
	int16_t x, y;
	const char *name;
	const char *description;
} test_location_t;

static const test_location_t LOCATIONS[] = {
	{    0,    0, "loc01_origin_coast",       "Continental Coastline & Shallows" },
	{   10,   10, "loc02_convex_cape",        "Convex Cape & Coastal Shelf" },
	{   20,   20, "loc03_multibiome_shore",   "Multi-Biome Shoreline (Grass + Sand)" },
	{   30,   30, "loc04_ocean_shallows",     "Ocean Shallows Looking to Shore" },
	{   60,   60, "loc05_island_chain",       "Island Chain & Turquoise Shelf" },
	{  -50,  -50, "loc06_continental_shelf",  "Continental Shelf Transition" },
	{ -120,   80, "loc07_ocean_trench",       "Deep Ocean Trench (Abyss)" },
	{  100, -200, "loc08_desert_coast",       "Desert Dunes Meeting Azure Water" },
	{ -250,  150, "loc09_tundra_fjord",       "Arctic Tundra Fjord & Rock Reefs" },
	{  350,  400, "loc10_archipelago",        "Equatorial Archipelago" },
	{ -400, -300, "loc11_abyssal_basin",      "Deep Abyssal Ocean Basin" },
	{  500,  500, "loc12_outer_shelf",        "Outer Coastal Shelf & Reefs" },
};
#define NUM_LOCATIONS (sizeof(LOCATIONS) / sizeof(LOCATIONS[0]))

int main(void)
{
	printf("=== Capturing Multi-Location Visual Verification Pictures ===\n");

	for (size_t i = 0; i < NUM_LOCATIONS; i++) {
		const test_location_t *loc = &LOCATIONS[i];
		nd_seven_forget();
		nd_client_state_t st;
		memset(&st, 0, sizeof(st));
		strcpy(st.preferred_driver, "lamina-seven");
		st.map_data.width = 7;
		st.map_data.height = 7;
		st.map_data.player_x = 3;
		st.map_data.player_y = 3;
		st.map_data.pos[0] = loc->x;
		st.map_data.pos[1] = loc->y;
		st.map_data.pos[2] = 0;
		st.map_data.pos[3] = 1; /* world 1 */

		/* Generate authentic 7x7 procedural world patch around (loc->x, loc->y) */
		int16_t w_level = nd_terrain_water_level(1);
		int water_count = 0, land_count = 0;
		int total_h = 0, total_d = 0;

		for (int r = 0; r < 7; r++) {
			for (int c = 0; c < 7; c++) {
				int idx = r * 7 + c;
				int ti = c - (int)st.map_data.player_x;
				int tj = r - (int)st.map_data.player_y;
				int32_t wx = (int32_t)(loc->x + ti) * NDW_TILE_W + NDW_TILE_W / 2;
				int32_t wy = -(int32_t)(loc->y - tj) * NDW_TILE_H + NDW_TILE_H / 2;

				nd_field_sample_t fs = nd_field_sample(1, 3, wx, wy);
				int is_submerged = (fs.elevation < w_level);
				st.map_data.tiles[idx].bio_idx = fs.bio;
				st.map_data.tiles[idx].exits = 0x1B;
				st.map_data.tiles[idx].flags = is_submerged ? ND_MAP_FLAG_WATER : 0;

				total_h += fs.elevation;
				if (is_submerged) {
					water_count++;
					total_d += (w_level - fs.elevation);
				} else {
					land_count++;
				}
			}
		}

		/* 1. Render Seven Stage (1280x720) */
		nd_seven_set_lamb(NDW_CENTER_X, NDW_CENTER_Y);
		nd_seven_render(s_stage_fb, STAGE_W, STAGE_H, &st);

		char ppm_stage[256];
		snprintf(ppm_stage, sizeof(ppm_stage), "art/visual-inspection/%s_seven.ppm", loc->name);
		dump_ppm(ppm_stage, s_stage_fb, STAGE_W, STAGE_H);

		/* 2. Render Lamina Minimap (224x224) */
		memset(s_map_fb, 0, sizeof(s_map_fb));
		nd_lamina_render_map_themed(s_map_fb, MAP_W, MAP_H, &st.map_data, "lamina", 43200, 0);

		char ppm_map[256];
		snprintf(ppm_map, sizeof(ppm_map), "art/visual-inspection/%s_map.ppm", loc->name);
		dump_ppm(ppm_map, s_map_fb, MAP_W, MAP_H);

		int avg_h = total_h / 49;
		int avg_d = water_count > 0 ? (total_d / water_count) : 0;
		printf("Location %2zu: [%4d, %4d] %-30s | Land: %2d Water: %2d | Avg H: %3d | Avg Depth: %2d\n",
		       i + 1, loc->x, loc->y, loc->name, land_count, water_count, avg_h, avg_d);
	}

	printf("=== All 12 Location Pictures Captured to art/visual-inspection/ ===\n");
	return 0;
}
