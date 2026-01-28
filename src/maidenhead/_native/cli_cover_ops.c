#include "cli_utils.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "coverage.h"
#include "core.h"
#include "geo.h"
#include "types.h"

int mh_cli_handle_cover_circle(int argc, char **argv, FILE *out, FILE *err) {
    const char *file_path = NULL;
    const char *format = "plain";
    int use_stdin = 0;
    int csv = 0;
    int precision = -1;
    double radius_km = 0.0;
    char **center_parts = NULL;
    size_t center_len = 0;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--file") == 0 && i + 1 < argc) {
            file_path = argv[++i];
        } else if (strcmp(argv[i], "--stdin") == 0) {
            use_stdin = 1;
        } else if (strcmp(argv[i], "--format") == 0 && i + 1 < argc) {
            format = argv[++i];
        } else if (strcmp(argv[i], "--csv") == 0) {
            csv = 1;
        } else if ((strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--precision") == 0) && i + 1 < argc) {
            precision = atoi(argv[++i]);
        } else {
            char **items = (char **)realloc(center_parts, (center_len + 1) * sizeof(char *));
            if (!items) {
                mh_cli_print_error(err, "allocation failed");
                return 2;
            }
            center_parts = items;
            center_parts[center_len++] = argv[i];
        }
    }

    mh_cli_lines lines;
    mh_cli_lines_init(&lines);
    int batch_err = mh_cli_read_lines(file_path, use_stdin, &lines, err);
    if (batch_err != 0) {
        free(center_parts);
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
                free(center_parts);
                mh_cli_lines_free(&lines);
                mh_cli_print_error(err, "allocation failed");
                return 2;
            }
            if (tokens.length != 3) {
                mh_cli_tokens_free(&tokens);
                free(center_parts);
                mh_cli_lines_free(&lines);
                mh_cli_print_error(err, "Expected lines: center radius_km precision");
                return 2;
            }
            double rad = atof(tokens.items[1]);
            int prec = atoi(tokens.items[2]);
            mh_point center;
            char loc_buf[12];
            int is_loc = 0;
            mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
            if (!mh_cli_parse_point_parts((const char **)&tokens.items[0], 1, &center, loc_buf, sizeof(loc_buf), &is_loc, &ctx)) {
                mh_cli_tokens_free(&tokens);
                free(center_parts);
                mh_cli_lines_free(&lines);
                mh_cli_print_error(err, "Point must be 'lat,lon' or a locator");
                return 2;
            }
            mh_list list = {NULL, 0};
            mh_status st = mh_cover_circle(&center, rad, prec, &list, &ctx);
            if (st != MH_OK) {
                mh_cli_tokens_free(&tokens);
                free(center_parts);
                mh_cli_lines_free(&lines);
                return mh_cli_print_mh_error(err, &ctx);
            }
            if (strcmp(format, "json") == 0) {
                if (i > 0) {
                    fputc(',', out);
                }
                fputs("{\"input\":[", out);
                mh_cli_json_print_point_input(out, tokens.items[0], is_loc, center.lat, center.lon);
                fputc(',', out);
                mh_cli_print_json_float(out, rad);
                fputc(',', out);
                fprintf(out, "%d", prec);
                fputs("],\"output\":", out);
                fputc('[', out);
                for (size_t j = 0; j < list.length; j++) {
                    if (j > 0) {
                        fputc(',', out);
                    }
                    mh_cli_print_json_string(out, list.items[j]);
                }
                fputc(']', out);
                mh_cli_json_obj_end(out);
            } else {
                if (strcmp(format, "csv") == 0) {
                    for (size_t j = 0; j < list.length; j++) {
                        fprintf(out, "%s\n", list.items[j]);
                    }
                } else {
                    const char *sep = " ";
                    for (size_t j = 0; j < list.length; j++) {
                        fprintf(out, "%s%s", list.items[j], j + 1 < list.length ? sep : "");
                    }
                    fprintf(out, "%s", i + 1 < lines.length ? "\n" : "");
                }
            }
            mh_free_list(&list);
            mh_cli_tokens_free(&tokens);
        }
        if (strcmp(format, "json") == 0) {
            mh_cli_json_bulk_end(out);
        } else {
            if (strcmp(format, "csv") != 0) {
                fputc('\n', out);
            }
        }
        free(center_parts);
        mh_cli_lines_free(&lines);
        return 0;
    }

    if (!center_parts || center_len < 2 || precision < 0) {
        free(center_parts);
        mh_cli_lines_free(&lines);
        mh_cli_print_error(err, "center, radius, and precision are required");
        return 2;
    }

    radius_km = atof(center_parts[center_len - 1]);
    center_len -= 1;

    mh_point center;
    char loc_buf[12];
    int is_loc = 0;
    mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
    if (!mh_cli_parse_point_parts((const char **)center_parts, center_len, &center, loc_buf, sizeof(loc_buf), &is_loc, &ctx)) {
        free(center_parts);
        mh_cli_lines_free(&lines);
        mh_cli_print_error(err, "Point must be 'lat,lon' or a locator");
        return 2;
    }
    mh_list list = {NULL, 0};
    mh_status st = mh_cover_circle(&center, radius_km, precision, &list, &ctx);
    if (st != MH_OK) {
        free(center_parts);
        mh_cli_lines_free(&lines);
        return mh_cli_print_mh_error(err, &ctx);
    }
    if (strcmp(format, "json") == 0) {
        fputs("{\"input\":[", out);
        mh_cli_json_print_point_input(out, center_parts[0], is_loc, center.lat, center.lon);
        fputc(',', out);
        mh_cli_print_json_float(out, radius_km);
        fputc(',', out);
        fprintf(out, "%d", precision);
        fputs("],\"output\":", out);
        fputc('[', out);
        for (size_t j = 0; j < list.length; j++) {
            if (j > 0) {
                fputc(',', out);
            }
            mh_cli_print_json_string(out, list.items[j]);
        }
        fputc(']', out);
        mh_cli_json_obj_end(out);
        fputc('\n', out);
    } else {
        if (csv) {
            fputs("locator\n", out);
            for (size_t j = 0; j < list.length; j++) {
                fprintf(out, "%s\n", list.items[j]);
            }
        } else {
            const char *sep = " ";
            for (size_t j = 0; j < list.length; j++) {
                fprintf(out, "%s%s", list.items[j], j + 1 < list.length ? sep : "");
            }
            fprintf(out, "\n");
        }
    }
    mh_free_list(&list);
    free(center_parts);
    mh_cli_lines_free(&lines);
    return 0;
}

int mh_cli_handle_cover_line(int argc, char **argv, FILE *out, FILE *err) {
    const char *file_path = NULL;
    const char *format = "plain";
    const char *method = "greatcircle";
    int use_stdin = 0;
    int csv = 0;
    int precision = -1;

    char **point_parts = NULL;
    size_t point_len = 0;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--file") == 0 && i + 1 < argc) {
            file_path = argv[++i];
        } else if (strcmp(argv[i], "--stdin") == 0) {
            use_stdin = 1;
        } else if (strcmp(argv[i], "--format") == 0 && i + 1 < argc) {
            format = argv[++i];
        } else if (strcmp(argv[i], "--csv") == 0) {
            csv = 1;
        } else if ((strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--precision") == 0) && i + 1 < argc) {
            precision = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--method") == 0 && i + 1 < argc) {
            method = argv[++i];
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

    int method_code = strcmp(method, "geodesic") == 0 ? MH_LINE_GEODESIC : MH_LINE_GREATCIRCLE;

    if (lines.length > 0) {
        if (strcmp(format, "json") == 0) {
            mh_cli_json_bulk_start(out, &lines);
        }
        if (strcmp(format, "csv") == 0) {
            fputs("locator\n", out);
        }
        for (size_t i = 0; i < lines.length; i++) {
            mh_cli_tokens tokens;
            if (!mh_cli_split_line(lines.items[i], &tokens)) {
                free(point_parts);
                mh_cli_lines_free(&lines);
                mh_cli_print_error(err, "allocation failed");
                return 2;
            }
            if (tokens.length != 3) {
                mh_cli_tokens_free(&tokens);
                free(point_parts);
                mh_cli_lines_free(&lines);
                mh_cli_print_error(err, "Expected lines: start end precision");
                return 2;
            }
            int prec = atoi(tokens.items[2]);
            mh_point a;
            mh_point b;
            char loc_a[12];
            char loc_b[12];
            int is_loc_a = 0;
            int is_loc_b = 0;
            mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
            if (!mh_cli_parse_point_parts((const char **)&tokens.items[0], 1, &a, loc_a, sizeof(loc_a), &is_loc_a, &ctx)) {
                mh_cli_tokens_free(&tokens);
                free(point_parts);
                mh_cli_lines_free(&lines);
                mh_cli_print_error(err, "Point must be 'lat,lon' or a locator");
                return 2;
            }
            if (!mh_cli_parse_point_parts((const char **)&tokens.items[1], 1, &b, loc_b, sizeof(loc_b), &is_loc_b, &ctx)) {
                mh_cli_tokens_free(&tokens);
                free(point_parts);
                mh_cli_lines_free(&lines);
                mh_cli_print_error(err, "Point must be 'lat,lon' or a locator");
                return 2;
            }
            mh_list list = {NULL, 0};
            mh_status st = mh_cover_line(&a, &b, prec, method_code, &list, &ctx);
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
                fputs("{\"input\":[", out);
                mh_cli_json_print_point_input(out, tokens.items[0], is_loc_a, a.lat, a.lon);
                fputc(',', out);
                mh_cli_json_print_point_input(out, tokens.items[1], is_loc_b, b.lat, b.lon);
                fputc(',', out);
                fprintf(out, "%d", prec);
                fputs("],\"output\":", out);
                fputc('[', out);
                for (size_t j = 0; j < list.length; j++) {
                    if (j > 0) {
                        fputc(',', out);
                    }
                    mh_cli_print_json_string(out, list.items[j]);
                }
                fputc(']', out);
                mh_cli_json_obj_end(out);
            } else {
                if (strcmp(format, "csv") == 0) {
                    for (size_t j = 0; j < list.length; j++) {
                        fprintf(out, "%s\n", list.items[j]);
                    }
                } else {
                    const char *sep = " ";
                    for (size_t j = 0; j < list.length; j++) {
                        fprintf(out, "%s%s", list.items[j], j + 1 < list.length ? sep : "");
                    }
                    fprintf(out, "%s", i + 1 < lines.length ? "\n" : "");
                }
            }
            mh_free_list(&list);
            mh_cli_tokens_free(&tokens);
        }
        if (strcmp(format, "json") == 0) {
            mh_cli_json_bulk_end(out);
        } else {
            if (strcmp(format, "csv") != 0) {
                fputc('\n', out);
            }
        }
        free(point_parts);
        mh_cli_lines_free(&lines);
        return 0;
    }

    if (point_len == 0 || precision < 0) {
        free(point_parts);
        mh_cli_lines_free(&lines);
        mh_cli_print_error(err, "start, end, and precision are required");
        return 2;
    }

    const char **a_parts = NULL;
    size_t a_len = 0;
    const char **b_parts = NULL;
    size_t b_len = 0;
    if (!mh_cli_split_two_points(point_parts, point_len, &a_parts, &a_len, &b_parts, &b_len)) {
        free(point_parts);
        mh_cli_lines_free(&lines);
        mh_cli_print_error(err, "expected two points");
        return 2;
    }
    mh_point a;
    mh_point b;
    char loc_buf[12];
    int is_loc_a = 0;
    int is_loc_b = 0;
    mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
    if (!mh_cli_parse_point_parts(a_parts, a_len, &a, loc_buf, sizeof(loc_buf), &is_loc_a, &ctx)) {
        free(point_parts);
        mh_cli_lines_free(&lines);
        mh_cli_print_error(err, "Point must be 'lat,lon' or a locator");
        return 2;
    }
    if (!mh_cli_parse_point_parts(b_parts, b_len, &b, loc_buf, sizeof(loc_buf), &is_loc_b, &ctx)) {
        free(point_parts);
        mh_cli_lines_free(&lines);
        mh_cli_print_error(err, "Point must be 'lat,lon' or a locator");
        return 2;
    }
    mh_list list = {NULL, 0};
    mh_status st = mh_cover_line(&a, &b, precision, method_code, &list, &ctx);
    if (st != MH_OK) {
        free(point_parts);
        mh_cli_lines_free(&lines);
        return mh_cli_print_mh_error(err, &ctx);
    }
    if (strcmp(format, "json") == 0) {
        fputs("{\"input\":[", out);
        mh_cli_json_print_point_input(out, a_parts[0], is_loc_a, a.lat, a.lon);
        fputc(',', out);
        mh_cli_json_print_point_input(out, b_parts[0], is_loc_b, b.lat, b.lon);
        fputc(',', out);
        fprintf(out, "%d", precision);
        fputs("],\"output\":", out);
        fputc('[', out);
        for (size_t j = 0; j < list.length; j++) {
            if (j > 0) {
                fputc(',', out);
            }
            mh_cli_print_json_string(out, list.items[j]);
        }
        fputc(']', out);
        mh_cli_json_obj_end(out);
        fputc('\n', out);
    } else {
        if (csv) {
            fputs("locator\n", out);
            for (size_t j = 0; j < list.length; j++) {
                fprintf(out, "%s\n", list.items[j]);
            }
        } else {
            const char *sep = " ";
            for (size_t j = 0; j < list.length; j++) {
                fprintf(out, "%s%s", list.items[j], j + 1 < list.length ? sep : "");
            }
            fprintf(out, "\n");
        }
    }
    mh_free_list(&list);
    free(point_parts);
    mh_cli_lines_free(&lines);
    return 0;
}
