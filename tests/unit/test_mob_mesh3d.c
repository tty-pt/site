#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <nd/mob_dna.h>
#include <nd/mob_mesh3d.h>

static void
test_lamb_aging_and_mesh(void)
{
	creature_dna_t lamb = creature_dna_lamb_default(42);
	creature_state_t st_young = { .age = 0.05f, .weight = 0.5f, .coat_state = 0, .seed = 1 };
	creature_state_t st_adult = { .age = 0.60f, .weight = 0.7f, .coat_state = 0, .seed = 1 };

	mob_skel3d_t sk_young, sk_adult;
	mob_build_ungulate_skel(&lamb, &st_young, &sk_young);
	mob_build_ungulate_skel(&lamb, &st_adult, &sk_adult);

	assert(sk_young.count == MOB_UNGULATE_BONE_COUNT);
	assert(sk_adult.count == MOB_UNGULATE_BONE_COUNT);

	/* Adult legs should be taller than newborn lamb legs */
	assert(sk_adult.bones[MOB_BONE_LEG_FL_UP].hz > sk_young.bones[MOB_BONE_LEG_FL_UP].hz);
	/* Horns should be ~0 at age 0.05 and > 0 at age 0.60 */
	assert(sk_young.bones[MOB_BONE_HORN_L].r0 < 0.001f);
	assert(sk_adult.bones[MOB_BONE_HORN_L].r0 > 0.005f);

	static mob_vtx3d_t vtx[1024];
	static uint16_t idx[4096];
	mob_mesh3d_t mesh = { .vtx = vtx, .idx = idx, .max_vtx = 1024, .max_idx = 4096 };

	mob_build_ungulate_mesh(&lamb, &st_adult, &mesh);
	assert(mesh.vtx_count > 100);
	assert(mesh.idx_count > 300);
	assert(mesh.idx_count % 3 == 0);

	for (uint32_t i = 0; i < mesh.vtx_count; i++) {
		assert(isfinite(mesh.vtx[i].x) && isfinite(mesh.vtx[i].y) && isfinite(mesh.vtx[i].z));
		assert(mesh.vtx[i].bone_idx < MOB_UNGULATE_BONE_COUNT);
		assert(mesh.vtx[i].bone_weight > 0.0f && mesh.vtx[i].bone_weight <= 1.0f);
	}
	printf("test_lamb_aging_and_mesh: PASS (vtx=%u idx=%u)\n", mesh.vtx_count, mesh.idx_count);
}

int
main(void)
{
	test_lamb_aging_and_mesh();
	return 0;
}
