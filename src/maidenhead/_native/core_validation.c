#include "core_validation.h"

#include "core_utils.h"
#include "core_error.h"
#include "constants.h"

int mh_pair_kind(int pair_index) {
    return (pair_index % 2 == 1) ? 1 : 0; /* 1 = letters, 0 = digits */
}

void mh_lon_lat_bases_for_pair(int pair_index, int *lon_base, int *lat_base) {
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

int mh_validate_precision_value(int precision, mh_error_context *err) {
    if (precision < 2) {
        mh_set_error(err, MH_ERR_PRECISION, "precision must be >= 2 characters");
        return 0;
    }
    else if (precision > 10) {
        mh_set_error(err, MH_ERR_PRECISION, "precision must be <= 10 characters");
        return 0;
    }
    else if (precision % 2 != 0) {
        mh_set_error(
            err,
            MH_ERR_PRECISION,
            "precision must be an even number of characters (2, 4, 6, 8 or 10)"
        );
        return 0;
    }
    return 1;
}
