#include "geo.h"

#include <math.h>

static const double MH_EARTH_RADIUS_KM = 6371.0088;

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
#ifdef MH_HAVE_GEODESIC
        return mh_geodesic_distance_km(a, b, out, err);
#else
        mh_set_error(err, MH_ERR_MISSING_DEP, "native geodesic distance requires GeographicLib");
        return MH_ERR_MISSING_DEP;
#endif
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

#ifndef MH_HAVE_GEODESIC
mh_status mh_geodesic_distance_km(
    const mh_point *a,
    const mh_point *b,
    double *out,
    mh_error_context *err
) {
    (void)a;
    (void)b;
    (void)out;
    mh_set_error(err, MH_ERR_MISSING_DEP, "native geodesic distance requires GeographicLib");
    return MH_ERR_MISSING_DEP;
}
#endif

#ifndef MH_HAVE_GEODESIC
mh_status mh_geodesic_midpoint(
    const mh_point *a,
    const mh_point *b,
    mh_point *out,
    mh_error_context *err
) {
    (void)a;
    (void)b;
    (void)out;
    mh_set_error(err, MH_ERR_MISSING_DEP, "native geodesic midpoint requires GeographicLib");
    return MH_ERR_MISSING_DEP;
}
#endif

#ifndef MH_HAVE_GEODESIC
mh_status mh_geodesic_area_km2(
    const mh_point *polygon,
    size_t n,
    double *out,
    mh_error_context *err
) {
    (void)polygon;
    (void)n;
    (void)out;
    mh_set_error(err, MH_ERR_MISSING_DEP, "native geodesic area requires GeographicLib");
    return MH_ERR_MISSING_DEP;
}
#endif

#ifndef MH_HAVE_GEODESIC
mh_status mh_geodesic_line_point(
    const mh_point *a,
    const mh_point *b,
    double fraction,
    mh_point *out,
    mh_error_context *err
) {
    (void)a;
    (void)b;
    (void)fraction;
    (void)out;
    mh_set_error(err, MH_ERR_MISSING_DEP, "native geodesic line requires GeographicLib");
    return MH_ERR_MISSING_DEP;
}
#endif
