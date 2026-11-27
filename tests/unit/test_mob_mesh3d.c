#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <nd/mob_dna.h>
#include <nd/mob_mesh3d.h>
#include <nd/mob_anim.h>

static void
assert_watertight_and_outward(const mob_mesh3d_t *mesh)
{
	assert(mesh->vtx_count > 100);
	assert(mesh->idx_count > 300);
	assert(mesh->idx_count % 3 == 0);

	/* Check every vertex has finite coords, unit normal, valid AO, mat_id & bone weights */
	for (uint32_t i = 0; i < mesh->vtx_count; i++) {
		const mob_vtx3d_t *v = &mesh->vtx[i];
		assert(isfinite(v->x) && isfinite(v->y) && isfinite(v->z));
		assert(isfinite(v->nx) && isfinite(v->ny) && isfinite(v->nz));
		float nlen = sqrtf(v->nx * v->nx + v->ny * v->ny + v->nz * v->nz);
		assert(fabsf(nlen - 1.0f) < 0.02f);
		assert(v->ao >= 0.4f && v->ao <= 1.001f);
		assert(v->bone_idx < mesh->skel.count);
		assert(v->bone_idx2 < mesh->skel.count);
		assert(v->mat_id < MOB_MAT_COUNT);
		assert(v->pal_slot < MOB_PAL_COUNT);
		assert(v->bone_weight > 0.0f && v->bone_weight <= 1.0f);
	}

	/* Verify signed volume > 0 (outward CCW winding) */
	double signed_vol6 = 0.0;
	for (uint32_t t = 0; t < mesh->idx_count; t += 3) {
		uint16_t ia = mesh->idx[t], ib = mesh->idx[t + 1], ic = mesh->idx[t + 2];
		assert(ia < mesh->vtx_count && ib < mesh->vtx_count && ic < mesh->vtx_count);
		const mob_vtx3d_t *a = &mesh->vtx[ia];
		const mob_vtx3d_t *b = &mesh->vtx[ib];
		const mob_vtx3d_t *c = &mesh->vtx[ic];
		double cx = (double)b->y * c->z - (double)b->z * c->y;
		double cy = (double)b->z * c->x - (double)b->x * c->z;
		double cz = (double)b->x * c->y - (double)b->y * c->x;
		signed_vol6 += (double)a->x * cx + (double)a->y * cy + (double)a->z * cz;
	}
	assert(signed_vol6 > 0.0);

	/* Watertight manifold check: every directed edge (u -> v) must have twin (v -> u) */
	uint32_t n_edges = mesh->idx_count;
	uint32_t *dir_edges = (uint32_t *)malloc(n_edges * sizeof(uint32_t));
	assert(dir_edges != NULL);
	for (uint32_t t = 0; t < mesh->idx_count; t += 3) {
		uint32_t a = mesh->idx[t], b = mesh->idx[t + 1], c = mesh->idx[t + 2];
		dir_edges[t + 0] = (a << 16) | b;
		dir_edges[t + 1] = (b << 16) | c;
		dir_edges[t + 2] = (c << 16) | a;
	}
	uint32_t unmatched = 0;
	for (uint32_t i = 0; i < n_edges; i++) {
		uint32_t u = dir_edges[i] >> 16;
		uint32_t v = dir_edges[i] & 0xFFFFu;
		uint32_t twin = (v << 16) | u;
		int found = 0;
		for (uint32_t j = 0; j < n_edges; j++) {
			if (dir_edges[j] == twin) { found = 1; break; }
		}
		if (!found) unmatched++;
	}
	free(dir_edges);
	assert(unmatched == 0);
}

static void
assert_pose_orthonormal(const mob_pose3d_t *pose, uint8_t expected_bones)
{
	assert(pose->count == expected_bones);
	for (uint8_t b = 0; b < pose->count; b++) {
		const float *m = pose->m[b];
		for (int k = 0; k < 12; k++)
			assert(isfinite(m[k]));
		float r0x = m[0], r0y = m[1], r0z = m[2];
		float r1x = m[4], r1y = m[5], r1z = m[6];
		float r2x = m[8], r2y = m[9], r2z = m[10];
		assert(fabsf(r0x * r0x + r0y * r0y + r0z * r0z - 1.0f) < 1e-4f);
		assert(fabsf(r1x * r1x + r1y * r1y + r1z * r1z - 1.0f) < 1e-4f);
		assert(fabsf(r2x * r2x + r2y * r2y + r2z * r2z - 1.0f) < 1e-4f);
		assert(fabsf(r0x * r1x + r0y * r1y + r0z * r1z) < 1e-4f);
		assert(fabsf(r0x * r2x + r0y * r2y + r0z * r2z) < 1e-4f);
		assert(fabsf(r1x * r2x + r1y * r2y + r1z * r2z) < 1e-4f);
		float det = r0x * (r1y * r2z - r1z * r2y)
		          - r0y * (r1x * r2z - r1z * r2x)
		          + r0z * (r1x * r2y - r1y * r2x);
		assert(fabsf(det - 1.0f) < 1e-4f);
	}
}

static void
test_lamb_aging_and_mesh(void)
{
	creature_dna_t lamb = creature_dna_lamb_default(42);
	creature_state_t st_young = { .age = 0.05f, .weight = 0.5f, .coat_state = 0, .seed = 1 };
	creature_state_t st_lamb  = { .age = 0.15f, .weight = 0.6f, .coat_state = 0, .seed = 42 };
	creature_state_t st_adult = { .age = 0.60f, .weight = 0.7f, .coat_state = 0, .seed = 1 };

	mob_skel3d_t sk_young, sk_adult;
	mob_build_ungulate_skel(&lamb, &st_young, &sk_young);
	mob_build_ungulate_skel(&lamb, &st_adult, &sk_adult);

	assert(sk_young.count == MOB_UNGULATE_BONE_COUNT);
	assert(sk_adult.count == MOB_UNGULATE_BONE_COUNT);
	assert(sk_adult.bones[MOB_BONE_LEG_FL_UP].hz > sk_young.bones[MOB_BONE_LEG_FL_UP].hz);
	assert(sk_young.bones[MOB_BONE_HORN_L].r0 < 0.001f);
	assert(sk_adult.bones[MOB_BONE_HORN_L].r0 > 0.005f);

	static mob_vtx3d_t vtx[2048];
	static uint16_t idx[8192];
	mob_mesh3d_t mesh = { .vtx = vtx, .idx = idx, .max_vtx = 2048, .max_idx = 8192 };

	/* Test young lamb (canonical ~0.40-0.65 m tall, hooves at z=0, centered XY, all 3 materials present) */
	mob_build_mesh(&lamb, &st_lamb, &mesh);
	assert_watertight_and_outward(&mesh);
	assert(fabsf(mesh.min_z) < 1e-5f);
	assert(mesh.max_z >= 0.40f && mesh.max_z <= 0.65f);
	assert(fabsf(mesh.min_x + mesh.max_x) < 1e-4f);
	assert(fabsf(mesh.min_y + mesh.max_y) < 1e-4f);

	uint32_t mat_counts[MOB_MAT_COUNT] = { 0 };
	uint32_t blended_count = 0;
	for (uint32_t i = 0; i < mesh.vtx_count; i++) {
		mat_counts[mesh.vtx[i].mat_id]++;
		if (mesh.vtx[i].bone_idx != mesh.vtx[i].bone_idx2)
			blended_count++;
	}
	assert(mat_counts[MOB_MAT_WOOL] > 100);
	assert(mat_counts[MOB_MAT_SKIN] > 100);
	assert(mat_counts[MOB_MAT_GLOSS] > 80); /* eyes + catchlights + nose + hooves */
	assert(blended_count > 50);             /* smooth 2-bone joints */

	printf("test_lamb_young_mesh: PASS (vtx=%u idx=%u wool=%u skin=%u gloss=%u blend=%u h=%.3fm)\n",
	       mesh.vtx_count, mesh.idx_count,
	       mat_counts[0], mat_counts[1], mat_counts[2], blended_count, mesh.max_z);

	/* Smooth-coated ungulates (horse/cow/pig) must NOT shade torso as wool */
	creature_dna_t horse = creature_dna_for_species(CREATURE_SPECIES_HORSE, 7);
	mob_build_mesh(&horse, &st_adult, &mesh);
	assert_watertight_and_outward(&mesh);
	uint32_t horse_wool = 0, horse_skin = 0;
	for (uint32_t i = 0; i < mesh.vtx_count; i++) {
		if (mesh.vtx[i].mat_id == MOB_MAT_WOOL) horse_wool++;
		if (mesh.vtx[i].mat_id == MOB_MAT_SKIN) horse_skin++;
	}
	assert(horse_wool == 0);
	assert(horse_skin > 400);
	printf("test_ungulate_generalized: PASS (horse vtx=%u wool=%u skin=%u h=%.3fm)\n",
	       mesh.vtx_count, horse_wool, horse_skin, mesh.max_z);

	/* Rebuild lamb for animation tests */
	mob_build_mesh(&lamb, &st_lamb, &mesh);

	/* Test Phase 4 hierarchical FK & LBS animation across idle, graze, trot, canter, hop, swim, slope */
	static mob_vtx3d_t posed_vtx[2048];
	mob_pose3d_t pose;

	/* Zero-param pose preserves exact identity */
	mob_pose_identity(&pose, mesh.skel.count);
	assert_pose_orthonormal(&pose, MOB_UNGULATE_BONE_COUNT);
	mob_skin_mesh(mesh.vtx, mesh.vtx_count, &pose, posed_vtx);
	for (uint32_t i = 0; i < mesh.vtx_count; i++) {
		assert(fabsf(posed_vtx[i].x - mesh.vtx[i].x) < 1e-5f);
		assert(fabsf(posed_vtx[i].y - mesh.vtx[i].y) < 1e-5f);
		assert(fabsf(posed_vtx[i].z - mesh.vtx[i].z) < 1e-5f);
	}

	/* Walk/trot: front-left leg swings visibly forward/backward vs bind pose */
	mob_anim_params_t p_trot = {
		.gait_phase = 1.2f, .speed_norm = 0.60f, .turn_rate = 1.5f,
		.anim_sec = 2.0f, .slope_pitch = 0.18f, .slope_roll = -0.10f
	};
	mob_eval_pose(lamb.archetype, &mesh.skel, &p_trot, &pose);
	assert_pose_orthonormal(&pose, MOB_UNGULATE_BONE_COUNT);
	mob_skin_mesh(mesh.vtx, mesh.vtx_count, &pose, posed_vtx);
	float max_leg_disp = 0.0f;
	for (uint32_t i = 0; i < mesh.vtx_count; i++) {
		float nlen = sqrtf(posed_vtx[i].nx * posed_vtx[i].nx +
		                   posed_vtx[i].ny * posed_vtx[i].ny +
		                   posed_vtx[i].nz * posed_vtx[i].nz);
		assert(fabsf(nlen - 1.0f) < 0.02f);
		if (mesh.vtx[i].bone_idx == MOB_BONE_LEG_FL_LO) {
			float dy = fabsf(posed_vtx[i].y - mesh.vtx[i].y);
			if (dy > max_leg_disp) max_leg_disp = dy;
		}
	}
	assert(max_leg_disp > 0.03f);

	/* Hop lifts entire mesh cleanly above ground */
	mob_anim_params_t p_hop = {
		.speed_norm = 0.7f, .hop_z = 0.28f, .hop_vel = 1.4f, .anim_sec = 3.0f
	};
	mob_eval_pose(lamb.archetype, &mesh.skel, &p_hop, &pose);
	assert_pose_orthonormal(&pose, MOB_UNGULATE_BONE_COUNT);
	mob_skin_mesh(mesh.vtx, mesh.vtx_count, &pose, posed_vtx);
	float min_hop_z = 1e9f;
	for (uint32_t i = 0; i < mesh.vtx_count; i++) {
		if (posed_vtx[i].z < min_hop_z) min_hop_z = posed_vtx[i].z;
	}
	assert(min_hop_z > 0.15f);

	/* Idle graze dips snout lower than alert idle */
	mob_anim_params_t p_alert = { .idle_sec = 0.5f, .anim_sec = 1.0f };
	mob_anim_params_t p_graze = { .idle_sec = 6.0f, .anim_sec = 1.0f };
	mob_eval_pose(lamb.archetype, &mesh.skel, &p_alert, &pose);
	mob_skin_mesh(mesh.vtx, mesh.vtx_count, &pose, posed_vtx);
	float snout_z_alert = 0.0f;
	uint32_t snout_n = 0;
	for (uint32_t i = 0; i < mesh.vtx_count; i++) {
		if (mesh.vtx[i].bone_idx == MOB_BONE_SNOUT) { snout_z_alert += posed_vtx[i].z; snout_n++; }
	}
	snout_z_alert /= (float)snout_n;

	mob_eval_pose(lamb.archetype, &mesh.skel, &p_graze, &pose);
	assert_pose_orthonormal(&pose, MOB_UNGULATE_BONE_COUNT);
	mob_skin_mesh(mesh.vtx, mesh.vtx_count, &pose, posed_vtx);
	float snout_z_graze = 0.0f;
	for (uint32_t i = 0; i < mesh.vtx_count; i++) {
		if (mesh.vtx[i].bone_idx == MOB_BONE_SNOUT) snout_z_graze += posed_vtx[i].z;
	}
	snout_z_graze /= (float)snout_n;
	assert(snout_z_graze < snout_z_alert - 0.08f);

	/* Canter & swim poses remain orthonormal with unit normals */
	mob_anim_params_t p_canter = { .gait_phase = 2.4f, .speed_norm = 1.0f, .anim_sec = 4.0f };
	mob_eval_pose(lamb.archetype, &mesh.skel, &p_canter, &pose);
	assert_pose_orthonormal(&pose, MOB_UNGULATE_BONE_COUNT);

	mob_anim_params_t p_swim = { .speed_norm = 0.4f, .in_water = 1, .anim_sec = 5.0f, .interact_t = 0.5f };
	mob_eval_pose(lamb.archetype, &mesh.skel, &p_swim, &pose);
	assert_pose_orthonormal(&pose, MOB_UNGULATE_BONE_COUNT);

	printf("test_lamb_anim: PASS (leg_disp=%.3fm hop_min_z=%.3fm graze_drop=%.3fm)\n",
	       max_leg_disp, min_hop_z, snout_z_alert - snout_z_graze);

	/* Test adult sheep (taller, horns grown, still watertight) */
	mob_build_mesh(&lamb, &st_adult, &mesh);
	assert_watertight_and_outward(&mesh);
	assert(fabsf(mesh.min_z) < 1e-5f);
	assert(mesh.max_z >= 0.65f && mesh.max_z <= 0.98f);
	printf("test_lamb_adult_mesh: PASS (vtx=%u idx=%u h=%.3fm len=%.3fm w=%.3fm)\n",
	       mesh.vtx_count, mesh.idx_count, mesh.max_z,
	       mesh.max_y - mesh.min_y, mesh.max_x - mesh.min_x);
}

static void
test_mobs_dsv_piscine_and_avian(void)
{
	static const char *fish_names[] = {
		"goldfish", "salmon", "tuna", "koifish",
		"dolphin", "shark", "moonfish", "rainbowfish"
	};
	static const char *bird_names[] = {
		"icebird", "parrot", "swallow", "woodpecker", "sparrow", "owl"
	};

	static mob_vtx3d_t vtx[2048], posed[2048];
	static uint16_t idx[8192];
	mob_mesh3d_t mesh = { .vtx = vtx, .idx = idx, .max_vtx = 2048, .max_idx = 8192 };
	mob_pose3d_t pose;

	/* Verify all 8 fish species from mobs.dsv */
	for (uint32_t f = 0; f < sizeof(fish_names) / sizeof(fish_names[0]); f++) {
		creature_dna_t dna;
		creature_state_t st;
		int matched = creature_dna_from_name(fish_names[f], 100u + f, &dna, &st);
		assert(matched == 1);
		assert(dna.archetype == CREATURE_ARCH_PISCINE);

		mob_build_mesh(&dna, &st, &mesh);
		assert_watertight_and_outward(&mesh);
		assert(mesh.skel.count == MOB_PISCINE_BONE_COUNT);
		assert(fabsf(mesh.min_z) < 1e-5f);
		assert(fabsf(mesh.min_x + mesh.max_x) < 1e-4f);
		assert(fabsf(mesh.min_y + mesh.max_y) < 1e-4f);

		mob_anim_params_t swim = { .speed_norm = 0.75f, .turn_rate = 0.8f, .anim_sec = 0.42f, .in_water = 1 };
		mob_eval_pose(dna.archetype, &mesh.skel, &swim, &pose);
		assert_pose_orthonormal(&pose, MOB_PISCINE_BONE_COUNT);
		mob_skin_mesh(mesh.vtx, mesh.vtx_count, &pose, posed);

		/* Tail caudal fin must show clear lateral S-wave displacement */
		float max_tail_dx = 0.0f;
		for (uint32_t i = 0; i < mesh.vtx_count; i++) {
			if (mesh.vtx[i].bone_idx == MOB_FISH_CAUDAL_UP) {
				float dx = fabsf(posed[i].x - mesh.vtx[i].x);
				if (dx > max_tail_dx) max_tail_dx = dx;
			}
		}
		assert(max_tail_dx > 0.01f);
	}
	printf("test_piscine_all_8_species: PASS (watertight + S-wave swim verified)\n");

	/* Verify all 6 bird species from mobs.dsv */
	for (uint32_t b = 0; b < sizeof(bird_names) / sizeof(bird_names[0]); b++) {
		creature_dna_t dna;
		creature_state_t st;
		int matched = creature_dna_from_name(bird_names[b], 200u + b, &dna, &st);
		assert(matched == 1);
		assert(dna.archetype == CREATURE_ARCH_AVIAN);

		mob_build_mesh(&dna, &st, &mesh);
		assert_watertight_and_outward(&mesh);
		assert(mesh.skel.count == MOB_AVIAN_BONE_COUNT);
		assert(fabsf(mesh.min_z) < 1e-5f);
		assert(fabsf(mesh.min_x + mesh.max_x) < 1e-4f);

		/* Airborne wingbeat vs perched wing fold */
		mob_anim_params_t fly = { .speed_norm = 0.85f, .hop_z = 0.6f, .anim_sec = 0.20f };
		mob_eval_pose(dna.archetype, &mesh.skel, &fly, &pose);
		assert_pose_orthonormal(&pose, MOB_AVIAN_BONE_COUNT);
		mob_skin_mesh(mesh.vtx, mesh.vtx_count, &pose, posed);
		float fly_span_x = 0.0f;
		for (uint32_t i = 0; i < mesh.vtx_count; i++) {
			if (fabsf(posed[i].x) > fly_span_x) fly_span_x = fabsf(posed[i].x);
		}

		mob_anim_params_t perch = { .speed_norm = 0.0f, .hop_z = 0.0f, .anim_sec = 1.0f };
		mob_eval_pose(dna.archetype, &mesh.skel, &perch, &pose);
		assert_pose_orthonormal(&pose, MOB_AVIAN_BONE_COUNT);
		mob_skin_mesh(mesh.vtx, mesh.vtx_count, &pose, posed);
		float perch_span_x = 0.0f;
		for (uint32_t i = 0; i < mesh.vtx_count; i++) {
			if (fabsf(posed[i].x) > perch_span_x) perch_span_x = fabsf(posed[i].x);
		}
		assert(perch_span_x < fly_span_x * 0.72f);
	}
	printf("test_avian_all_6_species: PASS (watertight + flight flap & perched fold verified)\n");
}

static void
test_biped_human_and_dna_resolution(void)
{
	static mob_vtx3d_t vtx[4096], posed[4096];
	static uint16_t idx[16384];
	mob_mesh3d_t mesh = { .vtx = vtx, .idx = idx, .max_vtx = 4096, .max_idx = 16384 };
	mob_pose3d_t pose;

	/* 1. Default human: watertight, outward CCW, grounded, ~1.55-1.95 m tall, 21 bones */
	creature_dna_t h0 = creature_dna_human_default(101u);
	creature_state_t st = { .age = 0.55f, .weight = 0.50f, .coat_state = 0, .seed = 101u };
	assert(h0.archetype == CREATURE_ARCH_BIPED);
	assert(h0.species == CREATURE_SPECIES_HUMAN);

	mob_build_mesh(&h0, &st, &mesh);
	assert_watertight_and_outward(&mesh);
	assert(mesh.skel.count == MOB_BIPED_BONE_COUNT);
	assert(fabsf(mesh.min_z) < 1e-5f);
	assert(mesh.max_z >= 1.50f && mesh.max_z <= 1.98f);
	assert(fabsf(mesh.min_x + mesh.max_x) < 1e-4f);
	assert(fabsf(mesh.min_y + mesh.max_y) < 1e-4f);

	uint32_t mats[MOB_MAT_COUNT] = { 0 };
	uint32_t pal_used[MOB_PAL_COUNT] = { 0 };
	for (uint32_t i = 0; i < mesh.vtx_count; i++) {
		mats[mesh.vtx[i].mat_id]++;
		pal_used[mesh.vtx[i].pal_slot]++;
	}
	assert(mats[MOB_MAT_SKIN] > 60);
	assert(mats[MOB_MAT_HAIR] > 20);
	assert(mats[MOB_MAT_CLOTH] > 60);
	assert(mats[MOB_MAT_LEATHER] > 20);
	assert(mats[MOB_MAT_GLOSS] > 16);
	assert(pal_used[MOB_PAL_PRIMARY] > 40);
	assert(pal_used[MOB_PAL_SECONDARY] > 20);
	assert(pal_used[MOB_PAL_ACCENT] > 40);

	/* 2. Armored + cloaked + bearded + helmeted variant: METAL material present & watertight */
	creature_dna_t knight = h0;
	knight.arch.biped.garment_top    = 2; /* armor cuirass */
	knight.arch.biped.garment_bottom = 1; /* robe/skirt */
	knight.arch.biped.headgear       = 2; /* helm */
	knight.arch.biped.beard_style    = 2; /* full beard */
	knight.arch.biped.cloak_len      = 2; /* long cloak */
	mob_build_mesh(&knight, &st, &mesh);
	assert_watertight_and_outward(&mesh);
	uint32_t metal_n = 0;
	for (uint32_t i = 0; i < mesh.vtx_count; i++) {
		if (mesh.vtx[i].mat_id == MOB_MAT_METAL) metal_n++;
	}
	assert(metal_n > 60);

	/* 3. Structural geom_hash: invariant to palette & seed, sensitive to geometry changes */
	creature_dna_t h1 = h0;
	h1.seed = 9999u;
	h1.palette[0] = 0xFF332211u;
	h1.palette[1] = 0xFF11AA44u;
	h1.palette[2] = 0xFF2244EEu;
	h1.palette[3] = 0xFFCC9922u;
	assert(creature_dna_geom_hash(&h0, &st) == creature_dna_geom_hash(&h1, &st));
	assert(creature_dna_geom_hash(&h0, &st) != creature_dna_geom_hash(&knight, &st));

	/* 4. Non-fragile entity resolution: player named "Sparrow" must NOT turn into a bird */
	creature_dna_t rdna;
	creature_state_t rst;
	int matched_sparrow_player = creature_dna_resolve_entity("", "Sparrow", 1, 77u, &rdna, &rst);
	assert(matched_sparrow_player == 0);
	assert(rdna.species != CREATURE_SPECIES_SPARROW);
	assert(rdna.archetype != CREATURE_ARCH_AVIAN);

	/* Explicit art token overrides name for any actor (including players) */
	int matched_human_art = creature_dna_resolve_entity("entity/human/1.jpeg", "Sparrow", 1, 77u, &rdna, &rst);
	assert(matched_human_art == 1);
	assert(rdna.species == CREATURE_SPECIES_HUMAN);
	assert(rdna.archetype == CREATURE_ARCH_BIPED);

	int matched_shark_art = creature_dna_resolve_entity("entity/shark/2.png", "Bob", 1, 88u, &rdna, &rst);
	assert(matched_shark_art == 1);
	assert(rdna.species == CREATURE_SPECIES_SHARK);
	assert(rdna.archetype == CREATURE_ARCH_PISCINE);

	/* Non-player NPC with empty art still resolves cleanly by species name */
	int matched_owl_npc = creature_dna_resolve_entity("", "Owl", 0, 55u, &rdna, &rst);
	assert(matched_owl_npc == 1);
	assert(rdna.species == CREATURE_SPECIES_OWL);
	assert(rdna.archetype == CREATURE_ARCH_AVIAN);

	/* 5. Biped FK poses across all locomotion & action states */
	mob_build_mesh(&h0, &st, &mesh);

	mob_anim_params_t p_walk = {
		.gait_phase = 1.1f, .speed_norm = 0.55f, .turn_rate = 0.8f,
		.anim_sec = 1.5f, .look_yaw = 0.35f, .look_pitch = -0.15f
	};
	mob_eval_pose(h0.archetype, &mesh.skel, &p_walk, &pose);
	assert_pose_orthonormal(&pose, MOB_BIPED_BONE_COUNT);
	mob_skin_mesh(mesh.vtx, mesh.vtx_count, &pose, posed);

	float max_leg_dy = 0.0f, max_arm_dy = 0.0f;
	for (uint32_t i = 0; i < mesh.vtx_count; i++) {
		float dy = fabsf(posed[i].y - mesh.vtx[i].y);
		if (mesh.vtx[i].bone_idx == MOB_BIPED_LEG_L_LO && dy > max_leg_dy) max_leg_dy = dy;
		if (mesh.vtx[i].bone_idx == MOB_BIPED_ARM_R_LO && dy > max_arm_dy) max_arm_dy = dy;
	}
	assert(max_leg_dy > 0.08f);
	assert(max_arm_dy > 0.04f);

	/* Right-hand swing displaces hand socket strongly */
	mob_anim_params_t p_swing = {
		.anim_sec = 2.0f, .action_id = MOB_ACT_SWING, .action_phase = 0.32f
	};
	mob_eval_pose(h0.archetype, &mesh.skel, &p_swing, &pose);
	assert_pose_orthonormal(&pose, MOB_BIPED_BONE_COUNT);
	const mob_bone_t *hr = &mesh.skel.bones[MOB_BIPED_HAND_R];
	const float *hm = pose.m[MOB_BIPED_HAND_R];
	float hx = hm[0] * hr->tx + hm[1] * hr->ty + hm[2]  * hr->tz + hm[3];
	float hy = hm[4] * hr->tx + hm[5] * hr->ty + hm[6]  * hr->tz + hm[7];
	float hz = hm[8] * hr->tx + hm[9] * hr->ty + hm[10] * hr->tz + hm[11];
	float hand_disp = sqrtf((hx - hr->tx) * (hx - hr->tx) +
	                        (hy - hr->ty) * (hy - hr->ty) +
	                        (hz - hr->tz) * (hz - hr->tz));
	assert(hand_disp > 0.35f);

	/* Verify all other biped modes remain strictly SO(3) orthonormal */
	static const mob_anim_params_t modes[] = {
		{ .speed_norm = 1.0f, .gait_phase = 2.1f, .anim_sec = 3.0f }, /* sprint */
		{ .speed_norm = 0.6f, .hop_z = 0.4f, .hop_vel = 2.2f, .anim_sec = 3.1f }, /* jump */
		{ .speed_norm = 0.4f, .in_water = 1, .anim_sec = 4.0f }, /* swim */
		{ .anim_sec = 5.0f, .action_id = MOB_ACT_USE,    .action_phase = 0.5f },
		{ .anim_sec = 5.1f, .action_id = MOB_ACT_SIT,    .action_phase = 0.5f },
		{ .anim_sec = 5.2f, .action_id = MOB_ACT_CROUCH, .action_phase = 0.5f },
		{ .anim_sec = 5.3f, .action_id = MOB_ACT_EMOTE,  .action_phase = 0.5f },
		{ .anim_sec = 5.4f, .action_id = MOB_ACT_DOWNED, .action_phase = 0.5f }
	};
	for (uint32_t k = 0; k < sizeof(modes) / sizeof(modes[0]); k++) {
		mob_eval_pose(h0.archetype, &mesh.skel, &modes[k], &pose);
		assert_pose_orthonormal(&pose, MOB_BIPED_BONE_COUNT);
	}

	printf("test_biped_human: PASS (vtx=%u idx=%u h=%.3fm metal=%u leg_dy=%.3fm arm_dy=%.3fm swing=%.3fm)\n",
	       mesh.vtx_count, mesh.idx_count, mesh.max_z, metal_n, max_leg_dy, max_arm_dy, hand_disp);
}

int
main(void)
{
	test_lamb_aging_and_mesh();
	test_mobs_dsv_piscine_and_avian();
	test_biped_human_and_dna_resolution();
	return 0;
}
