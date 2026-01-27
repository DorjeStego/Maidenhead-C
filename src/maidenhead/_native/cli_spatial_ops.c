#include "cli_utils.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core.h"
#include "types.h"

int mh_cli_handle_contains(int argc, char **argv, FILE *out, FILE *err) {
    const char *outer = NULL;
    const char *inner = NULL;

    for (int i = 2; i < argc; i++) {
        if (!outer) {
            outer = argv[i];
        } else if (!inner) {
            inner = argv[i];
        } else {
            mh_cli_print_error(err, "unexpected argument");
            return 2;
        }
    }

    if (!outer || !inner) {
        mh_cli_print_error(err, "outer and inner are required");
        return 2;
    }

    int contains = 0;
    mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
    mh_status st = mh_contains(outer, inner, &contains, &ctx);
    if (st != MH_OK) {
        return mh_cli_print_mh_error(err, &ctx);
    }
    fprintf(out, "%s\n", contains ? "true" : "false");
    return 0;
}

int mh_cli_handle_contains_point(int argc, char **argv, FILE *out, FILE *err) {
    const char *file_path = NULL;
    const char *format = "plain";
    int use_stdin = 0;
    const char *locator = NULL;
    char **latlon_parts = NULL;
    size_t latlon_len = 0;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--file") == 0 && i + 1 < argc) {
            file_path = argv[++i];
        } else if (strcmp(argv[i], "--stdin") == 0) {
            use_stdin = 1;
        } else if (strcmp(argv[i], "--format") == 0 && i + 1 < argc) {
            format = argv[++i];
        } else if (!locator) {
            locator = argv[i];
        } else {
            char **items = (char **)realloc(latlon_parts, (latlon_len + 1) * sizeof(char *));
            if (!items) {
                mh_cli_print_error(err, "allocation failed");
                return 2;
            }
            latlon_parts = items;
            latlon_parts[latlon_len++] = argv[i];
        }
    }

    mh_cli_lines lines;
    mh_cli_lines_init(&lines);
    int batch_err = mh_cli_read_lines(file_path, use_stdin, &lines, err);
    if (batch_err != 0) {
        free(latlon_parts);
        mh_cli_lines_free(&lines);
        return batch_err;
    }

    if (lines.length > 0) {
        if (strcmp(format, "json") == 0) {
            mh_cli_json_bulk_start(out, &lines);
        }
        for (size_t i = 0; i < lines.length; i++) {
            mh_cli_tokens tokens;
            if (!mh_cli_split_line(lines.items[i], &tokens)) {
                free(latlon_parts);
                mh_cli_lines_free(&lines);
                mh_cli_print_error(err, "allocation failed");
                return 2;
            }
            if (tokens.length < 3) {
                mh_cli_tokens_free(&tokens);
                free(latlon_parts);
                mh_cli_lines_free(&lines);
                mh_cli_print_error(err, "Expected lines: locator lat lon");
                return 2;
            }
            double lat = 0.0;
            double lon = 0.0;
            if (!mh_cli_split_latlon_parts((const char **)&tokens.items[1], tokens.length - 1, &lat, &lon)) {
                mh_cli_tokens_free(&tokens);
                free(latlon_parts);
                mh_cli_lines_free(&lines);
                mh_cli_print_error(err, "Latitude/longitude must be 'lat lon' or 'lat,lon'");
                return 2;
            }
            int hit = 0;
            mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
            mh_status st = mh_contains_point(tokens.items[0], lat, lon, &hit, &ctx);
            if (st != MH_OK) {
                mh_cli_tokens_free(&tokens);
                free(latlon_parts);
                mh_cli_lines_free(&lines);
                return mh_cli_print_mh_error(err, &ctx);
            }
            if (strcmp(format, "json") == 0) {
                if (i > 0) {
                    fputc(',', out);
                }
                fputs(hit ? "true" : "false", out);
            } else {
                fprintf(out, "%s%s", hit ? "true" : "false", i + 1 < lines.length ? "\n" : "");
            }
            mh_cli_tokens_free(&tokens);
        }
        if (strcmp(format, "json") == 0) {
            mh_cli_json_bulk_end(out);
        } else {
            fputc('\n', out);
        }
        free(latlon_parts);
        mh_cli_lines_free(&lines);
        return 0;
    }

    if (!locator) {
        free(latlon_parts);
        mh_cli_lines_free(&lines);
        mh_cli_print_error(err, "locator is required");
        return 2;
    }

    double lat = 0.0;
    double lon = 0.0;
    if (!mh_cli_split_latlon_parts((const char **)latlon_parts, latlon_len, &lat, &lon)) {
        free(latlon_parts);
        mh_cli_lines_free(&lines);
        mh_cli_print_error(err, "Latitude/longitude must be 'lat lon' or 'lat,lon'");
        return 2;
    }
    int hit = 0;
    mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
    mh_status st = mh_contains_point(locator, lat, lon, &hit, &ctx);
    if (st != MH_OK) {
        free(latlon_parts);
        mh_cli_lines_free(&lines);
        return mh_cli_print_mh_error(err, &ctx);
    }
    if (strcmp(format, "json") == 0) {
        fputs("{\"input\":[", out);
        mh_cli_print_json_string(out, locator);
        fputc(',', out);
        mh_cli_json_print_latlon(out, lat, lon);
        fputs("],\"output\":", out);
        fputs(hit ? "true" : "false", out);
        fputc('}', out);
        fputc('\n', out);
    } else {
        fprintf(out, "%s\n", hit ? "true" : "false");
    }
    free(latlon_parts);
    mh_cli_lines_free(&lines);
    return 0;
}

int mh_cli_handle_intersects_bbox(int argc, char **argv, FILE *out, FILE *err) {
    const char *file_path = NULL;
    const char *format = "plain";
    int use_stdin = 0;
    const char *locator = NULL;
    double min_lat = 0.0;
    double min_lon = 0.0;
    double max_lat = 0.0;
    double max_lon = 0.0;
    int have_bbox = 0;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--file") == 0 && i + 1 < argc) {
            file_path = argv[++i];
        } else if (strcmp(argv[i], "--stdin") == 0) {
            use_stdin = 1;
        } else if (strcmp(argv[i], "--format") == 0 && i + 1 < argc) {
            format = argv[++i];
        } else if (!locator) {
            locator = argv[i];
        } else if (!have_bbox && i + 3 < argc) {
            if (!mh_cli_parse_double(argv[i], &min_lat) ||
                !mh_cli_parse_double(argv[i + 1], &min_lon) ||
                !mh_cli_parse_double(argv[i + 2], &max_lat) ||
                !mh_cli_parse_double(argv[i + 3], &max_lon)) {
                mh_cli_print_error(err, "Expected lines: locator min_lat min_lon max_lat max_lon");
                return 2;
            }
            have_bbox = 1;
            i += 3;
        } else {
            mh_cli_print_error(err, "unexpected argument");
            return 2;
        }
    }

    mh_cli_lines lines;
    mh_cli_lines_init(&lines);
    int batch_err = mh_cli_read_lines(file_path, use_stdin, &lines, err);
    if (batch_err != 0) {
        mh_cli_lines_free(&lines);
        return batch_err;
    }

    if (lines.length > 0) {
        if (strcmp(format, "json") == 0) {
            mh_cli_json_bulk_start(out, &lines);
        }
        for (size_t i = 0; i < lines.length; i++) {
            mh_cli_tokens tokens;
            if (!mh_cli_split_line(lines.items[i], &tokens)) {
                mh_cli_lines_free(&lines);
                mh_cli_print_error(err, "allocation failed");
                return 2;
            }
            if (tokens.length != 5) {
                mh_cli_tokens_free(&tokens);
                mh_cli_lines_free(&lines);
                mh_cli_print_error(err, "Expected lines: locator min_lat min_lon max_lat max_lon");
                return 2;
            }
            mh_bbox bbox = {
                atof(tokens.items[1]),
                atof(tokens.items[2]),
                atof(tokens.items[3]),
                atof(tokens.items[4])
            };
            int hit = 0;
            mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
            mh_status st = mh_intersects_bbox(tokens.items[0], bbox, &hit, &ctx);
            if (st != MH_OK) {
                mh_cli_tokens_free(&tokens);
                mh_cli_lines_free(&lines);
                return mh_cli_print_mh_error(err, &ctx);
            }
            if (strcmp(format, "json") == 0) {
                if (i > 0) {
                    fputc(',', out);
                }
                fputs(hit ? "true" : "false", out);
            } else {
                fprintf(out, "%s%s", hit ? "true" : "false", i + 1 < lines.length ? "\n" : "");
            }
            mh_cli_tokens_free(&tokens);
        }
        if (strcmp(format, "json") == 0) {
            mh_cli_json_bulk_end(out);
        } else {
            fputc('\n', out);
        }
        mh_cli_lines_free(&lines);
        return 0;
    }

    if (!locator || !have_bbox) {
        mh_cli_lines_free(&lines);
        mh_cli_print_error(err, "locator min_lat min_lon max_lat max_lon are required");
        return 2;
    }

    mh_bbox bbox = {min_lat, min_lon, max_lat, max_lon};
    int hit = 0;
    mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
    mh_status st = mh_intersects_bbox(locator, bbox, &hit, &ctx);
    if (st != MH_OK) {
        mh_cli_lines_free(&lines);
        return mh_cli_print_mh_error(err, &ctx);
    }
    if (strcmp(format, "json") == 0) {
        fputs("{\"input\":[", out);
        mh_cli_print_json_string(out, locator);
        fputc(',', out);
        fprintf(out, "%.17g,%.17g,%.17g,%.17g", min_lat, min_lon, max_lat, max_lon);
        fputs("],\"output\":", out);
        fputs(hit ? "true" : "false", out);
        fputs("}\n", out);
    } else {
        fprintf(out, "%s\n", hit ? "true" : "false");
    }
    mh_cli_lines_free(&lines);
    return 0;
}

int mh_cli_handle_intersects_polygon(int argc, char **argv, FILE *out, FILE *err) {
    const char *file_path = NULL;
    const char *format = "plain";
    int use_stdin = 0;
    const char *locator = NULL;
    char **point_parts = NULL;
    size_t point_len = 0;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--file") == 0 && i + 1 < argc) {
            file_path = argv[++i];
        } else if (strcmp(argv[i], "--stdin") == 0) {
            use_stdin = 1;
        } else if (strcmp(argv[i], "--format") == 0 && i + 1 < argc) {
            format = argv[++i];
        } else if (!locator) {
            locator = argv[i];
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

    mh_cli_lines lines;
    mh_cli_lines_init(&lines);
    int batch_err = mh_cli_read_lines(file_path, use_stdin, &lines, err);
    if (batch_err != 0) {
        free(point_parts);
        mh_cli_lines_free(&lines);
        return batch_err;
    }

    if (lines.length > 0) {
        if (strcmp(format, "json") == 0) {
            mh_cli_json_bulk_start(out, &lines);
        }
        for (size_t i = 0; i < lines.length; i++) {
            mh_cli_tokens tokens;
            if (!mh_cli_split_line(lines.items[i], &tokens)) {
                free(point_parts);
                mh_cli_lines_free(&lines);
                mh_cli_print_error(err, "allocation failed");
                return 2;
            }
            if (tokens.length < 4) {
                mh_cli_tokens_free(&tokens);
                free(point_parts);
                mh_cli_lines_free(&lines);
                mh_cli_print_error(err, "Expected lines: locator lat,lon lat,lon lat,lon ...");
                return 2;
            }
            size_t poly_len = tokens.length - 1;
            mh_point *points = (mh_point *)calloc(poly_len, sizeof(mh_point));
            if (!points) {
                mh_cli_tokens_free(&tokens);
                free(point_parts);
                mh_cli_lines_free(&lines);
                mh_cli_print_error(err, "allocation failed");
                return 2;
            }
            for (size_t p = 0; p < poly_len; p++) {
                double lat = 0.0;
                double lon = 0.0;
                if (!mh_cli_parse_latlon_joined(tokens.items[p + 1], &lat, &lon)) {
                    free(points);
                    mh_cli_tokens_free(&tokens);
                    free(point_parts);
                    mh_cli_lines_free(&lines);
                    mh_cli_print_error(err, "Polygon points must be in lat,lon form");
                    return 2;
                }
                points[p].lat = lat;
                points[p].lon = lon;
            }
            int hit = 0;
            mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
            mh_status st = mh_intersects_polygon(tokens.items[0], points, poly_len, &hit, &ctx);
            free(points);
            if (st != MH_OK) {
                mh_cli_tokens_free(&tokens);
                free(point_parts);
                mh_cli_lines_free(&lines);
                return mh_cli_print_mh_error(err, &ctx);
            }
            if (strcmp(format, "json") == 0) {
                if (i > 0) {
                    fputc(',', out);
                }
                fputs(hit ? "true" : "false", out);
            } else {
                fprintf(out, "%s%s", hit ? "true" : "false", i + 1 < lines.length ? "\n" : "");
            }
            mh_cli_tokens_free(&tokens);
        }
        if (strcmp(format, "json") == 0) {
            mh_cli_json_bulk_end(out);
        } else {
            fputc('\n', out);
        }
        free(point_parts);
        mh_cli_lines_free(&lines);
        return 0;
    }

    if (!locator || point_len < 3) {
        free(point_parts);
        mh_cli_lines_free(&lines);
        mh_cli_print_error(err, "Expected points: locator lat,lon lat,lon lat,lon ...");
        return 2;
    }

    mh_point *points = (mh_point *)calloc(point_len, sizeof(mh_point));
    if (!points) {
        free(point_parts);
        mh_cli_lines_free(&lines);
        mh_cli_print_error(err, "allocation failed");
        return 2;
    }
    for (size_t p = 0; p < point_len; p++) {
        double lat = 0.0;
        double lon = 0.0;
        if (!mh_cli_parse_latlon_joined(point_parts[p], &lat, &lon)) {
            free(points);
            free(point_parts);
            mh_cli_lines_free(&lines);
            mh_cli_print_error(err, "Polygon points must be in lat,lon form");
            return 2;
        }
        points[p].lat = lat;
        points[p].lon = lon;
    }
    int hit = 0;
    mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
    mh_status st = mh_intersects_polygon(locator, points, point_len, &hit, &ctx);
    free(points);
    if (st != MH_OK) {
        free(point_parts);
        mh_cli_lines_free(&lines);
        return mh_cli_print_mh_error(err, &ctx);
    }
    if (strcmp(format, "json") == 0) {
        fputs("{\"input\":[", out);
        mh_cli_print_json_string(out, locator);
        for (size_t p = 0; p < point_len; p++) {
            fputc(',', out);
            mh_cli_json_print_latlon(out, points[p].lat, points[p].lon);
        }
        fputs("],\"output\":", out);
        fputs(hit ? "true" : "false", out);
        mh_cli_json_obj_end(out);
        fputc('\n', out);
    } else {
        fprintf(out, "%s\n", hit ? "true" : "false");
    }
    free(point_parts);
    mh_cli_lines_free(&lines);
    return 0;
}

int mh_cli_handle_neighbors(int argc, char **argv, FILE *out, FILE *err) {
    const char *locator = NULL;
    int ring = 1;
    int diagonals = 1;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--ring") == 0 && i + 1 < argc) {
            ring = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--diagonals") == 0) {
            diagonals = 1;
        } else if (strcmp(argv[i], "--no-diagonals") == 0) {
            diagonals = 0;
        } else if (!locator) {
            locator = argv[i];
        } else {
            mh_cli_print_error(err, "unexpected argument");
            return 2;
        }
    }

    if (!locator) {
        mh_cli_print_error(err, "locator is required");
        return 2;
    }

    mh_list list = {NULL, 0};
    mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
    mh_status st = mh_neighbors(locator, ring, diagonals, &list, &ctx);
    if (st != MH_OK) {
        return mh_cli_print_mh_error(err, &ctx);
    }
    for (size_t i = 0; i < list.length; i++) {
        fprintf(out, "%s%s", list.items[i], i + 1 < list.length ? " " : "");
    }
    fprintf(out, "\n");
    mh_free_list(&list);
    return 0;
}

int mh_cli_handle_adjacent(int argc, char **argv, FILE *out, FILE *err) {
    const char *locator = NULL;
    int diagonals = 0;
    int csv = 0;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--diagonals") == 0) {
            diagonals = 1;
        } else if (strcmp(argv[i], "--csv") == 0) {
            csv = 1;
        } else if (!locator) {
            locator = argv[i];
        } else {
            mh_cli_print_error(err, "unexpected argument");
            return 2;
        }
    }

    if (!locator) {
        mh_cli_print_error(err, "locator is required");
        return 2;
    }

    mh_kv_list list = {NULL, NULL, 0};
    mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
    mh_status st = mh_adjacent(locator, diagonals, &list, &ctx);
    if (st != MH_OK) {
        return mh_cli_print_mh_error(err, &ctx);
    }
    if (csv) {
        fputs("direction,locator\n", out);
        for (size_t i = 0; i < list.length; i++) {
            fprintf(out, "%s,%s\n", list.keys[i], list.values[i]);
        }
    } else {
        for (size_t i = 0; i < list.length; i++) {
            fprintf(out, "%s %s%s", list.keys[i], list.values[i], i + 1 < list.length ? "\n" : "");
        }
        fprintf(out, "\n");
    }
    mh_free_kv_list(&list);
    return 0;
}

