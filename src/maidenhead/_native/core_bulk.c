#include "core_bulk.h"

#include <stdlib.h>
#include <string.h>

#include "core_internal.h"
#include "core_simd.h"
#include "core_utils.h"
#include "core_error.h"

void mh_free_list(mh_list *list) {
    if (!list || !list->items) {
        return;
    }
    for (size_t i = 0; i < list->length; i++) {
        free(list->items[i]);
    }
    free(list->items);
    list->items = NULL;
    list->length = 0;
}

void mh_free_kv_list(mh_kv_list *list) {
    if (!list) {
        return;
    }
    if (list->keys) {
        for (size_t i = 0; i < list->length; i++) {
            free((void *)list->keys[i]);
        }
        free(list->keys);
    }
    if (list->values) {
        for (size_t i = 0; i < list->length; i++) {
            free((void *)list->values[i]);
        }
        free(list->values);
    }
    list->keys = NULL;
    list->values = NULL;
    list->length = 0;
}

mh_status mh_normalize_many(
    const char *const *locators,
    size_t n,
    mh_list *out,
    mh_error_context *err
) {
    if (!out) {
        mh_set_error(err, MH_ERR_INTERNAL, "output is required");
        return MH_ERR_INTERNAL;
    }
    if (!locators) {
        mh_set_error(err, MH_ERR_INTERNAL, "inputs are required");
        return MH_ERR_INTERNAL;
    }
    if (n == 0) {
        out->items = NULL;
        out->length = 0;
        return MH_OK;
    }

    char **items = (char **)calloc(n, sizeof(char *));
    if (!items) {
        mh_set_error(err, MH_ERR_INTERNAL, "allocation failed");
        return MH_ERR_INTERNAL;
    }

    size_t count = 0;
    for (size_t i = 0; i < n; i++) {
        char norm[12];
        size_t len2 = 0;
        mh_status st = mh_normalize_locator_len(locators[i], norm, sizeof(norm), &len2, err);
        if (st != MH_OK) {
            mh_free_list(&(mh_list){items, count});
            return st;
        }
        char *copy = (char *)malloc(len2 + 1);
        if (!copy) {
            mh_free_list(&(mh_list){items, count});
            mh_set_error(err, MH_ERR_INTERNAL, "allocation failed");
            return MH_ERR_INTERNAL;
        }
        memcpy(copy, norm, len2 + 1);
        items[count++] = copy;
    }

    out->items = items;
    out->length = count;
    return MH_OK;
}

mh_status mh_to_utm_zone_many(
    const char *const *locators,
    size_t n,
    mh_list *out,
    mh_error_context *err
) {
    if (!out) {
        mh_set_error(err, MH_ERR_INTERNAL, "output is required");
        return MH_ERR_INTERNAL;
    }
    if (!locators) {
        mh_set_error(err, MH_ERR_INTERNAL, "inputs are required");
        return MH_ERR_INTERNAL;
    }
    if (n == 0) {
        out->items = NULL;
        out->length = 0;
        return MH_OK;
    }

    char **items = (char **)calloc(n, sizeof(char *));
    if (!items) {
        mh_set_error(err, MH_ERR_INTERNAL, "allocation failed");
        return MH_ERR_INTERNAL;
    }

    size_t count = 0;
    for (size_t i = 0; i < n; i++) {
        char zone[8];
        mh_status st = mh_to_utm_zone(locators[i], zone, sizeof(zone), err);
        if (st != MH_OK) {
            mh_free_list(&(mh_list){items, count});
            return st;
        }
        size_t len = strlen(zone);
        char *copy = (char *)malloc(len + 1);
        if (!copy) {
            mh_free_list(&(mh_list){items, count});
            mh_set_error(err, MH_ERR_INTERNAL, "allocation failed");
            return MH_ERR_INTERNAL;
        }
        memcpy(copy, zone, len + 1);
        items[count++] = copy;
    }

    out->items = items;
    out->length = count;
    return MH_OK;
}

mh_status mh_from_latlon_many(
    const double *lats,
    const double *lons,
    size_t n,
    int precision,
    mh_list *out,
    mh_error_context *err
) {
    if (!out) {
        mh_set_error(err, MH_ERR_INTERNAL, "output is required");
        return MH_ERR_INTERNAL;
    }
    if (!lats || !lons) {
        mh_set_error(err, MH_ERR_INTERNAL, "inputs are required");
        return MH_ERR_INTERNAL;
    }
    mh_status prec_status = mh_validate_precision(precision, err);
    if (prec_status != MH_OK) {
        return prec_status;
    }

    if (n == 0) {
        out->items = NULL;
        out->length = 0;
        return MH_OK;
    }

    char **items = (char **)calloc(n, sizeof(char *));
    if (!items) {
        mh_set_error(err, MH_ERR_INTERNAL, "allocation failed");
        return MH_ERR_INTERNAL;
    }

    size_t count = 0;
#if defined(__x86_64__) && defined(__GNUC__)
    if (n >= 4 && __builtin_cpu_supports("avx")) {
        mh_status st = mh_from_latlon_many_avx(lats, lons, n, precision, items, &count, err);
        if (st != MH_OK) {
            mh_free_list(&(mh_list){items, count});
            return st;
        }
        out->items = items;
        out->length = count;
        return MH_OK;
    }
#endif
#if defined(MH_HAVE_NEON_SIMD)
    if (n >= 2) {
        mh_status st = mh_from_latlon_many_neon(lats, lons, n, precision, items, &count, err);
        if (st != MH_OK) {
            mh_free_list(&(mh_list){items, count});
            return st;
        }
        out->items = items;
        out->length = count;
        return MH_OK;
    }
#endif
    for (size_t i = 0; i < n; i++) {
        mh_grid grid;
        mh_status st = mh_from_latlon(lats[i], lons[i], precision, 1, &grid, err);
        if (st != MH_OK) {
            mh_free_list(&(mh_list){items, count});
            return st;
        }
        size_t len = strlen(grid.locator);
        char *copy = (char *)malloc(len + 1);
        if (!copy) {
            mh_free_list(&(mh_list){items, count});
            mh_set_error(err, MH_ERR_INTERNAL, "allocation failed");
            return MH_ERR_INTERNAL;
        }
        memcpy(copy, grid.locator, len + 1);
        items[count++] = copy;
    }

    out->items = items;
    out->length = count;
    return MH_OK;
}

mh_status mh_to_center_many(
    const char *const *locators,
    size_t n,
    double *lat_out,
    double *lon_out,
    mh_error_context *err
) {
    if (!lat_out || !lon_out) {
        mh_set_error(err, MH_ERR_INTERNAL, "output is required");
        return MH_ERR_INTERNAL;
    }
    if (!locators) {
        mh_set_error(err, MH_ERR_INTERNAL, "inputs are required");
        return MH_ERR_INTERNAL;
    }
    for (size_t i = 0; i < n; i++) {
        char norm[12];
        size_t len = 0;
        mh_status st = mh_normalize_locator_len(locators[i], norm, sizeof(norm), &len, err);
        if (st != MH_OK) {
            return st;
        }
        mh_bbox bbox;
        st = mh_to_bbox_norm(norm, len, &bbox, err);
        if (st != MH_OK) {
            return st;
        }
        lat_out[i] = (bbox.min_lat + bbox.max_lat) / 2.0;
        lon_out[i] = mh_normalize_lon((bbox.min_lon + bbox.max_lon) / 2.0);
    }
    return MH_OK;
}

mh_status mh_to_bbox_many(
    const char *const *locators,
    size_t n,
    mh_bbox *out,
    mh_error_context *err
) {
    if (!out) {
        mh_set_error(err, MH_ERR_INTERNAL, "output is required");
        return MH_ERR_INTERNAL;
    }
    if (!locators) {
        mh_set_error(err, MH_ERR_INTERNAL, "inputs are required");
        return MH_ERR_INTERNAL;
    }
    for (size_t i = 0; i < n; i++) {
        char norm[12];
        size_t len = 0;
        mh_status st = mh_normalize_locator_len(locators[i], norm, sizeof(norm), &len, err);
        if (st != MH_OK) {
            return st;
        }
        st = mh_to_bbox_norm(norm, len, &out[i], err);
        if (st != MH_OK) {
            return st;
        }
    }
    return MH_OK;
}
