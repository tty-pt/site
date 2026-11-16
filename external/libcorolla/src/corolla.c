#ifndef COROLLA_HEADER_ONLY_ACTIVE
#define COROLLA_C_COMPILING 1
#endif
#ifndef TTYPT_COROLLA_H
#include "../include/ttypt/corolla.h"
#endif
#include <math.h>
#include <float.h>

static double
wrap_unit(double x)
{
	double r = fmod(x, 1.0);
	if (r < 0.0)
		r += 1.0;
	if (r >= 1.0)
		r = 0.0;
	return r;
}

static void
vec3_norm(double v[3])
{
	double len2 = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
	if (len2 > 0.0) {
		double inv = 1.0 / sqrt(len2);
		v[0] *= inv;
		v[1] *= inv;
		v[2] *= inv;
	}
}

static double
vec3_dot(const double a[3], const double b[3])
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

COROLLA_API int
corolla_eval_frame(const corolla_surface_t *surf,
                   double u, double v, double h,
                   corolla_frame_t *out)
{
	if (!surf || !out)
		return -1;

	u = wrap_unit(u);
	v = wrap_unit(v);

	switch (surf->kind) {
	case COROLLA_TORUS: {
		double R = surf->r_major;
		double r0 = surf->r_minor;
		if (R <= 0.0 || r0 <= 0.0)
			return -1;

		double phi = COROLLA_TWO_PI * u;
		double theta = COROLLA_TWO_PI * (v - 0.5);
		double cp = cos(phi), sp = sin(phi);
		double ct = cos(theta), st = sin(theta);

		out->norm[0] = ct * cp;
		out->norm[1] = ct * sp;
		out->norm[2] = st;

		out->tan_u[0] = -sp;
		out->tan_u[1] =  cp;
		out->tan_u[2] =  0.0;

		out->tan_v[0] = -st * cp;
		out->tan_v[1] = -st * sp;
		out->tan_v[2] =  ct;

		double tube_r = r0 + h;
		double ring_r = R + tube_r * ct;
		out->pos[0] = ring_r * cp;
		out->pos[1] = ring_r * sp;
		out->pos[2] = tube_r * st;

		out->ds_u = COROLLA_TWO_PI * (R + r0 * ct);
		out->ds_v = COROLLA_TWO_PI * r0;
		return 0;
	}

	case COROLLA_SPHERE: {
		double a = surf->r_major;
		double b = (surf->r_minor > 0.0) ? surf->r_minor : a;
		if (a <= 0.0)
			return -1;

		double lon = COROLLA_TWO_PI * u;
		double lat = COROLLA_PI * (v - 0.5);
		double clon = cos(lon), slon = sin(lon);
		double clat = cos(lat), slat = sin(lat);

		/* Base surface point on spheroid (a, a, b) */
		double sx = a * clat * clon;
		double sy = a * clat * slon;
		double sz = b * slat;

		/* Outward normal proportional to (clat*clon/a, clat*slon/a, slat/b) */
		out->norm[0] = b * clat * clon;
		out->norm[1] = b * clat * slon;
		out->norm[2] = a * slat;
		vec3_norm(out->norm);

		out->tan_u[0] = -slon;
		out->tan_u[1] =  clon;
		out->tan_u[2] =  0.0;

		out->tan_v[0] = - a * slat * clon;
		out->tan_v[1] = - a * slat * slon;
		out->tan_v[2] =   b * clat;
		vec3_norm(out->tan_v);

		out->pos[0] = sx + h * out->norm[0];
		out->pos[1] = sy + h * out->norm[1];
		out->pos[2] = sz + h * out->norm[2];

		out->ds_u = COROLLA_TWO_PI * a * clat;
		out->ds_v = COROLLA_PI * sqrt(a * a * slat * slat + b * b * clat * clat);
		return 0;
	}

	case COROLLA_CYLINDER: {
		double R = surf->r_major;
		double H = surf->r_minor;
		if (R <= 0.0 || H <= 0.0)
			return -1;

		double lon = COROLLA_TWO_PI * u;
		double clon = cos(lon), slon = sin(lon);

		out->norm[0] = clon;
		out->norm[1] = slon;
		out->norm[2] = 0.0;

		out->tan_u[0] = -slon;
		out->tan_u[1] =  clon;
		out->tan_u[2] =  0.0;

		out->tan_v[0] = 0.0;
		out->tan_v[1] = 0.0;
		out->tan_v[2] = 1.0;

		out->pos[0] = (R + h) * clon;
		out->pos[1] = (R + h) * slon;
		out->pos[2] = (v - 0.5) * H;

		out->ds_u = COROLLA_TWO_PI * R;
		out->ds_v = H;
		return 0;
	}

	case COROLLA_PLANE: {
		double W = surf->r_major;
		double H = surf->r_minor;
		if (W <= 0.0 || H <= 0.0)
			return -1;

		out->norm[0] = 0.0;
		out->norm[1] = 0.0;
		out->norm[2] = 1.0;

		out->tan_u[0] = 1.0;
		out->tan_u[1] = 0.0;
		out->tan_u[2] = 0.0;

		out->tan_v[0] = 0.0;
		out->tan_v[1] = 1.0;
		out->tan_v[2] = 0.0;

		out->pos[0] = (u - 0.5) * W;
		out->pos[1] = (v - 0.5) * H;
		out->pos[2] = h;

		out->ds_u = W;
		out->ds_v = H;
		return 0;
	}

	default:
		return -1;
	}
}

COROLLA_API int
corolla_xyz_to_uvh(const corolla_surface_t *surf,
                   double x, double y, double z,
                   double *out_u, double *out_v, double *out_h)
{
	if (!surf)
		return -1;

	double u = 0.0, v = 0.0, h = 0.0;

	switch (surf->kind) {
	case COROLLA_TORUS: {
		double R = surf->r_major;
		double r0 = surf->r_minor;
		if (R <= 0.0 || r0 <= 0.0)
			return -1;

		double phi = atan2(y, x);
		if (phi < 0.0)
			phi += COROLLA_TWO_PI;
		u = wrap_unit(phi / COROLLA_TWO_PI);

		double rho = hypot(x, y);
		double dr = rho - R;
		double theta = atan2(z, dr);
		v = wrap_unit((theta / COROLLA_TWO_PI) + 0.5);
		h = hypot(dr, z) - r0;
		break;
	}

	case COROLLA_SPHERE: {
		double R = surf->r_major;
		if (R <= 0.0)
			return -1;
		double lon = atan2(y, x);
		if (lon < 0.0)
			lon += COROLLA_TWO_PI;
		u = wrap_unit(lon / COROLLA_TWO_PI);

		double dist = sqrt(x * x + y * y + z * z);
		double lat = (dist > 0.0) ? asin(z / dist) : 0.0;
		v = (lat / COROLLA_PI) + 0.5;
		if (v < 0.0) v = 0.0;
		if (v >= 1.0) v = 1.0 - DBL_EPSILON;
		h = dist - R;
		break;
	}

	case COROLLA_CYLINDER: {
		double R = surf->r_major;
		double H = surf->r_minor;
		if (R <= 0.0 || H <= 0.0)
			return -1;
		double lon = atan2(y, x);
		if (lon < 0.0)
			lon += COROLLA_TWO_PI;
		u = wrap_unit(lon / COROLLA_TWO_PI);
		v = wrap_unit((z / H) + 0.5);
		h = hypot(x, y) - R;
		break;
	}

	case COROLLA_PLANE: {
		double W = surf->r_major;
		double H = surf->r_minor;
		if (W <= 0.0 || H <= 0.0)
			return -1;
		u = wrap_unit((x / W) + 0.5);
		v = wrap_unit((y / H) + 0.5);
		h = z;
		break;
	}

	default:
		return -1;
	}

	if (out_u) *out_u = u;
	if (out_v) *out_v = v;
	if (out_h) *out_h = h;
	return 0;
}

/* --- Polynomial Root Solvers for Ray-Torus Quartic ----------------------- */

static int
solve_quadratic(double a, double b, double c, double roots[2])
{
	if (fabs(a) < 1e-14) {
		if (fabs(b) < 1e-14)
			return 0;
		roots[0] = -c / b;
		return 1;
	}
	double disc = b * b - 4.0 * a * c;
	if (disc < 0.0)
		return 0;
	if (disc == 0.0) {
		roots[0] = -0.5 * b / a;
		return 1;
	}
	double s = sqrt(disc);
	double q = (b >= 0.0) ? -0.5 * (b + s) : -0.5 * (b - s);
	double r0 = q / a;
	double r1 = c / q;
	if (r0 <= r1) {
		roots[0] = r0;
		roots[1] = r1;
	} else {
		roots[0] = r1;
		roots[1] = r0;
	}
	return 2;
}

/* Solve monic cubic x^3 + a*x^2 + b*x + c = 0. Returns 1 or 3 real roots. */
static int
solve_cubic_monic(double a, double b, double c, double roots[3])
{
	double a2 = a * a;
	double Q = (a2 - 3.0 * b) / 9.0;
	double R = (2.0 * a2 * a - 9.0 * a * b + 27.0 * c) / 54.0;
	double Q3 = Q * Q * Q;
	double R2 = R * R;
	double shift = a / 3.0;

	if (R2 < Q3) {
		double sqQ = sqrt(Q);
		double ratio = R / (Q * sqQ);
		if (ratio < -1.0) ratio = -1.0;
		if (ratio >  1.0) ratio =  1.0;
		double theta = acos(ratio);
		roots[0] = -2.0 * sqQ * cos(theta / 3.0) - shift;
		roots[1] = -2.0 * sqQ * cos((theta + COROLLA_TWO_PI) / 3.0) - shift;
		roots[2] = -2.0 * sqQ * cos((theta - COROLLA_TWO_PI) / 3.0) - shift;
		return 3;
	} else {
		double sgn = (R >= 0.0) ? 1.0 : -1.0;
		double A = -sgn * cbrt(fabs(R) + sqrt(R2 - Q3));
		double B = (fabs(A) > 1e-14) ? (Q / A) : 0.0;
		roots[0] = (A + B) - shift;
		return 1;
	}
}

/* Solve monic quartic x^4 + A*x^3 + B*x^2 + C*x + D = 0 using Ferrari's method
 * followed by Newton-Raphson polishing on the depressed quartic. */
static int
solve_quartic_monic(double A, double B, double C, double D, double roots[4])
{
	/* Depress quartic: x = y - A/4 -> y^4 + p*y^2 + q*y + r = 0 */
	double A2 = A * A;
	double p = B - 0.375 * A2;
	double q = C - 0.5 * A * B + 0.125 * A2 * A;
	double r = D - 0.25 * A * C + 0.0625 * A2 * B - (3.0 / 256.0) * A2 * A2;
	double shift = 0.25 * A;

	int n = 0;

	if (fabs(q) < 1e-12) {
		/* Biquadratic: (y^2)^2 + p*(y^2) + r = 0 */
		double z_roots[2];
		int nz = solve_quadratic(1.0, p, r, z_roots);
		for (int i = 0; i < nz; i++) {
			if (z_roots[i] >= 0.0) {
				double sy = sqrt(z_roots[i]);
				roots[n++] = -sy - shift;
				if (sy > 0.0)
					roots[n++] = sy - shift;
			}
		}
	} else {
		/* Resolvent cubic for m = y^2 factor:
		 * z^3 + 2*p*z^2 + (p^2 - 4*r)*z - q^2 = 0 */
		double c_roots[3];
		int nc = solve_cubic_monic(2.0 * p, p * p - 4.0 * r, -q * q, c_roots);

		/* Pick largest positive root z > 0 for numerical stability */
		double z = -1.0;
		for (int i = 0; i < nc; i++) {
			if (c_roots[i] > z)
				z = c_roots[i];
		}

		if (z > 0.0) {
			double s = sqrt(z);
			double d1 = 0.5 * (p + z - q / s);
			double d2 = 0.5 * (p + z + q / s);
			double r1[2], r2[2];
			int n1 = solve_quadratic(1.0,  s, d1, r1);
			int n2 = solve_quadratic(1.0, -s, d2, r2);
			for (int i = 0; i < n1; i++)
				roots[n++] = r1[i] - shift;
			for (int i = 0; i < n2; i++)
				roots[n++] = r2[i] - shift;
		}
	}

	/* Polish each root with 3 Newton-Raphson iterations on the original monic quartic
	 * and sort in ascending order */
	for (int i = 0; i < n; i++) {
		double x = roots[i];
		for (int it = 0; it < 3; it++) {
			double f  = (((x + A) * x + B) * x + C) * x + D;
			double df = ((4.0 * x + 3.0 * A) * x + 2.0 * B) * x + C;
			if (fabs(df) > 1e-12)
				x -= f / df;
		}
		roots[i] = x;
	}

	for (int i = 0; i < n - 1; i++) {
		for (int j = i + 1; j < n; j++) {
			if (roots[j] < roots[i]) {
				double tmp = roots[i];
				roots[i] = roots[j];
				roots[j] = tmp;
			}
		}
	}
	return n;
}

COROLLA_API int
corolla_ray_hit(const corolla_surface_t *surf,
                const double orig[3], const double dir[3],
                double t_min, double t_max,
                corolla_hit_t *out)
{
	if (!surf || !orig || !dir)
		return 0;

	corolla_hit_t local_out;
	if (!out)
		out = &local_out;
	out->hit = 0;

	double d2 = vec3_dot(dir, dir);
	if (d2 < 1e-20)
		return 0;

	double best_t = -1.0;

	switch (surf->kind) {
	case COROLLA_TORUS: {
		double R = surf->r_major;
		double r0 = surf->r_minor;
		if (R <= 0.0 || r0 <= 0.0)
			return 0;

		/* Scale coordinates by 1/R to keep quartic coefficients near O(1) */
		double invR = 1.0 / R;
		double ox = orig[0] * invR;
		double oy = orig[1] * invR;
		double oz = orig[2] * invR;

		double inv_d = 1.0 / sqrt(d2);
		double dx = dir[0] * inv_d;
		double dy = dir[1] * inv_d;
		double dz = dir[2] * inv_d;

		/* Along ray, shift origin to closest point to (0,0,0) to prevent catastrophic
		 * cancellation when ray originates far from the torus */
		double t_shift_scaled = -(ox * dx + oy * dy + oz * dz);
		ox += t_shift_scaled * dx;
		oy += t_shift_scaled * dy;
		oz += t_shift_scaled * dz;

		double r_scaled = r0 * invR;
		double R2 = 1.0;
		double r2 = r_scaled * r_scaled;

		/* Quick bounding sphere rejection around shifted origin */
		double b_rad = 1.0 + r_scaled;
		double dist2_closest = ox * ox + oy * oy + oz * oz;
		if (dist2_closest > b_rad * b_rad + 1e-9)
			return 0;

		/* Quartic for unit direction d = (dx, dy, dz) with ox*dx + oy*dy + oz*dz = 0:
		 * (|o + s*d|^2 + R^2 - r^2)^2 = 4*R^2 * ((ox + s*dx)^2 + (oy + s*dy)^2) */
		double od = ox * dx + oy * dy + oz * dz; /* ~0 after shift */
		double K  = (ox * ox + oy * oy + oz * oz) - (R2 + r2);

		double A = 4.0 * od;
		double B = 2.0 * K + 4.0 * od * od + 4.0 * R2 * dz * dz;
		double C = 4.0 * K * od + 8.0 * R2 * oz * dz;
		double D = K * K - 4.0 * R2 * (r2 - oz * oz);

		double s_roots[4];
		int n_roots = solve_quartic_monic(A, B, C, D, s_roots);

		for (int i = 0; i < n_roots; i++) {
			/* Convert scaled arc length s back to caller ray parameter t */
			double s_unshifted = s_roots[i] + t_shift_scaled;
			double t = (s_unshifted * R) * inv_d;
			if (t >= t_min && t <= t_max) {
				/* Verify residual against true torus equation */
				double px = orig[0] + t * dir[0];
				double py = orig[1] + t * dir[1];
				double pz = orig[2] + t * dir[2];
				double dr = hypot(px, py) - R;
				double err = fabs(hypot(dr, pz) - r0);
				if (err <= 1e-4 * r0 + 1e-6) {
					best_t = t;
					break;
				}
			}
		}
		break;
	}

	case COROLLA_SPHERE: {
		double a = surf->r_major;
		double b = (surf->r_minor > 0.0) ? surf->r_minor : a;
		if (a <= 0.0)
			return 0;

		double qa = (dir[0] * dir[0] + dir[1] * dir[1]) / (a * a) + (dir[2] * dir[2]) / (b * b);
		double qb = 2.0 * ((orig[0] * dir[0] + orig[1] * dir[1]) / (a * a) + (orig[2] * dir[2]) / (b * b));
		double qc = (orig[0] * orig[0] + orig[1] * orig[1]) / (a * a) + (orig[2] * orig[2]) / (b * b) - 1.0;

		double roots[2];
		int n = solve_quadratic(qa, qb, qc, roots);
		for (int i = 0; i < n; i++) {
			if (roots[i] >= t_min && roots[i] <= t_max) {
				best_t = roots[i];
				break;
			}
		}
		break;
	}

	case COROLLA_CYLINDER: {
		double R = surf->r_major;
		double H = surf->r_minor;
		if (R <= 0.0 || H <= 0.0)
			return 0;

		double qa = dir[0] * dir[0] + dir[1] * dir[1];
		double qb = 2.0 * (orig[0] * dir[0] + orig[1] * dir[1]);
		double qc = orig[0] * orig[0] + orig[1] * orig[1] - R * R;

		double roots[2];
		int n = solve_quadratic(qa, qb, qc, roots);
		for (int i = 0; i < n; i++) {
			double t = roots[i];
			if (t >= t_min && t <= t_max) {
				double z = orig[2] + t * dir[2];
				if (z >= -0.5 * H && z <= 0.5 * H) {
					best_t = t;
					break;
				}
			}
		}
		break;
	}

	case COROLLA_PLANE: {
		double W = surf->r_major;
		double H = surf->r_minor;
		if (W <= 0.0 || H <= 0.0 || fabs(dir[2]) < 1e-14)
			return 0;

		double t = -orig[2] / dir[2];
		if (t >= t_min && t <= t_max) {
			double x = orig[0] + t * dir[0];
			double y = orig[1] + t * dir[1];
			if (x >= -0.5 * W && x <= 0.5 * W && y >= -0.5 * H && y <= 0.5 * H)
				best_t = t;
		}
		break;
	}

	default:
		return 0;
	}

	if (best_t < 0.0)
		return 0;

	out->hit = 1;
	out->t = best_t;
	out->pos[0] = orig[0] + best_t * dir[0];
	out->pos[1] = orig[1] + best_t * dir[1];
	out->pos[2] = orig[2] + best_t * dir[2];

	double u = 0.0, v = 0.0;
	corolla_xyz_to_uvh(surf, out->pos[0], out->pos[1], out->pos[2], &u, &v, NULL);
	out->u = u;
	out->v = v;

	corolla_frame_t fr;
	if (corolla_eval_frame(surf, u, v, 0.0, &fr) == 0) {
		out->norm[0] = fr.norm[0];
		out->norm[1] = fr.norm[1];
		out->norm[2] = fr.norm[2];
	}
	return 1;
}

COROLLA_API int
corolla_occluded(const corolla_surface_t *surf,
                 double u, double v, double h,
                 const double dir_to_light[3])
{
	if (!surf || !dir_to_light)
		return 1;

	corolla_frame_t fr;
	if (corolla_eval_frame(surf, u, v, h, &fr) != 0)
		return 1;

	/* Self-horizon check: if light is below local tangent plane, point is in night */
	double ndotl = vec3_dot(fr.norm, dir_to_light);
	if (ndotl <= 1e-9)
		return 1;

	/* Convex manifolds (Sphere, Plane) cannot cast shadows onto their own exterior */
	if (surf->kind == COROLLA_SPHERE || surf->kind == COROLLA_PLANE)
		return 0;

	/* For non-convex manifolds (Torus, Cylinder), trace ray toward light to test
	 * whether another part of the manifold (e.g., torus outer ring) blocks the light */
	double scale = (surf->r_major > surf->r_minor) ? surf->r_major : surf->r_minor;
	double t_eps = 1e-4 * scale;
	double t_max = 4.0 * (surf->r_major + surf->r_minor + fabs(h));

	corolla_hit_t hit;
	if (corolla_ray_hit(surf, fr.pos, dir_to_light, t_eps, t_max, &hit))
		return 1;

	return 0;
}

COROLLA_API void
corolla_sun_dir(double axial_tilt_rad,
                double orbit_angle_rad,
                double spin_angle_rad,
                double out_sun_dir[3])
{
	if (!out_sun_dir)
		return;

	/* Solar declination delta satisfies sin(delta) = sin(epsilon) * sin(tau) */
	double sin_delta = sin(axial_tilt_rad) * sin(orbit_angle_rad);
	if (sin_delta >  1.0) sin_delta =  1.0;
	if (sin_delta < -1.0) sin_delta = -1.0;
	double cos_delta = sqrt(1.0 - sin_delta * sin_delta);

	/* Hour angle in body-fixed frame as body rotates by spin_angle_rad */
	double hour_angle = orbit_angle_rad - spin_angle_rad;
	out_sun_dir[0] = cos(hour_angle) * cos_delta;
	out_sun_dir[1] = sin(hour_angle) * cos_delta;
	out_sun_dir[2] = sin_delta;
}

COROLLA_API double
corolla_insolation(const corolla_surface_t *surf,
                   double u, double v,
                   const double sun_dir[3])
{
	if (!surf || !sun_dir)
		return 0.0;

	corolla_frame_t fr;
	if (corolla_eval_frame(surf, u, v, 0.0, &fr) != 0)
		return 0.0;

	double s[3] = { sun_dir[0], sun_dir[1], sun_dir[2] };
	vec3_norm(s);

	double cos_inc = vec3_dot(fr.norm, s);
	if (cos_inc <= 1e-9)
		return 0.0;

	if (surf->kind == COROLLA_TORUS || surf->kind == COROLLA_CYLINDER) {
		double scale = (surf->r_major > surf->r_minor) ? surf->r_major : surf->r_minor;
		double t_eps = 1e-4 * scale;
		double t_max = 4.0 * (surf->r_major + surf->r_minor);
		if (corolla_ray_hit(surf, fr.pos, s, t_eps, t_max, NULL))
			return 0.0;
	}

	return cos_inc;
}

COROLLA_API double
corolla_annual_insolation(const corolla_surface_t *surf,
                          double u, double v,
                          double axial_tilt_rad,
                          int season_samples,
                          int day_samples)
{
	if (!surf)
		return 0.0;

	if (season_samples <= 0)
		season_samples = 16;
	if (day_samples <= 0)
		day_samples = 16;

	double total = 0.0;
	int count = season_samples * day_samples;

	for (int is = 0; is < season_samples; is++) {
		double tau = COROLLA_TWO_PI * ((double)is + 0.5) / (double)season_samples;
		for (int id = 0; id < day_samples; id++) {
			double phi = COROLLA_TWO_PI * ((double)id + 0.5) / (double)day_samples;
			double sun_dir[3];
			corolla_sun_dir(axial_tilt_rad, tau, phi, sun_dir);
			total += corolla_insolation(surf, u, v, sun_dir);
		}
	}

	return total / (double)count;
}

COROLLA_API int
corolla_orbit_pos(const corolla_orbit_t *orb,
                  double t,
                  double out_pos[3])
{
	if (!orb || !out_pos || orb->semi_major_axis <= 0.0 || orb->period <= 0.0)
		return -1;

	double e = orb->eccentricity;
	if (e < 0.0 || e >= 1.0)
		return -1;

	double M = orb->mean_anomaly_0 + COROLLA_TWO_PI * (t / orb->period);
	M = fmod(M, COROLLA_TWO_PI);
	if (M < 0.0)
		M += COROLLA_TWO_PI;

	/* Solve Kepler's equation E - e * sin(E) = M via Newton-Raphson */
	double E = M;
	for (int it = 0; it < 8; it++) {
		double f  = E - e * sin(E) - M;
		double df = 1.0 - e * cos(E);
		E -= f / df;
	}

	/* Orbital plane coordinates (x_orb along periapsis, y_orb perpendicular) */
	double a = orb->semi_major_axis;
	double x_orb = a * (cos(E) - e);
	double y_orb = a * sqrt(1.0 - e * e) * sin(E);

	/* Rotate by argument of periapsis (omega), inclination (i), and ascending node (Omega) */
	double cw = cos(orb->arg_periapsis_rad), sw = sin(orb->arg_periapsis_rad);
	double ci = cos(orb->inclination_rad),   si = sin(orb->inclination_rad);
	double cO = cos(orb->lon_asc_node_rad),  sO = sin(orb->lon_asc_node_rad);

	/* Position in orbital plane rotated by omega */
	double x1 = cw * x_orb - sw * y_orb;
	double y1 = sw * x_orb + cw * y_orb;

	/* Tilt by inclination i and rotate by ascending node Omega */
	out_pos[0] = cO * x1 - sO * ci * y1;
	out_pos[1] = sO * x1 + cO * ci * y1;
	out_pos[2] = si * y1;

	return 0;
}

COROLLA_API int
corolla_eval_body_pose(const corolla_body_t *body,
                       const double parent_pos[3],
                       double t,
                       corolla_pose_t *out)
{
	if (!body || !out)
		return -1;

	double orb_pos[3] = { 0.0, 0.0, 0.0 };
	double orb_angle = 0.0;
	if (body->orbit.semi_major_axis > 0.0 && body->orbit.period > 0.0) {
		if (corolla_orbit_pos(&body->orbit, t, orb_pos) == 0) {
			orb_angle = atan2(orb_pos[1], orb_pos[0]);
			if (orb_angle < 0.0)
				orb_angle += COROLLA_TWO_PI;
		}
	} else if (body->orbit.period > 0.0) {
		orb_angle = body->orbit.mean_anomaly_0 + COROLLA_TWO_PI * (t / body->orbit.period);
		orb_angle = fmod(orb_angle, COROLLA_TWO_PI);
		if (orb_angle < 0.0)
			orb_angle += COROLLA_TWO_PI;
	}

	out->center[0] = (parent_pos ? parent_pos[0] : 0.0) + orb_pos[0];
	out->center[1] = (parent_pos ? parent_pos[1] : 0.0) + orb_pos[1];
	out->center[2] = (parent_pos ? parent_pos[2] : 0.0) + orb_pos[2];
	out->orbit_angle = orb_angle;

	double phi = body->spin_phase_0;
	if (fabs(body->spin_period) > 1e-12)
		phi += COROLLA_TWO_PI * (t / body->spin_period);
	phi = fmod(phi, COROLLA_TWO_PI);
	if (phi < 0.0)
		phi += COROLLA_TWO_PI;
	out->spin_angle = phi;

	/* Build orthonormal rotation matrix R = R_z(alpha) * R_x(-eps) * R_z(phi):
	 *   R_x(-eps) = [ 1,     0,      0    ]
	 *               [ 0,  cos(e),  sin(e) ]
	 *               [ 0, -sin(e),  cos(e) ]
	 *   R_z(alpha) * R_x(-eps) =
	 *     [ cos(a), -sin(a)*cos(e), -sin(a)*sin(e) ]
	 *     [ sin(a),  cos(a)*cos(e),  cos(a)*sin(e) ]
	 *     [   0,        -sin(e),         cos(e)    ]
	 * Multiplying on the right by R_z(phi) yields the 3x3 body-to-inertial matrix.
	 * At alpha = 0, a star direction s_inert = (cos(tau), sin(tau), 0) transforms to
	 * s_body = R^T * s_inert = R_z(-phi) * (cos(tau), cos(e)*sin(tau), -sin(e)*sin(tau)),
	 * matching corolla_sun_dir's seasonal declination geometry. */
	double ca = cos(body->axis_azimuth_rad), sa = sin(body->axis_azimuth_rad);
	double ce = cos(body->axial_tilt_rad),   se = sin(body->axial_tilt_rad);
	double cp = cos(phi),                    sp = sin(phi);

	/* Tilt matrix T = R_z(alpha) * R_x(-eps) */
	double t00 =  ca, t01 = -sa * ce, t02 = -sa * se;
	double t10 =  sa, t11 =  ca * ce, t12 =  ca * se;
	double t20 = 0.0, t21 = -se,      t22 =  ce;

	/* R = T * R_z(phi) */
	out->rot[0] = t00 * cp + t01 * sp;
	out->rot[1] = -t00 * sp + t01 * cp;
	out->rot[2] = t02;

	out->rot[3] = t10 * cp + t11 * sp;
	out->rot[4] = -t10 * sp + t11 * cp;
	out->rot[5] = t12;

	out->rot[6] = t20 * cp + t21 * sp;
	out->rot[7] = -t20 * sp + t21 * cp;
	out->rot[8] = t22;

	/* inv_rot = R^T */
	out->inv_rot[0] = out->rot[0];
	out->inv_rot[1] = out->rot[3];
	out->inv_rot[2] = out->rot[6];
	out->inv_rot[3] = out->rot[1];
	out->inv_rot[4] = out->rot[4];
	out->inv_rot[5] = out->rot[7];
	out->inv_rot[6] = out->rot[2];
	out->inv_rot[7] = out->rot[5];
	out->inv_rot[8] = out->rot[8];

	return 0;
}

COROLLA_API void
corolla_pose_vec_to_inertial(const corolla_pose_t *pose,
                             const double v_body[3],
                             double out_inertial[3])
{
	if (!v_body || !out_inertial)
		return;
	double x = v_body[0], y = v_body[1], z = v_body[2];
	if (!pose) {
		out_inertial[0] = x;
		out_inertial[1] = y;
		out_inertial[2] = z;
		return;
	}
	out_inertial[0] = pose->rot[0] * x + pose->rot[1] * y + pose->rot[2] * z;
	out_inertial[1] = pose->rot[3] * x + pose->rot[4] * y + pose->rot[5] * z;
	out_inertial[2] = pose->rot[6] * x + pose->rot[7] * y + pose->rot[8] * z;
}

COROLLA_API void
corolla_pose_vec_to_body(const corolla_pose_t *pose,
                         const double v_inertial[3],
                         double out_body[3])
{
	if (!v_inertial || !out_body)
		return;
	double x = v_inertial[0], y = v_inertial[1], z = v_inertial[2];
	if (!pose) {
		out_body[0] = x;
		out_body[1] = y;
		out_body[2] = z;
		return;
	}
	out_body[0] = pose->inv_rot[0] * x + pose->inv_rot[1] * y + pose->inv_rot[2] * z;
	out_body[1] = pose->inv_rot[3] * x + pose->inv_rot[4] * y + pose->inv_rot[5] * z;
	out_body[2] = pose->inv_rot[6] * x + pose->inv_rot[7] * y + pose->inv_rot[8] * z;
}

COROLLA_API void
corolla_pose_point_to_inertial(const corolla_pose_t *pose,
                               const double p_body[3],
                               double out_inertial[3])
{
	if (!p_body || !out_inertial)
		return;
	double rot_p[3] = { 0.0, 0.0, 0.0 };
	corolla_pose_vec_to_inertial(pose, p_body, rot_p);
	out_inertial[0] = rot_p[0] + (pose ? pose->center[0] : 0.0);
	out_inertial[1] = rot_p[1] + (pose ? pose->center[1] : 0.0);
	out_inertial[2] = rot_p[2] + (pose ? pose->center[2] : 0.0);
}

COROLLA_API void
corolla_pose_point_to_body(const corolla_pose_t *pose,
                           const double p_inertial[3],
                           double out_body[3])
{
	if (!p_inertial || !out_body)
		return;
	double d[3] = {
		p_inertial[0] - (pose ? pose->center[0] : 0.0),
		p_inertial[1] - (pose ? pose->center[1] : 0.0),
		p_inertial[2] - (pose ? pose->center[2] : 0.0)
	};
	corolla_pose_vec_to_body(pose, d, out_body);
}

COROLLA_API int
corolla_pose_relative(const corolla_pose_t *observer,
                      const double observer_local_origin[3],
                      const corolla_pose_t *target,
                      corolla_pose_t *out_rel)
{
	if (!observer || !target || !out_rel)
		return -1;

	/* Relative center in observer's body frame:
	 * observer->inv_rot * (target->center - observer->center) - observer_local_origin */
	double dc_inert[3] = {
		target->center[0] - observer->center[0],
		target->center[1] - observer->center[1],
		target->center[2] - observer->center[2]
	};
	double dc_obs[3] = { 0.0, 0.0, 0.0 };
	corolla_pose_vec_to_body(observer, dc_inert, dc_obs);
	out_rel->center[0] = dc_obs[0] - (observer_local_origin ? observer_local_origin[0] : 0.0);
	out_rel->center[1] = dc_obs[1] - (observer_local_origin ? observer_local_origin[1] : 0.0);
	out_rel->center[2] = dc_obs[2] - (observer_local_origin ? observer_local_origin[2] : 0.0);

	/* Relative rotation matrix: R_rel = observer->inv_rot * target->rot */
	for (int r = 0; r < 3; r++) {
		for (int c = 0; c < 3; c++) {
			out_rel->rot[r * 3 + c] =
				observer->inv_rot[r * 3 + 0] * target->rot[0 * 3 + c] +
				observer->inv_rot[r * 3 + 1] * target->rot[1 * 3 + c] +
				observer->inv_rot[r * 3 + 2] * target->rot[2 * 3 + c];
		}
	}
	for (int r = 0; r < 3; r++) {
		for (int c = 0; c < 3; c++) {
			out_rel->inv_rot[r * 3 + c] = out_rel->rot[c * 3 + r];
		}
	}
	out_rel->spin_angle = target->spin_angle;
	out_rel->orbit_angle = target->orbit_angle;
	return 0;
}

static void
aabb_accum_surf_pt(const corolla_surface_t *surf,
                   const corolla_pose_t *pose,
                   const double ref_origin[3],
                   double u, double v, double h,
                   double out_min[3], double out_max[3])
{
	corolla_frame_t fr;
	if (corolla_eval_frame(surf, u, v, h, &fr) != 0)
		return;
	double pt[3];
	if (pose) {
		corolla_pose_point_to_inertial(pose, fr.pos, pt);
	} else {
		pt[0] = fr.pos[0];
		pt[1] = fr.pos[1];
		pt[2] = fr.pos[2];
	}
	if (ref_origin) {
		pt[0] -= ref_origin[0];
		pt[1] -= ref_origin[1];
		pt[2] -= ref_origin[2];
	}
	for (int k = 0; k < 3; k++) {
		if (pt[k] < out_min[k]) out_min[k] = pt[k];
		if (pt[k] > out_max[k]) out_max[k] = pt[k];
	}
}

/* Collect up to 12 sample coordinates in [a0, a0 + da]: endpoints, midpoint, quarter-integer
 * multiples k * 0.25, and rotated-axis critical angles atan2(by, bx) / (2*pi) + 0.5*m. */
static int
collect_angle_extrema(double a0, double da, double phase_offset,
                      const corolla_pose_t *pose, int is_poloidal,
                      double out_vals[12])
{
	int n = 0;
	out_vals[n++] = a0;
	if (da <= 0.0)
		return n;

	out_vals[n++] = a0 + da;
	out_vals[n++] = a0 + 0.5 * da;

	if (da >= 1.0) {
		out_vals[n++] = a0 + 0.25;
		out_vals[n++] = a0 + 0.50;
		out_vals[n++] = a0 + 0.75;
	} else {
		double k_start = floor(a0 * 4.0);
		for (int i = 0; i <= 5; i++) {
			double val = (k_start + (double)i) * 0.25;
			if (val > a0 + 1e-12 && val < a0 + da - 1e-12 && n < 12)
				out_vals[n++] = val;
		}
	}

	if (pose && !is_poloidal) {
		/* For each inertial axis r in 0..2, X_r depends on (rot[r,0]*cos(phi) + rot[r,1]*sin(phi)),
		 * which has extrema at phi = atan2(rot[r,1], rot[r,0]) + m*pi. */
		for (int r = 0; r < 3; r++) {
			double bx = pose->rot[r * 3 + 0];
			double by = pose->rot[r * 3 + 1];
			if (hypot(bx, by) > 1e-12) {
				double u_crit = atan2(by, bx) / COROLLA_TWO_PI - phase_offset;
				u_crit = wrap_unit(u_crit);
				double base_k = floor(a0);
				for (int m = -1; m <= 2; m++) {
					for (int half = 0; half < 2; half++) {
						double cand = base_k + (double)m + u_crit + 0.5 * (double)half;
						if (cand > a0 + 1e-12 && cand < a0 + da - 1e-12 && n < 12)
							out_vals[n++] = cand;
					}
				}
			}
		}
	}
	return n;
}

COROLLA_API int
corolla_patch_aabb(const corolla_surface_t *surf,
                   const corolla_pose_t *pose,
                   const double ref_origin[3],
                   double u0, double v0,
                   double du, double dv,
                   double h_min, double h_max,
                   double out_min[3],
                   double out_max[3])
{
	if (!surf || !out_min || !out_max || du < 0.0 || dv < 0.0)
		return -1;

	if (h_max < h_min) {
		double tmp = h_min;
		h_min = h_max;
		h_max = tmp;
	}

	out_min[0] = out_min[1] = out_min[2] =  DBL_MAX;
	out_max[0] = out_max[1] = out_max[2] = -DBL_MAX;

	double u_samples[12], v_samples[12];
	int nu = collect_angle_extrema(u0, du, 0.0, pose, 0, u_samples);
	int nv = collect_angle_extrema(v0, dv, 0.5, pose, 1, v_samples);

	for (int iu = 0; iu < nu; iu++) {
		for (int iv = 0; iv < nv; iv++) {
			aabb_accum_surf_pt(surf, pose, ref_origin,
			                   u_samples[iu], v_samples[iv], h_min,
			                   out_min, out_max);
			if (h_max > h_min) {
				aabb_accum_surf_pt(surf, pose, ref_origin,
				                   u_samples[iu], v_samples[iv], h_max,
				                   out_min, out_max);
			}
		}
	}

	/* When pose is rotated, poloidal angle theta = 2*pi*(v - 0.5) also couples with rot[r,2]*sin(theta).
	 * Sample 5 evenly-spaced interior points in (u, v) so rotated sub-quadrant patches are tightly bounded. */
	if (pose && surf->kind != COROLLA_PLANE && (du > 0.0 || dv > 0.0)) {
		for (int iu = 1; iu < 4; iu++) {
			double u = u0 + du * (0.25 * (double)iu);
			for (int iv = 1; iv < 4; iv++) {
				double v = v0 + dv * (0.25 * (double)iv);
				aabb_accum_surf_pt(surf, pose, ref_origin, u, v, h_min, out_min, out_max);
				if (h_max > h_min)
					aabb_accum_surf_pt(surf, pose, ref_origin, u, v, h_max, out_min, out_max);
			}
		}
		/* Sub-interval residual sag for 1/4-subdivided patch (d_angle / 8) */
		double dphi = COROLLA_TWO_PI * fmin(du, 0.25) * 0.25;
		double dtheta = COROLLA_TWO_PI * fmin(dv, 0.25) * 0.25;
		double r_max_u = surf->r_major + surf->r_minor + fabs(h_max);
		double r_max_v = surf->r_minor + fabs(h_max);
		double bulge = r_max_u * (1.0 - cos(0.5 * dphi)) + r_max_v * (1.0 - cos(0.5 * dtheta));
		for (int k = 0; k < 3; k++) {
			out_min[k] -= bulge;
			out_max[k] += bulge;
		}
	}

	return 0;
}

/* ============================================================================
 * 4. 1D Annual Insolation LUT, Planetary Wind, Cloud Advection & Whittaker
 *    Climate Transfer Layer (coupled with libchaff)
 * ============================================================================ */

static double
clamp_range(double x, double lo, double hi)
{
	if (x < lo) return lo;
	if (x > hi) return hi;
	return x;
}

COROLLA_API int
corolla_bake_annual_insolation_lut(const corolla_surface_t *surf,
                                   double axial_tilt_rad,
                                   int season_samples,
                                   int day_samples,
                                   double *out_lut,
                                   size_t lut_len)
{
	if (!surf || !out_lut || lut_len == 0)
		return -1;

	for (size_t i = 0; i < lut_len; i++) {
		double v = (double)i / (double)lut_len;
		out_lut[i] = corolla_annual_insolation(surf, 0.0, v,
		                                       axial_tilt_rad,
		                                       season_samples,
		                                       day_samples);
	}
	return 0;
}

COROLLA_API double
corolla_sample_annual_insolation_lut(const double *lut,
                                     size_t lut_len,
                                     double v)
{
	if (!lut || lut_len == 0)
		return 0.0;
	if (lut_len == 1)
		return lut[0];

	v = wrap_unit(v);
	double pos = v * (double)lut_len;
	size_t i0 = (size_t)pos;
	if (i0 >= lut_len)
		i0 = 0;
	size_t i1 = (i0 + 1) % lut_len;
	double frac = pos - (double)i0;
	return lut[i0] + (lut[i1] - lut[i0]) * frac;
}

COROLLA_API int
corolla_climate_model_init(corolla_climate_model_t *model,
                           const corolla_surface_t *surf,
                           double axial_tilt_rad,
                           double spin_rate,
                           double temp_min,
                           double temp_max,
                           double lapse_rate,
                           double sea_level,
                           double rain_max,
                           uint32_t seed)
{
	if (!model || !surf)
		return -1;

	memset(model, 0, sizeof(*model));
	model->surf = *surf;
	model->axial_tilt_rad = axial_tilt_rad;
	model->spin_rate = (fabs(spin_rate) > 1e-9) ? spin_rate : 1.0;
	model->temp_min = temp_min;
	model->temp_max = temp_max;
	model->lapse_rate = lapse_rate;
	model->sea_level = sea_level;
	model->rain_max = (rain_max > 0.0) ? rain_max : 512.0;
	model->seed = seed;

	if (corolla_bake_annual_insolation_lut(surf, axial_tilt_rad, 16, 16,
	                                       model->annual_insol_lut,
	                                       COROLLA_LUT_DEFAULT_LEN) == 0) {
		model->lut_valid = 1;
	}
	return 0;
}

COROLLA_API void
corolla_whittaker_classify(double mean_temp,
                           double mean_moisture,
                           double temp_min,
                           double temp_max,
                           double rain_max,
                           uint8_t *out_primary,
                           uint8_t *out_secondary,
                           uint8_t *out_blend)
{
	if (rain_max <= 0.0)
		rain_max = 512.0;
	double temp_span = temp_max - temp_min;
	if (temp_span <= 0.0)
		temp_span = 138.0;

	double rn_step = rain_max * 0.25;
	double tmp_step = temp_span * 0.25;

	/* 1. Permanent ice sheets & glaciers below temp_min */
	if (mean_temp <= temp_min) {
		uint8_t polar_adj = (mean_moisture < rn_step)
		                  ? COROLLA_BIOME_COLD_DESERT
		                  : COROLLA_BIOME_TUNDRA;
		double d_ice = temp_min - mean_temp;
		if (d_ice < 16.0) {
			double t = (16.0 - d_ice) / 16.0;
			uint8_t b = (uint8_t)(t * t * 128.0 + 0.5);
			if (out_primary)   *out_primary   = COROLLA_BIOME_PERMANENT_ICE;
			if (out_secondary) *out_secondary = polar_adj;
			if (out_blend)     *out_blend     = b;
		} else {
			if (out_primary)   *out_primary   = COROLLA_BIOME_PERMANENT_ICE;
			if (out_secondary) *out_secondary = COROLLA_BIOME_PERMANENT_ICE;
			if (out_blend)     *out_blend     = 0;
		}
		return;
	}

	double m_clamped = clamp_range(mean_moisture, 0.0, rain_max - 1e-6);
	double t_clamped = clamp_range(mean_temp, temp_min, temp_max - 1e-6);

	int rn_bit  = (int)(m_clamped / rn_step);
	int tmp_bit = (int)((t_clamped - temp_min) / tmp_step);
	if (rn_bit < 0)  rn_bit = 0;
	if (rn_bit > 3)  rn_bit = 3;
	if (tmp_bit < 0) tmp_bit = 0;
	if (tmp_bit > 3) tmp_bit = 3;

	uint8_t primary = (uint8_t)(2 + rn_bit + (tmp_bit << 2));

	double rn_frac  = ((m_clamped - (double)rn_bit * rn_step) / rn_step) * 256.0;
	double tmp_frac = (((t_clamped - temp_min) - (double)tmp_bit * tmp_step) / tmp_step) * 256.0;

	double d_rn  = (rn_frac < 128.0)  ? rn_frac  : (256.0 - rn_frac);
	double d_tmp = (tmp_frac < 128.0) ? tmp_frac : (256.0 - tmp_frac);

	uint8_t secondary = primary;
	double min_d = 128.0;

	if (tmp_bit == 0 && tmp_frac < 48.0) {
		secondary = COROLLA_BIOME_PERMANENT_ICE;
		min_d = tmp_frac;
	} else if (d_rn < d_tmp) {
		min_d = d_rn;
		if (rn_frac < 128.0 && rn_bit > 0)
			secondary = (uint8_t)(2 + (rn_bit - 1) + (tmp_bit << 2));
		else if (rn_frac >= 128.0 && rn_bit < 3)
			secondary = (uint8_t)(2 + (rn_bit + 1) + (tmp_bit << 2));
	} else {
		min_d = d_tmp;
		if (tmp_frac < 128.0 && tmp_bit > 0)
			secondary = (uint8_t)(2 + rn_bit + ((tmp_bit - 1) << 2));
		else if (tmp_frac >= 128.0 && tmp_bit < 3)
			secondary = (uint8_t)(2 + rn_bit + ((tmp_bit + 1) << 2));
	}

	uint8_t blend = 0;
	if (min_d < 48.0 && secondary != primary) {
		double t = (48.0 - min_d) / 48.0;
		blend = (uint8_t)(t * t * 128.0 + 0.5);
	}

	if (out_primary)   *out_primary   = primary;
	if (out_secondary) *out_secondary = secondary;
	if (out_blend)     *out_blend     = blend;
}

COROLLA_API int
corolla_eval_wind(const corolla_climate_model_t *model,
                  double u, double v,
                  double grad_u, double grad_v,
                  const double sun_dir[3],
                  double time_ticks,
                  corolla_wind_t *out_wind)
{
	if (!model || !out_wind)
		return -1;

	memset(out_wind, 0, sizeof(*out_wind));

	corolla_frame_t fr;
	if (corolla_eval_frame(&model->surf, u, v, 0.0, &fr) != 0)
		return -1;

	/* 1. Coriolis 3-Cell Atmospheric Circulation from vertical normal component n_z:
	 *    - Equatorial belt (|n_z| < 0.5): Trade Winds blow westward (-spin_rate)
	 *      and converge toward the equator (-n_z).
	 *    - Mid-latitude Ferrel belt (0.5 <= |n_z| < 0.866): Westerlies blow eastward (+spin_rate)
	 *      and poleward.
	 *    - Polar crown belt (|n_z| >= 0.866): Polar Easterlies blow westward (-spin_rate).
	 *    Using cos(3 * asin(n_z)) gives a smooth C-infinity 3-cell zonal profile! */
	double nz = clamp_range(fr.norm[2], -1.0, 1.0);
	double lat_eff = asin(nz);
	double spin = (model->spin_rate >= 0.0) ? 1.0 : -1.0;

	double zonal_u = -spin * 0.75 * cos(3.0 * lat_eff);
	double merid_v = -0.35 * sin(3.0 * lat_eff);

	/* 2. Subsolar Thermal Draft:
	 *    Solar heating at the subsolar point creates a low-pressure thermal draft
	 *    pulling surface air along the tangent plane toward `sun_dir`. */
	double thermal_u = 0.0;
	double thermal_v = 0.0;
	double solar_conv = 0.0;
	if (sun_dir) {
		double s[3] = { sun_dir[0], sun_dir[1], sun_dir[2] };
		vec3_norm(s);
		double ndots = vec3_dot(fr.norm, s);
		if (ndots > 0.0) {
			thermal_u = 0.22 * vec3_dot(fr.tan_u, s);
			thermal_v = 0.22 * vec3_dot(fr.tan_v, s);
			solar_conv = 0.30 * ndots;
		}
	}

	/* 3. Divergence-Free Curl-Noise Eddies via libchaff (`chaff_sample_flow_2d`):
	 *    Map (u, v) to 16-bit periodic lattice coordinates and evaluate streamfunction Psi. */
	int16_t lx = (int16_t)((int32_t)(wrap_unit(u) * 65536.0));
	int16_t ly = (int16_t)((int32_t)(wrap_unit(v) * 65536.0));
	int16_t lt = (int16_t)((int32_t)time_ticks);

	chaff_layer_t default_psi_layers[2] = {
		{ .shift = 10, .vshift = 8, .amp = 96, .seed_mode = CHAFF_SEED_PER_LAYER, .contrast = 256 },
		{ .shift = 7,  .vshift = 8, .amp = 48, .seed_mode = CHAFF_SEED_PER_LAYER, .contrast = 256 }
	};
	chaff_field_t default_psi_field = {
		.seed = model->seed ^ 0x57494E44u,
		.dim = 3,
		.flags = 0,
		.nlayers = 2,
		.layers = default_psi_layers
	};
	const chaff_field_t *psi_f = model->psi_field ? model->psi_field : &default_psi_field;

	chaff_flow_desc_t flow_desc = {
		.base_vx_q8 = (int16_t)((zonal_u + thermal_u) * 256.0),
		.base_vy_q8 = (int16_t)((merid_v + thermal_v) * 256.0),
		.curl_amp_q8 = 96
	};
	chaff_flow_2d_t flow = chaff_sample_flow_2d(psi_f, &flow_desc, lx, ly, lt);

	double wu = (double)flow.vx_q8 / 256.0;
	double wv = (double)flow.vy_q8 / 256.0;

	double eddy_du = wu - (zonal_u + thermal_u);
	double eddy_dv = wv - (merid_v + thermal_v);
	double gust = clamp_range(hypot(eddy_du, eddy_dv) * 2.5, 0.0, 1.0);

	/* 4. Vertical Uplift: ITCZ/Ferrel convergence + Orographic Slope Lift (w . grad_H) */
	double itcz_conv = 0.45 * cos(3.0 * lat_eff); /* + at ITCZ & subpolar front, - in Hadley desert belt */
	double orographic_w = (wu * grad_u + wv * grad_v) * 0.015;

	out_wind->wind_u   = wu;
	out_wind->wind_v   = wv;
	out_wind->uplift_w = itcz_conv + solar_conv + orographic_w;
	out_wind->gust     = gust;
	return 0;
}

COROLLA_API int
corolla_eval_climate(const corolla_climate_model_t *model,
                     double u, double v,
                     double elev,
                     double grad_u, double grad_v,
                     const double sun_dir[3],
                     double time_ticks,
                     corolla_climate_t *out)
{
	if (!model || !out)
		return -1;

	memset(out, 0, sizeof(*out));
	u = wrap_unit(u);
	v = wrap_unit(v);

	corolla_frame_t fr;
	if (corolla_eval_frame(&model->surf, u, v, 0.0, &fr) != 0)
		return -1;

	/* 1. Annual & Instantaneous Insolation from libcorolla */
	double ann_insol = model->lut_valid
	                 ? corolla_sample_annual_insolation_lut(model->annual_insol_lut,
	                                                        COROLLA_LUT_DEFAULT_LEN, v)
	                 : corolla_annual_insolation(&model->surf, u, v,
	                                             model->axial_tilt_rad, 8, 8);
	out->annual_insol = ann_insol;

	double inst_insol = 0.0;
	double signed_sol = 0.0;
	if (sun_dir) {
		double s[3] = { sun_dir[0], sun_dir[1], sun_dir[2] };
		vec3_norm(s);
		double raw_dot = vec3_dot(fr.norm, s);
		inst_insol = corolla_insolation(&model->surf, u, v, s);
		if (raw_dot > 0.0) {
			/* If occluded by opposite torus rim during day, attenuate signed solar */
			signed_sol = (inst_insol > 0.0) ? inst_insol : (raw_dot * 0.25);
		} else {
			signed_sol = raw_dot;
		}
	}
	out->instant_insol = inst_insol;
	out->signed_solar  = signed_sol;

	/* 2. Planetary Wind & Orographic Uplift */
	corolla_eval_wind(model, u, v, grad_u, grad_v, sun_dir, time_ticks, &out->wind);

	/* 3. Metric-Aware Multi-Scale libchaff Climate Fields:
	 *    Scale fine octave weights by metric circumference ratio ds_u / (ds_u_max)
	 *    so narrow regions (torus inner hub or sphere poles) never pinch! */
	int32_t rx = (int32_t)(u * 65536.0);
	int32_t ry = (int32_t)(v * 65536.0);

	double metric_ratio = (fr.ds_v > 1e-6) ? (fr.ds_u / fr.ds_v) : 1.0;
	double narrow_factor = clamp_range(1.0 - (metric_ratio / 2.5), 0.0, 1.0);

	double tm_norm = 0.5;
	if (model->temp_field) {
		chaff_sample_2d_t ts = chaff_sample_grad_2d(model->temp_field, (int16_t)rx, (int16_t)ry);
		tm_norm = clamp_range(0.5 + (double)ts.elevation / 256.0, 0.0, 1.0);
	} else {
		uint32_t tm_macro = chaff_wpoint_periodic_2d(rx, ry, 14, 16, 8, model->seed + 53u);
		uint32_t tm_prov  = chaff_wpoint_periodic_2d(rx, ry, 10, 16, 8, model->seed + 153u);
		uint32_t tm_reg   = chaff_wpoint_periodic_2d(rx, ry, 7,  16, 8, model->seed + 253u);
		double w_m = 0.38 + 0.12 * narrow_factor;
		double w_p = 0.38 + 0.08 * narrow_factor;
		double w_r = 1.0 - (w_m + w_p);
		double raw = ((double)(tm_macro >> 16) * w_m +
		              (double)(tm_prov  >> 16) * w_p +
		              (double)(tm_reg   >> 16) * w_r) / 256.0;
		tm_norm = clamp_range(raw, 0.0, 1.0);
	}

	double cl_norm = 0.5;
	if (model->rain_field) {
		chaff_sample_2d_t rs = chaff_sample_grad_2d(model->rain_field, (int16_t)rx, (int16_t)ry);
		cl_norm = clamp_range(0.5 + (double)rs.elevation / 256.0, 0.0, 1.0);
	} else {
		uint32_t cl_macro = chaff_wpoint_periodic_2d(rx, ry, 14, 16, 8, model->seed + 1u);
		uint32_t cl_prov  = chaff_wpoint_periodic_2d(rx, ry, 10, 16, 8, model->seed + 101u);
		uint32_t cl_reg   = chaff_wpoint_periodic_2d(rx, ry, 7,  16, 8, model->seed + 201u);
		double w_m = 0.25 + 0.15 * narrow_factor;
		double w_p = 0.40 + 0.10 * narrow_factor;
		double w_r = 1.0 - (w_m + w_p);
		double raw = ((double)(cl_macro >> 16) * w_m +
		              (double)(cl_prov  >> 16) * w_p +
		              (double)(cl_reg   >> 16) * w_r) / 256.0;
		cl_norm = clamp_range(0.5 + (raw - 0.5) * 2.0, 0.0, 1.0);
	}

	/* 4. Mean Annual Temperature:
	 *    Driven by annual self-shadowed insolation I_year(v) + meridional heat transport
	 *    (1 - |n_z|) - polar crown ice-albedo feedback - alpine altitude lapse + noise. */
	double nz_abs = fabs(fr.norm[2]);
	/* Normalize annual insolation against peak equatorial value (~1/pi = 0.31831) */
	double insol_norm = clamp_range(ann_insol * COROLLA_PI, 0.0, 1.0);
	double thermal_lat = 0.68 * insol_norm + 0.32 * (1.0 - nz_abs);

	double polar_albedo_cool = 0.0;
	if (nz_abs > 0.85) {
		polar_albedo_cool = ((nz_abs - 0.85) / 0.15) * 38.0;
	}

	double temp_span = model->temp_max - model->temp_min;
	double t_solar = model->temp_min + temp_span * thermal_lat - polar_albedo_cool;
	double elev_rel = elev - model->sea_level;
	double t_lapse = (elev_rel > 180.0) ? ((elev_rel - 180.0) * model->lapse_rate) : 0.0;
	double t_noise = (tm_norm - 0.5) * 32.0;

	out->mean_temp = t_solar - t_lapse + t_noise;

	/* 5. Permanent Annual Moisture:
	 *    Driven by base rainfall noise + 3-cell atmospheric circulation (ITCZ wet,
	 *    Hadley subtropical desert dry, Ferrel mid-latitude wet) + orographic lift/shadow. */
	double rain_max = model->rain_max;
	double moisture = cl_norm * rain_max;

	/* ITCZ equatorial rain belt (|n_z| < 0.27) */
	if (nz_abs < 0.27) {
		moisture += ((0.27 - nz_abs) / 0.27) * (0.166 * rain_max);
	}
	/* Subtropical Hadley descending dry belt (0.28 <= |n_z| <= 0.56, peak at 0.43) */
	if (nz_abs >= 0.28 && nz_abs <= 0.56) {
		double d_had = fabs(nz_abs - 0.42);
		if (d_had < 0.14) {
			double t_had = (0.14 - d_had) / 0.14;
			double desert_target = 0.14 * rain_max + 0.08 * moisture;
			moisture -= (moisture - desert_target) * t_had * 0.78;
		}
	}
	/* Mid-latitude Ferrel cyclonic storm belt (0.56 < |n_z| <= 0.82) */
	if (nz_abs > 0.56 && nz_abs <= 0.82) {
		double d_mid = fabs(nz_abs - 0.69);
		if (d_mid < 0.13 && moisture < 0.82 * rain_max) {
			double t_mid = (0.13 - d_mid) / 0.13;
			moisture += (0.82 * rain_max - moisture) * t_mid * 0.58;
		}
	}
	/* Subpolar / Boreal low-evaporation belt (0.82 < |n_z| <= 0.94) */
	if (nz_abs > 0.82 && nz_abs <= 0.94) {
		double d_bor = fabs(nz_abs - 0.88);
		if (d_bor < 0.06) {
			moisture += ((0.06 - d_bor) / 0.06) * (0.16 * rain_max);
		}
	}

	/* Orographic precipitation boost (windward uplift > 0) & leeward rain shadow (< 0) */
	double oro_slope = out->wind.wind_u * grad_u + out->wind.wind_v * grad_v;
	moisture += oro_slope * (0.22 * rain_max);
	moisture = clamp_range(moisture, 0.0, rain_max);
	out->mean_moisture = moisture;

	/* 6. Diurnal Temperature Swing (moderated by water and humidity) */
	double diurnal_amp = (elev < model->sea_level) ? 13.0 : 15.0;
	if (elev >= model->sea_level && moisture < 0.40 * rain_max) {
		diurnal_amp += (1.0 - moisture / (0.40 * rain_max)) * 3.0;
	}
	out->current_temp = out->mean_temp + signed_sol * diurnal_amp;

	/* 7. Semi-Lagrangian Advected 4D Cloud Cover & Instantaneous Precipitation:
	 *    Back-trace along local surface wind (u - w_u * dt, v - w_v * dt) and sample
	 *    4D spatio-temporal chaff field (`chaff_sample_grad_4d`). */
	double adv_u = wrap_unit(u - out->wind.wind_u * 0.008);
	double adv_v = wrap_unit(v - out->wind.wind_v * 0.008);
	int16_t cx = (int16_t)((int32_t)(adv_u * 65536.0));
	int16_t cy = (int16_t)((int32_t)(adv_v * 65536.0));
	int16_t cz = (int16_t)((int32_t)(elev * 4.0));
	int16_t cw = (int16_t)((int32_t)time_ticks);

	chaff_layer_t default_cloud_layers[2] = {
		{ .shift = 9, .vshift = 8, .amp = 80, .seed_mode = CHAFF_SEED_PER_LAYER, .contrast = 256 },
		{ .shift = 6, .vshift = 8, .amp = 48, .seed_mode = CHAFF_SEED_PER_LAYER, .contrast = 256 }
	};
	chaff_field_t default_cloud_field = {
		.seed = model->seed ^ 0x434C4F55u,
		.dim = 4,
		.flags = 0,
		.nlayers = 2,
		.layers = default_cloud_layers
	};
	const chaff_field_t *cloud_f = model->cloud_field ? model->cloud_field : &default_cloud_field;
	chaff_sample_4d_t cs = chaff_sample_grad_4d(cloud_f, cx, cy, cz, cw);

	double cloud_noise = clamp_range(0.5 + (double)cs.val / 256.0, 0.0, 1.0);
	double humidity_norm = moisture / rain_max;
	double uplift_boost = clamp_range(out->wind.uplift_w * 0.35, -0.30, 0.40);
	double cloud = clamp_range(cloud_noise * 0.65 + humidity_norm * 0.35 + uplift_boost - 0.18, 0.0, 1.0);
	out->cloud_cover = cloud;
	out->precipitation = (cloud > 0.55) ? clamp_range((cloud - 0.55) / 0.45, 0.0, 1.0) : 0.0;

	/* 8. Whittaker Biome Classification */
	if (elev < model->sea_level) {
		out->primary_biome   = COROLLA_BIOME_WATER;
		out->secondary_biome = COROLLA_BIOME_WATER;
		out->biome_blend     = 0;
	} else {
		corolla_whittaker_classify(out->mean_temp, out->mean_moisture,
		                           model->temp_min, model->temp_max,
		                           model->rain_max,
		                           &out->primary_biome,
		                           &out->secondary_biome,
		                           &out->biome_blend);
	}
	return 0;
}

