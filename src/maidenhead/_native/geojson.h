#ifndef MAIDENHEAD_NATIVE_GEOJSON_H
#define MAIDENHEAD_NATIVE_GEOJSON_H

#include <stddef.h>

#include "errors.h"
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

mh_status mh_to_geojson_polygon(
    const char *locator,
    char *output,
    size_t output_len,
    mh_error_context *err
);

mh_status mh_to_geojson_point(
    double lat,
    double lon,
    char *output,
    size_t output_len,
    mh_error_context *err
);

mh_status mh_to_geojson_feature(
    const char *locator,
    char *output,
    size_t output_len,
    mh_error_context *err
);

mh_status mh_to_geojson_feature_point(
    double lat,
    double lon,
    char *output,
    size_t output_len,
    mh_error_context *err
);

mh_status mh_to_geojson_feature_collection(
    const char *const *locators,
    size_t n,
    char *output,
    size_t output_len,
    mh_error_context *err
);

mh_status mh_to_geojson_bbox(
    const char *locator,
    double out[4],
    mh_error_context *err
);

mh_status mh_to_geojson_bbox_point(
    double lat,
    double lon,
    double out[4],
    mh_error_context *err
);

mh_status mh_to_geojson_envelope(
    const char *locator,
    char *output,
    size_t output_len,
    mh_error_context *err
);

mh_status mh_to_geojson_envelope_point(
    double lat,
    double lon,
    char *output,
    size_t output_len,
    mh_error_context *err
);

#ifdef __cplusplus
}
#endif

#endif
