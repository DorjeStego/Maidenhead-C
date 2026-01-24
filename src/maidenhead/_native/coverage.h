#ifndef MAIDENHEAD_NATIVE_COVERAGE_H
#define MAIDENHEAD_NATIVE_COVERAGE_H

#include <stddef.h>

#include "errors.h"
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MH_LINE_GREATCIRCLE (0)
#define MH_LINE_GEODESIC (1)

mh_status mh_cover_circle(
    const mh_point *center,
    double radius_km,
    int precision,
    mh_list *out,
    mh_error_context *err
);

mh_status mh_cover_line(
    const mh_point *a,
    const mh_point *b,
    int precision,
    int method,
    mh_list *out,
    mh_error_context *err
);

#ifdef __cplusplus
}
#endif

#endif
