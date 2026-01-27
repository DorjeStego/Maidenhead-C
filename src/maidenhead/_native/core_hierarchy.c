#include "core_hierarchy.h"

#include <string.h>

#include "core_internal.h"
#include "core_error.h"

static mh_status mh_children_recurse(
    char *buffer,
    int pair_index,
    int end_pairs,
    int (*callback)(const char *locator, void *userdata),
    void *userdata,
    mh_error_context *err
) {
    if (pair_index > end_pairs) {
        if (callback(buffer, userdata) != 0) {
            mh_set_error(err, MH_ERR_INTERNAL, "callback failed");
            return MH_ERR_INTERNAL;
        }
        return MH_OK;
    }

    int lon_base = 0;
    int lat_base = 0;
    mh_lon_lat_bases_for_pair(pair_index, &lon_base, &lat_base);

    int offset = (pair_index - 1) * 2;
    if (mh_pair_kind(pair_index)) {
        for (int lon_i = 0; lon_i < lon_base; lon_i++) {
            for (int lat_i = 0; lat_i < lat_base; lat_i++) {
                buffer[offset] = mh_encode_letter(lon_i, pair_index, err);
                if (err && err->code != MH_OK) {
                    return err->code;
                }
                buffer[offset + 1] = mh_encode_letter(lat_i, pair_index, err);
                if (err && err->code != MH_OK) {
                    return err->code;
                }
                mh_status st = mh_children_recurse(
                    buffer,
                    pair_index + 1,
                    end_pairs,
                    callback,
                    userdata,
                    err
                );
                if (st != MH_OK) {
                    return st;
                }
            }
        }
    } else {
        for (int lon_i = 0; lon_i < lon_base; lon_i++) {
            for (int lat_i = 0; lat_i < lat_base; lat_i++) {
                buffer[offset] = mh_encode_digit(lon_i, err);
                if (err && err->code != MH_OK) {
                    return err->code;
                }
                buffer[offset + 1] = mh_encode_digit(lat_i, err);
                if (err && err->code != MH_OK) {
                    return err->code;
                }
                mh_status st = mh_children_recurse(
                    buffer,
                    pair_index + 1,
                    end_pairs,
                    callback,
                    userdata,
                    err
                );
                if (st != MH_OK) {
                    return st;
                }
            }
        }
    }
    return MH_OK;
}

mh_status mh_parent(
    const char *locator,
    int precision,
    mh_grid *out,
    mh_error_context *err
) {
    if (!out) {
        mh_set_error(err, MH_ERR_INTERNAL, "output is required");
        return MH_ERR_INTERNAL;
    }
    char norm[12];
    mh_status st = mh_normalize_locator(locator, norm, sizeof(norm), err);
    if (st != MH_OK) {
        return st;
    }
    int p = (int)strlen(norm);

    int target = precision;
    if (precision < 0) {
        if (p <= 2) {
            mh_set_error(err, MH_ERR_PRECISION, "cannot parent a 2-character locator");
            return MH_ERR_PRECISION;
        }
        target = p - 2;
    } else {
        if (!mh_validate_precision_value(precision, err)) {
            return err ? err->code : MH_ERR_PRECISION;
        }
        if (precision >= p) {
            mh_set_error(err, MH_ERR_PRECISION, "parent precision must be less than locator precision");
            return MH_ERR_PRECISION;
        }
    }

    memcpy(out->locator, norm, (size_t)target);
    out->locator[target] = '\0';
    out->precision = target;
    return MH_OK;
}

mh_status mh_children_iter(
    const char *locator,
    int precision,
    int (*callback)(const char *locator, void *userdata),
    void *userdata,
    mh_error_context *err
) {
    if (!callback) {
        mh_set_error(err, MH_ERR_INTERNAL, "callback is required");
        return MH_ERR_INTERNAL;
    }
    char norm[12];
    mh_status st = mh_normalize_locator(locator, norm, sizeof(norm), err);
    if (st != MH_OK) {
        return st;
    }
    int p0 = (int)strlen(norm);
    if (!mh_validate_precision_value(precision, err)) {
        return err ? err->code : MH_ERR_PRECISION;
    }
    if (precision <= p0) {
        mh_set_error(err, MH_ERR_PRECISION, "child precision must be greater than locator precision");
        return MH_ERR_PRECISION;
    }

    int start_pairs = p0 / 2;
    int end_pairs = precision / 2;
    char buffer[12];
    memcpy(buffer, norm, (size_t)p0);
    buffer[precision] = '\0';

    return mh_children_recurse(
        buffer,
        start_pairs + 1,
        end_pairs,
        callback,
        userdata,
        err
    );
}
