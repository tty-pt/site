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
						float min_fol_proj = h_wu;
						uint32_t fol_cnt = 0u;
						uint32_t mid_bark_cnt = 0u;
						for (uint32_t vi = 0; vi < slice.vert_count; vi++) {
							const nd_mesh3d_vert_t *v = &b.verts[slice.first_vert + vi];
							float mat = v->col[3];
							float vz = v->pos[2];
							float vy = v->pos[1];
							float h_proj = vz - 0.7002075f * vy;
							if (mat >= 0.19f && mat <= 0.36f) {
								fol_cnt++;
								if (vz < min_fol_z) min_fol_z = vz;
								if (h_proj < min_fol_proj) min_fol_proj = h_proj;
							} else if (mat < 0.05f) {
								if (vz >= h_wu * 0.32f && vz <= h_wu * 0.72f)
									mid_bark_cnt++;
							}
						}
						assert(fol_cnt > 0u && "Every tree must emit foliage vertices");
						assert(min_fol_z >= h_wu * 0.17f &&
						       "Tree foliage 3D Z must never descend to the ground below clear bole");
						assert(min_fol_proj >= h_wu * 0.16f &&
						       "Tree foliage 55-deg projected height must never descend to the ground");
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
		assert(b.vert_count < ND_MESH3D_MAX_VERTS - 16384u &&
		       "All 128 plant templates must fit comfortably within VBO vertex budget");
		assert(b.idx_count < ND_MESH3D_MAX_INDICES - 32768u &&
		       "All 128 plant templates must fit comfortably within VBO index budget");
		printf("Stage 4 Geometry Budget: 128 plant templates used %u / %u verts, %u / %u indices\n",
		       b.vert_count, ND_MESH3D_MAX_VERTS, b.idx_count, ND_MESH3D_MAX_INDICES);
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

	printf("PASS: test_nd_plant_model (16 species validation, Cholesky PSD, MTG validation, Da Vinci pipe-model, dt-invariance, ephemeral delta decay, serialization, 3D geometry)\n");
	return 0;
}
