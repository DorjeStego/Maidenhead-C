#include "geo.h"

#include <math.h>

#if defined(__x86_64__) && defined(__AVX2__)
#define MH_HAVE_X86_SIMD 1
#include <immintrin.h>
#endif
#if defined(__aarch64__) && defined(__ARM_NEON)
#define MH_HAVE_NEON_SIMD 1
#include <arm_neon.h>
#endif

/*
 * SIMD geodesic acceleration requires a vector math backend for trig/atan2.
 * Gate the SIMD hooks behind an explicit opt-in compile definition.
 */
#if defined(MH_HAVE_SIMD_MATH) && MH_HAVE_SIMD_MATH
#define MH_SIMD_GEODESIC_ENABLED 1
#else
#define MH_SIMD_GEODESIC_ENABLED 0
#endif

static const double MH_EARTH_RADIUS_KM = 6371.0088;
/* WGS84 ellipsoid parameters for in-built geodesic calculations. */
static const double MH_WGS84_A_M = 6378137.0;
static const double MH_WGS84_F = 1.0 / 298.257223563;
static const double MH_WGS84_B_M = MH_WGS84_A_M * (1.0 - MH_WGS84_F);

static void mh_set_error(mh_error_context *err, mh_status code, const char *message) {
    if (!err) {
        return;
    }
    err->code = code;
    err->message = message;
    err->key = NULL;
    err->value = NULL;
}

static double mh_deg2rad(double deg) {
    return deg * (M_PI / 180.0);
}

static double mh_rad2deg(double rad) {
    return rad * (180.0 / M_PI);
}

static double mh_normalize_lon(double lon) {
    double wrapped = fmod(lon + 180.0, 360.0);
    if (wrapped < 0.0) {
        wrapped += 360.0;
    }
    return wrapped - 180.0;
}

/* Vincenty inverse on WGS84; returns distance (m) and forward azimuth (deg). */
static mh_status mh_vincenty_inverse(
    const mh_point *a,
    const mh_point *b,
    double *distance_m,
    double *azimuth1_deg,
    mh_error_context *err
) {
    if (!a || !b || !distance_m || !azimuth1_deg) {
        mh_set_error(err, MH_ERR_INTERNAL, "output is required");
        return MH_ERR_INTERNAL;
    }

    const double phi1 = mh_deg2rad(a->lat);
    const double phi2 = mh_deg2rad(b->lat);
    const double L = mh_deg2rad(mh_normalize_lon(b->lon - a->lon));

    const double U1 = atan((1.0 - MH_WGS84_F) * tan(phi1));
    const double U2 = atan((1.0 - MH_WGS84_F) * tan(phi2));
    const double sinU1 = sin(U1);
    const double cosU1 = cos(U1);
    const double sinU2 = sin(U2);
    const double cosU2 = cos(U2);

    double lambda = L;
    double lambda_prev = 0.0;
    double sin_lambda = 0.0;
    double cos_lambda = 1.0;
    double sin_sigma = 0.0;
    double cos_sigma = 1.0;
    double sigma = 0.0;
    double sin_alpha = 0.0;
    double cos2_alpha = 1.0;
    double cos2_sigma_m = 0.0;

    /* Iterate to converge lambda. */
    for (int iter = 0; iter < 100; iter++) {
        sin_lambda = sin(lambda);
        cos_lambda = cos(lambda);
        const double t1 = cosU2 * sin_lambda;
        const double t2 = cosU1 * sinU2 - sinU1 * cosU2 * cos_lambda;
        sin_sigma = sqrt(t1 * t1 + t2 * t2);
        if (sin_sigma == 0.0) {
            *distance_m = 0.0;
            *azimuth1_deg = 0.0;
            return MH_OK;
        }
        cos_sigma = sinU1 * sinU2 + cosU1 * cosU2 * cos_lambda;
        sigma = atan2(sin_sigma, cos_sigma);
        sin_alpha = (cosU1 * cosU2 * sin_lambda) / sin_sigma;
        cos2_alpha = 1.0 - sin_alpha * sin_alpha;
        if (cos2_alpha != 0.0) {
            cos2_sigma_m = cos_sigma - (2.0 * sinU1 * sinU2) / cos2_alpha;
        } else {
            /* Equatorial line. */
            cos2_sigma_m = 0.0;
        }
        const double C = (MH_WGS84_F / 16.0) * cos2_alpha * (4.0 + MH_WGS84_F * (4.0 - 3.0 * cos2_alpha));
        lambda_prev = lambda;
        lambda = L + (1.0 - C) * MH_WGS84_F * sin_alpha *
            (sigma + C * sin_sigma * (cos2_sigma_m + C * cos_sigma * (-1.0 + 2.0 * cos2_sigma_m * cos2_sigma_m)));
        if (fabs(lambda - lambda_prev) < 1e-12) {
            break;
        }
        if (iter == 99) {
            mh_set_error(err, MH_ERR_INTERNAL, "geodesic inverse did not converge");
            return MH_ERR_INTERNAL;
        }
    }

    const double u2 = cos2_alpha * ((MH_WGS84_A_M * MH_WGS84_A_M - MH_WGS84_B_M * MH_WGS84_B_M) /
        (MH_WGS84_B_M * MH_WGS84_B_M));
    const double A = 1.0 + (u2 / 16384.0) *
        (4096.0 + u2 * (-768.0 + u2 * (320.0 - 175.0 * u2)));
    const double B = (u2 / 1024.0) *
        (256.0 + u2 * (-128.0 + u2 * (74.0 - 47.0 * u2)));
    const double delta_sigma = B * sin_sigma * (
        cos2_sigma_m + (B / 4.0) * (
            cos_sigma * (-1.0 + 2.0 * cos2_sigma_m * cos2_sigma_m) -
            (B / 6.0) * cos2_sigma_m * (-3.0 + 4.0 * sin_sigma * sin_sigma) *
            (-3.0 + 4.0 * cos2_sigma_m * cos2_sigma_m)
        )
    );

    *distance_m = MH_WGS84_B_M * A * (sigma - delta_sigma);

    const double alpha1 = atan2(
        cosU2 * sin_lambda,
        cosU1 * sinU2 - sinU1 * cosU2 * cos_lambda
    );
    *azimuth1_deg = fmod(mh_rad2deg(alpha1) + 360.0, 360.0);
    return MH_OK;
}

/* Vincenty direct on WGS84; azimuth in deg, distance in m. */
static mh_status mh_vincenty_direct(
    const mh_point *start,
    double azimuth1_deg,
    double distance_m,
    mh_point *out,
    mh_error_context *err
) {
    if (!start || !out) {
        mh_set_error(err, MH_ERR_INTERNAL, "output is required");
        return MH_ERR_INTERNAL;
    }
    if (distance_m < 0.0) {
        mh_set_error(err, MH_ERR_OUT_OF_RANGE, "distance must be >= 0");
        return MH_ERR_OUT_OF_RANGE;
    }

    const double alpha1 = mh_deg2rad(azimuth1_deg);
    const double phi1 = mh_deg2rad(start->lat);
    const double lambda1 = mh_deg2rad(start->lon);

    const double U1 = atan((1.0 - MH_WGS84_F) * tan(phi1));
    const double sinU1 = sin(U1);
    const double cosU1 = cos(U1);
    const double sin_alpha1 = sin(alpha1);
    const double cos_alpha1 = cos(alpha1);

    const double sin_alpha = cosU1 * sin_alpha1;
    const double cos2_alpha = 1.0 - sin_alpha * sin_alpha;
    const double u2 = cos2_alpha * ((MH_WGS84_A_M * MH_WGS84_A_M - MH_WGS84_B_M * MH_WGS84_B_M) /
        (MH_WGS84_B_M * MH_WGS84_B_M));
    const double A = 1.0 + (u2 / 16384.0) *
        (4096.0 + u2 * (-768.0 + u2 * (320.0 - 175.0 * u2)));
    const double B = (u2 / 1024.0) *
        (256.0 + u2 * (-128.0 + u2 * (74.0 - 47.0 * u2)));

    const double sigma1 = atan2(tan(U1), cos_alpha1);

    double sigma = distance_m / (MH_WGS84_B_M * A);
    double sigma_prev = 0.0;
    double cos2_sigma_m = 0.0;
    double sin_sigma = 0.0;
    double cos_sigma = 1.0;
    double delta_sigma = 0.0;

    for (int iter = 0; iter < 100; iter++) {
        cos2_sigma_m = cos(2.0 * sigma1 + sigma);
        sin_sigma = sin(sigma);
        cos_sigma = cos(sigma);
        delta_sigma = B * sin_sigma * (
            cos2_sigma_m + (B / 4.0) * (
                cos_sigma * (-1.0 + 2.0 * cos2_sigma_m * cos2_sigma_m) -
                (B / 6.0) * cos2_sigma_m * (-3.0 + 4.0 * sin_sigma * sin_sigma) *
                (-3.0 + 4.0 * cos2_sigma_m * cos2_sigma_m)
            )
        );
        sigma_prev = sigma;
        sigma = distance_m / (MH_WGS84_B_M * A) + delta_sigma;
        if (fabs(sigma - sigma_prev) < 1e-12) {
            break;
        }
        if (iter == 99) {
            mh_set_error(err, MH_ERR_INTERNAL, "geodesic direct did not converge");
            return MH_ERR_INTERNAL;
        }
    }

    const double tmp = sinU1 * sin_sigma - cosU1 * cos_sigma * cos_alpha1;
    const double phi2 = atan2(
        sinU1 * cos_sigma + cosU1 * sin_sigma * cos_alpha1,
        (1.0 - MH_WGS84_F) * sqrt(sin_alpha * sin_alpha + tmp * tmp)
    );
    const double lambda = atan2(
        sin_sigma * sin_alpha1,
        cosU1 * cos_sigma - sinU1 * sin_sigma * cos_alpha1
    );
    const double C = (MH_WGS84_F / 16.0) * cos2_alpha * (4.0 + MH_WGS84_F * (4.0 - 3.0 * cos2_alpha));
    const double L = lambda - (1.0 - C) * MH_WGS84_F * sin_alpha * (
        sigma + C * sin_sigma * (cos2_sigma_m + C * cos_sigma * (-1.0 + 2.0 * cos2_sigma_m * cos2_sigma_m))
    );

    out->lat = mh_rad2deg(phi2);
    out->lon = mh_normalize_lon(mh_rad2deg(lambda1 + L));
    return MH_OK;
}

/* Approximate authalic radius (km) for WGS84; used for geodesic area fallback. */
static double mh_wgs84_authalic_radius_km(void) {
    const double e2 = MH_WGS84_F * (2.0 - MH_WGS84_F);
    const double e = sqrt(e2);
    const double q_p = (1.0 - e2) * (
        (1.0 / (1.0 - e2)) - (1.0 / (2.0 * e)) * log((1.0 - e) / (1.0 + e))
    );
    const double r2 = 0.5 * MH_WGS84_A_M * MH_WGS84_A_M * q_p;
    return sqrt(r2) / 1000.0;
}

/* Spherical excess area on a sphere of radius r_km. */
static double mh_spherical_polygon_area_km2(const mh_point *poly, size_t n, double r_km) {
    if (!poly || n < 3) {
        return 0.0;
    }
    double total = 0.0;
    for (size_t i = 0; i < n; i++) {
        const mh_point *p1 = &poly[i];
        const mh_point *p2 = &poly[(i + 1) % n];
        const double lat1 = mh_deg2rad(p1->lat);
        const double lat2 = mh_deg2rad(p2->lat);
        const double lon1 = mh_deg2rad(p1->lon);
        const double lon2 = mh_deg2rad(p2->lon);
        const double dlon = lon2 - lon1;
        total += dlon * (2.0 + sin(lat1) + sin(lat2));
    }
    return fabs(total) * (r_km * r_km) / 2.0;
}

mh_status mh_distance_km(
    const mh_point *a,
    const mh_point *b,
    int method,
    double *out,
    mh_error_context *err
) {
    if (!a || !b || !out) {
        mh_set_error(err, MH_ERR_INTERNAL, "output is required");
        return MH_ERR_INTERNAL;
    }
    if (method == MH_DISTANCE_GEODESIC) {
        return mh_geodesic_distance_km(a, b, out, err);
    }

    double lat1 = a->lat;
    double lon1 = a->lon;
    double lat2 = b->lat;
    double lon2 = b->lon;

    double phi1 = mh_deg2rad(lat1);
    double phi2 = mh_deg2rad(lat2);
    double dphi = mh_deg2rad(lat2 - lat1);
    double dlambda = mh_deg2rad(mh_normalize_lon(lon2 - lon1));

    double sin_dphi2 = sin(dphi / 2.0);
    double sin_dlambda2 = sin(dlambda / 2.0);

    double h = sin_dphi2 * sin_dphi2
        + cos(phi1) * cos(phi2) * sin_dlambda2 * sin_dlambda2;
    if (h < 0.0) {
        h = 0.0;
    } else if (h > 1.0) {
        h = 1.0;
    }
    *out = 2.0 * MH_EARTH_RADIUS_KM * asin(sqrt(h));
    return MH_OK;
}

/*
 * SIMD hooks: we keep the structure architecture-aware, but default to scalar
 * unless a SIMD math backend is explicitly enabled.
 */
static int mh_try_geodesic_distance_many_simd(
    const mh_point *a,
    const mh_point *b,
    size_t n,
    double *out_km
) {
#if MH_SIMD_GEODESIC_ENABLED && (defined(MH_HAVE_X86_SIMD) || defined(MH_HAVE_NEON_SIMD))
    /* Placeholder: requires vector trig/atan2 to be worthwhile. */
    (void)a;
    (void)b;
    (void)n;
    (void)out_km;
    return 0;
#else
    (void)a;
    (void)b;
    (void)n;
    (void)out_km;
    return 0;
#endif
}

static int mh_try_geodesic_line_points_many_simd(
    const mh_point *a,
    const mh_point *b,
    const double *fractions,
    size_t n,
    mh_point *out
) {
#if MH_SIMD_GEODESIC_ENABLED && (defined(MH_HAVE_X86_SIMD) || defined(MH_HAVE_NEON_SIMD))
    (void)a;
    (void)b;
    (void)fractions;
    (void)n;
    (void)out;
    return 0;
#else
    (void)a;
    (void)b;
    (void)fractions;
    (void)n;
    (void)out;
    return 0;
#endif
}

mh_status mh_geodesic_distance_many(
    const mh_point *a,
    const mh_point *b,
    size_t n,
    double *out_km,
    mh_error_context *err
) {
    if (!a || !b || !out_km) {
        mh_set_error(err, MH_ERR_INTERNAL, "output is required");
        return MH_ERR_INTERNAL;
    }
    if (n == 0) {
        return MH_OK;
    }

    if (mh_try_geodesic_distance_many_simd(a, b, n, out_km)) {
        return MH_OK;
    }

    for (size_t i = 0; i < n; i++) {
        mh_status st = mh_geodesic_distance_km(&a[i], &b[i], &out_km[i], err);
        if (st != MH_OK) {
            return st;
        }
    }
    return MH_OK;
}

mh_status mh_geodesic_line_points_many(
    const mh_point *a,
    const mh_point *b,
    const double *fractions,
    size_t n,
    mh_point *out,
    mh_error_context *err
) {
    if (!a || !b || !fractions || !out) {
        mh_set_error(err, MH_ERR_INTERNAL, "output is required");
        return MH_ERR_INTERNAL;
    }
    if (n == 0) {
        return MH_OK;
    }

    if (mh_try_geodesic_line_points_many_simd(a, b, fractions, n, out)) {
        return MH_OK;
    }

    /* Scalar fast path: solve the inverse once, then step along the line. */
    double dist_m = 0.0;
    double az1 = 0.0;
    mh_status inv_st = mh_vincenty_inverse(a, b, &dist_m, &az1, err);
    if (inv_st != MH_OK) {
        return inv_st;
    }
    for (size_t i = 0; i < n; i++) {
        double f = fractions[i];
        if (f < 0.0) {
            f = 0.0;
        } else if (f > 1.0) {
            f = 1.0;
        }
        mh_status st = mh_vincenty_direct(a, az1, dist_m * f, &out[i], err);
        if (st != MH_OK) {
            return st;
        }
    }
    return MH_OK;
}

mh_status mh_bearing_deg(
    const mh_point *a,
    const mh_point *b,
    double *out,
    mh_error_context *err
) {
    if (!a || !b || !out) {
        mh_set_error(err, MH_ERR_INTERNAL, "output is required");
        return MH_ERR_INTERNAL;
    }
    double phi1 = mh_deg2rad(a->lat);
    double phi2 = mh_deg2rad(b->lat);
    double lambda1 = mh_deg2rad(a->lon);
    double lambda2 = mh_deg2rad(b->lon);
    double dlambda = lambda2 - lambda1;

    double y = sin(dlambda) * cos(phi2);
    double x = cos(phi1) * sin(phi2) - sin(phi1) * cos(phi2) * cos(dlambda);
    double theta = atan2(y, x);
    double brng = fmod(mh_rad2deg(theta) + 360.0, 360.0);
    *out = brng;
    return MH_OK;
}

mh_status mh_midpoint(
    const mh_point *a,
    const mh_point *b,
    mh_point *out,
    mh_error_context *err
) {
    if (!a || !b || !out) {
        mh_set_error(err, MH_ERR_INTERNAL, "output is required");
        return MH_ERR_INTERNAL;
    }

    double phi1 = mh_deg2rad(a->lat);
    double lambda1 = mh_deg2rad(a->lon);
    double phi2 = mh_deg2rad(b->lat);
    double lambda2 = mh_deg2rad(b->lon);

    double dlambda = lambda2 - lambda1;
    double bx = cos(phi2) * cos(dlambda);
    double by = cos(phi2) * sin(dlambda);

    double phi3 = atan2(
        sin(phi1) + sin(phi2),
        sqrt((cos(phi1) + bx) * (cos(phi1) + bx) + by * by)
    );
    double lambda3 = lambda1 + atan2(by, cos(phi1) + bx);

    out->lat = mh_rad2deg(phi3);
    out->lon = mh_normalize_lon(mh_rad2deg(lambda3));
    return MH_OK;
}

mh_status mh_great_circle_path(
    const mh_point *a,
    const mh_point *b,
    size_t n,
    mh_point *out,
    mh_error_context *err
) {
    if (!a || !b || !out) {
        mh_set_error(err, MH_ERR_INTERNAL, "output is required");
        return MH_ERR_INTERNAL;
    }
    if (n < 2) {
        mh_set_error(err, MH_ERR_OUT_OF_RANGE, "n must be >= 2");
        return MH_ERR_OUT_OF_RANGE;
    }

    double lat1 = a->lat;
    double lon1 = a->lon;
    double lat2 = b->lat;
    double lon2 = b->lon;

    double phi1 = mh_deg2rad(lat1);
    double lambda1 = mh_deg2rad(lon1);
    double phi2 = mh_deg2rad(lat2);
    double lambda2 = mh_deg2rad(lon2);

    double d = 2.0 * asin(sqrt(
        pow(sin((phi2 - phi1) / 2.0), 2.0)
        + cos(phi1) * cos(phi2) * pow(sin((lambda2 - lambda1) / 2.0), 2.0)
    ));

    if (d == 0.0) {
        for (size_t i = 0; i < n; i++) {
            out[i].lat = lat1;
            out[i].lon = lon1;
        }
        return MH_OK;
    }

    for (size_t i = 0; i < n; i++) {
        double f = (double)i / (double)(n - 1);
        double a_coeff = sin((1.0 - f) * d) / sin(d);
        double b_coeff = sin(f * d) / sin(d);
        double x = a_coeff * cos(phi1) * cos(lambda1) + b_coeff * cos(phi2) * cos(lambda2);
        double y = a_coeff * cos(phi1) * sin(lambda1) + b_coeff * cos(phi2) * sin(lambda2);
        double z = a_coeff * sin(phi1) + b_coeff * sin(phi2);
        double lat = atan2(z, sqrt(x * x + y * y));
        double lon = atan2(y, x);
        out[i].lat = mh_rad2deg(lat);
        out[i].lon = mh_normalize_lon(mh_rad2deg(lon));
    }
    return MH_OK;
}

mh_status mh_bearing_bin(
    const mh_point *a,
    const mh_point *b,
    double bin_size,
    double *out,
    mh_error_context *err
) {
    if (!out || !a || !b) {
        mh_set_error(err, MH_ERR_INTERNAL, "output is required");
        return MH_ERR_INTERNAL;
    }
    if (bin_size <= 0.0) {
        mh_set_error(err, MH_ERR_OUT_OF_RANGE, "bin_size must be > 0");
        return MH_ERR_OUT_OF_RANGE;
    }
    double bearing = 0.0;
    mh_status st = mh_bearing_deg(a, b, &bearing, err);
    if (st != MH_OK) {
        return st;
    }
    *out = floor(bearing / bin_size) * bin_size;
    return MH_OK;
}

mh_status mh_azimuthal_sector(
    const mh_point *a,
    const mh_point *b,
    double width_deg,
    double *start,
    double *end,
    mh_error_context *err
) {
    if (!start || !end || !a || !b) {
        mh_set_error(err, MH_ERR_INTERNAL, "output is required");
        return MH_ERR_INTERNAL;
    }
    if (width_deg <= 0.0) {
        mh_set_error(err, MH_ERR_OUT_OF_RANGE, "width_deg must be > 0");
        return MH_ERR_OUT_OF_RANGE;
    }
    double bearing = 0.0;
    mh_status st = mh_bearing_deg(a, b, &bearing, err);
    if (st != MH_OK) {
        return st;
    }
    double half = width_deg / 2.0;
    *start = fmod(bearing - half + 360.0, 360.0);
    *end = fmod(bearing + half, 360.0);
    return MH_OK;
}

mh_status mh_geodesic_distance_km(
    const mh_point *a,
    const mh_point *b,
    double *out,
    mh_error_context *err
) {
    double dist_m = 0.0;
    double az1 = 0.0;
    mh_status st = mh_vincenty_inverse(a, b, &dist_m, &az1, err);
    if (st != MH_OK) {
        return st;
    }
    *out = dist_m / 1000.0;
    return MH_OK;
}

mh_status mh_geodesic_midpoint(
    const mh_point *a,
    const mh_point *b,
    mh_point *out,
    mh_error_context *err
) {
    double dist_m = 0.0;
    double az1 = 0.0;
    mh_status st = mh_vincenty_inverse(a, b, &dist_m, &az1, err);
    if (st != MH_OK) {
        return st;
    }
    return mh_vincenty_direct(a, az1, dist_m * 0.5, out, err);
}

mh_status mh_geodesic_area_km2(
    const mh_point *polygon,
    size_t n,
    double *out,
    mh_error_context *err
) {
    if (!polygon || !out) {
        mh_set_error(err, MH_ERR_INTERNAL, "output is required");
        return MH_ERR_INTERNAL;
    }
    if (n < 3) {
        *out = 0.0;
        return MH_OK;
    }
    const double r_km = mh_wgs84_authalic_radius_km();
    *out = mh_spherical_polygon_area_km2(polygon, n, r_km);
    return MH_OK;
}

mh_status mh_geodesic_line_point(
    const mh_point *a,
    const mh_point *b,
    double fraction,
    mh_point *out,
    mh_error_context *err
) {
    if (!a || !b || !out) {
        mh_set_error(err, MH_ERR_INTERNAL, "output is required");
        return MH_ERR_INTERNAL;
    }
    if (fraction < 0.0) {
        fraction = 0.0;
    } else if (fraction > 1.0) {
        fraction = 1.0;
    }
    double dist_m = 0.0;
    double az1 = 0.0;
    mh_status st = mh_vincenty_inverse(a, b, &dist_m, &az1, err);
    if (st != MH_OK) {
        return st;
    }
    return mh_vincenty_direct(a, az1, dist_m * fraction, out, err);
}
