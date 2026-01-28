#include "core_metrics.h"

#include <math.h>
#include <string.h>

#include "constants.h"
#include "core_internal.h"
#include "core_error.h"
#include "geo.h"

static mh_status mh_step_size_for_precision(
    int precision,
    double *lon_step,
    double *lat_step,
    mh_error_context *err
) {
    if (!lon_step || !lat_step) {
        mh_set_error(err, MH_ERR_INTERNAL, "output is required");
        return MH_ERR_INTERNAL;
    }
    mh_status st = mh_validate_precision(precision, err);
    if (st != MH_OK) {
        return st;
    }
    int pairs = precision / 2;
    double lon_cell = MH_LON_SPAN_DEG;
    double lat_cell = MH_LAT_SPAN_DEG;
    for (int i = 1; i <= pairs; i++) {
        int lon_base = 0;
        int lat_base = 0;
        mh_lon_lat_bases_for_pair(i, &lon_base, &lat_base);
        lon_cell /= (double)lon_base;
        lat_cell /= (double)lat_base;
    }
    *lon_step = lon_cell;
    *lat_step = lat_cell;
    return MH_OK;
}

mh_status mh_validate_precision(int precision, mh_error_context *err) {
    if (!mh_validate_precision_value(precision, err)) {
        return err ? err->code : MH_ERR_PRECISION;
    }
    return MH_OK;
}

mh_status mh_precision_of(const char *locator, int *out, mh_error_context *err) {
    if (!locator || !out) {
        mh_set_error(err, MH_ERR_INVALID_LOCATOR, "locator must be str");
        return MH_ERR_INVALID_LOCATOR;
    }
    size_t len = strlen(locator);
    if (!mh_validate_precision_value((int)len, err)) {
        return err ? err->code : MH_ERR_PRECISION;
    }
    *out = (int)len;
    return MH_OK;
}

mh_status mh_cell_size_deg(
    const char *locator,
    double *width_deg,
    double *height_deg,
    mh_error_context *err
) {
    if (!width_deg || !height_deg) {
        mh_set_error(err, MH_ERR_INTERNAL, "output is required");
        return MH_ERR_INTERNAL;
    }
    int precision = 0;
    mh_status st = mh_precision_of(locator, &precision, err);
    if (st != MH_OK) {
        return st;
    }
    double lon_step = 0.0;
    double lat_step = 0.0;
    st = mh_step_size_for_precision(precision, &lon_step, &lat_step, err);
    if (st != MH_OK) {
        return st;
    }
    *width_deg = lon_step;
    *height_deg = lat_step;
    return MH_OK;
}

mh_status mh_cell_size_km(
    const char *locator,
    int use_at_lat,
    double at_lat,
    int method,
    double *width_km,
    double *height_km,
    mh_error_context *err
) {
    if (!width_km || !height_km) {
        mh_set_error(err, MH_ERR_INTERNAL, "output is required");
        return MH_ERR_INTERNAL;
    }
    if (method != MH_DISTANCE_HAVERSINE && method != MH_DISTANCE_GEODESIC) {
        mh_set_error(err, MH_ERR_UNSUPPORTED, "unknown distance method");
        return MH_ERR_UNSUPPORTED;
    }
    int precision = 0;
    mh_status st = mh_precision_of(locator, &precision, err);
    if (st != MH_OK) {
        return st;
    }
    double lon_step = 0.0;
    double lat_step = 0.0;
    st = mh_step_size_for_precision(precision, &lon_step, &lat_step, err);
    if (st != MH_OK) {
        return st;
    }
    double lat = 0.0;
    double lon = 0.0;
    st = mh_to_center_latlon(locator, &lat, &lon, err);
    if (st != MH_OK) {
        return st;
    }
    double half_lon = lon_step / 2.0;
    double half_lat = lat_step / 2.0;
    mh_point a = {use_at_lat ? at_lat : lat, lon - half_lon};
    mh_point b = {use_at_lat ? at_lat : lat, lon + half_lon};
    mh_point c = {lat - half_lat, lon};
    mh_point d = {lat + half_lat, lon};
    st = mh_distance_km(&a, &b, method, width_km, err);
    if (st != MH_OK) {
        return st;
    }
    st = mh_distance_km(&c, &d, method, height_km, err);
    if (st != MH_OK) {
        return st;
    }
    return MH_OK;
}

mh_status mh_area_km2(
    const char *locator,
    int method,
    double *out,
    mh_error_context *err
) {
    if (!out) {
        mh_set_error(err, MH_ERR_INTERNAL, "output is required");
        return MH_ERR_INTERNAL;
    }
    if (method != MH_DISTANCE_HAVERSINE && method != MH_DISTANCE_GEODESIC) {
        mh_set_error(err, MH_ERR_UNSUPPORTED, "unknown distance method");
        return MH_ERR_UNSUPPORTED;
    }
    if (method == MH_DISTANCE_GEODESIC) {
        mh_bbox bbox;
        mh_status st = mh_to_bbox(locator, &bbox, err);
        if (st != MH_OK) {
            return st;
        }
        mh_point poly[4] = {
            {bbox.min_lat, bbox.min_lon},
            {bbox.min_lat, bbox.max_lon},
            {bbox.max_lat, bbox.max_lon},
            {bbox.max_lat, bbox.min_lon},
        };
        double area_km2 = 0.0;
        st = mh_geodesic_area_km2(poly, 4, &area_km2, err);
        if (st != MH_OK) {
            return st;
        }
        if (area_km2 < 0.0) {
            area_km2 = -area_km2;
        }
        *out = area_km2;
        return MH_OK;
    }

    int precision = 0;
    mh_status st = mh_precision_of(locator, &precision, err);
    if (st != MH_OK) {
        return st;
    }
    double lon_step = 0.0;
    double lat_step = 0.0;
    st = mh_step_size_for_precision(precision, &lon_step, &lat_step, err);
    if (st != MH_OK) {
        return st;
    }
    double lat = 0.0;
    double lon = 0.0;
    st = mh_to_center_latlon(locator, &lat, &lon, err);
    if (st != MH_OK) {
        return st;
    }
    double half_lon = lon_step / 2.0;
    double half_lat = lat_step / 2.0;
    mh_point a = {lat, lon - half_lon};
    mh_point b = {lat, lon + half_lon};
    mh_point c = {lat - half_lat, lon};
    mh_point d = {lat + half_lat, lon};
    double width_km = 0.0;
    double height_km = 0.0;
    st = mh_distance_km(&a, &b, MH_DISTANCE_HAVERSINE, &width_km, err);
    if (st != MH_OK) {
        return st;
    }
    st = mh_distance_km(&c, &d, MH_DISTANCE_HAVERSINE, &height_km, err);
    if (st != MH_OK) {
        return st;
    }
    *out = width_km * height_km;
    return MH_OK;
}

mh_status mh_diagonal_km(
    const char *locator,
    int method,
    double *out,
    mh_error_context *err
) {
    if (!out) {
        mh_set_error(err, MH_ERR_INTERNAL, "output is required");
        return MH_ERR_INTERNAL;
    }
    if (method != MH_DISTANCE_HAVERSINE && method != MH_DISTANCE_GEODESIC) {
        mh_set_error(err, MH_ERR_UNSUPPORTED, "unknown distance method");
        return MH_ERR_UNSUPPORTED;
    }
    mh_bbox bbox;
    mh_status st = mh_to_bbox(locator, &bbox, err);
    if (st != MH_OK) {
        return st;
    }
    mh_point a = {bbox.min_lat, bbox.min_lon};
    mh_point b = {bbox.max_lat, bbox.max_lon};
    st = mh_distance_km(&a, &b, method, out, err);
    if (st != MH_OK) {
        return st;
    }
    return MH_OK;
}
