#ifndef MAIDENHEAD_NATIVE_CORE_SIMD_H
#define MAIDENHEAD_NATIVE_CORE_SIMD_H

#include "core.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__x86_64__) && defined(__GNUC__)
mh_status mh_from_latlon_many_avx(
    const double *lats,
    const double *lons,
    size_t n,
    int precision,
    char **items,
    size_t *count,
    mh_error_context *err
);
#endif

#if defined(MH_HAVE_NEON_SIMD)
mh_status mh_from_latlon_many_neon(
    const double *lats,
    const double *lons,
    size_t n,
    int precision,
    char **items,
    size_t *count,
    mh_error_context *err
);
#endif

#ifdef __cplusplus
}
#endif

#endif
