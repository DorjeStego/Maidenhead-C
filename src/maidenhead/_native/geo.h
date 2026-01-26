#ifndef MAIDENHEAD_NATIVE_GEO_H
#define MAIDENHEAD_NATIVE_GEO_H

#include <stddef.h>

#include "errors.h"
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Distance methods */
#define MH_DISTANCE_HAVERSINE (0)
#define MH_DISTANCE_GEODESIC (1)

mh_status mh_distance_km(
    const mh_point *a,
    const mh_point *b,
    int method,
    double *out,
    mh_error_context *err
);

mh_status mh_bearing_deg(
    const mh_point *a,
    const mh_point *b,
    double *out,
    mh_error_context *err
);

mh_status mh_midpoint(
    const mh_point *a,
    const mh_point *b,
    mh_point *out,
    mh_error_context *err
);

mh_status mh_great_circle_path(
    const mh_point *a,
    const mh_point *b,
    size_t n,
    mh_point *out,
    mh_error_context *err
);

mh_status mh_bearing_bin(
    const mh_point *a,
    const mh_point *b,
    double bin_size,
    double *out,
    mh_error_context *err
);

mh_status mh_azimuthal_sector(
    const mh_point *a,
    const mh_point *b,
    double width_deg,
    double *start,
    double *end,
    mh_error_context *err
);

/* In-built geodesic APIs (WGS84 Vincenty/authalic fallbacks) */

mh_status mh_geodesic_distance_km(
    const mh_point *a,
    const mh_point *b,
    double *out,
    mh_error_context *err
);

mh_status mh_geodesic_midpoint(
    const mh_point *a,
    const mh_point *b,
    mh_point *out,
    mh_error_context *err
);

mh_status mh_geodesic_area_km2(
    const mh_point *polygon,
    size_t n,
    double *out,
    mh_error_context *err
);

mh_status mh_geodesic_line_point(
    const mh_point *a,
    const mh_point *b,
    double fraction,
    mh_point *out,
    mh_error_context *err
);

/* Bulk geodesic helpers with SIMD-ready hooks. */
mh_status mh_geodesic_distance_many(
    const mh_point *a,
    const mh_point *b,
    size_t n,
    double *out_km,
    mh_error_context *err
);

mh_status mh_geodesic_line_points_many(
    const mh_point *a,
    const mh_point *b,
    const double *fractions,
    size_t n,
    mh_point *out,
    mh_error_context *err
);

#ifdef __cplusplus
}
#endif

#endif
