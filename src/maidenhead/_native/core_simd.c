#include "core_simd.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "constants.h"

#if defined(__x86_64__) && defined(__GNUC__)
#include <immintrin.h>
#endif
#if defined(__aarch64__) && defined(__ARM_NEON) && defined(__ARM_FEATURE_FRINT)
#define MH_HAVE_NEON_SIMD 1
#include <arm_neon.h>
#endif

static void mh_set_error(mh_error_context *err, mh_status code, const char *message) {
    if (!err) {
        return;
    }
    err->code = code;
    err->message = message;
    err->key = NULL;
    err->value = NULL;
}

#if defined(__x86_64__) && defined(__GNUC__)
__attribute__((target("avx")))
mh_status mh_from_latlon_many_avx(
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
            mh_status st = mh_from_latlon(lat_buf[lane], lon_buf[lane], precision, 0, &grid, err);
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
mh_status mh_from_latlon_many_neon(
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
            mh_status st = mh_from_latlon(lat_buf[lane], lon_buf[lane], precision, 0, &grid, err);
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
