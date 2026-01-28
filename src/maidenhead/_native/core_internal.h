#ifndef MAIDENHEAD_NATIVE_CORE_INTERNAL_H
#define MAIDENHEAD_NATIVE_CORE_INTERNAL_H

#include "core.h"

#ifdef __cplusplus
extern "C" {
#endif

mh_status mh_normalize_locator_len(
    const char *input,
    char *output,
    size_t output_len,
    size_t *out_len,
    mh_error_context *err
);

mh_status mh_to_bbox_norm(
    const char *norm,
    size_t len,
    mh_bbox *out,
    mh_error_context *err
);

int mh_validate_precision_value(int precision, mh_error_context *err);
int mh_pair_kind(int pair_index);
void mh_lon_lat_bases_for_pair(int pair_index, int *lon_base, int *lat_base);
char mh_encode_letter(int idx, int pair_index, mh_error_context *err);
char mh_encode_digit(int idx, mh_error_context *err);
mh_status mh_from_latlon_encoded(
    double latf,
    double lonf,
    int precision,
    mh_grid *out,
    mh_error_context *err
);

#ifdef __cplusplus
}
#endif

#endif
