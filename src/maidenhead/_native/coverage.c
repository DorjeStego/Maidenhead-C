#include "coverage.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "core.h"
#include "geo.h"

static void mh_set_error(mh_error_context *err, mh_status code, const char *message) {
    if (!err) {
        return;
    }
    err->code = code;
    err->message = message;
    err->key = NULL;
    err->value = NULL;
}

mh_status mh_cover_circle(
    const mh_point *center,
    double radius_km,
    int precision,
    mh_list *out,
    mh_error_context *err
) {
    if (!center || !out) {
        mh_set_error(err, MH_ERR_INTERNAL, "output is required");
        return MH_ERR_INTERNAL;
    }
    if (radius_km <= 0.0) {
        mh_set_error(err, MH_ERR_OUT_OF_RANGE, "radius_km must be > 0");
        return MH_ERR_OUT_OF_RANGE;
    }

    double lat_c = center->lat;
    double lon_c = center->lon;

    double lat_deg = radius_km / 111.32;
    double cos_lat = cos(lat_c * (M_PI / 180.0));
    double lon_deg = (fabs(cos_lat) < 1e-12) ? 180.0 : (radius_km / (111.32 * cos_lat));

    double min_lat = lat_c - lat_deg;
    double max_lat = lat_c + lat_deg;

    if (min_lat < -90.0) {
        min_lat = -90.0;
    }
    if (max_lat > 90.0) {
        max_lat = 90.0;
    }

    double min_lon = lon_c - lon_deg;
    double max_lon = lon_c + lon_deg;

    mh_bbox base_bbox;
    mh_status st;
    mh_grid center_loc;
    st = mh_from_latlon(lat_c, lon_c, precision, 1, &center_loc, err);
    if (st != MH_OK) {
        return st;
    }
    st = mh_to_bbox(center_loc.locator, &base_bbox, err);
    if (st != MH_OK) {
        return st;
    }

    double lon_step = base_bbox.max_lon - base_bbox.min_lon;
    double lat_step = base_bbox.max_lat - base_bbox.min_lat;

    size_t cap = 128;
    char **items = (char **)calloc(cap, sizeof(char *));
    if (!items) {
        mh_set_error(err, MH_ERR_INTERNAL, "allocation failed");
        return MH_ERR_INTERNAL;
    }

    size_t count = 0;
    char *center_copy = (char *)malloc(strlen(center_loc.locator) + 1);
    if (!center_copy) {
        free(items);
        mh_set_error(err, MH_ERR_INTERNAL, "allocation failed");
        return MH_ERR_INTERNAL;
    }
    strcpy(center_copy, center_loc.locator);
    items[count++] = center_copy;

    double lon_ranges[2][2];
    int range_count = 1;
    if (min_lon <= max_lon) {
        lon_ranges[0][0] = min_lon;
        lon_ranges[0][1] = max_lon;
    } else {
        lon_ranges[0][0] = min_lon;
        lon_ranges[0][1] = 180.0;
        lon_ranges[1][0] = -180.0;
        lon_ranges[1][1] = max_lon;
        range_count = 2;
    }

    double lat = min_lat + lat_step / 2.0;
    while (lat <= max_lat + 1e-12) {
        for (int r = 0; r < range_count; r++) {
            double lon = lon_ranges[r][0] + lon_step / 2.0;
            while (lon <= lon_ranges[r][1] + 1e-12) {
                mh_grid loc;
                st = mh_from_latlon(lat, lon, precision, 1, &loc, err);
                if (st != MH_OK) {
                    mh_free_list(&(mh_list){items, count});
                    return st;
                }
                int duplicate = 0;
                for (size_t i = 0; i < count; i++) {
                    if (strcmp(items[i], loc.locator) == 0) {
                        duplicate = 1;
                        break;
                    }
                }
                if (!duplicate) {
                    double clat = 0.0;
                    double clon = 0.0;
                    st = mh_to_center_latlon(loc.locator, &clat, &clon, err);
                    if (st != MH_OK) {
                        mh_free_list(&(mh_list){items, count});
                        return st;
                    }
                    mh_point a = {lat_c, lon_c};
                    mh_point b = {clat, clon};
                    double dist = 0.0;
                    st = mh_distance_km(&a, &b, MH_DISTANCE_HAVERSINE, &dist, err);
                    if (st != MH_OK) {
                        mh_free_list(&(mh_list){items, count});
                        return st;
                    }
                    mh_bbox loc_bbox;
                    st = mh_to_bbox(loc.locator, &loc_bbox, err);
                    if (st != MH_OK) {
                        mh_free_list(&(mh_list){items, count});
                        return st;
                    }
                    mh_point sw = {loc_bbox.min_lat, loc_bbox.min_lon};
                    mh_point ne = {loc_bbox.max_lat, loc_bbox.max_lon};
                    double diag = 0.0;
                    st = mh_distance_km(&sw, &ne, MH_DISTANCE_HAVERSINE, &diag, err);
                    if (st != MH_OK) {
                        mh_free_list(&(mh_list){items, count});
                        return st;
                    }
                    if (dist <= radius_km + diag / 2.0) {
                        if (count == cap) {
                            size_t new_cap = cap * 2;
                            char **new_items = (char **)realloc(items, new_cap * sizeof(char *));
                            if (!new_items) {
                                mh_free_list(&(mh_list){items, count});
                                mh_set_error(err, MH_ERR_INTERNAL, "allocation failed");
                                return MH_ERR_INTERNAL;
                            }
                            items = new_items;
                            cap = new_cap;
                        }
                        char *copy = (char *)malloc(strlen(loc.locator) + 1);
                        if (!copy) {
                            mh_free_list(&(mh_list){items, count});
                            mh_set_error(err, MH_ERR_INTERNAL, "allocation failed");
                            return MH_ERR_INTERNAL;
                        }
                        strcpy(copy, loc.locator);
                        items[count++] = copy;
                    }
                }
                lon += lon_step;
            }
        }
        lat += lat_step;
    }

    out->items = items;
    out->length = count;
    return MH_OK;
}

mh_status mh_cover_line(
    const mh_point *a,
    const mh_point *b,
    int precision,
    int method,
    mh_list *out,
    mh_error_context *err
) {
    if (!a || !b || !out) {
        mh_set_error(err, MH_ERR_INTERNAL, "output is required");
        return MH_ERR_INTERNAL;
    }
    if (precision % 2 != 0) {
        mh_set_error(err, MH_ERR_PRECISION, "precision must be an even number of characters (2, 4, 6, ...)");
        return MH_ERR_PRECISION;
    }

    double total = 0.0;
    mh_status st = mh_distance_km(a, b, MH_DISTANCE_HAVERSINE, &total, err);
    if (st != MH_OK) {
        return st;
    }

    double mid_lat = (a->lat + b->lat) / 2.0;
    double mid_lon = (a->lon + b->lon) / 2.0;
    mh_grid mid_loc;
    st = mh_from_latlon(mid_lat, mid_lon, precision, 1, &mid_loc, err);
    if (st != MH_OK) {
        return st;
    }
    mh_bbox mid_bbox;
    st = mh_to_bbox(mid_loc.locator, &mid_bbox, err);
    if (st != MH_OK) {
        return st;
    }
    mh_point sw = {mid_bbox.min_lat, mid_bbox.min_lon};
    mh_point ne = {mid_bbox.max_lat, mid_bbox.max_lon};
    double step = 0.0;
    st = mh_distance_km(&sw, &ne, MH_DISTANCE_HAVERSINE, &step, err);
    if (st != MH_OK) {
        return st;
    }

    int steps = (int)ceil(total / fmax(step, 1e-6));
    if (steps < 1) {
        steps = 1;
    }

    size_t cap = (size_t)steps + 2;
    char **items = (char **)calloc(cap, sizeof(char *));
    if (!items) {
        mh_set_error(err, MH_ERR_INTERNAL, "allocation failed");
        return MH_ERR_INTERNAL;
    }
    size_t count = 0;

    mh_point *geo_points = NULL;
    if (method == MH_LINE_GEODESIC) {
        const size_t npts = (size_t)steps + 1;
        double *fractions = (double *)malloc(npts * sizeof(double));
        geo_points = (mh_point *)malloc(npts * sizeof(mh_point));
        if (!fractions || !geo_points) {
            free(fractions);
            free(geo_points);
            mh_free_list(&(mh_list){items, count});
            mh_set_error(err, MH_ERR_INTERNAL, "allocation failed");
            return MH_ERR_INTERNAL;
        }
        for (size_t i = 0; i < npts; i++) {
            fractions[i] = (double)i / (double)steps;
        }
        mh_status st_geo = mh_geodesic_line_points_many(a, b, fractions, npts, geo_points, err);
        free(fractions);
        if (st_geo != MH_OK) {
            free(geo_points);
            mh_free_list(&(mh_list){items, count});
            return st_geo;
        }
    }

    for (int i = 0; i <= steps; i++) {
        double frac = (double)i / (double)steps;
        mh_point pt;
        if (method == MH_LINE_GEODESIC) {
            pt = geo_points[(size_t)i];
        } else {
            mh_point a_rad = *a;
            mh_point b_rad = *b;
            double lat1 = a_rad.lat * (M_PI / 180.0);
            double lon1 = a_rad.lon * (M_PI / 180.0);
            double lat2 = b_rad.lat * (M_PI / 180.0);
            double lon2 = b_rad.lon * (M_PI / 180.0);
            double d = 2.0 * asin(sqrt(
                pow(sin((lat2 - lat1) / 2.0), 2.0) +
                cos(lat1) * cos(lat2) * pow(sin((lon2 - lon1) / 2.0), 2.0)
            ));
            if (d == 0.0) {
                pt.lat = a->lat;
                pt.lon = a->lon;
            } else {
                double a_coeff = sin((1.0 - frac) * d) / sin(d);
                double b_coeff = sin(frac * d) / sin(d);
                double x = a_coeff * cos(lat1) * cos(lon1) + b_coeff * cos(lat2) * cos(lon2);
                double y = a_coeff * cos(lat1) * sin(lon1) + b_coeff * cos(lat2) * sin(lon2);
                double z = a_coeff * sin(lat1) + b_coeff * sin(lat2);
                double lat = atan2(z, sqrt(x * x + y * y));
                double lon = atan2(y, x);
                pt.lat = lat * (180.0 / M_PI);
                pt.lon = lon * (180.0 / M_PI);
            }
        }

        mh_grid loc;
        st = mh_from_latlon(pt.lat, pt.lon, precision, 1, &loc, err);
        if (st != MH_OK) {
            mh_free_list(&(mh_list){items, count});
            return st;
        }
        int duplicate = 0;
        for (size_t j = 0; j < count; j++) {
            if (strcmp(items[j], loc.locator) == 0) {
                duplicate = 1;
                break;
            }
        }
        if (duplicate) {
            continue;
        }
        char *copy = (char *)malloc(strlen(loc.locator) + 1);
        if (!copy) {
            mh_free_list(&(mh_list){items, count});
            mh_set_error(err, MH_ERR_INTERNAL, "allocation failed");
            return MH_ERR_INTERNAL;
        }
        strcpy(copy, loc.locator);
        if (count == cap) {
            size_t new_cap = cap * 2;
            char **new_items = (char **)realloc(items, new_cap * sizeof(char *));
            if (!new_items) {
                free(copy);
                mh_free_list(&(mh_list){items, count});
                mh_set_error(err, MH_ERR_INTERNAL, "allocation failed");
                return MH_ERR_INTERNAL;
            }
            items = new_items;
            cap = new_cap;
        }
        items[count++] = copy;
    }

    free(geo_points);
    out->items = items;
    out->length = count;
    return MH_OK;
}
