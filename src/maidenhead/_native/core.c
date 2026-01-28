#include "core.h"

#include "constants.h"
#include "core_utils.h"
#include "core_error.h"

mh_status mh_split_bbox(
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
