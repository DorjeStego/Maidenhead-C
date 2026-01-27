#ifndef MAIDENHEAD_NATIVE_CORE_UTILS_H
#define MAIDENHEAD_NATIVE_CORE_UTILS_H

#include <stddef.h>

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

double mh_normalize_lon(double lon);
double mh_clamp_lat(double lat);
double mh_wrap_lon_near(double lon, double ref);
int mh_lon_overlap(double a_min, double a_max, double b_min, double b_max);
int mh_point_in_poly(const mh_point *poly, size_t n, double lat, double lon);
int mh_segments_intersect(mh_point a1, mh_point a2, mh_point b1, mh_point b2);

#ifdef __cplusplus
}
#endif

#endif
