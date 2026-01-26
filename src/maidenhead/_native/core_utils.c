#include "core_utils.h"

#include <math.h>

#include "constants.h"

double mh_normalize_lon(double lon) {
    double wrapped = fmod(lon + 180.0, 360.0);
    if (wrapped < 0.0) {
        wrapped += 360.0;
    }
    return wrapped - 180.0;
}

double mh_clamp_lat(double lat) {
    if (lat > MH_LAT_MAX_DEG) {
        return MH_LAT_MAX_DEG;
    }
    if (lat < MH_LAT_MIN_DEG) {
        return MH_LAT_MIN_DEG;
    }
    return lat;
}

double mh_wrap_lon_near(double lon, double ref) {
    double ln = mh_normalize_lon(lon);
    double rf = mh_normalize_lon(ref);
    double diff = ln - rf;
    if (diff > 180.0) {
        return ln - 360.0;
    }
    if (diff < -180.0) {
        return ln + 360.0;
    }
    return ln;
}

int mh_lon_overlap(double a_min, double a_max, double b_min, double b_max) {
    if (a_min <= a_max && b_min <= b_max) {
        return !(a_max < b_min || b_max < a_min);
    }
    if (a_min > a_max) {
        return mh_lon_overlap(a_min, MH_LON_MAX_DEG, b_min, b_max)
            || mh_lon_overlap(MH_LON_MIN_DEG, a_max, b_min, b_max);
    }
    if (b_min > b_max) {
        return mh_lon_overlap(a_min, a_max, b_min, MH_LON_MAX_DEG)
            || mh_lon_overlap(a_min, a_max, MH_LON_MIN_DEG, b_max);
    }
    return 0;
}

int mh_point_in_poly(const mh_point *poly, size_t n, double lat, double lon) {
    int inside = 0;
    size_t j = n - 1;
    for (size_t i = 0; i < n; i++) {
        double lat_i = poly[i].lat;
        double lon_i = poly[i].lon;
        double lat_j = poly[j].lat;
        double lon_j = poly[j].lon;
        int intersect = ((lon_i > lon) != (lon_j > lon)) &&
            (lat < (lat_j - lat_i) * (lon - lon_i) / (lon_j - lon_i + 1e-15) + lat_i);
        if (intersect) {
            inside = !inside;
        }
        j = i;
    }
    return inside;
}

static double mh_orient(mh_point p, mh_point q, mh_point r) {
    return (q.lon - p.lon) * (r.lat - q.lat) - (q.lat - p.lat) * (r.lon - q.lon);
}

static int mh_on_segment(mh_point p, mh_point q, mh_point r) {
    return q.lat <= fmax(p.lat, r.lat) && q.lat >= fmin(p.lat, r.lat)
        && q.lon <= fmax(p.lon, r.lon) && q.lon >= fmin(p.lon, r.lon);
}

int mh_segments_intersect(mh_point a1, mh_point a2, mh_point b1, mh_point b2) {
    double o1 = mh_orient(a1, a2, b1);
    double o2 = mh_orient(a1, a2, b2);
    double o3 = mh_orient(b1, b2, a1);
    double o4 = mh_orient(b1, b2, a2);
    if (o1 == 0 && mh_on_segment(a1, b1, a2)) {
        return 1;
    }
    if (o2 == 0 && mh_on_segment(a1, b2, a2)) {
        return 1;
    }
    if (o3 == 0 && mh_on_segment(b1, a1, b2)) {
        return 1;
    }
    if (o4 == 0 && mh_on_segment(b1, a2, b2)) {
        return 1;
    }
    return (o1 > 0) != (o2 > 0) && (o3 > 0) != (o4 > 0);
}
