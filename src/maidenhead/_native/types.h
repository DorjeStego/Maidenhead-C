#ifndef MAIDENHEAD_NATIVE_TYPES_H
#define MAIDENHEAD_NATIVE_TYPES_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Grid square value object. Locator is null-terminated. */
typedef struct mh_grid {
    char locator[12]; /* up to 10 chars + null + spare */
    int precision;    /* 2,4,6,8,10 */
} mh_grid;

typedef struct mh_point {
    double lat;
    double lon;
} mh_point;

typedef struct mh_bbox {
    double min_lat;
    double min_lon;
    double max_lat;
    double max_lon;
} mh_bbox;

typedef struct mh_corners_t {
    mh_point nw;
    mh_point ne;
    mh_point sw;
    mh_point se;
} mh_corners_t;

/* Simple list carriers for bulk APIs (owned by caller). */
typedef struct mh_list {
    char **items;
    size_t length;
} mh_list;

typedef struct mh_kv_list {
    const char **keys;
    const char **values;
    size_t length;
} mh_kv_list;

/* Polygon point list for intersects */
typedef struct mh_polygon {
    const mh_point *points;
    size_t length;
} mh_polygon;

#ifdef __cplusplus
}
#endif

#endif
