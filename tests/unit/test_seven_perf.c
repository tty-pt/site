#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <nd/bcp-types.h>
#include <nd/seven-world.h>
#include "nd_seven.h"
#include "nd_seven_motion.h"

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

typedef struct {
	double total_ms;
	double sync_ms;
	double macro_ms;
	double near_ms;
	double bot_ms;
	double sort_ms;
	double scen_ms;
	double gwc_ms;
	double end_ms;
	double terrain_chunks;
	double flora_probes;
	double bb_flushes;
	double mesh3d_count;
	double bb_count;
	double ar_populates;
} perf_acc_t;

static void acc_add(perf_acc_t *a, const nd_seven_perf_t *p)
{
	a->total_ms       += p->total_ms;
	a->sync_ms        += p->sync_ms;
	a->macro_ms       += p->macro_backdrop_ms;
	a->near_ms        += p->near_room_ms;
	a->bot_ms         += p->botanical_ms;
	a->sort_ms        += p->sort_batch_ms;
	a->scen_ms        += p->scenery_ms;
	a->gwc_ms         += p->grass_water_clouds_ms;
	a->end_ms         += p->end_frame_ms;
	a->terrain_chunks += p->terrain_chunks;
	a->flora_probes   += p->flora_probes;
	a->bb_flushes     += p->bb_flushes;
	a->mesh3d_count   += p->mesh3d_count;
	a->bb_count       += p->bb_count;
	a->ar_populates   += p->ar_populates;
}

static void print_row(const char *label, const perf_acc_t *a, int n)
{
	double inv = 1.0 / (double)n;
	printf("%-18s | %6.2f | %5.2f | %6.2f | %5.2f | %6.2f | %5.2f | %6.2f | %5.2f | %5.2f | %5.0f | %6.0f | %4.1f | %5.0f | %5.0f | %4.1f\n",
	       label,
	       a->total_ms * inv,
	       a->sync_ms * inv,
	       a->macro_ms * inv,
	       a->near_ms * inv,
	       a->bot_ms * inv,
	       a->sort_ms * inv,
	       a->scen_ms * inv,
	       a->gwc_ms * inv,
	       a->end_ms * inv,
	       a->terrain_chunks * inv,
	       a->flora_probes * inv,
	       a->bb_flushes * inv,
	       a->mesh3d_count * inv,
	       a->bb_count * inv,
	       a->ar_populates * inv);
}

int main(void)
{
	setup_map(10, 10, 10);
	nd_seven_apply_quality_preset(ND_QUALITY_HIGH);
	nd_seven_reset_cam();
	for (int s = 0; s < 35; s++) nd_seven_step_cam(0.05f);
	nd_seven_set_zoom(1.0f);

	/* 1. Disabled by default: rendering must NOT record profiler stats */
	assert(nd_seven_perf_get_enabled() == 0);
	nd_seven_perf_reset();
	nd_seven_render(1280, 720, &s_st);
	assert(nd_seven_perf_last()->frame_count == 0);
	assert(nd_seven_perf_last()->total_ms == 0.0f);

	/* Warm up pipelines & steady-state chunks with profiler off */
	for (int i = 0; i < 3; i++)
		nd_seven_render(1280, 720, &s_st);
	assert(nd_seven_perf_last()->frame_count == 0);

	/* 2. Enable profiler and verify reset preserves enabled flag */
	nd_seven_perf_set_enabled(1);
	assert(nd_seven_perf_get_enabled() == 1);
	nd_seven_perf_reset();
	assert(nd_seven_perf_get_enabled() == 1);
	assert(nd_seven_perf_last()->frame_count == 0);

	printf("Scenario           | tot_ms |  sync |  macro |  near |    bot |  sort |   scen |   gwc |   end | t_chk | probes | flsh | m3d_n |  bb_n | pop\n");
	printf("-------------------+--------+-------+--------+-------+--------+-------+--------+-------+-------+-------+--------+------+-------+-------+-----\n");

	/* Scenario A: High preset, stationary camera (steady-state, 10 frames) */
	perf_acc_t acc_stat = {0};
	for (int i = 0; i < 10; i++) {
		nd_seven_render(1280, 720, &s_st);
		const nd_seven_perf_t *p = nd_seven_perf_last();
		assert(p->frame_count == (uint32_t)(i + 1));
		assert(p->total_ms > 0.0f);
		assert(p->terrain_chunks > 0u);
		assert(p->bb_flushes > 0u);
		assert(p->flora_probes > 0u);
		acc_add(&acc_stat, p);
	}
	print_row("High stationary", &acc_stat, 10);

	/* Scenario B: High preset, panning orbit (12 frames, 30 deg yaw step) */
	perf_acc_t acc_pan = {0};
	for (int i = 0; i < 12; i++) {
		nd_seven_set_cam_angles((float)(i * 30), 45.0f);
		for (int s = 0; s < 10; s++) nd_seven_step_cam(0.05f);
		nd_seven_render(1280, 720, &s_st);
		acc_add(&acc_pan, nd_seven_perf_last());
	}
	print_row("High panning", &acc_pan, 12);

	/* Scenario C: High preset, zoom sweep (1.0 -> 0.05 across 8 frames) */
	static const float zooms[8] = { 1.0f, 0.7f, 0.45f, 0.28f, 0.16f, 0.10f, 0.07f, 0.05f };
	perf_acc_t acc_zoom = {0};
	nd_seven_reset_cam();
	for (int s = 0; s < 35; s++) nd_seven_step_cam(0.05f);
	for (int i = 0; i < 8; i++) {
		nd_seven_set_zoom(zooms[i]);
		nd_seven_render(1280, 720, &s_st);
		acc_add(&acc_zoom, nd_seven_perf_last());
	}
	print_row("High zooming", &acc_zoom, 8);

	/* Scenario D: Low preset, stationary camera (steady-state, 10 frames) */
	nd_seven_apply_quality_preset(ND_QUALITY_LOW);
	nd_seven_set_zoom(1.0f);
	nd_seven_reset_cam();
	for (int s = 0; s < 35; s++) nd_seven_step_cam(0.05f);
	nd_seven_render(1280, 720, &s_st); /* warmup */
	perf_acc_t acc_low = {0};
	for (int i = 0; i < 10; i++) {
		nd_seven_render(1280, 720, &s_st);
		acc_add(&acc_low, nd_seven_perf_last());
	}
	print_row("Low stationary", &acc_low, 10);

	/* 3. Disable profiler again: frame_count must freeze */
	uint32_t frozen = nd_seven_perf_last()->frame_count;
	nd_seven_perf_set_enabled(0);
	nd_seven_render(1280, 720, &s_st);
	assert(nd_seven_perf_last()->frame_count == frozen);

	printf("PASS: Seven P0 frame-phase profiler verified\n");
	return 0;
}
