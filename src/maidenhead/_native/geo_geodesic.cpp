#include "geo.h"

#include <GeographicLib/Geodesic.hpp>
#include <GeographicLib/GeodesicLine.hpp>
#include <GeographicLib/PolygonArea.hpp>
#include <cmath>

static void mh_set_error(mh_error_context *err, mh_status code, const char *message) {
    if (!err) {
        return;
    }
    err->code = code;
    err->message = message;
    err->key = NULL;
    err->value = NULL;
}

extern "C" mh_status mh_geodesic_distance_km(
    const mh_point *a,
    const mh_point *b,
    double *out,
    mh_error_context *err
) {
    if (!a || !b || !out) {
        mh_set_error(err, MH_ERR_INTERNAL, "output is required");
        return MH_ERR_INTERNAL;
    }
    double s12 = 0.0;
    GeographicLib::Geodesic::WGS84.Inverse(a->lat, a->lon, b->lat, b->lon, s12);
    *out = s12 / 1000.0;
    return MH_OK;
}

extern "C" mh_status mh_geodesic_midpoint(
    const mh_point *a,
    const mh_point *b,
    mh_point *out,
    mh_error_context *err
) {
    if (!a || !b || !out) {
        mh_set_error(err, MH_ERR_INTERNAL, "output is required");
        return MH_ERR_INTERNAL;
    }
    double s12 = 0.0;
    double azi1 = 0.0;
    double azi2 = 0.0;
    GeographicLib::Geodesic::WGS84.Inverse(a->lat, a->lon, b->lat, b->lon, s12, azi1, azi2);
    GeographicLib::GeodesicLine line = GeographicLib::Geodesic::WGS84.Line(a->lat, a->lon, azi1);
    double lat = 0.0;
    double lon = 0.0;
    line.Position(s12 / 2.0, lat, lon);
    out->lat = lat;
    out->lon = lon;
    return MH_OK;
}

extern "C" mh_status mh_geodesic_area_km2(
    const mh_point *polygon,
    size_t n,
    double *out,
    mh_error_context *err
) {
    if (!polygon || !out) {
        mh_set_error(err, MH_ERR_INTERNAL, "output is required");
        return MH_ERR_INTERNAL;
    }
    GeographicLib::PolygonArea<GeographicLib::Geodesic> poly(GeographicLib::Geodesic::WGS84, false);
    for (size_t i = 0; i < n; i++) {
        poly.AddPoint(polygon[i].lat, polygon[i].lon);
    }
    double area = 0.0;
    double perimeter = 0.0;
    poly.Compute(false, true, area, perimeter);
    area = std::fabs(area);
    *out = area / 1e6;
    return MH_OK;
}

extern "C" mh_status mh_geodesic_line_point(
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
    double s12 = 0.0;
    double azi1 = 0.0;
    double azi2 = 0.0;
    GeographicLib::Geodesic::WGS84.Inverse(a->lat, a->lon, b->lat, b->lon, s12, azi1, azi2);
    GeographicLib::GeodesicLine line = GeographicLib::Geodesic::WGS84.Line(a->lat, a->lon, azi1);
    double lat = 0.0;
    double lon = 0.0;
    line.Position(s12 * fraction, lat, lon);
    out->lat = lat;
    out->lon = lon;
    return MH_OK;
}
