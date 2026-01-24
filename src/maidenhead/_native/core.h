#ifndef MAIDENHEAD_NATIVE_CORE_H
#define MAIDENHEAD_NATIVE_CORE_H

#include <stddef.h>

#include "errors.h"
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

mh_status mh_validate_precision(int precision, mh_error_context *err);

mh_status mh_normalize_locator(
    const char *input,
    char *output,
    size_t output_len,
    mh_error_context *err
);

mh_status mh_parse_locator(const char *input, mh_grid *out, mh_error_context *err);

mh_status mh_precision_of(const char *locator, int *out, mh_error_context *err);

mh_status mh_normalize_many(
    const char *const *locators,
    size_t n,
    mh_list *out,
    mh_error_context *err
);

mh_status mh_to_bbox(const char *locator, mh_bbox *out, mh_error_context *err);

mh_status mh_to_center_latlon(
    const char *locator,
    double *lat,
    double *lon,
    mh_error_context *err
);

mh_status mh_from_latlon(
    double lat,
    double lon,
    int precision,
    int clamp,
    mh_grid *out,
    mh_error_context *err
);

mh_status mh_format_locator(
    const char *locator,
    int precision,
    int mode,
    mh_grid *out,
    mh_error_context *err
);

mh_status mh_to_bbox_split(
    const char *locator,
    mh_bbox *parts,
    size_t *parts_len,
    mh_error_context *err
);

mh_status mh_split_bbox_list(
    mh_bbox bbox,
    mh_bbox *parts,
    size_t *parts_len,
    mh_error_context *err
);

mh_status mh_corners(const char *locator, mh_corners_t *out, mh_error_context *err);

mh_status mh_cell_size_deg(
    const char *locator,
    double *width_deg,
    double *height_deg,
    mh_error_context *err
);

mh_status mh_cell_size_km(
    const char *locator,
    int use_at_lat,
    double at_lat,
    int method,
    double *width_km,
    double *height_km,
    mh_error_context *err
);

mh_status mh_area_km2(
    const char *locator,
    int method,
    double *out,
    mh_error_context *err
);

mh_status mh_diagonal_km(
    const char *locator,
    int method,
    double *out,
    mh_error_context *err
);

mh_status mh_parent(
    const char *locator,
    int precision,
    mh_grid *out,
    mh_error_context *err
);

mh_status mh_children_iter(
    const char *locator,
    int precision,
    int (*callback)(const char *locator, void *userdata),
    void *userdata,
    mh_error_context *err
);

mh_status mh_neighbors(
    const char *locator,
    int ring,
    int diagonals,
    mh_list *out,
    mh_error_context *err
);

mh_status mh_adjacent(
    const char *locator,
    int diagonals,
    mh_kv_list *out,
    mh_error_context *err
);

void mh_free_list(mh_list *list);
void mh_free_kv_list(mh_kv_list *list);

mh_status mh_step(
    const char *locator,
    int dlat_cells,
    int dlon_cells,
    mh_grid *out,
    mh_error_context *err
);

mh_status mh_contains(
    const char *outer,
    const char *inner,
    int *out,
    mh_error_context *err
);

mh_status mh_contains_point(
    const char *locator,
    double lat,
    double lon,
    int *out,
    mh_error_context *err
);

mh_status mh_intersects_bbox(
    const char *locator,
    mh_bbox bbox,
    int *out,
    mh_error_context *err
);

mh_status mh_intersects_polygon(
    const char *locator,
    const mh_point *points,
    size_t n,
    int *out,
    mh_error_context *err
);

mh_status mh_split_bbox(
    mh_bbox bbox,
    mh_bbox *parts,
    size_t *parts_len,
    mh_error_context *err
);

mh_status mh_to_utm_zone(
    const char *locator,
    char *output,
    size_t output_len,
    mh_error_context *err
);

mh_status mh_to_utm_zone_many(
    const char *const *locators,
    size_t n,
    mh_list *out,
    mh_error_context *err
);

/* Bulk helpers (arrays managed by caller). */

mh_status mh_from_latlon_many(
    const double *lats,
    const double *lons,
    size_t n,
    int precision,
    mh_list *out,
    mh_error_context *err
);

mh_status mh_to_center_many(
    const char *const *locators,
    size_t n,
    double *lat_out,
    double *lon_out,
    mh_error_context *err
);

mh_status mh_to_bbox_many(
    const char *const *locators,
    size_t n,
    mh_bbox *out,
    mh_error_context *err
);

#ifdef __cplusplus
}
#endif

#endif
