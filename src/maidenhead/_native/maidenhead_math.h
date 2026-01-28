#ifndef MH_MAIDENHEAD_MATH_H
#define MH_MAIDENHEAD_MATH_H

#include "errors.h"

int mh_step_size_for_precision(int precision, double *lon_step, double *lat_step, mh_error_context *err);

#endif
