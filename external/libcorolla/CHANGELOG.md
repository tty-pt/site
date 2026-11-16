# Changelog

All notable changes to `libcorolla` are documented in this file.

## [1.0.0] - 2026-11-15

### Added
- **Parametric 2-Manifold Surface Embedding (`include/ttypt/corolla.h`, `src/corolla.c`)**:
  - Added `corolla_surface_t`, `corolla_frame_t`, `corolla_eval_frame()`, and `corolla_xyz_to_uvh()` supporting `COROLLA_TORUS`, `COROLLA_SPHERE`, `COROLLA_CYLINDER`, and `COROLLA_PLANE`.
- **Exact Analytical Ray-Manifold Intersection & Self-Occlusion**:
  - Added `corolla_ray_hit()` and `corolla_occluded()` with closed-form quadratic and quartic root solvers for torus, sphere, cylinder, and plane manifolds.
- **3D Keplerian Orbits & Rigid-Body Pose Ephemeris**:
  - Added `corolla_orbit_t`, `corolla_body_t`, `corolla_pose_t`, `corolla_orbit_pos()`, `corolla_eval_body_pose()`, `corolla_frame_to_world()`, and `corolla_body_ray_hit()` for multi-body celestial systems with full 3D inclination, eccentricity, axial obliquity, and diurnal spin.
- **Solar Insolation, Multi-Belt Atmospheric Climate & 3D Manifold Patch Bounds**:
  - Added `corolla_sun_dir()`, `corolla_insolation()`, `corolla_annual_insolation()`, and `corolla_eval_climate()` (3-cell atmospheric circulation, subtropical high-pressure desert belts, ITCZ equatorial rain belt, and directional orographic rain shadow).
  - Added `corolla_manifold_patch_bounds_3d()` for conservative 3D AABB evaluation of curved surface patches (`libareole` `ar_bounds_3d_fn` integration).
