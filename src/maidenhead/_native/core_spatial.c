#include "core_spatial.h"

#include <stdlib.h>
#include <string.h>

#include "core_utils.h"
#include "core_error.h"

mh_status mh_neighbors(
    const char *locator,
    int ring,
    int diagonals,
    mh_list *out,
    mh_error_context *err
) {
    if (!out) {
        mh_set_error(err, MH_ERR_INTERNAL, "output is required");
        return MH_ERR_INTERNAL;
    }
    if (ring < 1) {
        mh_set_error(err, MH_ERR_OUT_OF_RANGE, "ring must be an integer >= 1");
        return MH_ERR_OUT_OF_RANGE;
    }

    mh_grid grid;
    mh_status st = mh_parse_locator(locator, &grid, err);
    if (st != MH_OK) {
        return st;
    }

    mh_bbox bbox;
    st = mh_to_bbox(locator, &bbox, err);
    if (st != MH_OK) {
        return st;
    }
    double dlat = bbox.max_lat - bbox.min_lat;
    double dlon = bbox.max_lon - bbox.min_lon;
    double clat = (bbox.min_lat + bbox.max_lat) / 2.0;
    double clon = mh_normalize_lon((bbox.min_lon + bbox.max_lon) / 2.0);

    int span = ring * 2 + 1;
    size_t max_count = (size_t)(span * span - 1);
    char **items = (char **)calloc(max_count, sizeof(char *));
    if (!items) {
        mh_set_error(err, MH_ERR_INTERNAL, "allocation failed");
        return MH_ERR_INTERNAL;
    }

    size_t count = 0;
    for (int dy = -ring; dy <= ring; dy++) {
        for (int dx = -ring; dx <= ring; dx++) {
            if (dx == 0 && dy == 0) {
                continue;
            }
            if (!diagonals && dx != 0 && dy != 0) {
                continue;
            }
            double lat2 = clat + (double)dy * dlat;
            double lon2 = mh_normalize_lon(clon + (double)dx * dlon);
            lat2 = mh_clamp_lat(lat2);
            mh_grid neighbor;
            st = mh_from_latlon(lat2, lon2, grid.precision, 1, &neighbor, err);
            if (st != MH_OK) {
                mh_free_list(&(mh_list){items, count});
                return st;
            }
            int duplicate = 0;
            for (size_t i = 0; i < count; i++) {
                if (strcmp(items[i], neighbor.locator) == 0) {
                    duplicate = 1;
                    break;
                }
            }
            if (duplicate) {
                continue;
            }
            size_t len = strlen(neighbor.locator);
            char *copy = (char *)malloc(len + 1);
            if (!copy) {
                mh_free_list(&(mh_list){items, count});
                mh_set_error(err, MH_ERR_INTERNAL, "allocation failed");
                return MH_ERR_INTERNAL;
            }
            memcpy(copy, neighbor.locator, len + 1);
            items[count++] = copy;
        }
    }

    out->items = items;
    out->length = count;
    return MH_OK;
}

mh_status mh_adjacent(
    const char *locator,
    int diagonals,
    mh_kv_list *out,
    mh_error_context *err
) {
    if (!out) {
        mh_set_error(err, MH_ERR_INTERNAL, "output is required");
        return MH_ERR_INTERNAL;
    }
    mh_grid grid;
    mh_status st = mh_parse_locator(locator, &grid, err);
    if (st != MH_OK) {
        return st;
    }
    mh_bbox bbox;
    st = mh_to_bbox(locator, &bbox, err);
    if (st != MH_OK) {
        return st;
    }
    double dlat = bbox.max_lat - bbox.min_lat;
    double dlon = bbox.max_lon - bbox.min_lon;
    double clat = (bbox.min_lat + bbox.max_lat) / 2.0;
    double clon = mh_normalize_lon((bbox.min_lon + bbox.max_lon) / 2.0);

    const char *base_keys[] = {"N", "S", "E", "W"};
    int base_offsets[][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    const char *diag_keys[] = {"NE", "NW", "SE", "SW"};
    int diag_offsets[][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};

    size_t count = diagonals ? 8 : 4;
    const char **keys = (const char **)calloc(count, sizeof(char *));
    const char **values = (const char **)calloc(count, sizeof(char *));
    if (!keys || !values) {
        free(keys);
        free(values);
        mh_set_error(err, MH_ERR_INTERNAL, "allocation failed");
        return MH_ERR_INTERNAL;
    }

    size_t idx = 0;
    for (int i = 0; i < 4; i++) {
        double lat2 = clat + (double)base_offsets[i][0] * dlat;
        double lon2 = mh_normalize_lon(clon + (double)base_offsets[i][1] * dlon);
        lat2 = mh_clamp_lat(lat2);
        mh_grid neighbor;
        st = mh_from_latlon(lat2, lon2, grid.precision, 1, &neighbor, err);
        if (st != MH_OK) {
            mh_free_kv_list(&(mh_kv_list){keys, values, idx});
            return st;
        }
        size_t klen = strlen(base_keys[i]);
        char *kcopy = (char *)malloc(klen + 1);
        size_t vlen = strlen(neighbor.locator);
        char *vcopy = (char *)malloc(vlen + 1);
        if (!kcopy || !vcopy) {
            free(kcopy);
            free(vcopy);
            mh_free_kv_list(&(mh_kv_list){keys, values, idx});
            mh_set_error(err, MH_ERR_INTERNAL, "allocation failed");
            return MH_ERR_INTERNAL;
        }
        memcpy(kcopy, base_keys[i], klen + 1);
        memcpy(vcopy, neighbor.locator, vlen + 1);
        keys[idx] = kcopy;
        values[idx] = vcopy;
        idx++;
    }

    if (diagonals) {
        for (int i = 0; i < 4; i++) {
            double lat2 = clat + (double)diag_offsets[i][0] * dlat;
            double lon2 = mh_normalize_lon(clon + (double)diag_offsets[i][1] * dlon);
            lat2 = mh_clamp_lat(lat2);
            mh_grid neighbor;
            st = mh_from_latlon(lat2, lon2, grid.precision, 1, &neighbor, err);
            if (st != MH_OK) {
                mh_free_kv_list(&(mh_kv_list){keys, values, idx});
                return st;
            }
            size_t klen = strlen(diag_keys[i]);
            char *kcopy = (char *)malloc(klen + 1);
            size_t vlen = strlen(neighbor.locator);
            char *vcopy = (char *)malloc(vlen + 1);
            if (!kcopy || !vcopy) {
                free(kcopy);
                free(vcopy);
                mh_free_kv_list(&(mh_kv_list){keys, values, idx});
                mh_set_error(err, MH_ERR_INTERNAL, "allocation failed");
                return MH_ERR_INTERNAL;
            }
            memcpy(kcopy, diag_keys[i], klen + 1);
            memcpy(vcopy, neighbor.locator, vlen + 1);
            keys[idx] = kcopy;
            values[idx] = vcopy;
            idx++;
        }
    }

    out->keys = keys;
    out->values = values;
    out->length = idx;
    return MH_OK;
}

mh_status mh_step(
    const char *locator,
    int dlat_cells,
    int dlon_cells,
    mh_grid *out,
    mh_error_context *err
) {
    if (!out) {
        mh_set_error(err, MH_ERR_INTERNAL, "output is required");
        return MH_ERR_INTERNAL;
    }
    mh_grid grid;
    mh_status st = mh_parse_locator(locator, &grid, err);
    if (st != MH_OK) {
        return st;
    }
    mh_bbox bbox;
    st = mh_to_bbox(locator, &bbox, err);
    if (st != MH_OK) {
        return st;
    }
    double dlat = bbox.max_lat - bbox.min_lat;
    double dlon = bbox.max_lon - bbox.min_lon;
    double clat = (bbox.min_lat + bbox.max_lat) / 2.0;
    double clon = mh_normalize_lon((bbox.min_lon + bbox.max_lon) / 2.0);
    double lat2 = clat + (double)dlat_cells * dlat;
    double lon2 = mh_normalize_lon(clon + (double)dlon_cells * dlon);
    lat2 = mh_clamp_lat(lat2);
    return mh_from_latlon(lat2, lon2, grid.precision, 1, out, err);
}

mh_status mh_contains(
    const char *outer,
    const char *inner,
    int *out,
    mh_error_context *err
) {
    if (!out) {
        mh_set_error(err, MH_ERR_INTERNAL, "output is required");
        return MH_ERR_INTERNAL;
    }
    mh_bbox outer_bbox;
    mh_bbox inner_bbox;
    mh_status st = mh_to_bbox(outer, &outer_bbox, err);
    if (st != MH_OK) {
        return st;
    }
    st = mh_to_bbox(inner, &inner_bbox, err);
    if (st != MH_OK) {
        return st;
    }

    mh_bbox outer_parts[2];
    mh_bbox inner_parts[2];
    size_t outer_len = 0;
    size_t inner_len = 0;
    mh_split_bbox_list(outer_bbox, outer_parts, &outer_len, err);
    mh_split_bbox_list(inner_bbox, inner_parts, &inner_len, err);

    if (outer_len == 0) {
        outer_parts[0] = outer_bbox;
        outer_len = 1;
    }
    if (inner_len == 0) {
        inner_parts[0] = inner_bbox;
        inner_len = 1;
    }

    for (size_t i = 0; i < inner_len; i++) {
        int contained = 0;
        for (size_t j = 0; j < outer_len; j++) {
            mh_bbox o = outer_parts[j];
            mh_bbox in = inner_parts[i];
            if (o.min_lat <= in.min_lat &&
                o.max_lat >= in.max_lat &&
                o.min_lon <= in.min_lon &&
                o.max_lon >= in.max_lon) {
                contained = 1;
                break;
            }
        }
        if (!contained) {
            *out = 0;
            return MH_OK;
        }
    }
    *out = 1;
    return MH_OK;
}

mh_status mh_contains_point(
    const char *locator,
    double lat,
    double lon,
    int *out,
    mh_error_context *err
) {
    if (!out) {
        mh_set_error(err, MH_ERR_INTERNAL, "output is required");
        return MH_ERR_INTERNAL;
    }
    mh_bbox bbox;
    mh_status st = mh_to_bbox(locator, &bbox, err);
    if (st != MH_OK) {
        return st;
    }
    if (lat < bbox.min_lat || lat > bbox.max_lat) {
        *out = 0;
        return MH_OK;
    }
    if (bbox.min_lon <= bbox.max_lon) {
        *out = (lon >= bbox.min_lon && lon <= bbox.max_lon) ? 1 : 0;
        return MH_OK;
    }
    *out = (lon >= bbox.min_lon || lon <= bbox.max_lon) ? 1 : 0;
    return MH_OK;
}

mh_status mh_intersects_bbox(
    const char *locator,
    mh_bbox bbox,
    int *out,
    mh_error_context *err
) {
    if (!out) {
        mh_set_error(err, MH_ERR_INTERNAL, "output is required");
        return MH_ERR_INTERNAL;
    }
    mh_bbox loc_bbox;
    mh_status st = mh_to_bbox(locator, &loc_bbox, err);
    if (st != MH_OK) {
        return st;
    }
    if (loc_bbox.max_lat < bbox.min_lat || bbox.max_lat < loc_bbox.min_lat) {
        *out = 0;
        return MH_OK;
    }

    double a_min = loc_bbox.min_lon;
    double a_max = loc_bbox.max_lon;
    double b_min = bbox.min_lon;
    double b_max = bbox.max_lon;
    *out = mh_lon_overlap(a_min, a_max, b_min, b_max) ? 1 : 0;
    return MH_OK;
}

mh_status mh_intersects_polygon(
    const char *locator,
    const mh_point *points,
    size_t n,
    int *out,
    mh_error_context *err
) {
    if (!out) {
        mh_set_error(err, MH_ERR_INTERNAL, "output is required");
        return MH_ERR_INTERNAL;
    }
    if (!points || n < 3) {
        mh_set_error(err, MH_ERR_OUT_OF_RANGE, "polygon must have at least 3 points");
        return MH_ERR_OUT_OF_RANGE;
    }

    mh_bbox bbox;
    mh_status st = mh_to_bbox(locator, &bbox, err);
    if (st != MH_OK) {
        return st;
    }

    double min_lat = bbox.min_lat;
    double min_lon = bbox.min_lon;
    double max_lat = bbox.max_lat;
    double max_lon = bbox.max_lon;

    double ref_lon = mh_normalize_lon((min_lon + max_lon) / 2.0);

    min_lon = mh_wrap_lon_near(min_lon, ref_lon);
    max_lon = mh_wrap_lon_near(max_lon, ref_lon);

    mh_point *poly = (mh_point *)malloc(sizeof(mh_point) * n);
    if (!poly) {
        mh_set_error(err, MH_ERR_INTERNAL, "allocation failed");
        return MH_ERR_INTERNAL;
    }

    double poly_min_lat = points[0].lat;
    double poly_max_lat = points[0].lat;
    double poly_min_lon = mh_wrap_lon_near(points[0].lon, ref_lon);
    double poly_max_lon = poly_min_lon;

    for (size_t i = 0; i < n; i++) {
        poly[i].lat = points[i].lat;
        poly[i].lon = mh_wrap_lon_near(points[i].lon, ref_lon);
        if (poly[i].lat < poly_min_lat) {
            poly_min_lat = poly[i].lat;
        }
        if (poly[i].lat > poly_max_lat) {
            poly_max_lat = poly[i].lat;
        }
        if (poly[i].lon < poly_min_lon) {
            poly_min_lon = poly[i].lon;
        }
        if (poly[i].lon > poly_max_lon) {
            poly_max_lon = poly[i].lon;
        }
    }

    if (max_lat < poly_min_lat || poly_max_lat < min_lat) {
        free(poly);
        *out = 0;
        return MH_OK;
    }
    if (max_lon < poly_min_lon || poly_max_lon < min_lon) {
        free(poly);
        *out = 0;
        return MH_OK;
    }

    mh_point corners[4];
    corners[0].lat = min_lat;
    corners[0].lon = mh_wrap_lon_near(min_lon, ref_lon);
    corners[1].lat = min_lat;
    corners[1].lon = mh_wrap_lon_near(max_lon, ref_lon);
    corners[2].lat = max_lat;
    corners[2].lon = mh_wrap_lon_near(max_lon, ref_lon);
    corners[3].lat = max_lat;
    corners[3].lon = mh_wrap_lon_near(min_lon, ref_lon);

    for (size_t i = 0; i < 4; i++) {
        if (mh_point_in_poly(poly, n, corners[i].lat, corners[i].lon)) {
            free(poly);
            *out = 1;
            return MH_OK;
        }
    }

    for (size_t i = 0; i < n; i++) {
        double lat = poly[i].lat;
        double lon = poly[i].lon;
        if (lat >= min_lat && lat <= max_lat && lon >= min_lon && lon <= max_lon) {
            free(poly);
            *out = 1;
            return MH_OK;
        }
    }

    mh_point bbox_edges[4][2] = {
        {{min_lat, min_lon}, {min_lat, max_lon}},
        {{min_lat, max_lon}, {max_lat, max_lon}},
        {{max_lat, max_lon}, {max_lat, min_lon}},
        {{max_lat, min_lon}, {min_lat, min_lon}},
    };

    for (size_t i = 0; i < n; i++) {
        mh_point p1 = poly[i];
        mh_point p2 = poly[(i + 1) % n];
        for (int e = 0; e < 4; e++) {
            if (mh_segments_intersect(p1, p2, bbox_edges[e][0], bbox_edges[e][1])) {
                free(poly);
                *out = 1;
                return MH_OK;
            }
        }
    }

    free(poly);
    *out = 0;
    return MH_OK;
}
