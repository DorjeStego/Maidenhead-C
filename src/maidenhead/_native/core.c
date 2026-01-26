#include "core.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "constants.h"
#include "core_utils.h"
#include "geo.h"

#if defined(__x86_64__) && defined(__GNUC__)
#include <immintrin.h>
#endif
#if defined(__aarch64__) && defined(__ARM_NEON) && defined(__ARM_FEATURE_FRINT)
#define MH_HAVE_NEON_SIMD 1
#include <arm_neon.h>
#endif

static mh_status mh_step_size_for_precision(
    int precision,
    double *lon_step,
    double *lat_step,
    mh_error_context *err
);

static void mh_set_error(mh_error_context *err, mh_status code, const char *message) {
    if (!err) {
        return;
    }
    err->code = code;
    err->message = message;
    err->key = NULL;
    err->value = NULL;
}

static int mh_pair_kind(int pair_index) {
    return (pair_index % 2 == 1) ? 1 : 0; /* 1 = letters, 0 = digits */
}

static void mh_lon_lat_bases_for_pair(int pair_index, int *lon_base, int *lat_base) {
    if (pair_index == 1) {
        *lon_base = MH_FIELD_BASE;
        *lat_base = MH_FIELD_BASE;
        return;
    }
    if (pair_index == 2) {
        *lon_base = MH_SQUARE_BASE;
        *lat_base = MH_SQUARE_BASE;
        return;
    }
    if (pair_index == 3) {
        *lon_base = MH_SUBSQUARE_BASE;
        *lat_base = MH_SUBSQUARE_BASE;
        return;
    }
    if (mh_pair_kind(pair_index)) {
        *lon_base = MH_SUBSQUARE_BASE;
        *lat_base = MH_SUBSQUARE_BASE;
        return;
    }
    *lon_base = MH_SQUARE_BASE;
    *lat_base = MH_SQUARE_BASE;
}

static int mh_validate_precision_value(int precision, mh_error_context *err) {
    if (precision < 2) {
        mh_set_error(err, MH_ERR_PRECISION, "precision must be >= 2 characters");
        return 0;
    }
    if (precision > 10) {
        mh_set_error(err, MH_ERR_PRECISION, "precision must be <= 10 characters");
        return 0;
    }
    if (precision % 2 != 0) {
        mh_set_error(
            err,
            MH_ERR_PRECISION,
            "precision must be an even number of characters (2, 4, 6, ...)"
        );
        return 0;
    }
    return 1;
}


static int mh_decode_letter(char ch, int pair_index, mh_error_context *err) {
    if (pair_index == 1) {
        char u = (char)toupper((unsigned char)ch);
        if (u < 'A' || u > 'R') {
            mh_set_error(err, MH_ERR_INVALID_LOCATOR, "invalid field letter");
            return -1;
        }
        return (int)(u - 'A');
    }
    char l = (char)tolower((unsigned char)ch);
    if (l < 'a' || l > 'x') {
        mh_set_error(err, MH_ERR_INVALID_LOCATOR, "invalid letter");
        return -1;
    }
    return (int)(l - 'a');
}

static int mh_decode_digit(char ch, mh_error_context *err) {
    if (ch < '0' || ch > '9') {
        mh_set_error(err, MH_ERR_INVALID_LOCATOR, "invalid digit");
        return -1;
    }
    return (int)(ch - '0');
}

static char mh_encode_letter(int idx, int pair_index, mh_error_context *err) {
    if (pair_index == 1) {
        if (idx < 0 || idx >= MH_FIELD_BASE) {
            mh_set_error(err, MH_ERR_INVALID_LOCATOR, "field index out of range");
            return 'A';
        }
        return (char)('A' + idx);
    }
    if (idx < 0 || idx >= MH_SUBSQUARE_BASE) {
        mh_set_error(err, MH_ERR_INVALID_LOCATOR, "letter index out of range");
        return 'a';
    }
    return (char)('a' + idx);
}

static char mh_encode_digit(int idx, mh_error_context *err) {
    if (idx < 0 || idx > 9) {
        mh_set_error(err, MH_ERR_INVALID_LOCATOR, "digit index out of range");
        return '0';
    }
    return (char)('0' + idx);
}

static mh_status mh_step_size_for_precision(
    int precision,
    double *lon_step,
    double *lat_step,
    mh_error_context *err
) {
    if (!lon_step || !lat_step) {
        mh_set_error(err, MH_ERR_INTERNAL, "output is required");
        return MH_ERR_INTERNAL;
    }
    mh_status st = mh_validate_precision(precision, err);
    if (st != MH_OK) {
        return st;
    }
    int pairs = precision / 2;
    double lon_cell = MH_LON_SPAN_DEG;
    double lat_cell = MH_LAT_SPAN_DEG;
    for (int i = 1; i <= pairs; i++) {
        int lon_base = 0;
        int lat_base = 0;
        mh_lon_lat_bases_for_pair(i, &lon_base, &lat_base);
        lon_cell /= (double)lon_base;
        lat_cell /= (double)lat_base;
    }
    *lon_step = lon_cell;
    *lat_step = lat_cell;
    return MH_OK;
}

static mh_status mh_from_latlon_encoded(
    double latf,
    double lonf,
    int precision,
    mh_grid *out,
    mh_error_context *err
) {
    if (!out) {
        mh_set_error(err, MH_ERR_INTERNAL, "output is required");
        return MH_ERR_INTERNAL;
    }
    double x = lonf - MH_LON_MIN_DEG;
    double y = latf - MH_LAT_MIN_DEG;
    double lon_cell = MH_LON_SPAN_DEG;
    double lat_cell = MH_LAT_SPAN_DEG;

    int pair_count = precision / 2;
    for (int i = 0; i < pair_count; i++) {
        int pair_index = i + 1;
        int lon_base = 0;
        int lat_base = 0;
        mh_lon_lat_bases_for_pair(pair_index, &lon_base, &lat_base);

        lon_cell /= (double)lon_base;
        lat_cell /= (double)lat_base;

        int lon_i = (int)floor(x / lon_cell);
        int lat_i = (int)floor(y / lat_cell);

        if (lon_i < 0) {
            lon_i = 0;
        } else if (lon_i >= lon_base) {
            lon_i = lon_base - 1;
        }
        if (lat_i < 0) {
            lat_i = 0;
        } else if (lat_i >= lat_base) {
            lat_i = lat_base - 1;
        }

        x -= lon_i * lon_cell;
        y -= lat_i * lat_cell;

        if (mh_pair_kind(pair_index)) {
            out->locator[2 * i] = mh_encode_letter(lon_i, pair_index, err);
            if (err && err->code != MH_OK) {
                return err->code;
            }
            out->locator[2 * i + 1] = mh_encode_letter(lat_i, pair_index, err);
            if (err && err->code != MH_OK) {
                return err->code;
            }
        } else {
            out->locator[2 * i] = mh_encode_digit(lon_i, err);
            if (err && err->code != MH_OK) {
                return err->code;
            }
            out->locator[2 * i + 1] = mh_encode_digit(lat_i, err);
            if (err && err->code != MH_OK) {
                return err->code;
            }
        }
    }

    out->locator[precision] = '\0';
    out->precision = precision;
    return MH_OK;
}

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

mh_status mh_validate_precision(int precision, mh_error_context *err) {
    if (!mh_validate_precision_value(precision, err)) {
        return err ? err->code : MH_ERR_PRECISION;
    }
    return MH_OK;
}

static mh_status mh_normalize_locator_len(
    const char *input,
    char *output,
    size_t output_len,
    size_t *out_len,
    mh_error_context *err
) {
    if (!input) {
        mh_set_error(err, MH_ERR_INVALID_LOCATOR, "locator must be str");
        return MH_ERR_INVALID_LOCATOR;
    }

    const char *start = input;
    while (*start && isspace((unsigned char)*start)) {
        start++;
    }
    const char *end = input + strlen(input);
    while (end > start && isspace((unsigned char)*(end - 1))) {
        end--;
    }

    size_t len = (size_t)(end - start);
    if (len == 0) {
        mh_set_error(err, MH_ERR_INVALID_LOCATOR, "locator is empty");
        return MH_ERR_INVALID_LOCATOR;
    }
    if (!mh_validate_precision_value((int)len, err)) {
        return err ? err->code : MH_ERR_PRECISION;
    }
    if (output_len < len + 1) {
        mh_set_error(err, MH_ERR_INTERNAL, "output buffer too small");
        return MH_ERR_INTERNAL;
    }

    int pair_count = (int)len / 2;
    for (int i = 0; i < pair_count; i++) {
        int pair_index = i + 1;
        char a = start[2 * i];
        char b = start[2 * i + 1];
        if (mh_pair_kind(pair_index)) {
            int lon_idx = mh_decode_letter(a, pair_index, err);
            if (lon_idx < 0) {
                return err ? err->code : MH_ERR_INVALID_LOCATOR;
            }
            int lat_idx = mh_decode_letter(b, pair_index, err);
            if (lat_idx < 0) {
                return err ? err->code : MH_ERR_INVALID_LOCATOR;
            }
            output[2 * i] = mh_encode_letter(lon_idx, pair_index, err);
            if (err && err->code != MH_OK) {
                return err->code;
            }
            output[2 * i + 1] = mh_encode_letter(lat_idx, pair_index, err);
            if (err && err->code != MH_OK) {
                return err->code;
            }
        } else {
            int lon_idx = mh_decode_digit(a, err);
            if (lon_idx < 0) {
                return err ? err->code : MH_ERR_INVALID_LOCATOR;
            }
            int lat_idx = mh_decode_digit(b, err);
            if (lat_idx < 0) {
                return err ? err->code : MH_ERR_INVALID_LOCATOR;
            }
            output[2 * i] = mh_encode_digit(lon_idx, err);
            if (err && err->code != MH_OK) {
                return err->code;
            }
            output[2 * i + 1] = mh_encode_digit(lat_idx, err);
            if (err && err->code != MH_OK) {
                return err->code;
            }
        }
    }
    output[len] = '\0';
    if (out_len) {
        *out_len = len;
    }
    return MH_OK;
}

static mh_status mh_to_bbox_norm(
    const char *norm,
    size_t len,
    mh_bbox *out,
    mh_error_context *err
) {
    if (!out) {
        mh_set_error(err, MH_ERR_INTERNAL, "output is required");
        return MH_ERR_INTERNAL;
    }
    if (!norm) {
        mh_set_error(err, MH_ERR_INVALID_LOCATOR, "locator must be str");
        return MH_ERR_INVALID_LOCATOR;
    }
    if (!mh_validate_precision_value((int)len, err)) {
        return err ? err->code : MH_ERR_PRECISION;
    }

    int pair_count = (int)len / 2;

    double lon_min = MH_LON_MIN_DEG;
    double lat_min = MH_LAT_MIN_DEG;
    double lon_cell = MH_LON_SPAN_DEG;
    double lat_cell = MH_LAT_SPAN_DEG;

    for (int i = 0; i < pair_count; i++) {
        int pair_index = i + 1;
        int lon_base = 0;
        int lat_base = 0;
        mh_lon_lat_bases_for_pair(pair_index, &lon_base, &lat_base);

        lon_cell /= (double)lon_base;
        lat_cell /= (double)lat_base;

        char a = norm[2 * i];
        char b = norm[2 * i + 1];
        int lon_idx = 0;
        int lat_idx = 0;
        if (mh_pair_kind(pair_index)) {
            lon_idx = mh_decode_letter(a, pair_index, err);
            if (lon_idx < 0) {
                return err ? err->code : MH_ERR_INVALID_LOCATOR;
            }
            lat_idx = mh_decode_letter(b, pair_index, err);
            if (lat_idx < 0) {
                return err ? err->code : MH_ERR_INVALID_LOCATOR;
            }
        } else {
            lon_idx = mh_decode_digit(a, err);
            if (lon_idx < 0) {
                return err ? err->code : MH_ERR_INVALID_LOCATOR;
            }
            lat_idx = mh_decode_digit(b, err);
            if (lat_idx < 0) {
                return err ? err->code : MH_ERR_INVALID_LOCATOR;
            }
        }
        lon_min += lon_idx * lon_cell;
        lat_min += lat_idx * lat_cell;
    }

    out->min_lat = lat_min;
    out->min_lon = lon_min;
    out->max_lat = lat_min + lat_cell;
    out->max_lon = lon_min + lon_cell;
    return MH_OK;
}

mh_status mh_normalize_locator(
    const char *input,
    char *output,
    size_t output_len,
    mh_error_context *err
) {
    return mh_normalize_locator_len(input, output, output_len, NULL, err);
}

mh_status mh_parse_locator(const char *input, mh_grid *out, mh_error_context *err) {
    if (!out) {
        mh_set_error(err, MH_ERR_INTERNAL, "output is required");
        return MH_ERR_INTERNAL;
    }
    mh_status st = mh_normalize_locator(input, out->locator, sizeof(out->locator), err);
    if (st != MH_OK) {
        return st;
    }
    out->precision = (int)strlen(out->locator);
    return MH_OK;
}

mh_status mh_precision_of(const char *locator, int *out, mh_error_context *err) {
    if (!locator || !out) {
        mh_set_error(err, MH_ERR_INVALID_LOCATOR, "locator must be str");
        return MH_ERR_INVALID_LOCATOR;
    }
    size_t len = strlen(locator);
    if (!mh_validate_precision_value((int)len, err)) {
        return err ? err->code : MH_ERR_PRECISION;
    }
    *out = (int)len;
    return MH_OK;
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

mh_status mh_to_bbox(const char *locator, mh_bbox *out, mh_error_context *err) {
    char norm[12];
    mh_status st;
    if (!out) {
        mh_set_error(err, MH_ERR_INTERNAL, "output is required");
        return MH_ERR_INTERNAL;
    }
    st = mh_normalize_locator(locator, norm, sizeof(norm), err);
    if (st != MH_OK) {
        return st;
    }
    return mh_to_bbox_norm(norm, strlen(norm), out, err);
}

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

mh_status mh_from_latlon(
    double lat,
    double lon,
    int precision,
    int clamp,
    mh_grid *out,
    mh_error_context *err
) {
    if (!out) {
        mh_set_error(err, MH_ERR_INTERNAL, "output is required");
        return MH_ERR_INTERNAL;
    }
    if (!(precision == 2 || precision == 4 || precision == 6 || precision == 8 || precision == 10)) {
        mh_set_error(err, MH_ERR_PRECISION, "precision must be one of 2, 4, 6, 8, 10");
        return MH_ERR_PRECISION;
    }
    if (!mh_validate_precision_value(precision, err)) {
        return err ? err->code : MH_ERR_PRECISION;
    }

    double latf = lat;
    double lonf = lon;

    if (clamp) {
        lonf = mh_normalize_lon(lonf);
        latf = mh_clamp_lat(latf);

        if (lonf >= MH_LON_MAX_DEG) {
            lonf = MH_LON_MAX_DEG - MH_CLAMP_EPS_DEG;
        }
        if (latf >= MH_LAT_MAX_DEG) {
            latf = MH_LAT_MAX_DEG - MH_CLAMP_EPS_DEG;
        }
        if (lonf <= MH_LON_MIN_DEG) {
            lonf = MH_LON_MIN_DEG + MH_CLAMP_EPS_DEG;
        }
        if (latf <= MH_LAT_MIN_DEG) {
            latf = MH_LAT_MIN_DEG + MH_CLAMP_EPS_DEG;
        }
    } else {
        if (latf < MH_LAT_MIN_DEG || latf > MH_LAT_MAX_DEG) {
            mh_set_error(err, MH_ERR_OUT_OF_RANGE, "lat out of range [-90, 90]");
            return MH_ERR_OUT_OF_RANGE;
        }
        lonf = mh_normalize_lon(lonf);
    }

    return mh_from_latlon_encoded(latf, lonf, precision, out, err);
}

mh_status mh_format_locator(
    const char *locator,
    int precision,
    int mode,
    mh_grid *out,
    mh_error_context *err
) {
    if (!out) {
        mh_set_error(err, MH_ERR_INTERNAL, "output is required");
        return MH_ERR_INTERNAL;
    }
    if (!locator) {
        mh_set_error(err, MH_ERR_INVALID_LOCATOR, "locator must be str");
        return MH_ERR_INVALID_LOCATOR;
    }
    if (!mh_validate_precision_value(precision, err)) {
        return err ? err->code : MH_ERR_PRECISION;
    }

    char norm[12];
    mh_status st = mh_normalize_locator(locator, norm, sizeof(norm), err);
    if (st != MH_OK) {
        return st;
    }

    int p = (int)strlen(norm);
    if (precision == p) {
        strncpy(out->locator, norm, sizeof(out->locator));
        out->locator[sizeof(out->locator) - 1] = '\0';
        out->precision = p;
        return MH_OK;
    }

    if (mode == 2) { /* error */
        mh_set_error(err, MH_ERR_PRECISION, "precision does not match locator");
        return MH_ERR_PRECISION;
    }

    if (precision < p) {
        if (mode != 0 && mode != 1) {
            mh_set_error(err, MH_ERR_INVALID_LOCATOR, "Unknown mode");
            return MH_ERR_INVALID_LOCATOR;
        }
        memcpy(out->locator, norm, (size_t)precision);
        out->locator[precision] = '\0';
        out->precision = precision;
        return MH_OK;
    }

    if (mode == 0) { /* truncate */
        mh_set_error(err, MH_ERR_PRECISION, "cannot truncate to a higher precision");
        return MH_ERR_PRECISION;
    }

    if (mode != 1) {
        mh_set_error(err, MH_ERR_INVALID_LOCATOR, "Unknown mode");
        return MH_ERR_INVALID_LOCATOR;
    }

    double lat = 0.0;
    double lon = 0.0;
    st = mh_to_center_latlon(norm, &lat, &lon, err);
    if (st != MH_OK) {
        return st;
    }
    return mh_from_latlon(lat, lon, precision, 1, out, err);
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

mh_status mh_cell_size_deg(
    const char *locator,
    double *width_deg,
    double *height_deg,
    mh_error_context *err
) {
    if (!width_deg || !height_deg) {
        mh_set_error(err, MH_ERR_INTERNAL, "output is required");
        return MH_ERR_INTERNAL;
    }
    int precision = 0;
    mh_status st = mh_precision_of(locator, &precision, err);
    if (st != MH_OK) {
        return st;
    }
    double lon_step = 0.0;
    double lat_step = 0.0;
    st = mh_step_size_for_precision(precision, &lon_step, &lat_step, err);
    if (st != MH_OK) {
        return st;
    }
    *width_deg = lon_step;
    *height_deg = lat_step;
    return MH_OK;
}

mh_status mh_cell_size_km(
    const char *locator,
    int use_at_lat,
    double at_lat,
    int method,
    double *width_km,
    double *height_km,
    mh_error_context *err
) {
    if (!width_km || !height_km) {
        mh_set_error(err, MH_ERR_INTERNAL, "output is required");
        return MH_ERR_INTERNAL;
    }
    if (method != MH_DISTANCE_HAVERSINE && method != MH_DISTANCE_GEODESIC) {
        mh_set_error(err, MH_ERR_UNSUPPORTED, "unknown distance method");
        return MH_ERR_UNSUPPORTED;
    }
    int precision = 0;
    mh_status st = mh_precision_of(locator, &precision, err);
    if (st != MH_OK) {
        return st;
    }
    double lon_step = 0.0;
    double lat_step = 0.0;
    st = mh_step_size_for_precision(precision, &lon_step, &lat_step, err);
    if (st != MH_OK) {
        return st;
    }
    double lat = 0.0;
    double lon = 0.0;
    st = mh_to_center_latlon(locator, &lat, &lon, err);
    if (st != MH_OK) {
        return st;
    }
    double half_lon = lon_step / 2.0;
    double half_lat = lat_step / 2.0;
    mh_point a = {use_at_lat ? at_lat : lat, lon - half_lon};
    mh_point b = {use_at_lat ? at_lat : lat, lon + half_lon};
    mh_point c = {lat - half_lat, lon};
    mh_point d = {lat + half_lat, lon};
    st = mh_distance_km(&a, &b, method, width_km, err);
    if (st != MH_OK) {
        return st;
    }
    st = mh_distance_km(&c, &d, method, height_km, err);
    if (st != MH_OK) {
        return st;
    }
    return MH_OK;
}

mh_status mh_area_km2(
    const char *locator,
    int method,
    double *out,
    mh_error_context *err
) {
    if (!out) {
        mh_set_error(err, MH_ERR_INTERNAL, "output is required");
        return MH_ERR_INTERNAL;
    }
    if (method != MH_DISTANCE_HAVERSINE && method != MH_DISTANCE_GEODESIC) {
        mh_set_error(err, MH_ERR_UNSUPPORTED, "unknown distance method");
        return MH_ERR_UNSUPPORTED;
    }
    if (method == MH_DISTANCE_GEODESIC) {
        mh_bbox bbox;
        mh_status st = mh_to_bbox(locator, &bbox, err);
        if (st != MH_OK) {
            return st;
        }
        mh_point poly[4] = {
            {bbox.min_lat, bbox.min_lon},
            {bbox.min_lat, bbox.max_lon},
            {bbox.max_lat, bbox.max_lon},
            {bbox.max_lat, bbox.min_lon},
        };
        double area_km2 = 0.0;
        st = mh_geodesic_area_km2(poly, 4, &area_km2, err);
        if (st != MH_OK) {
            return st;
        }
        if (area_km2 < 0.0) {
            area_km2 = -area_km2;
        }
        *out = area_km2;
        return MH_OK;
    }

    int precision = 0;
    mh_status st = mh_precision_of(locator, &precision, err);
    if (st != MH_OK) {
        return st;
    }
    double lon_step = 0.0;
    double lat_step = 0.0;
    st = mh_step_size_for_precision(precision, &lon_step, &lat_step, err);
    if (st != MH_OK) {
        return st;
    }
    double lat = 0.0;
    double lon = 0.0;
    st = mh_to_center_latlon(locator, &lat, &lon, err);
    if (st != MH_OK) {
        return st;
    }
    double half_lon = lon_step / 2.0;
    double half_lat = lat_step / 2.0;
    mh_point a = {lat, lon - half_lon};
    mh_point b = {lat, lon + half_lon};
    mh_point c = {lat - half_lat, lon};
    mh_point d = {lat + half_lat, lon};
    double width_km = 0.0;
    double height_km = 0.0;
    st = mh_distance_km(&a, &b, MH_DISTANCE_HAVERSINE, &width_km, err);
    if (st != MH_OK) {
        return st;
    }
    st = mh_distance_km(&c, &d, MH_DISTANCE_HAVERSINE, &height_km, err);
    if (st != MH_OK) {
        return st;
    }
    *out = width_km * height_km;
    return MH_OK;
}

mh_status mh_diagonal_km(
    const char *locator,
    int method,
    double *out,
    mh_error_context *err
) {
    if (!out) {
        mh_set_error(err, MH_ERR_INTERNAL, "output is required");
        return MH_ERR_INTERNAL;
    }
    if (method != MH_DISTANCE_HAVERSINE && method != MH_DISTANCE_GEODESIC) {
        mh_set_error(err, MH_ERR_UNSUPPORTED, "unknown distance method");
        return MH_ERR_UNSUPPORTED;
    }
    mh_bbox bbox;
    mh_status st = mh_to_bbox(locator, &bbox, err);
    if (st != MH_OK) {
        return st;
    }
    mh_point a = {bbox.min_lat, bbox.min_lon};
    mh_point b = {bbox.max_lat, bbox.max_lon};
    st = mh_distance_km(&a, &b, method, out, err);
    if (st != MH_OK) {
        return st;
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

#if defined(__x86_64__) && defined(__GNUC__)
__attribute__((target("avx")))
static mh_status mh_from_latlon_many_avx(
    const double *lats,
    const double *lons,
    size_t n,
    int precision,
    char **items,
    size_t *count,
    mh_error_context *err
) {
    const __m256d v180 = _mm256_set1_pd(180.0);
    const __m256d vneg180 = _mm256_set1_pd(-180.0);
    const __m256d v360 = _mm256_set1_pd(360.0);
    const __m256d v90 = _mm256_set1_pd(90.0);
    const __m256d vneg90 = _mm256_set1_pd(-90.0);
    const __m256d veps = _mm256_set1_pd(MH_CLAMP_EPS_DEG);
    const __m256d vzero = _mm256_setzero_pd();

    size_t i = 0;
    for (; i + 3 < n; i += 4) {
        __m256d lat = _mm256_loadu_pd(lats + i);
        __m256d lon = _mm256_loadu_pd(lons + i);

        __m256d wrapped = _mm256_add_pd(lon, v180);
        __m256d q = _mm256_div_pd(wrapped, v360);
        __m256d qfloor = _mm256_floor_pd(q);
        wrapped = _mm256_sub_pd(wrapped, _mm256_mul_pd(qfloor, v360));
        __m256d mask_neg = _mm256_cmp_pd(wrapped, vzero, _CMP_LT_OQ);
        wrapped = _mm256_add_pd(wrapped, _mm256_and_pd(mask_neg, v360));
        __m256d lonf = _mm256_sub_pd(wrapped, v180);

        lat = _mm256_min_pd(lat, v90);
        lat = _mm256_max_pd(lat, vneg90);

        __m256d mask_lat_ge = _mm256_cmp_pd(lat, v90, _CMP_GE_OQ);
        lat = _mm256_sub_pd(lat, _mm256_and_pd(mask_lat_ge, veps));
        __m256d mask_lat_le = _mm256_cmp_pd(lat, vneg90, _CMP_LE_OQ);
        lat = _mm256_add_pd(lat, _mm256_and_pd(mask_lat_le, veps));

        __m256d mask_lon_ge = _mm256_cmp_pd(lonf, v180, _CMP_GE_OQ);
        lonf = _mm256_sub_pd(lonf, _mm256_and_pd(mask_lon_ge, veps));
        __m256d mask_lon_le = _mm256_cmp_pd(lonf, vneg180, _CMP_LE_OQ);
        lonf = _mm256_add_pd(lonf, _mm256_and_pd(mask_lon_le, veps));

        double lat_buf[4];
        double lon_buf[4];
        _mm256_storeu_pd(lat_buf, lat);
        _mm256_storeu_pd(lon_buf, lonf);

        for (int lane = 0; lane < 4; lane++) {
            mh_grid grid;
            mh_status st = mh_from_latlon_encoded(lat_buf[lane], lon_buf[lane], precision, &grid, err);
            if (st != MH_OK) {
                return st;
            }
            size_t len = strlen(grid.locator);
            char *copy = (char *)malloc(len + 1);
            if (!copy) {
                mh_set_error(err, MH_ERR_INTERNAL, "allocation failed");
                return MH_ERR_INTERNAL;
            }
            memcpy(copy, grid.locator, len + 1);
            items[(*count)++] = copy;
        }
    }

    for (; i < n; i++) {
        mh_grid grid;
        mh_status st = mh_from_latlon(lats[i], lons[i], precision, 1, &grid, err);
        if (st != MH_OK) {
            return st;
        }
        size_t len = strlen(grid.locator);
        char *copy = (char *)malloc(len + 1);
        if (!copy) {
            mh_set_error(err, MH_ERR_INTERNAL, "allocation failed");
            return MH_ERR_INTERNAL;
        }
        memcpy(copy, grid.locator, len + 1);
        items[(*count)++] = copy;
    }

    return MH_OK;
}
#endif

#if defined(MH_HAVE_NEON_SIMD)
__attribute__((target("neon")))
static mh_status mh_from_latlon_many_neon(
    const double *lats,
    const double *lons,
    size_t n,
    int precision,
    char **items,
    size_t *count,
    mh_error_context *err
) {
    const float64x2_t v180 = vdupq_n_f64(180.0);
    const float64x2_t vneg180 = vdupq_n_f64(-180.0);
    const float64x2_t v360 = vdupq_n_f64(360.0);
    const float64x2_t v90 = vdupq_n_f64(90.0);
    const float64x2_t vneg90 = vdupq_n_f64(-90.0);
    const float64x2_t veps = vdupq_n_f64(MH_CLAMP_EPS_DEG);
    const float64x2_t vzero = vdupq_n_f64(0.0);

    size_t i = 0;
    for (; i + 1 < n; i += 2) {
        float64x2_t lat = vld1q_f64(lats + i);
        float64x2_t lon = vld1q_f64(lons + i);

        float64x2_t wrapped = vaddq_f64(lon, v180);
        float64x2_t q = vdivq_f64(wrapped, v360);
        float64x2_t qfloor = vrndmq_f64(q);
        wrapped = vsubq_f64(wrapped, vmulq_f64(qfloor, v360));
        uint64x2_t mask_neg = vcltq_f64(wrapped, vzero);
        wrapped = vaddq_f64(wrapped, vbslq_f64(mask_neg, v360, vzero));
        float64x2_t lonf = vsubq_f64(wrapped, v180);

        lat = vminq_f64(lat, v90);
        lat = vmaxq_f64(lat, vneg90);

        uint64x2_t mask_lat_ge = vcgeq_f64(lat, v90);
        lat = vsubq_f64(lat, vbslq_f64(mask_lat_ge, veps, vzero));
        uint64x2_t mask_lat_le = vcleq_f64(lat, vneg90);
        lat = vaddq_f64(lat, vbslq_f64(mask_lat_le, veps, vzero));

        uint64x2_t mask_lon_ge = vcgeq_f64(lonf, v180);
        lonf = vsubq_f64(lonf, vbslq_f64(mask_lon_ge, veps, vzero));
        uint64x2_t mask_lon_le = vcleq_f64(lonf, vneg180);
        lonf = vaddq_f64(lonf, vbslq_f64(mask_lon_le, veps, vzero));

        double lat_buf[2];
        double lon_buf[2];
        vst1q_f64(lat_buf, lat);
        vst1q_f64(lon_buf, lonf);

        for (int lane = 0; lane < 2; lane++) {
            mh_grid grid;
            mh_status st = mh_from_latlon_encoded(lat_buf[lane], lon_buf[lane], precision, &grid, err);
            if (st != MH_OK) {
                return st;
            }
            size_t len = strlen(grid.locator);
            char *copy = (char *)malloc(len + 1);
            if (!copy) {
                mh_set_error(err, MH_ERR_INTERNAL, "allocation failed");
                return MH_ERR_INTERNAL;
            }
            memcpy(copy, grid.locator, len + 1);
            items[(*count)++] = copy;
        }
    }

    for (; i < n; i++) {
        mh_grid grid;
        mh_status st = mh_from_latlon(lats[i], lons[i], precision, 1, &grid, err);
        if (st != MH_OK) {
            return st;
        }
        size_t len = strlen(grid.locator);
        char *copy = (char *)malloc(len + 1);
        if (!copy) {
            mh_set_error(err, MH_ERR_INTERNAL, "allocation failed");
            return MH_ERR_INTERNAL;
        }
        memcpy(copy, grid.locator, len + 1);
        items[(*count)++] = copy;
    }

    return MH_OK;
}
#endif

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
    if (!mh_validate_precision_value(precision, err)) {
        return err ? err->code : MH_ERR_PRECISION;
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
