#ifndef MAIDENHEAD_NATIVE_CONSTANTS_H
#define MAIDENHEAD_NATIVE_CONSTANTS_H

/* Canonical ranges */
#define MH_LON_MIN_DEG (-180.0)
#define MH_LON_MAX_DEG (180.0)
#define MH_LAT_MIN_DEG (-90.0)
#define MH_LAT_MAX_DEG (90.0)

#define MH_LON_SPAN_DEG (360.0)
#define MH_LAT_SPAN_DEG (180.0)

/* Field/subsquare bases */
#define MH_FIELD_BASE (18)
#define MH_SQUARE_BASE (10)
#define MH_SUBSQUARE_BASE (24)

/* Numeric stability */
#define MH_CLAMP_EPS_DEG (1e-10)

#endif
