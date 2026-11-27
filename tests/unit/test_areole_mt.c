/*
 * test_areole_mt.c — Multi-threaded determinism & race-freedom test for
 * libstele stl_wq_t + libareole async staging + nd-world nd_field_sample.
 *
 * Verifies that 4-thread parallel chunk population via stl_wq produces
 * bit-for-bit identical elevation, biome, water, and RGBA outputs to
 * single-threaded synchronous population across multiple worlds and LODs.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ttypt/wq.h>
#include <ttypt/areole.h>
#include <nd/field.h>

static void test_pack_sample(const nd_field_sample_t *fs, ar_sample_t *out)
{
	out->val = fs->elevation_q12_4;
	out->rgba = ((uint32_t)fs->bio) |
	            ((uint32_t)fs->bio2 << 8) |
	            ((uint32_t)fs->blend << 16) |
	            ((uint32_t)(fs->is_water ? 255u : 128u) << 24);
	out->flags = fs->is_water ? 1u : 0u;
	out->attr[0] = fs->bio;
	out->attr[1] = fs->bio2;
	out->attr[2] = fs->blend;
	out->attr[3] = fs->river_carve_q4;
	out->attr[4] = (uint8_t)(fs->water_level & 0xFF);
	out->attr[5] = (uint8_t)((fs->water_level >> 8) & 0xFF);
	out->attr[6] = (uint8_t)(fs->fine_elev & 0xFF);
	out->attr[7] = 0;
}

static void mt_sampler(void *ctx, uint16_t world, uint8_t level,
                       int32_t wx, int32_t wy, ar_sample_t *out)
{
	(void)ctx; (void)level;
	nd_field_sample_t fs = nd_field_sample_step(world, 0, wx, wy);
	test_pack_sample(&fs, out);
}

static void mt_row_sampler(void *ctx, uint16_t world, uint8_t level,
                           int32_t wy, int32_t ox, int32_t span_w,
                           uint32_t count, ar_sample_t *out_row)
{
	(void)ctx; (void)level;
	nd_field_sample_t row_fs[64];
	assert(count <= 64u);
	nd_field_sample_row_step(world, 0, wy, ox, span_w, count, row_fs);
	for (uint32_t c = 0; c < count; c++)
		test_pack_sample(&row_fs[c], &out_row[c]);
}

typedef struct {
	uint16_t world;
	int32_t ox, oy;
	int32_t span_w, span_h;
	nd_field_sample_t out[17 * 17];
	stl_wq_task_t task;
} raw_job_t;

static void raw_field_worker(void *arg)
{
	raw_job_t *j = (raw_job_t *)arg;
	for (int r = 0; r <= 16; r++) {
		int32_t wy = j->oy + (int32_t)(((int64_t)r * j->span_h) / 16);
		nd_field_sample_row_step(j->world, 0, wy, j->ox, j->span_w, 17u, &j->out[r * 17]);
	}
}

static void test_direct_field_mt_determinism(stl_wq_t *wq)
{
	enum { NJOBS = 96 };
	raw_job_t *jobs_mt   = (raw_job_t *)calloc(NJOBS, sizeof(raw_job_t));
	raw_job_t *jobs_sync = (raw_job_t *)calloc(NJOBS, sizeof(raw_job_t));
	assert(jobs_mt && jobs_sync);

	for (int i = 0; i < NJOBS; i++) {
		uint16_t world = (uint16_t)((i % 3) == 0 ? 1 : (i % 3 == 1 ? 0 : 2));
		int32_t ox = (i - 48) * 160;
		int32_t oy = ((i * 7) % 31 - 15) * 128;
		int32_t shift = (i % 4);
		jobs_mt[i].world = jobs_sync[i].world = world;
		jobs_mt[i].ox = jobs_sync[i].ox = ox;
		jobs_mt[i].oy = jobs_sync[i].oy = oy;
		jobs_mt[i].span_w = jobs_sync[i].span_w = 160 << shift;
		jobs_mt[i].span_h = jobs_sync[i].span_h = 128 << shift;
		stl_wq_task_init(&jobs_mt[i].task);
	}

	/* Single-threaded golden reference */
	for (int i = 0; i < NJOBS; i++)
		raw_field_worker(&jobs_sync[i]);

	/* Multi-threaded parallel execution on 4 pthreads */
	for (int i = 0; i < NJOBS; i++) {
		int rc = stl_wq_submit(wq, &jobs_mt[i].task, raw_field_worker, &jobs_mt[i]);
		assert(rc == 0);
	}
	stl_wq_drain(wq);

	for (int i = 0; i < NJOBS; i++) {
		assert(stl_wq_done(&jobs_mt[i].task));
		if (memcmp(jobs_mt[i].out, jobs_sync[i].out, sizeof(jobs_mt[i].out)) != 0) {
			fprintf(stderr, "FAIL: raw_field_worker MT mismatch on job %d (world=%u ox=%d oy=%d)\n",
			        i, jobs_mt[i].world, jobs_mt[i].ox, jobs_mt[i].oy);
			abort();
		}
	}

	free(jobs_mt);
	free(jobs_sync);
	printf("PASS: 4-thread nd_field_sample_row_step determinism verified (%d jobs x 289 samples = %d exact matches)\n",
	       NJOBS, NJOBS * 289);
}

static void test_areole_async_staging_determinism(stl_wq_t *wq)
{
	ar_config_t cfg_sync = {0};
	cfg_sync.base_w = 160;
	cfg_sync.base_h = 128;
	cfg_sync.grid_dim = 16;
	cfg_sync.sampler = mt_sampler;
	cfg_sync.row_sampler = mt_row_sampler;
	cfg_sync.max_active = 128;
	cfg_sync.pool_capacity = 512;
	cfg_sync.max_levels = 10;
	cfg_sync.default_rgba = 0xFF3C804Eu;
	cfg_sync.max_populates_per_frame = 0;
	cfg_sync.wq = NULL;

	ar_config_t cfg_mt = cfg_sync;
	cfg_mt.max_populates_per_frame = 1; /* Force child chunks onto async worker pool */
	cfg_mt.wq = wq;

	ar_system_t *sys_sync = ar_system_create_ex(&cfg_sync);
	ar_system_t *sys_mt   = ar_system_create_ex(&cfg_mt);
	assert(sys_sync && sys_mt);

	const ar_node_t *out_sync[128];
	const ar_node_t *out_mt[128];

	/* Step 1: Seed coarse parents at wide distance */
	ar_view_t view = {0};
	view.cam_x = 0.0f;
	view.cam_y = 0.0f;
	view.cam_z = 2000.0f;
	view.pitch = 1.1f;
	view.fov = 0.9f;
	view.zoom = 0.15f;
	view.viewport_w = 1280.0f;
	view.viewport_h = 720.0f;
	view.eff_cam_dist = 2000.0f;
	view.min_level = 0;
	view.max_level = 8;
	view.target_quad_px = 160.0f;

	ar_system_begin_frame(sys_sync);
	ar_system_begin_frame(sys_mt);
	size_t n0_s = ar_system_update(sys_sync, 1, &view, out_sync, 128);
	size_t n0_m = ar_system_update(sys_mt,   1, &view, out_mt,   128);
	assert(n0_s > 0 && n0_s == n0_m);
	stl_wq_drain(wq);

	/* Step 2: Zoom in closely to force many fine child chunks to populate asynchronously */
	uint32_t total_upgrades = 0;
	ar_metrics_t m;
	for (int frame = 0; frame < 6; frame++) {
		ar_system_begin_frame(sys_sync);
		ar_system_begin_frame(sys_mt);
		ar_system_get_metrics(sys_mt, &m);
		total_upgrades += m.frame_deferred_upgrades;

		view.cam_x = (float)(frame * 160);
		view.cam_y = (float)(frame * 128);
		view.cam_z = 400.0f;
		view.zoom = 1.0f;
		view.eff_cam_dist = 400.0f;

		size_t ns = ar_system_update(sys_sync, 1, &view, out_sync, 128);
		size_t nm = ar_system_update(sys_mt,   1, &view, out_mt,   128);
		assert(ns > 0 && ns == nm);

		/* Drain workers, commit async staging on next begin_frame, then re-query same pose */
		stl_wq_drain(wq);
		ar_system_begin_frame(sys_mt);
		ar_system_get_metrics(sys_mt, &m);
		total_upgrades += m.frame_deferred_upgrades;
		nm = ar_system_update(sys_mt, 1, &view, out_mt, 128);
		assert(ns == nm);

		for (size_t i = 0; i < ns; i++) {
			const ar_node_t *a = out_sync[i];
			const ar_node_t *b = out_mt[i];
			assert(a->world == b->world && a->level == b->level);
			assert(a->ox == b->ox && a->oy == b->oy);
			assert((b->flags & AR_NODE_FLAG_DEFERRED) == 0);
			assert(a->min_val == b->min_val && a->max_val == b->max_val);
			assert(a->flags == b->flags);
			assert(memcmp(a->values, b->values, a->vert_count * sizeof(int32_t)) == 0);
			assert(memcmp(a->rgba, b->rgba, a->vert_count * sizeof(uint32_t)) == 0);
		}
	}

	assert(total_upgrades > 0);
	printf("PASS: libareole 4-thread async staging bit-for-bit parity verified (async_upgrades=%u)\n",
	       total_upgrades);

	ar_system_destroy(sys_mt);
	ar_system_destroy(sys_sync);
}

int main(void)
{
	stl_wq_t *wq = stl_wq_create("test_mt", 4, 256);
	assert(wq != NULL);
	assert(stl_wq_thread_count(wq) == 4u);

	test_direct_field_mt_determinism(wq);
	test_areole_async_staging_determinism(wq);

	stl_wq_destroy(wq);
	printf("ALL MULTI-THREADED DETERMINISM TESTS PASSED\n");
	return 0;
}
