#include "core_locator.h"

#include <ctype.h>
#include <math.h>
#include <string.h>

#include "constants.h"
#include "core_internal.h"
#include "core_utils.h"
#include "core_error.h"

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
    if (!(precision % 2 == 0 && precision <= 10)) {
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

char mh_encode_letter(int idx, int pair_index, mh_error_context *err) {
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

char mh_encode_digit(int idx, mh_error_context *err) {
    if (idx < 0 || idx > 9) {
        mh_set_error(err, MH_ERR_INVALID_LOCATOR, "digit index out of range");
        return '0';
    }
    return (char)('0' + idx);
}

mh_status mh_from_latlon_encoded(
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

mh_status mh_normalize_locator_len(
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

mh_status mh_to_bbox_norm(
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
