# libcorolla

Generic parametric 2-manifold surface embedding, exact analytical ray-surface intersection & self-occlusion, 3D Keplerian orbital mechanics, and climate insolation library in pure C (`<ttypt/corolla.h>`).

## Core Capabilities

1. **Parametric 2-Manifold Differential Frames (`corolla_eval_frame`, `corolla_xyz_to_uvh`)**:
   - Maps normalized surface coordinates $(u, v) \in [0, 1)^2$ and normal altitude offset $h$ to 3D Cartesian position, outward unit normal $\hat{n}$, orthonormal tangent vectors $(\hat{t}_u, \hat{t}_v)$, and metric arc-length scale factors $(ds_u, ds_v)$.
   - Supports Ring Torus (`COROLLA_TORUS`), Sphere / Oblate Spheroid (`COROLLA_SPHERE`), Cylinder / Ring Habitat (`COROLLA_CYLINDER`), and Euclidean Plane (`COROLLA_PLANE`).
2. **Exact Analytical Ray-Surface Intersection & Self-Occlusion (`corolla_ray_hit`, `corolla_occluded`)**:
   - Closed-form quadratic and Ferrari/companion-polished quartic root solvers for ray-torus, ray-sphere, ray-cylinder, and ray-plane intersections.
   - Exact self-shadowing (e.g., torus outer ring shadowing the inner hub equator).
3. **3D Keplerian Orbital Mechanics & Rigid-Body Pose Ephemeris (`corolla_orbit_pos`, `corolla_eval_body_pose`, `corolla_body_ray_hit`)**:
   - Evaluates 3D orbital positions and instantaneous rigid-body poses (`corolla_pose_t`: translation + $3\times 3$ body-to-inertial and inertial-to-body orthonormal rotation matrices) under arbitrary orbital inclination, eccentricity, axial obliquity, and diurnal spin.
4. **Solar Irradiance, Multi-Belt Climate & 3D Patch Bounds (`corolla_insolation`, `corolla_annual_insolation`, `corolla_eval_climate`, `corolla_manifold_patch_bounds_3d`)**:
   - Computes instantaneous and annually integrated insolation, physically grounded temperature/humidity/wind climate belts (Hadley/ITCZ, subtropical desert highs, mid-latitude westerlies, and orographic rain shadows), and conservative 3D AABB bounds for quadtree LOD culling (`libareole`).

## Build & Test

```sh
make
make test
```

## License

BSD-2-Clause (`Copyright (c) 2026, tty-pt`).
