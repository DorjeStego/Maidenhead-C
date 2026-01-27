#include "cli_utils.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core.h"
#include "geo.h"
#include "types.h"

int mh_cli_handle_distance(int argc, char **argv, FILE *out, FILE *err) {
    const char *method = "haversine";
    int digits = MH_CLI_DEFAULT_DIGITS;
    char **point_parts = NULL;
    size_t point_len = 0;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--method") == 0 && i + 1 < argc) {
            method = argv[++i];
        } else if (strcmp(argv[i], "--digits") == 0 && i + 1 < argc) {
            digits = atoi(argv[++i]);
        } else {
            char **items = (char **)realloc(point_parts, (point_len + 1) * sizeof(char *));
            if (!items) {
                mh_cli_print_error(err, "allocation failed");
                return 2;
            }
            point_parts = items;
            point_parts[point_len++] = argv[i];
        }
    }

    const char **a_parts = NULL;
    size_t a_len = 0;
    const char **b_parts = NULL;
    size_t b_len = 0;
    if (!mh_cli_split_two_points(point_parts, point_len, &a_parts, &a_len, &b_parts, &b_len)) {
        free(point_parts);
        mh_cli_print_error(err, "expected two points");
        return 2;
    }

    mh_point a;
    mh_point b;
    char loc_buf[12];
    int is_loc = 0;
    mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
    if (!mh_cli_parse_point_parts(a_parts, a_len, &a, loc_buf, sizeof(loc_buf), &is_loc, &ctx)) {
        free(point_parts);
        mh_cli_print_error(err, "Point must be 'lat,lon' or a locator");
        return 2;
    }
    if (!mh_cli_parse_point_parts(b_parts, b_len, &b, loc_buf, sizeof(loc_buf), &is_loc, &ctx)) {
        free(point_parts);
        mh_cli_print_error(err, "Point must be 'lat,lon' or a locator");
        return 2;
    }

    int method_code = strcmp(method, "geodesic") == 0 ? MH_DISTANCE_GEODESIC : MH_DISTANCE_HAVERSINE;
    double dist = 0.0;
    mh_status st = mh_distance_km(&a, &b, method_code, &dist, &ctx);
    free(point_parts);
    if (st != MH_OK) {
        return mh_cli_print_mh_error(err, &ctx);
    }
    fprintf(out, "%.*f\n", digits, dist);
    return 0;
}

int mh_cli_handle_bearing(int argc, char **argv, FILE *out, FILE *err) {
    int digits = MH_CLI_DEFAULT_DIGITS;
    char **point_parts = NULL;
    size_t point_len = 0;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--digits") == 0 && i + 1 < argc) {
            digits = atoi(argv[++i]);
        } else {
            char **items = (char **)realloc(point_parts, (point_len + 1) * sizeof(char *));
            if (!items) {
                mh_cli_print_error(err, "allocation failed");
                return 2;
            }
            point_parts = items;
            point_parts[point_len++] = argv[i];
        }
    }

    const char **a_parts = NULL;
    size_t a_len = 0;
    const char **b_parts = NULL;
    size_t b_len = 0;
    if (!mh_cli_split_two_points(point_parts, point_len, &a_parts, &a_len, &b_parts, &b_len)) {
        free(point_parts);
        mh_cli_print_error(err, "expected two points");
        return 2;
    }

    mh_point a;
    mh_point b;
    char loc_buf[12];
    int is_loc = 0;
    mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
    if (!mh_cli_parse_point_parts(a_parts, a_len, &a, loc_buf, sizeof(loc_buf), &is_loc, &ctx)) {
        free(point_parts);
        mh_cli_print_error(err, "Point must be 'lat,lon' or a locator");
        return 2;
    }
    if (!mh_cli_parse_point_parts(b_parts, b_len, &b, loc_buf, sizeof(loc_buf), &is_loc, &ctx)) {
        free(point_parts);
        mh_cli_print_error(err, "Point must be 'lat,lon' or a locator");
        return 2;
    }

    double bearing = 0.0;
    mh_status st = mh_bearing_deg(&a, &b, &bearing, &ctx);
    free(point_parts);
    if (st != MH_OK) {
        return mh_cli_print_mh_error(err, &ctx);
    }
    fprintf(out, "%.*f\n", digits, bearing);
    return 0;
}

int mh_cli_handle_midpoint(int argc, char **argv, FILE *out, FILE *err) {
    const char *method = "greatcircle";
    int digits = MH_CLI_DEFAULT_DIGITS;
    int csv = 0;
    char **point_parts = NULL;
    size_t point_len = 0;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--method") == 0 && i + 1 < argc) {
            method = argv[++i];
        } else if (strcmp(argv[i], "--digits") == 0 && i + 1 < argc) {
            digits = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--csv") == 0) {
            csv = 1;
        } else {
            char **items = (char **)realloc(point_parts, (point_len + 1) * sizeof(char *));
            if (!items) {
                mh_cli_print_error(err, "allocation failed");
                return 2;
            }
            point_parts = items;
            point_parts[point_len++] = argv[i];
        }
    }

    const char **a_parts = NULL;
    size_t a_len = 0;
    const char **b_parts = NULL;
    size_t b_len = 0;
    if (!mh_cli_split_two_points(point_parts, point_len, &a_parts, &a_len, &b_parts, &b_len)) {
        free(point_parts);
        mh_cli_print_error(err, "expected two points");
        return 2;
    }

    mh_point a;
    mh_point b;
    mh_point out_pt;
    char loc_buf[12];
    int is_loc = 0;
    mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
    if (!mh_cli_parse_point_parts(a_parts, a_len, &a, loc_buf, sizeof(loc_buf), &is_loc, &ctx)) {
        free(point_parts);
        mh_cli_print_error(err, "Point must be 'lat,lon' or a locator");
        return 2;
    }
    if (!mh_cli_parse_point_parts(b_parts, b_len, &b, loc_buf, sizeof(loc_buf), &is_loc, &ctx)) {
        free(point_parts);
        mh_cli_print_error(err, "Point must be 'lat,lon' or a locator");
        return 2;
    }

    mh_status st;
    if (strcmp(method, "geodesic") == 0) {
        st = mh_geodesic_midpoint(&a, &b, &out_pt, &ctx);
    } else {
        st = mh_midpoint(&a, &b, &out_pt, &ctx);
    }
    free(point_parts);
    if (st != MH_OK) {
        return mh_cli_print_mh_error(err, &ctx);
    }
    const char *sep = csv ? "," : " ";
    if (csv) {
        fputs("lat,lon\n", out);
    }
    fprintf(out, "%.*f%s%.*f\n", digits, out_pt.lat, sep, digits, out_pt.lon);
    return 0;
}

int mh_cli_handle_great_circle(int argc, char **argv, FILE *out, FILE *err) {
    int digits = MH_CLI_DEFAULT_DIGITS;
    int csv = 0;
    int points_count = 100;
    char **point_parts = NULL;
    size_t point_len = 0;

    for (int i = 2; i < argc; i++) {
        if ((strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "--points-count") == 0) && i + 1 < argc) {
            points_count = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--digits") == 0 && i + 1 < argc) {
            digits = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--csv") == 0) {
            csv = 1;
        } else {
            char **items = (char **)realloc(point_parts, (point_len + 1) * sizeof(char *));
            if (!items) {
                mh_cli_print_error(err, "allocation failed");
                return 2;
            }
            point_parts = items;
            point_parts[point_len++] = argv[i];
        }
    }

    const char **a_parts = NULL;
    size_t a_len = 0;
    const char **b_parts = NULL;
    size_t b_len = 0;
    if (!mh_cli_split_two_points(point_parts, point_len, &a_parts, &a_len, &b_parts, &b_len)) {
        free(point_parts);
        mh_cli_print_error(err, "expected two points");
        return 2;
    }

    mh_point a;
    mh_point b;
    char loc_buf[12];
    int is_loc = 0;
    mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
    if (!mh_cli_parse_point_parts(a_parts, a_len, &a, loc_buf, sizeof(loc_buf), &is_loc, &ctx)) {
        free(point_parts);
        mh_cli_print_error(err, "Point must be 'lat,lon' or a locator");
        return 2;
    }
    if (!mh_cli_parse_point_parts(b_parts, b_len, &b, loc_buf, sizeof(loc_buf), &is_loc, &ctx)) {
        free(point_parts);
        mh_cli_print_error(err, "Point must be 'lat,lon' or a locator");
        return 2;
    }

    if (points_count <= 0) {
        free(point_parts);
        mh_cli_print_error(err, "points count must be > 0");
        return 2;
    }
    mh_point *points = (mh_point *)calloc((size_t)points_count, sizeof(mh_point));
    if (!points) {
        free(point_parts);
        mh_cli_print_error(err, "allocation failed");
        return 2;
    }
    mh_status st = mh_great_circle_path(&a, &b, (size_t)points_count, points, &ctx);
    if (st != MH_OK) {
        free(points);
        free(point_parts);
        return mh_cli_print_mh_error(err, &ctx);
    }
    const char *sep = csv ? "," : " ";
    if (csv) {
        fputs("lat,lon\n", out);
    }
    for (int i = 0; i < points_count; i++) {
        fprintf(out, "%.*f%s%.*f\n", digits, points[i].lat, sep, digits, points[i].lon);
    }
    if (!csv) {
        fprintf(out, "\n");
    }
    free(points);
    free(point_parts);
    return 0;
}

int mh_cli_handle_bearing_bin(int argc, char **argv, FILE *out, FILE *err) {
    int digits = MH_CLI_DEFAULT_DIGITS;
    double bin_size = 5.0;
    char **point_parts = NULL;
    size_t point_len = 0;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--bin-size") == 0 && i + 1 < argc) {
            if (!mh_cli_parse_double(argv[++i], &bin_size)) {
                mh_cli_print_error(err, "invalid bin size");
                return 2;
            }
        } else if (strcmp(argv[i], "--digits") == 0 && i + 1 < argc) {
            digits = atoi(argv[++i]);
        } else {
            char **items = (char **)realloc(point_parts, (point_len + 1) * sizeof(char *));
            if (!items) {
                mh_cli_print_error(err, "allocation failed");
                return 2;
            }
            point_parts = items;
            point_parts[point_len++] = argv[i];
        }
    }

    const char **a_parts = NULL;
    size_t a_len = 0;
    const char **b_parts = NULL;
    size_t b_len = 0;
    if (!mh_cli_split_two_points(point_parts, point_len, &a_parts, &a_len, &b_parts, &b_len)) {
        free(point_parts);
        mh_cli_print_error(err, "expected two points");
        return 2;
    }

    mh_point a;
    mh_point b;
    char loc_buf[12];
    int is_loc = 0;
    mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
    if (!mh_cli_parse_point_parts(a_parts, a_len, &a, loc_buf, sizeof(loc_buf), &is_loc, &ctx)) {
        free(point_parts);
        mh_cli_print_error(err, "Point must be 'lat,lon' or a locator");
        return 2;
    }
    if (!mh_cli_parse_point_parts(b_parts, b_len, &b, loc_buf, sizeof(loc_buf), &is_loc, &ctx)) {
        free(point_parts);
        mh_cli_print_error(err, "Point must be 'lat,lon' or a locator");
        return 2;
    }

    double out_bin = 0.0;
    mh_status st = mh_bearing_bin(&a, &b, bin_size, &out_bin, &ctx);
    free(point_parts);
    if (st != MH_OK) {
        return mh_cli_print_mh_error(err, &ctx);
    }
    fprintf(out, "%.*f\n", digits, out_bin);
    return 0;
}

int mh_cli_handle_azimuthal_sector(int argc, char **argv, FILE *out, FILE *err) {
    int digits = MH_CLI_DEFAULT_DIGITS;
    int csv = 0;
    double width = 0.0;
    char **point_parts = NULL;
    size_t point_len = 0;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--width") == 0 && i + 1 < argc) {
            if (!mh_cli_parse_double(argv[++i], &width)) {
                mh_cli_print_error(err, "invalid width");
                return 2;
            }
        } else if (strcmp(argv[i], "--digits") == 0 && i + 1 < argc) {
            digits = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--csv") == 0) {
            csv = 1;
        } else {
            char **items = (char **)realloc(point_parts, (point_len + 1) * sizeof(char *));
            if (!items) {
                mh_cli_print_error(err, "allocation failed");
                return 2;
            }
            point_parts = items;
            point_parts[point_len++] = argv[i];
        }
    }

    if (width <= 0.0) {
        free(point_parts);
        mh_cli_print_error(err, "width is required");
        return 2;
    }

    const char **a_parts = NULL;
    size_t a_len = 0;
    const char **b_parts = NULL;
    size_t b_len = 0;
    if (!mh_cli_split_two_points(point_parts, point_len, &a_parts, &a_len, &b_parts, &b_len)) {
        free(point_parts);
        mh_cli_print_error(err, "expected two points");
        return 2;
    }

    mh_point a;
    mh_point b;
    char loc_buf[12];
    int is_loc = 0;
    mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
    if (!mh_cli_parse_point_parts(a_parts, a_len, &a, loc_buf, sizeof(loc_buf), &is_loc, &ctx)) {
        free(point_parts);
        mh_cli_print_error(err, "Point must be 'lat,lon' or a locator");
        return 2;
    }
    if (!mh_cli_parse_point_parts(b_parts, b_len, &b, loc_buf, sizeof(loc_buf), &is_loc, &ctx)) {
        free(point_parts);
        mh_cli_print_error(err, "Point must be 'lat,lon' or a locator");
        return 2;
    }

    double start = 0.0;
    double end = 0.0;
    mh_status st = mh_azimuthal_sector(&a, &b, width, &start, &end, &ctx);
    free(point_parts);
    if (st != MH_OK) {
        return mh_cli_print_mh_error(err, &ctx);
    }
    const char *sep = csv ? "," : " ";
    if (csv) {
        fputs("start_deg,end_deg\n", out);
    }
    fprintf(out, "%.*f%s%.*f\n", digits, start, sep, digits, end);
    return 0;
}

int mh_cli_handle_azimuth(int argc, char **argv, FILE *out, FILE *err) {
    int digits = MH_CLI_DEFAULT_DIGITS;
    int csv = 0;
    int range_mode = 0;
    char **point_parts = NULL;
    size_t point_len = 0;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--digits") == 0 && i + 1 < argc) {
            digits = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--csv") == 0) {
            csv = 1;
        } else if (strcmp(argv[i], "--range") == 0) {
            range_mode = 1;
        } else {
            char **items = (char **)realloc(point_parts, (point_len + 1) * sizeof(char *));
            if (!items) {
                mh_cli_print_error(err, "allocation failed");
                return 2;
            }
            point_parts = items;
            point_parts[point_len++] = argv[i];
        }
    }

    const char **a_parts = NULL;
    size_t a_len = 0;
    const char **b_parts = NULL;
    size_t b_len = 0;
    if (!mh_cli_split_two_points(point_parts, point_len, &a_parts, &a_len, &b_parts, &b_len)) {
        free(point_parts);
        mh_cli_print_error(err, "expected two points");
        return 2;
    }

    char loc_a[12];
    char loc_b[12];
    mh_point a;
    mh_point b;
    int is_loc_a = 0;
    int is_loc_b = 0;
    mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
    if (!mh_cli_parse_point_parts(a_parts, a_len, &a, loc_a, sizeof(loc_a), &is_loc_a, &ctx)) {
        free(point_parts);
        mh_cli_print_error(err, "Point must be 'lat,lon' or a locator");
        return 2;
    }
    if (!mh_cli_parse_point_parts(b_parts, b_len, &b, loc_b, sizeof(loc_b), &is_loc_b, &ctx)) {
        free(point_parts);
        mh_cli_print_error(err, "Point must be 'lat,lon' or a locator");
        return 2;
    }

    double bearing = 0.0;
    double dist = 0.0;
    mh_status st = mh_bearing_deg(&a, &b, &bearing, &ctx);
    if (st != MH_OK) {
        free(point_parts);
        return mh_cli_print_mh_error(err, &ctx);
    }
    st = mh_distance_km(&a, &b, MH_DISTANCE_HAVERSINE, &dist, &ctx);
    if (st != MH_OK) {
        free(point_parts);
        return mh_cli_print_mh_error(err, &ctx);
    }

    const char *sep = csv ? "," : " ";
    if (csv) {
        if (range_mode && is_loc_a && is_loc_b) {
            fputs("bearing_deg,min_distance_km,max_distance_km\n", out);
        } else {
            fputs("bearing_deg,distance_km\n", out);
        }
    }
    if (range_mode && is_loc_a && is_loc_b) {
        mh_corners_t corners_a;
        mh_corners_t corners_b;
        st = mh_corners(loc_a, &corners_a, &ctx);
        if (st != MH_OK) {
            free(point_parts);
            return mh_cli_print_mh_error(err, &ctx);
        }
        st = mh_corners(loc_b, &corners_b, &ctx);
        if (st != MH_OK) {
            free(point_parts);
            return mh_cli_print_mh_error(err, &ctx);
        }
        mh_point corners_a_pts[4] = {corners_a.nw, corners_a.ne, corners_a.sw, corners_a.se};
        mh_point corners_b_pts[4] = {corners_b.nw, corners_b.ne, corners_b.sw, corners_b.se};
        double min_km = 0.0;
        double max_km = 0.0;
        int first = 1;
        for (int ia = 0; ia < 4; ia++) {
            for (int ib = 0; ib < 4; ib++) {
                double d = 0.0;
                st = mh_distance_km(&corners_a_pts[ia], &corners_b_pts[ib], MH_DISTANCE_HAVERSINE, &d, &ctx);
                if (st != MH_OK) {
                    free(point_parts);
                    return mh_cli_print_mh_error(err, &ctx);
                }
                if (first) {
                    min_km = d;
                    max_km = d;
                    first = 0;
                } else {
                    if (d < min_km) {
                        min_km = d;
                    }
                    if (d > max_km) {
                        max_km = d;
                    }
                }
            }
        }
        fprintf(out, "%.*f%s%.*f%s%.*f\n", digits, bearing, sep, digits, min_km, sep, digits, max_km);
    } else {
        fprintf(out, "%.*f%s%.*f\n", digits, bearing, sep, digits, dist);
    }
    free(point_parts);
    return 0;
}

int mh_cli_handle_initial_bearing(int argc, char **argv, FILE *out, FILE *err) {
    const char *loc_a = NULL;
    const char *loc_b = NULL;
    int digits = MH_CLI_DEFAULT_DIGITS;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--digits") == 0 && i + 1 < argc) {
            digits = atoi(argv[++i]);
        } else if (!loc_a) {
            loc_a = argv[i];
        } else if (!loc_b) {
            loc_b = argv[i];
        } else {
            mh_cli_print_error(err, "unexpected argument");
            return 2;
        }
    }
    if (!loc_a || !loc_b) {
        mh_cli_print_error(err, "locator_a and locator_b are required");
        return 2;
    }
    double lat_a = 0.0;
    double lon_a = 0.0;
    double lat_b = 0.0;
    double lon_b = 0.0;
    mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
    mh_status st = mh_to_center_latlon(loc_a, &lat_a, &lon_a, &ctx);
    if (st != MH_OK) {
        return mh_cli_print_mh_error(err, &ctx);
    }
    st = mh_to_center_latlon(loc_b, &lat_b, &lon_b, &ctx);
    if (st != MH_OK) {
        return mh_cli_print_mh_error(err, &ctx);
    }
    mh_point a = {lat_a, lon_a};
    mh_point b = {lat_b, lon_b};
    double bearing = 0.0;
    st = mh_bearing_deg(&a, &b, &bearing, &ctx);
    if (st != MH_OK) {
        return mh_cli_print_mh_error(err, &ctx);
    }
    fprintf(out, "%.*f\n", digits, bearing);
    return 0;
}

