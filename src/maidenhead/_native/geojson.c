#include "geojson.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "core.h"

static void mh_set_error(mh_error_context *err, mh_status code, const char *message) {
    if (!err) {
        return;
    }
    err->code = code;
    err->message = message;
    err->key = NULL;
    err->value = NULL;
}

static int mh_append(char *buf, size_t buf_len, size_t *used, const char *fmt, ...) {
    if (*used >= buf_len) {
        return 0;
    }
    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(buf + *used, buf_len - *used, fmt, args);
    va_end(args);
    if (written < 0) {
        return 0;
    }
    if ((size_t)written >= buf_len - *used) {
        return 0;
    }
    *used += (size_t)written;
    return 1;
}

static int mh_append_polygon_geometry(char *buf, size_t buf_len, size_t *used, mh_bbox bbox) {
    return mh_append(
        buf,
        buf_len,
        used,
        "{\"type\":\"Polygon\",\"coordinates\":[[[%.17g,%.17g],[%.17g,%.17g],[%.17g,%.17g],[%.17g,%.17g],[%.17g,%.17g]]]}",
        bbox.min_lon, bbox.min_lat,
        bbox.max_lon, bbox.min_lat,
        bbox.max_lon, bbox.max_lat,
        bbox.min_lon, bbox.max_lat,
        bbox.min_lon, bbox.min_lat
    );
}

mh_status mh_to_geojson_point(
    double lat,
    double lon,
    char *output,
    size_t output_len,
    mh_error_context *err
) {
    size_t used = 0;
    if (!mh_append(
            output,
            output_len,
            &used,
            "{\"type\":\"Point\",\"coordinates\":[%.17g,%.17g]}",
            lon,
            lat
        )) {
        mh_set_error(err, MH_ERR_INTERNAL, "output buffer too small");
        return MH_ERR_INTERNAL;
    }
    return MH_OK;
}

mh_status mh_to_geojson_bbox(
    const char *locator,
    double out[4],
    mh_error_context *err
) {
    mh_bbox bbox;
    mh_status st = mh_to_bbox(locator, &bbox, err);
    if (st != MH_OK) {
        return st;
    }
    out[0] = bbox.min_lon;
    out[1] = bbox.min_lat;
    out[2] = bbox.max_lon;
    out[3] = bbox.max_lat;
    return MH_OK;
}

mh_status mh_to_geojson_bbox_point(
    double lat,
    double lon,
    double out[4],
    mh_error_context *err
) {
    (void)err;
    out[0] = lon;
    out[1] = lat;
    out[2] = lon;
    out[3] = lat;
    return MH_OK;
}

mh_status mh_to_geojson_polygon(
    const char *locator,
    char *output,
    size_t output_len,
    mh_error_context *err
) {
    mh_bbox bbox;
    mh_status st = mh_to_bbox(locator, &bbox, err);
    if (st != MH_OK) {
        return st;
    }
    size_t used = 0;
    if (!mh_append_polygon_geometry(output, output_len, &used, bbox)) {
        mh_set_error(err, MH_ERR_INTERNAL, "output buffer too small");
        return MH_ERR_INTERNAL;
    }
    return MH_OK;
}

mh_status mh_to_geojson_feature(
    const char *locator,
    char *output,
    size_t output_len,
    mh_error_context *err
) {
    mh_bbox bbox;
    mh_status st = mh_to_bbox(locator, &bbox, err);
    if (st != MH_OK) {
        return st;
    }
    size_t used = 0;
    if (!mh_append(output, output_len, &used, "{\"type\":\"Feature\",\"geometry\":")) {
        mh_set_error(err, MH_ERR_INTERNAL, "output buffer too small");
        return MH_ERR_INTERNAL;
    }
    if (!mh_append_polygon_geometry(output, output_len, &used, bbox)) {
        mh_set_error(err, MH_ERR_INTERNAL, "output buffer too small");
        return MH_ERR_INTERNAL;
    }
    if (!mh_append(output, output_len, &used, ",\"properties\":{}}")) {
        mh_set_error(err, MH_ERR_INTERNAL, "output buffer too small");
        return MH_ERR_INTERNAL;
    }
    return MH_OK;
}

mh_status mh_to_geojson_feature_point(
    double lat,
    double lon,
    char *output,
    size_t output_len,
    mh_error_context *err
) {
    size_t used = 0;
    if (!mh_append(output, output_len, &used, "{\"type\":\"Feature\",\"geometry\":")) {
        mh_set_error(err, MH_ERR_INTERNAL, "output buffer too small");
        return MH_ERR_INTERNAL;
    }
    if (!mh_append(
            output,
            output_len,
            &used,
            "{\"type\":\"Point\",\"coordinates\":[%.17g,%.17g]}",
            lon,
            lat
        )) {
        mh_set_error(err, MH_ERR_INTERNAL, "output buffer too small");
        return MH_ERR_INTERNAL;
    }
    if (!mh_append(output, output_len, &used, ",\"properties\":{}}")) {
        mh_set_error(err, MH_ERR_INTERNAL, "output buffer too small");
        return MH_ERR_INTERNAL;
    }
    return MH_OK;
}

mh_status mh_to_geojson_feature_collection(
    const char *const *locators,
    size_t n,
    char *output,
    size_t output_len,
    mh_error_context *err
) {
    size_t used = 0;
    if (!mh_append(output, output_len, &used, "{\"type\":\"FeatureCollection\",\"features\":[")) {
        mh_set_error(err, MH_ERR_INTERNAL, "output buffer too small");
        return MH_ERR_INTERNAL;
    }
    for (size_t i = 0; i < n; i++) {
        mh_bbox bbox;
        mh_status st = mh_to_bbox(locators[i], &bbox, err);
        if (st != MH_OK) {
            return st;
        }
        if (i > 0) {
            if (!mh_append(output, output_len, &used, ",")) {
                mh_set_error(err, MH_ERR_INTERNAL, "output buffer too small");
                return MH_ERR_INTERNAL;
            }
        }
        if (!mh_append(output, output_len, &used, "{\"type\":\"Feature\",\"geometry\":")) {
            mh_set_error(err, MH_ERR_INTERNAL, "output buffer too small");
            return MH_ERR_INTERNAL;
        }
        if (!mh_append_polygon_geometry(output, output_len, &used, bbox)) {
            mh_set_error(err, MH_ERR_INTERNAL, "output buffer too small");
            return MH_ERR_INTERNAL;
        }
        if (!mh_append(output, output_len, &used, ",\"properties\":{}}")) {
            mh_set_error(err, MH_ERR_INTERNAL, "output buffer too small");
            return MH_ERR_INTERNAL;
        }
    }
    if (!mh_append(output, output_len, &used, "]}")) {
        mh_set_error(err, MH_ERR_INTERNAL, "output buffer too small");
        return MH_ERR_INTERNAL;
    }
    return MH_OK;
}

mh_status mh_to_geojson_envelope(
    const char *locator,
    char *output,
    size_t output_len,
    mh_error_context *err
) {
    mh_bbox bbox;
    mh_status st = mh_to_bbox(locator, &bbox, err);
    if (st != MH_OK) {
        return st;
    }
    size_t used = 0;
    if (bbox.min_lat == bbox.max_lat && bbox.min_lon == bbox.max_lon) {
        if (!mh_append(
                output,
                output_len,
                &used,
                "{\"type\":\"Point\",\"coordinates\":[%.17g,%.17g],\"bbox\":[%.17g,%.17g,%.17g,%.17g]}",
                bbox.min_lon,
                bbox.min_lat,
                bbox.min_lon,
                bbox.min_lat,
                bbox.max_lon,
                bbox.max_lat
            )) {
            mh_set_error(err, MH_ERR_INTERNAL, "output buffer too small");
            return MH_ERR_INTERNAL;
        }
        return MH_OK;
    }
    if (!mh_append(
            output,
            output_len,
            &used,
            "{\"type\":\"Polygon\",\"coordinates\":[[[%.17g,%.17g],[%.17g,%.17g],[%.17g,%.17g],[%.17g,%.17g],[%.17g,%.17g]]],\"bbox\":[%.17g,%.17g,%.17g,%.17g]}",
            bbox.min_lon, bbox.min_lat,
            bbox.max_lon, bbox.min_lat,
            bbox.max_lon, bbox.max_lat,
            bbox.min_lon, bbox.max_lat,
            bbox.min_lon, bbox.min_lat,
            bbox.min_lon, bbox.min_lat, bbox.max_lon, bbox.max_lat
        )) {
        mh_set_error(err, MH_ERR_INTERNAL, "output buffer too small");
        return MH_ERR_INTERNAL;
    }
    return MH_OK;
}

mh_status mh_to_geojson_envelope_point(
    double lat,
    double lon,
    char *output,
    size_t output_len,
    mh_error_context *err
) {
    size_t used = 0;
    if (!mh_append(
            output,
            output_len,
            &used,
            "{\"type\":\"Point\",\"coordinates\":[%.17g,%.17g],\"bbox\":[%.17g,%.17g,%.17g,%.17g]}",
            lon,
            lat,
            lon,
            lat,
            lon,
            lat
        )) {
        mh_set_error(err, MH_ERR_INTERNAL, "output buffer too small");
        return MH_ERR_INTERNAL;
    }
    return MH_OK;
}
