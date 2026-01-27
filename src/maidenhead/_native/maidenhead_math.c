#include "maidenhead_math.h"

#include "core.h"

int mh_step_size_for_precision(int precision, double *lon_step, double *lat_step, mh_error_context *err) {
    mh_status st = mh_validate_precision(precision, err);
    if (st != MH_OK) {
        return st;
    }
    int pairs = precision / 2;
    double lon_cell = 360.0;
    double lat_cell = 180.0;
    for (int i = 1; i <= pairs; i++) {
        int base = 0;
        if (i == 1) {
            base = 18;
        } else if (i % 2 == 0) {
            base = 10;
        } else {
            base = 24;
        }
        lon_cell /= (double)base;
        lat_cell /= (double)base;
    }
    *lon_step = lon_cell;
    *lat_step = lat_cell;
    return MH_OK;
}
