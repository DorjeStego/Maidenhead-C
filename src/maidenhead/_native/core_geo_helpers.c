#include "core_geo_helpers.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "core_utils.h"
#include "constants.h"
#include "core_error.h"

mh_status mh_to_center_latlon(
    const char *locator,
    double *lat,
    double *lon,
    mh_error_context *err
) {
    mh_bbox bbox;
    mh_status st = mh_to_bbox(locator, &bbox, err);
    if (st != MH_OK) {
        return st;
    }
    if (!lat || !lon) {
        mh_set_error(err, MH_ERR_INTERNAL, "output is required");
        return MH_ERR_INTERNAL;
    }
    *lat = (bbox.min_lat + bbox.max_lat) / 2.0;
    *lon = mh_normalize_lon((bbox.min_lon + bbox.max_lon) / 2.0);
    return MH_OK;
}

mh_status mh_to_bbox_split(
    const char *locator,
    mh_bbox *parts,
    size_t *parts_len,
    mh_error_context *err
) {
    if (!parts || !parts_len) {
        mh_set_error(err, MH_ERR_INTERNAL, "output is required");
        return MH_ERR_INTERNAL;
    }
    mh_bbox bbox;
    mh_status st = mh_to_bbox(locator, &bbox, err);
    if (st != MH_OK) {
        return st;
    }
    return mh_split_bbox_list(bbox, parts, parts_len, err);
}

mh_status mh_split_bbox(
    mh_bbox bbox,
    mh_bbox *parts,
    size_t *parts_len,
    mh_error_context *err
) {
    return mh_split_bbox_list(bbox, parts, parts_len, err);
}

mh_status mh_split_bbox_list(
    mh_bbox bbox,
    mh_bbox *parts,
    size_t *parts_len,
    mh_error_context *err
) {
    if (!parts || !parts_len) {
        mh_set_error(err, MH_ERR_INTERNAL, "output is required");
        return MH_ERR_INTERNAL;
    }

    double min_lon = mh_normalize_lon(bbox.min_lon);
    double max_lon = mh_normalize_lon(bbox.max_lon);

    if (min_lon <= max_lon) {
        *parts_len = 0;
        return MH_OK;
    }

    mh_bbox west = {bbox.min_lat, min_lon, bbox.max_lat, MH_LON_MAX_DEG};
    mh_bbox east = {bbox.min_lat, MH_LON_MIN_DEG, bbox.max_lat, max_lon};

    size_t count = 0;
    if (west.min_lon != west.max_lon) {
        parts[count++] = west;
    }
    if (east.min_lon != east.max_lon) {
        parts[count++] = east;
    }

    *parts_len = count;
    return MH_OK;
}

mh_status mh_corners(const char *locator, mh_corners_t *out, mh_error_context *err) {
    if (!out) {
        mh_set_error(err, MH_ERR_INTERNAL, "output is required");
        return MH_ERR_INTERNAL;
    }
    mh_bbox bbox;
    mh_status st = mh_to_bbox(locator, &bbox, err);
    if (st != MH_OK) {
        return st;
    }
    out->nw.lat = bbox.max_lat;
    out->nw.lon = bbox.min_lon;
    out->ne.lat = bbox.max_lat;
    out->ne.lon = bbox.max_lon;
    out->sw.lat = bbox.min_lat;
    out->sw.lon = bbox.min_lon;
    out->se.lat = bbox.min_lat;
    out->se.lon = bbox.max_lon;
    return MH_OK;
}

mh_status mh_to_utm_zone(
    const char *locator,
    char *output,
    size_t output_len,
    mh_error_context *err
) {
    if (!output || output_len == 0) {
        mh_set_error(err, MH_ERR_INTERNAL, "output is required");
        return MH_ERR_INTERNAL;
    }
    double lat = 0.0;
    double lon = 0.0;
    mh_status st = mh_to_center_latlon(locator, &lat, &lon, err);
    if (st != MH_OK) {
        return st;
    }
    int zone = (int)floor((lon + 180.0) / 6.0) + 1;
    char hemi = (lat >= 0.0) ? 'N' : 'S';
    int written = snprintf(output, output_len, "%d%c", zone, hemi);
    if (written < 0 || (size_t)written >= output_len) {
        mh_set_error(err, MH_ERR_INTERNAL, "output buffer too small");
        return MH_ERR_INTERNAL;
    }
    return MH_OK;
}
