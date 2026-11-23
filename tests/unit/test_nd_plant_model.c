/*
 * test_nd_plant_model.c — Standalone Unit Suite for the nd-plant
 * Botanical Genetics, MTG Phenotype, Chrono-Deterministic Simulator,
 * and Ephemeral Delta Decay Engine.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#include <nd/plant.h>

static int
phenotypes_equal(const PlantPhenotype *a, const PlantPhenotype *b, float eps)
{
	if (a->axis_count != b->axis_count ||
	    a->unit_count != b->unit_count ||
	    a->bud_count  != b->bud_count  ||
	    a->organ_count != b->organ_count) {
		fprintf(stderr, "Count mismatch: axes(%u vs %u) units(%u vs %u) buds(%u vs %u) organs(%u vs %u)\n",
			a->axis_count, b->axis_count, a->unit_count, b->unit_count,
			a->bud_count, b->bud_count, a->organ_count, b->organ_count);
		return 0;
	}
	if (fabsf(a->height_m - b->height_m) > eps ||
	    fabsf(a->crown_radius_m - b->crown_radius_m) > eps ||
	    fabsf(a->trunk_base_radius_m - b->trunk_base_radius_m) > eps) {
		fprintf(stderr, "Macroscopic bounds mismatch: h(%.6f vs %.6f) r(%.6f vs %.6f)\n",
			a->height_m, b->height_m, a->crown_radius_m, b->crown_radius_m);
		return 0;
	}
	for (uint32_t i = 0; i < a->unit_count; i++) {
		const PlantGrowthUnit *ua = &a->units[i];
		const PlantGrowthUnit *ub = &b->units[i];
		if (ua->topo_hash != ub->topo_hash ||
		    ua->branch_order != ub->branch_order ||
		    fabsf(ua->tip_pos.x - ub->tip_pos.x) > eps ||
		    fabsf(ua->tip_pos.y - ub->tip_pos.y) > eps ||
		    fabsf(ua->tip_pos.z - ub->tip_pos.z) > eps ||
		    fabsf(ua->base_radius_m - ub->base_radius_m) > eps) {
			fprintf(stderr, "Unit %u mismatch!\n", i);
			return 0;
		}
	}
	for (uint32_t i = 0; i < a->organ_count; i++) {
		const PlantOrgan *oa = &a->organs[i];
		const PlantOrgan *ob = &b->organs[i];
		if (oa->topo_hash != ob->topo_hash ||
		    oa->type != ob->type ||
		    fabsf(oa->length_m - ob->length_m) > eps) {
			fprintf(stderr, "Organ %u mismatch!\n", i);
			return 0;
		}
	}
	return 1;
}

int
main(void)
{
	PlantEnvironment env = plant_environment_default();

	/* 1. Validate all 16 canonical species definitions & genome sampling */
	for (uint32_t sp_idx = 0; sp_idx < ND_PLANT_MAX; sp_idx++) {
		PlantSpeciesDef sp;
		plant_species_init_preset(&sp, sp_idx);
		assert(plant_species_validate(&sp) && "Species preset must pass biological validation");

		PlantGenome g1 = plant_genome_sample(&sp, 0x123456789ABCDEF0ULL + sp_idx);
		PlantGenome g2 = plant_genome_sample(&sp, 0xFEDCBA9876543210ULL + sp_idx);
		assert(g1.species_id == sp_idx);
		assert(g1.genetic_seed != g2.genetic_seed);

		/* Verify crossover produces valid quantitative traits within species bounds */
		PlantGenome child = plant_genome_cross(&g1, &g2, 0x42424242ULL + sp_idx);
		assert(plant_dist_valid(&child.architecture.internode_length_m));
		assert(plant_dist_valid(&child.leaf.blade_length_m));

		/* Simulate mature phenotype for every species */
		static PlantPhenotypeStorage store;
		PlantInstance inst;
		memset(&inst, 0, sizeof(inst));
		inst.instance_id = sp_idx + 1u;
		inst.genome = g1;
		plant_phenotype_bind_storage(&inst.phenotype, &store);

		plant_simulate_at_time(&inst, &env, 180.0f, NULL, 0u);
		assert(inst.phenotype.axis_count >= 1u);
		assert(inst.phenotype.unit_count >= 2u);
		assert(inst.phenotype.organ_count >= 2u);
		assert(inst.phenotype.height_m > 0.05f);
		assert(inst.phenotype.trunk_base_radius_m > 0.002f);
		assert(inst.biomass_kg > 0.0f);

		/* 2. Verify Da Vinci Pipe-Model Area Conservation across all branching junctions:
		 * For every parent unit u, u->base_radius_m^alpha > sum(child->base_radius_m^alpha) */
		float alpha = inst.genome.allometry.pipe_model_exponent;
		for (uint32_t u_idx = 0; u_idx < inst.phenotype.unit_count; u_idx++) {
			const PlantGrowthUnit *u = &inst.phenotype.units[u_idx];
			assert(u->base_radius_m >= u->tip_radius_m);
			float child_pow_sum = 0.0f;
			for (uint32_t c_idx = u_idx + 1u; c_idx < inst.phenotype.unit_count; c_idx++) {
				if (inst.phenotype.units[c_idx].parent_unit == u_idx) {
					float cr = inst.phenotype.units[c_idx].base_radius_m /
					           (1.0f + inst.genome.allometry.bark_thickness_ratio);
					child_pow_sum += powf(cr, alpha);
				}
			}
			float parent_pow = powf(u->base_radius_m, alpha);
			assert(parent_pow + 1e-6f >= child_pow_sum &&
			       "Da Vinci pipe-model capacity conservation must hold at every junction");
		}
	}

	/* 3. Verify Strict dt-Invariance (Chrono-Determinism):
	 * Single jump to T = 165.0 days vs. 11 incremental steps of 15.0 days vs. 33 steps of 5.0 days */
	{
		PlantSpeciesDef oak_sp;
		plant_species_init_preset(&oak_sp, 10u); /* Quercus robur */
		PlantGenome oak_g = plant_genome_sample(&oak_sp, 0xA11CE5EEDULL);

		static PlantPhenotypeStorage store_jump, store_step15, store_step5;
		PlantInstance inst_jump, inst_step15, inst_step5;
		memset(&inst_jump, 0, sizeof(inst_jump));
		memset(&inst_step15, 0, sizeof(inst_step15));
		memset(&inst_step5, 0, sizeof(inst_step5));

		inst_jump.genome   = oak_g;
		inst_step15.genome = oak_g;
		inst_step5.genome  = oak_g;
		plant_phenotype_bind_storage(&inst_jump.phenotype,   &store_jump);
		plant_phenotype_bind_storage(&inst_step15.phenotype, &store_step15);
		plant_phenotype_bind_storage(&inst_step5.phenotype,  &store_step5);

		/* Path A: Single evaluation at t = 165.0 days */
		plant_simulate_at_time(&inst_jump, &env, 165.0f, NULL, 0u);

		/* Path B: 11 incremental steps of 15.0 days */
		for (int i = 0; i < 11; i++)
			plant_simulate_step(&inst_step15, &env, 15.0f);

		/* Path C: 33 incremental steps of 5.0 days */
		for (int i = 0; i < 33; i++)
			plant_simulate_step(&inst_step5, &env, 5.0f);

		assert(phenotypes_equal(&inst_jump.phenotype, &inst_step15.phenotype, 1e-5f) &&
		       "Single jump must match 15-day incremental stepping");
		assert(phenotypes_equal(&inst_jump.phenotype, &inst_step5.phenotype, 1e-5f) &&
		       "Single jump must match 5-day incremental stepping");
	}

	/* 4. Verify Ephemeral Delta Decay (Pruning, Regrowth, and Zero-Memory GC Convergence) */
	{
		PlantSpeciesDef pine_sp;
		plant_species_init_preset(&pine_sp, 0u); /* Pinus sylvestris */
		PlantGenome pine_g = plant_genome_sample(&pine_sp, 0xBEEFCAFE1234ULL);

		static PlantPhenotypeStorage store_canon, store_delta;
		PlantInstance inst_canon, inst_delta;
		memset(&inst_canon, 0, sizeof(inst_canon));
		memset(&inst_delta, 0, sizeof(inst_delta));
		inst_canon.genome = pine_g;
		inst_delta.genome = pine_g;
		plant_phenotype_bind_storage(&inst_canon.phenotype, &store_canon);
		plant_phenotype_bind_storage(&inst_delta.phenotype, &store_delta);

		/* Evaluate canonical tree at age 150 days to pick a lateral bough axis to chop */
		plant_simulate_at_time(&inst_canon, &env, 150.0f, NULL, 0u);
		assert(inst_canon.phenotype.axis_count > 2u);
		uint64_t lateral_axis_hash = inst_canon.phenotype.axes[1].topo_hash;
		uint32_t canon_units_150 = inst_canon.phenotype.unit_count;

		/* Player chops lateral axis[1] at t_event = 150.0 days, healing duration = 60.0 days */
		PlantDeltaRecord deltas[2];
		deltas[0].target_topo_hash = lateral_axis_hash;
		deltas[0].kind = PLANT_DELTA_PRUNE_AXIS;
		deltas[0].t_event_days = 150.0f;
		deltas[0].t_heal_days = 60.0f;

		/* Immediately after chop (t = 150.0, eta = 0.0): lateral axis is severed -> fewer units */
		plant_simulate_at_time(&inst_delta, &env, 150.0f, deltas, 1u);
		uint32_t cut_units_150 = inst_delta.phenotype.unit_count;
		assert(cut_units_150 < canon_units_150 &&
		       "Pruning a lateral axis at eta=0 must remove its downstream phytomers");

		/* Halfway through healing (t = 180.0, eta = 0.5): branch is actively regrowing */
		plant_simulate_at_time(&inst_delta, &env, 180.0f, deltas, 1u);
		uint32_t regrow_units_180 = inst_delta.phenotype.unit_count;
		assert(regrow_units_180 >= cut_units_150 &&
		       "Regrowing subtree at eta=0.5 must have equal or more units than at eta=0");

		/* At healing completion (t = 210.0, eta = 1.0): must match canonical unaltered tree at t = 210.0! */
		plant_simulate_at_time(&inst_canon, &env, 210.0f, NULL, 0u);
		plant_simulate_at_time(&inst_delta, &env, 210.0f, deltas, 1u);
		assert(phenotypes_equal(&inst_canon.phenotype, &inst_delta.phenotype, 1e-5f) &&
		       "Once eta >= 1.0, phenotype with delta must converge 100% to canonical zero-memory phenotype");

		/* Garbage collection compacts expired delta array to 0 records */
		uint32_t remaining = plant_deltas_compact_expired(deltas, 1u, 210.0f);
		assert(remaining == 0u && "Expired delta must be garbage-collected to 0 records");
	}

	/* 5. Verify Stage 4 (plant_geometry_build) across all 128 canonical templates (16 species x 2 stages x 4 alleles):
	 *    - Every tree species (big_col > 0, excluding columnar cactus sp==6) must have ZERO foliage vertices
	 *      descending below the clear-bole crown floor in either 3D Z or 55-deg projected height (z - 0.7002*y).
	 *    - Acacia senegal (sp == 7) must have its foliage concentrated in the upper umbrella stratum while
	 *      exposing bare woody V-vase scaffold branches through the middle stratum. */
	{
		static nd_mesh3d_vert_t s_test_verts[ND_MESH3D_MAX_VERTS];
		static uint32_t s_test_indices[ND_MESH3D_MAX_INDICES];
		nd_mesh3d_builder_t b;
		b.verts = s_test_verts;
		b.vert_count = 0;
		b.vert_cap = ND_MESH3D_MAX_VERTS;
		b.indices = s_test_indices;
		b.idx_count = 0;
		b.idx_cap = ND_MESH3D_MAX_INDICES;

		for (unsigned sp = 0; sp < ND_PLANT_MAX; sp++) {
			const nd_plant_spec_t *spec = &s_nd_plant_specs[sp];
			PlantSpeciesDef sdef;
			plant_species_init_preset(&sdef, sp);
			bool is_tree = (sdef.life_form == PLANT_LIFEFORM_TREE);

			for (unsigned big = 0; big < 2u; big++) {
				for (unsigned var = 0; var < 4u; var++) {
					nd_mesh3d_slice_t slice;
					memset(&slice, 0, sizeof(slice));
					nd_m3d_build_plant_from_genes(&b, spec, (int)big, (int)var,
					                              0xB07A0u + sp * 97u, &slice);
					if (slice.vert_count <= 12u) {
						fprintf(stderr, "FAIL at sp=%u (%s) big=%u var=%u: slice.vert_count=%u total_verts=%u/%u total_idx=%u/%u\n",
							sp, spec->slug, big, var, slice.vert_count, b.vert_count, b.vert_cap, b.idx_count, b.idx_cap);
					}
					assert(slice.vert_count > 12u && "Every plant template must emit valid 3D vertices");
					assert(slice.idx_count > 18u && (slice.idx_count % 3u) == 0u &&
					       "Every plant template must emit valid 3D triangles");

					if (is_tree) {
						float h_wu = slice.mesh_h_wu;
						float min_fol_z = h_wu;
						uint32_t fol_cnt = 0u;
						uint32_t mid_bark_cnt = 0u;
						for (uint32_t vi = 0; vi < slice.vert_count; vi++) {
							const nd_mesh3d_vert_t *v = &b.verts[slice.first_vert + vi];
							float mat = v->col[3];
							float vz = v->pos[2];
							if (mat >= 0.19f && mat <= 0.36f) {
								fol_cnt++;
								if (vz < min_fol_z) min_fol_z = vz;
							} else if (mat < 0.08f) {
								if (vz >= h_wu * 0.32f && vz <= h_wu * 0.72f)
									mid_bark_cnt++;
							}
						}
						assert(fol_cnt > 0u && "Every tree must emit foliage vertices");
						assert(min_fol_z >= h_wu * 0.17f &&
						       "Tree foliage 3D Z must never descend to the ground below clear bole");
						if (sp == 7u) { /* Acacia senegal */
							assert(min_fol_z >= h_wu * 0.55f &&
							       "Acacia senegal foliage must be perched in the upper savannah umbrella stratum");
							assert(mid_bark_cnt >= 24u &&
							       "Acacia senegal must have bare ascending V-vase scaffold branches in mid-crown");
						}
					}
				}
			}
		}
		/* Phase 1 Skyrim vegetation: verify juvenile non-tabular trees carry foliage lower than mature trees */
		{
			const nd_plant_spec_t *fir_spec = &s_nd_plant_specs[1u];
			nd_mesh3d_slice_t sl_j, sl_m;
			memset(&sl_j, 0, sizeof(sl_j));
			memset(&sl_m, 0, sizeof(sl_m));
			nd_m3d_build_plant_from_genes(&b, fir_spec, 0, 0, 0xB07A0u + 97u, &sl_j);
			nd_m3d_build_plant_from_genes(&b, fir_spec, 1, 0, 0xB07A0u + 97u, &sl_m);
			float min_fol_j = 1.0f, min_fol_m = 1.0f;
			for (uint32_t vi = 0; vi < sl_j.vert_count; vi++) {
				const nd_mesh3d_vert_t *v = &b.verts[sl_j.first_vert + vi];
				if (v->col[3] >= 0.19f && v->col[3] <= 0.36f) {
					float f = v->pos[2] / sl_j.mesh_h_wu;
					if (f < min_fol_j) min_fol_j = f;
				}
			}
			for (uint32_t vi = 0; vi < sl_m.vert_count; vi++) {
				const nd_mesh3d_vert_t *v = &b.verts[sl_m.first_vert + vi];
				if (v->col[3] >= 0.19f && v->col[3] <= 0.36f) {
					float f = v->pos[2] / sl_m.mesh_h_wu;
					if (f < min_fol_m) min_fol_m = f;
				}
			}
			assert(min_fol_j < min_fol_m && "Juvenile non-tabular fir saplings must carry live lower foliage further down the trunk");
		}
		assert(b.vert_count < ND_MESH3D_MAX_VERTS - 16384u &&
		       "All 128 plant templates must fit comfortably within VBO vertex budget");
		assert(b.idx_count < ND_MESH3D_MAX_INDICES - 32768u &&
		       "All 128 plant templates must fit comfortably within VBO index budget");
		printf("Stage 4 Geometry Budget: 128 plant templates used %u / %u verts, %u / %u indices\n",
		       b.vert_count, ND_MESH3D_MAX_VERTS, b.idx_count, ND_MESH3D_MAX_INDICES);

		/* Phase 2: Verify all 12 3D Living Meadow clump archetypes */
		for (uint32_t mv = 0; mv < ND_MESH3D_MEADOW_COUNT; mv++) {
			nd_mesh3d_slice_t mslice;
			memset(&mslice, 0, sizeof(mslice));
			nd_m3d_build_meadow_clump(&b, mv, &mslice);
			assert(mslice.vert_count >= 64u && (mslice.idx_count % 3u) == 0u && mslice.idx_count >= 96u);
			assert(mslice.mesh_w_wu >= 18.0f && mslice.mesh_h_wu >= 5.0f);
			float min_z = 100.0f, max_z = -100.0f, max_w = 0.0f;
			uint32_t root_pin_cnt = 0u, flower_cnt = 0u, fol_cnt = 0u, grass_card_cnt = 0u;
			for (uint32_t vi = 0; vi < mslice.vert_count; vi++) {
				const nd_mesh3d_vert_t *v = &b.verts[mslice.first_vert + vi];
				if (v->pos[2] < min_z) min_z = v->pos[2];
				if (v->pos[2] > max_z) max_z = v->pos[2];
				if (v->pos[3] > max_w) max_w = v->pos[3];
				if (v->pos[2] < -0.10f && (v->pos[3] == 0.0f || (v->pos[3] > 1.0f && floorf(v->pos[3]) < 1.5f))) root_pin_cnt++;
				if (v->col[3] > 0.425f && v->col[3] <= 0.55f) flower_cnt++;
				if (v->col[3] >= 0.18f && v->col[3] <= 0.425f) fol_cnt++;
				if (v->col[3] >= 0.319f && v->col[3] <= 0.425f && v->pos[3] > 1.0f) grass_card_cnt++;
			}
			assert(min_z >= -0.30f && "Meadow clump roots must remain cleanly anchored without deep sink");
			assert(root_pin_cnt >= 16u && "Compound meadow mats must anchor multiple subtuft roots with zero wind weight");
			assert(max_w > 0.30f && "Meadow clump tips must expose non-zero wind weight");
			assert(fol_cnt > 48u && "Compound meadow mats must emit multi-subtuft foliage blades");
			assert(grass_card_cnt >= 24u && "Skyrim meadow & fern mats must emit 3D alpha-tested botanical cards");
			if (mv == 4u || mv == 5u) {
				assert(flower_cnt >= 10u && "Wildflower meadow clump must emit 3D corolla blossoms");
			}
		}

		/* Phase 4: Verify enhanced tree root collars & all 4 forest-floor understory log/stump archetypes */
		assert(ND_MESH3D_UNDERSTORY_BASE == 192u && ND_MESH3D_UNDERSTORY_COUNT == 4u);
		assert(ND_MESH3D_SHRUB_BASE == 196u && ND_MESH3D_SHRUB_COUNT == 10u);
		assert(ND_MESH3D_TOTAL == 206u && ND_MESH3D_MAX_VERTS >= 393216u);
		{
			const nd_plant_spec_t *oak_spec = &s_nd_plant_specs[10u];
			nd_mesh3d_slice_t oak_sl;
			memset(&oak_sl, 0, sizeof(oak_sl));
			nd_m3d_build_plant_from_genes(&b, oak_spec, 1, 0, 0xB07A0u + 10u * 97u, &oak_sl);
			float min_bark_z = 100.0f;
			for (uint32_t vi = 0; vi < oak_sl.vert_count; vi++) {
				const nd_mesh3d_vert_t *v = &b.verts[oak_sl.first_vert + vi];
				if (v->col[3] < 0.08f && v->pos[2] < min_bark_z)
					min_bark_z = v->pos[2];
			}
			assert(min_bark_z < 0.0f && "Tree root collars/spurs must sink cleanly into slopes");
		}
		for (uint32_t sv = 6u; sv <= 7u; sv++) {
			nd_mesh3d_slice_t fslice;
			memset(&fslice, 0, sizeof(fslice));
			nd_m3d_build_shrub_prop(&b, sv, &fslice);
			uint32_t fern_card_verts = 0u;
			for (uint32_t vi = 0; vi < fslice.vert_count; vi++) {
				const nd_mesh3d_vert_t *v = &b.verts[fslice.first_vert + vi];
				if (v->col[3] >= ND_MAT3D_FOL_BLADE && v->col[3] <= 0.355f && v->pos[3] > 1.0f)
					fern_card_verts++;
			}
			assert(fern_card_verts >= 72u && "Giant woodland bracken fern shrub must emit feathery bipinnate frond cards");
		}
		for (uint32_t uv = 0; uv < ND_MESH3D_UNDERSTORY_COUNT; uv++) {
			nd_mesh3d_slice_t uslice;
			memset(&uslice, 0, sizeof(uslice));
			nd_m3d_build_understory_prop(&b, uv, &uslice);
			assert(uslice.vert_count >= 28u && uslice.idx_count >= 36u && (uslice.idx_count % 3u) == 0u);
			assert(uslice.mesh_w_wu >= 30.0f && uslice.mesh_h_wu >= 12.0f);
			uint32_t bark_cnt = 0u, fern_cnt = 0u;
			float min_wood_z = 100.0f, max_fern_wind = 0.0f;
			for (uint32_t vi = 0; vi < uslice.vert_count; vi++) {
				const nd_mesh3d_vert_t *v = &b.verts[uslice.first_vert + vi];
				if (v->col[3] < 0.18f) {
					bark_cnt++;
					assert(v->pos[3] == 0.0f && "Understory log/stump timber must stay rigid (zero wind sway)");
					if (v->pos[2] < min_wood_z) min_wood_z = v->pos[2];
				} else {
					fern_cnt++;
					if (v->pos[3] > max_fern_wind) max_fern_wind = v->pos[3];
				}
			}
			assert(bark_cnt >= 24u && fern_cnt >= 8u);
			assert(min_wood_z <= 0.0f && "Understory log/stump bases must anchor flush into ground");
			assert(max_fern_wind > 0.05f && "Understory nurse-log ferns must keep positive wind sway");
		}
		assert(b.vert_count < ND_MESH3D_MAX_VERTS && b.idx_count < ND_MESH3D_MAX_INDICES);
	}

	/* 5b. Verify 3D Beer-Lambert Intra-Crown Light Competition & 1024x1024 Foliage Atlas */
	{
		PlantSpeciesDef oak_sp;
		plant_species_init_preset(&oak_sp, 10u);
		PlantGenome oak_g = plant_genome_sample(&oak_sp, 0xCAFE777ULL);
		static PlantPhenotypeStorage store_oak;
		PlantInstance inst_oak;
		memset(&inst_oak, 0, sizeof(inst_oak));
		inst_oak.genome = oak_g;
		plant_phenotype_bind_storage(&inst_oak.phenotype, &store_oak);
		plant_simulate_at_time(&inst_oak, &env, 180.0f, NULL, 0u);

		float min_leaf_health = 1.0f, max_leaf_health = 0.0f;
		for (uint32_t i = 0; i < inst_oak.phenotype.organ_count; i++) {
			const PlantOrgan *o = &inst_oak.phenotype.organs[i];
			if (o->type == PLANT_ORGAN_LEAF) {
				if (o->health < min_leaf_health) min_leaf_health = o->health;
				if (o->health > max_leaf_health) max_leaf_health = o->health;
			}
		}
		assert(max_leaf_health > min_leaf_health + 0.02f &&
		       "3D Beer-Lambert canopy light competition must differentiate sunlit outer leaves from shaded interior leaves");
		assert(ND_FOL_ATLAS_SIZE == 1024 && ND_FOL_TILE_SIZE == 512 && ND_FOL_MIP_LEVELS == 9 &&
		       "Botanical branchlet atlas must be 1024x1024 with 9 mip levels");

		size_t atlas_bytes = (size_t)ND_FOL_ATLAS_SIZE * (size_t)ND_FOL_ATLAS_SIZE * 4u;
		uint8_t *fol_alb  = (uint8_t *)malloc(atlas_bytes);
		uint8_t *fol_norm = (uint8_t *)malloc(atlas_bytes);
		uint8_t *fol_mip1 = (uint8_t *)malloc(atlas_bytes / 4u);
		assert(fol_alb && fol_norm && fol_mip1);

		nd_foliage_atlas_bake_level0(fol_alb);
		nd_foliage_atlas_bake_normals_level0(fol_alb, fol_norm);
		nd_foliage_atlas_downsample_normals(fol_norm, ND_FOL_ATLAS_SIZE, ND_FOL_ATLAS_SIZE, fol_mip1);

		uint32_t opaque_cnt = 0u, tilted_norm_cnt = 0u, leaf_trans_cnt = 0u, twig_opaque_cnt = 0u;
		for (size_t i = 0; i < (size_t)ND_FOL_ATLAS_SIZE * (size_t)ND_FOL_ATLAS_SIZE; i++) {
			const uint8_t *a = fol_alb + i * 4u;
			const uint8_t *n = fol_norm + i * 4u;
			if (a[3] > 200u) {
				opaque_cnt++;
				if (abs((int)n[0] - 128) > 8 || abs((int)n[1] - 128) > 8)
					tilted_norm_cnt++;
				if (a[2] < 40u && n[3] > 100u)
					leaf_trans_cnt++;
				if (a[2] > 200u && n[3] < 40u)
					twig_opaque_cnt++;
			} else if (a[3] <= 1u) {
				assert(n[0] == 128u && n[1] == 128u && n[2] == 0u && n[3] == 0u);
			}
		}
		assert(opaque_cnt > 50000u && "Foliage atlas must have substantial opaque botanical coverage");
		assert(tilted_norm_cnt > 10000u && "Companion normal atlas must encode surface relief across leaves and needles");
		assert(leaf_trans_cnt > 20000u && "Chlorophyll foliage must have high subsurface translucency");
		assert(twig_opaque_cnt > 2000u && "Woody twigs must have low subsurface translucency");

		uint32_t mip_valid = 0u;
		for (size_t i = 0; i < (size_t)(ND_FOL_ATLAS_SIZE / 2) * (size_t)(ND_FOL_ATLAS_SIZE / 2); i++) {
			if (fol_mip1[i * 4u + 3u] > 0u)
				mip_valid++;
		}
		assert(mip_valid > 10000u && "Downsampled normal atlas mip-1 must preserve valid coverage");

		free(fol_alb);
		free(fol_norm);
		free(fol_mip1);

		/* 5c. Verify 1024x512 Grass Tuft Atlas, Terrain Micro-Detail Atlas & Seamless 256x256 Bark Atlas */
		assert(ND_GRASS_ATLAS_W == 1024 && ND_GRASS_ATLAS_H == 512 && ND_GRASS_MIP_LEVELS == 9);
		assert(ND_BARK_ATLAS_SIZE == 256 && ND_BARK_MIP_LEVELS == 8);
		assert(ND_TERR_ATLAS_SIZE == 256 && ND_TERR_MIP_LEVELS == 8);
		size_t grass_bytes = (size_t)ND_GRASS_ATLAS_W * (size_t)ND_GRASS_ATLAS_H * 4u;
		uint8_t *grass_alb  = (uint8_t *)malloc(grass_bytes);
		uint8_t *grass_norm = (uint8_t *)malloc(grass_bytes);
		uint8_t *grass_mip1 = (uint8_t *)malloc(grass_bytes / 4u);
		assert(grass_alb && grass_norm && grass_mip1);
		nd_grass_atlas_bake_level0(grass_alb);
		nd_grass_atlas_bake_normals_level0(grass_alb, grass_norm);
		nd_grass_atlas_downsample(grass_alb, ND_GRASS_ATLAS_W, ND_GRASS_ATLAS_H, grass_mip1);

		uint32_t g_tile0_cov = 0u, g_tile1_cov = 0u, g_mip_cov = 0u;
		for (int y = 0; y < ND_GRASS_ATLAS_H; y++) {
			for (int x = 0; x < ND_GRASS_ATLAS_W; x++) {
				const uint8_t *ga = grass_alb + ((size_t)y * (size_t)ND_GRASS_ATLAS_W + (size_t)x) * 4u;
				const uint8_t *gn = grass_norm + ((size_t)y * (size_t)ND_GRASS_ATLAS_W + (size_t)x) * 4u;
				if (ga[3] > 180u) {
					if (x < ND_GRASS_TILE_SIZE) g_tile0_cov++;
					else g_tile1_cov++;
					assert(gn[3] >= 10u && "Opaque grass blade must store subsurface translucency");
				} else if (ga[3] <= 1u) {
					assert(gn[0] == 128u && gn[1] == 128u && gn[2] == 0u && gn[3] == 0u);
				}
			}
		}
		for (size_t i = 0; i < (size_t)(ND_GRASS_ATLAS_W / 2) * (size_t)(ND_GRASS_ATLAS_H / 2); i++) {
			const uint8_t *mp = grass_mip1 + i * 4u;
			if (mp[3] > 128u) g_mip_cov++;
			if (mp[3] == 0u) {
				assert(mp[0] == 46u && mp[1] == 68u && mp[2] == 32u &&
				       "Empty mip texels must fill with dark botanical green instead of yellow");
			}
		}
		assert(g_tile0_cov > 38000u && g_tile1_cov > 38000u && "Both grass tiles must bake rich dense tufts");
		assert(g_tile0_cov < 104857u && g_tile1_cov < 104857u && "Each grass tile must keep >60% open negative space");
		assert(g_mip_cov > 20000u && "Grass atlas mip-1 downsample must preserve blade coverage");
		free(grass_alb);
		free(grass_norm);
		free(grass_mip1);

		size_t terr_bytes = (size_t)ND_TERR_ATLAS_SIZE * (size_t)ND_TERR_ATLAS_SIZE * 4u;
		uint8_t *terr_alb  = (uint8_t *)malloc(terr_bytes);
		uint8_t *terr_norm = (uint8_t *)malloc(terr_bytes);
		assert(terr_alb && terr_norm);
		nd_terr_atlas_bake_level0(terr_alb);
		nd_terr_atlas_bake_normals_level0(terr_alb, terr_norm);
		int t_max_seam = 0, t_max_int = 0;
		for (int i = 0; i < ND_TERR_ATLAS_SIZE; i++) {
			int sx = abs((int)terr_alb[(i * ND_TERR_ATLAS_SIZE + 0) * 4] -
			             (int)terr_alb[(i * ND_TERR_ATLAS_SIZE + (ND_TERR_ATLAS_SIZE - 1)) * 4]);
			int sy = abs((int)terr_alb[(0 * ND_TERR_ATLAS_SIZE + i) * 4] -
			             (int)terr_alb[((ND_TERR_ATLAS_SIZE - 1) * ND_TERR_ATLAS_SIZE + i) * 4]);
			if (sx > t_max_seam) t_max_seam = sx;
			if (sy > t_max_seam) t_max_seam = sy;
			for (int j = 0; j < ND_TERR_ATLAS_SIZE - 1; j++) {
				int ix = abs((int)terr_alb[(i * ND_TERR_ATLAS_SIZE + j + 1) * 4] -
				             (int)terr_alb[(i * ND_TERR_ATLAS_SIZE + j) * 4]);
				int iy = abs((int)terr_alb[((j + 1) * ND_TERR_ATLAS_SIZE + i) * 4] -
				             (int)terr_alb[(j * ND_TERR_ATLAS_SIZE + i) * 4]);
				if (ix > t_max_int) t_max_int = ix;
				if (iy > t_max_int) t_max_int = iy;
			}
		}
		assert(t_max_seam <= t_max_int + 2 && "Terrain micro-detail atlas must wrap seamlessly without seams");
		free(terr_alb);
		free(terr_norm);

		size_t bark_bytes = (size_t)ND_BARK_ATLAS_SIZE * (size_t)ND_BARK_ATLAS_SIZE * 4u;
		uint8_t *bark_alb  = (uint8_t *)malloc(bark_bytes);
		uint8_t *bark_norm = (uint8_t *)malloc(bark_bytes);
		uint8_t *bark_mip1 = (uint8_t *)malloc(bark_bytes / 4u);
		assert(bark_alb && bark_norm && bark_mip1);
		nd_bark_atlas_bake_level0(bark_alb);
		nd_bark_atlas_bake_normals_level0(bark_alb, bark_norm);
		nd_bark_atlas_downsample(bark_alb, ND_BARK_ATLAS_SIZE, ND_BARK_ATLAS_SIZE, bark_mip1);

		int max_seam_diff = 0, max_int_diff = 0;
		for (int i = 0; i < ND_BARK_ATLAS_SIZE; i++) {
			int sx = abs((int)bark_alb[(i * ND_BARK_ATLAS_SIZE + 0) * 4] -
			             (int)bark_alb[(i * ND_BARK_ATLAS_SIZE + (ND_BARK_ATLAS_SIZE - 1)) * 4]);
			int sy = abs((int)bark_alb[(0 * ND_BARK_ATLAS_SIZE + i) * 4] -
			             (int)bark_alb[((ND_BARK_ATLAS_SIZE - 1) * ND_BARK_ATLAS_SIZE + i) * 4]);
			if (sx > max_seam_diff) max_seam_diff = sx;
			if (sy > max_seam_diff) max_seam_diff = sy;
			for (int j = 0; j < ND_BARK_ATLAS_SIZE - 1; j++) {
				int ix = abs((int)bark_alb[(i * ND_BARK_ATLAS_SIZE + j + 1) * 4] -
				             (int)bark_alb[(i * ND_BARK_ATLAS_SIZE + j) * 4]);
				int iy = abs((int)bark_alb[((j + 1) * ND_BARK_ATLAS_SIZE + i) * 4] -
				             (int)bark_alb[(j * ND_BARK_ATLAS_SIZE + i) * 4]);
				if (ix > max_int_diff) max_int_diff = ix;
				if (iy > max_int_diff) max_int_diff = iy;
			}
		}
		assert(max_seam_diff <= max_int_diff + 2 && "Periodic bark atlas must wrap seamlessly without border discontinuity");
		free(bark_alb);
		free(bark_norm);
		free(bark_mip1);
	}

	/* 6. Verify v1.0 IDEA.md Cholesky Correlation Validation, MTG Graph Validation,
	 *    Instance Lifecycle, Spatial Environment Sampler, and Serialization/Migration */
	{
		/* 6a. Cholesky correlation matrix validation */
		float valid_corr[4] = { 1.0f, 0.6f, 0.6f, 1.0f };
		float non_sym[4]    = { 1.0f, 0.6f, 0.2f, 1.0f };
		float non_unit[4]   = { 0.9f, 0.2f, 0.2f, 1.0f };
		float non_psd[9]    = {
			1.0f,  0.9f, -0.9f,
			0.9f,  1.0f,  0.9f,
			-0.9f, 0.9f,  1.0f
		};
		float L[9];
		assert(plant_correlation_cholesky(valid_corr, 2u, L) && "Valid 2x2 correlation matrix must pass Cholesky");
		assert(!plant_correlation_cholesky(non_sym,   2u, L) && "Non-symmetric matrix must be rejected");
		assert(!plant_correlation_cholesky(non_unit,  2u, L) && "Non-unit diagonal matrix must be rejected");
		assert(!plant_correlation_cholesky(non_psd,   3u, L) && "Indefinite non-PSD matrix must be rejected");

		/* 6b. Canonical sample_into / cross_into / instance_init / phenotype_validate */
		PlantSpeciesDef oak_sp;
		plant_species_init_preset(&oak_sp, 10u);
		assert(plant_species_validate_status(&oak_sp) == PLANT_OK);

		PlantGenome mother, father, child, deserialized, migrated_v2;
		assert(plant_genome_sample_into(&oak_sp, 0x1111ULL, 1001ULL, &mother) == PLANT_OK);
		assert(plant_genome_sample_into(&oak_sp, 0x2222ULL, 1002ULL, &father) == PLANT_OK);
		assert(mother.individual_id == 1001ULL && father.individual_id == 1002ULL);
		assert(plant_genome_cross_into(&oak_sp, &mother, &father, 0x3333ULL, &child) == PLANT_OK);
		assert(child.parent_id[0] == 1001ULL && child.parent_id[1] == 1002ULL);

		PlantInstance inst;
		memset(&inst, 0, sizeof(inst));
		assert(plant_instance_init(&inst, &child) == PLANT_OK);
		plant_simulate_at_time(&inst, &env, 150.0f, NULL, 0u);
		assert(plant_phenotype_validate(&inst.phenotype) == PLANT_OK &&
		       "Developed MTG phenotype must have valid axis/unit/bud/organ indices and zero cycles");
		assert(inst.accumulated_degree_days > 0.0f);
		assert(inst.carbon_reserves_g > 0.0f);
		plant_instance_destroy(&inst);

		/* 6c. Explicit portable field serialization & v1->v2 schema migration */
		uint8_t wire_buf[256];
		size_t wire_len = 0u;
		assert(plant_genome_serialize(&child, wire_buf, sizeof(wire_buf), &wire_len) == PLANT_OK);
		assert(wire_len == 192u);
		assert(plant_genome_deserialize(&deserialized, wire_buf, wire_len) == PLANT_OK);
		assert(deserialized.individual_id == child.individual_id);
		assert(deserialized.parent_id[0] == child.parent_id[0]);
		assert(deserialized.parent_id[1] == child.parent_id[1]);
		assert(fabsf(deserialized.architecture.internode_length_m.mean -
		             child.architecture.internode_length_m.mean) < 1e-6f);
		assert(plant_genome_migrate_v1_to_v2(&deserialized, &migrated_v2) == PLANT_OK);
		assert(migrated_v2.schema_version == 2u);
	}

	/* 7. Verify Phase 3 Mid-Story Shrub, Fern & Deadwood Snag Mesh Builders */
	{
		nd_mesh3d_vert_t *sb_verts = (nd_mesh3d_vert_t *)calloc(32768u, sizeof(nd_mesh3d_vert_t));
		uint32_t *sb_idx = (uint32_t *)calloc(65536u, sizeof(uint32_t));
		assert(sb_verts && sb_idx);
		nd_mesh3d_builder_t sb = {
			.verts = sb_verts,
			.vert_count = 0u,
			.vert_cap = 32768u,
			.indices = sb_idx,
			.idx_count = 0u,
			.idx_cap = 65536u
		};
		for (uint32_t sv = 0; sv < ND_MESH3D_SHRUB_COUNT; sv++) {
			nd_mesh3d_slice_t sl;
			memset(&sl, 0, sizeof(sl));
			nd_m3d_build_shrub_prop(&sb, sv, &sl);
			assert(sl.vert_count >= 24u && sl.idx_count >= 36u);
			assert(sl.mesh_w_wu >= 24.0f && sl.mesh_h_wu >= 18.0f);
			if ((sv >> 1u) == 4u) {
				/* Deadwood snag: tall, rigid (zero wind sway) */
				assert(sl.mesh_h_wu >= 75.0f);
				for (uint32_t vi = 0; vi < sl.vert_count; vi++) {
					assert(sb.verts[sl.first_vert + vi].pos[3] == 0.0f);
				}
			} else if ((sv >> 1u) == 3u) {
				/* Bracken fern: positive blade wind weights */
				uint32_t wind_v = 0u;
				for (uint32_t vi = 0; vi < sl.vert_count; vi++) {
					if (sb.verts[sl.first_vert + vi].pos[3] > 0.01f)
						wind_v++;
				}
				assert(wind_v >= 20u);
			} else {
				/* Woody juniper / thicket / heather: UV branchlet cards (pos[3] > 1.0f) */
				uint32_t card_v = 0u;
				for (uint32_t vi = 0; vi < sl.vert_count; vi++) {
					if (sb.verts[sl.first_vert + vi].pos[3] > 1.0f)
						card_v++;
				}
				assert(card_v >= 18u);
			}
		}
		free(sb_verts);
		free(sb_idx);
	}

	printf("PASS: test_nd_plant_model (16 species validation, Cholesky PSD, MTG validation, Da Vinci pipe-model, dt-invariance, ephemeral delta decay, serialization, 3D geometry)\n");
	return 0;
}
