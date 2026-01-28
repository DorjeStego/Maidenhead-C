#include "cli_utils.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core.h"
#include "geo.h"
#include "geojson.h"
#include "types.h"

int mh_cli_handle_normalize(int argc, char **argv, FILE *out, FILE *err) {
    const char *locator = NULL;
    const char *file_path = NULL;
    const char *format = "plain";
    int use_stdin = 0;

    mh_cli_lines lines;
    mh_cli_lines_init(&lines);

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
            mh_cli_print_error(err, "unexpected argument");
            return 2;
        }
    }

    int batch_err = mh_cli_read_lines(file_path, use_stdin, &lines, err);
    if (batch_err != 0) {
        mh_cli_lines_free(&lines);
        return batch_err;
    }

    if (lines.length > 0) {
        if (strcmp(format, "json") == 0) {
            fputc('[', out);
        }
        for (size_t i = 0; i < lines.length; i++) {
            char norm[12];
            mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
            mh_status st = mh_normalize_locator(lines.items[i], norm, sizeof(norm), &ctx);
            if (st != MH_OK) {
                mh_cli_lines_free(&lines);
                return mh_cli_print_mh_error(err, &ctx);
            }
            if (strcmp(format, "json") == 0) {
                if (i > 0) {
                    fputc(',', out);
                }
                mh_cli_print_json_string(out, norm);
            } else {
                fprintf(out, "%s%s", norm, i + 1 < lines.length ? "\n" : "");
            }
        }
        if (strcmp(format, "json") == 0) {
            mh_cli_json_bulk_end(out);
        } else {
            fputc('\n', out);
        }
        mh_cli_lines_free(&lines);
        return 0;
    }

    if (!mh_cli_require_arg(locator, "locator is required unless --file/--stdin is provided", err)) {
        mh_cli_lines_free(&lines);
        return 2;
    }

    char norm[12];
    mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
    mh_status st = mh_normalize_locator(locator, norm, sizeof(norm), &ctx);
    if (st != MH_OK) {
        mh_cli_lines_free(&lines);
        return mh_cli_print_mh_error(err, &ctx);
    }
    if (strcmp(format, "json") == 0) {
        mh_cli_json_single_start_string(out, locator);
        mh_cli_print_json_string(out, norm);
        mh_cli_json_single_end(out);
    } else {
        fprintf(out, "%s\n", norm);
    }
    mh_cli_lines_free(&lines);
    return 0;
}

int mh_cli_handle_validate(int argc, char **argv, FILE *out, FILE *err) {
    const char *locator = NULL;
    int print_output = 0;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--print") == 0) {
            print_output = 1;
        } else if (!locator) {
            locator = argv[i];
        } else {
            mh_cli_print_error(err, "unexpected argument");
            return 2;
        }
    }
    if (!mh_cli_require_arg(locator, "locator is required", err)) {
        return 2;
    }
    char norm[12];
    mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
    mh_status st = mh_normalize_locator(locator, norm, sizeof(norm), &ctx);
    if (st != MH_OK) {
        if (print_output) {
            fprintf(out, "invalid\n");
        }
        return 2;
    }
    if (print_output) {
        fprintf(out, "valid\n");
    }
    return 0;
}

int mh_cli_handle_center(int argc, char **argv, FILE *out, FILE *err) {
    const char *locator = NULL;
    const char *file_path = NULL;
    const char *format = "plain";
    int use_stdin = 0;
    int digits = MH_CLI_DEFAULT_DIGITS;
    int csv = 0;

    mh_cli_lines lines;
    mh_cli_lines_init(&lines);

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--digits") == 0 && i + 1 < argc) {
            digits = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--csv") == 0) {
            csv = 1;
        } else if (strcmp(argv[i], "--file") == 0 && i + 1 < argc) {
            file_path = argv[++i];
        } else if (strcmp(argv[i], "--stdin") == 0) {
            use_stdin = 1;
        } else if (strcmp(argv[i], "--format") == 0 && i + 1 < argc) {
            format = argv[++i];
        } else if (!locator) {
            locator = argv[i];
        } else {
            mh_cli_print_error(err, "unexpected argument");
            return 2;
        }
    }

    int batch_err = mh_cli_read_lines(file_path, use_stdin, &lines, err);
    if (batch_err != 0) {
        mh_cli_lines_free(&lines);
        return batch_err;
    }

    if (strcmp(format, "csv") == 0) {
        csv = 1;
    }

    const char *sep = csv ? "," : " ";
    if (lines.length > 0) {
        if (strcmp(format, "json") == 0) {
            mh_cli_json_bulk_start(out, &lines);
        }
        if (csv && strcmp(format, "json") != 0) {
            fputs("input,lat,lon\n", out);
        }
        for (size_t i = 0; i < lines.length; i++) {
            double lat = 0.0;
            double lon = 0.0;
            mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
            mh_status st = mh_to_center_latlon(lines.items[i], &lat, &lon, &ctx);
            if (st != MH_OK) {
                mh_cli_lines_free(&lines);
                return mh_cli_print_mh_error(err, &ctx);
            }
            if (strcmp(format, "json") == 0) {
                if (i > 0) {
                    fputc(',', out);
                }
                fprintf(out, "[%.*f,%.*f]", digits, lat, digits, lon);
            } else {
                if (csv) {
                    fprintf(out, "%s,%.*f,%.*f\n", lines.items[i], digits, lat, digits, lon);
                } else {
                    fprintf(out, "%.*f%s%.*f\n", digits, lat, sep, digits, lon);
                }
            }
        }
        if (strcmp(format, "json") == 0) {
            mh_cli_json_bulk_end(out);
        } else {
            if (!csv) {
                fputc('\n', out);
            }
        }
        mh_cli_lines_free(&lines);
        return 0;
    }

    if (!mh_cli_require_arg(locator, "locator is required unless --file/--stdin is provided", err)) {
        mh_cli_lines_free(&lines);
        return 2;
    }

    double lat = 0.0;
    double lon = 0.0;
    mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
    mh_status st = mh_to_center_latlon(locator, &lat, &lon, &ctx);
    if (st != MH_OK) {
        mh_cli_lines_free(&lines);
        return mh_cli_print_mh_error(err, &ctx);
    }
    if (strcmp(format, "json") == 0) {
        mh_cli_json_single_start_string(out, locator);
        fputc('[', out);
        fprintf(out, "%.*f,%.*f", digits, lat, digits, lon);
        fputc(']', out);
        mh_cli_json_single_end(out);
    } else {
        if (csv) {
            fputs("input,lat,lon\n", out);
        }
        if (csv) {
            fprintf(out, "%s,%.*f,%.*f\n", locator, digits, lat, digits, lon);
        } else {
            fprintf(out, "%.*f%s%.*f\n", digits, lat, sep, digits, lon);
        }
    }
    mh_cli_lines_free(&lines);
    return 0;
}

int mh_cli_handle_bbox(int argc, char **argv, FILE *out, FILE *err) {
    const char *locator = NULL;
    const char *file_path = NULL;
    const char *format = "plain";
    int use_stdin = 0;
    int digits = MH_CLI_DEFAULT_DIGITS;
    int csv = 0;
    int split = 0;

    mh_cli_lines lines;
    mh_cli_lines_init(&lines);

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--digits") == 0 && i + 1 < argc) {
            digits = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--csv") == 0) {
            csv = 1;
        } else if (strcmp(argv[i], "--split") == 0) {
            split = 1;
        } else if (strcmp(argv[i], "--file") == 0 && i + 1 < argc) {
            file_path = argv[++i];
        } else if (strcmp(argv[i], "--stdin") == 0) {
            use_stdin = 1;
        } else if (strcmp(argv[i], "--format") == 0 && i + 1 < argc) {
            format = argv[++i];
        } else if (!locator) {
            locator = argv[i];
        } else {
            mh_cli_print_error(err, "unexpected argument");
            return 2;
        }
    }

    int batch_err = mh_cli_read_lines(file_path, use_stdin, &lines, err);
    if (batch_err != 0) {
        mh_cli_lines_free(&lines);
        return batch_err;
    }

    if (strcmp(format, "csv") == 0) {
        csv = 1;
    }

    const char *sep = csv ? "," : " ";
    if (lines.length > 0) {
        if (strcmp(format, "json") == 0) {
            mh_cli_json_bulk_start(out, &lines);
        }
        if (csv && strcmp(format, "json") != 0) {
            fputs("input,min_lat,min_lon,max_lat,max_lon\n", out);
        }
        for (size_t i = 0; i < lines.length; i++) {
            mh_bbox bbox;
            mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
            mh_status st = mh_to_bbox(lines.items[i], &bbox, &ctx);
            if (st != MH_OK) {
                mh_cli_lines_free(&lines);
                return mh_cli_print_mh_error(err, &ctx);
            }
            mh_bbox parts[2];
            size_t parts_len = 0;
            if (split) {
                st = mh_split_bbox_list(bbox, parts, &parts_len, &ctx);
                if (st != MH_OK) {
                    mh_cli_lines_free(&lines);
                    return mh_cli_print_mh_error(err, &ctx);
                }
                if (parts_len == 0) {
                    parts[0] = bbox;
                    parts_len = 1;
                }
            } else {
                parts[0] = bbox;
                parts_len = 1;
            }

            if (strcmp(format, "json") == 0) {
                if (i > 0) {
                    fputc(',', out);
                }
                mh_cli_json_obj_start_input_string(out, lines.items[i]);
                fputc('[', out);
                for (size_t p = 0; p < parts_len; p++) {
                    if (p > 0) {
                        fputc(',', out);
                    }
                    fprintf(
                        out,
                        "[%.*f,%.*f,%.*f,%.*f]",
                        digits,
                        parts[p].min_lat,
                        digits,
                        parts[p].min_lon,
                        digits,
                        parts[p].max_lat,
                        digits,
                        parts[p].max_lon
                    );
                }
                fputc(']', out);
                mh_cli_json_obj_end(out);
            } else if (strcmp(format, "csv") == 0) {
                for (size_t p = 0; p < parts_len; p++) {
                    char buf[128];
                    mh_cli_format_bbox(buf, sizeof(buf), &parts[p], digits, sep);
                    fprintf(out, "%s,%s\n", lines.items[i], buf);
                }
            } else {
                for (size_t p = 0; p < parts_len; p++) {
                    char buf[128];
                    mh_cli_format_bbox(buf, sizeof(buf), &parts[p], digits, sep);
                    fprintf(out, "%s\n", buf);
                }
            }
        }
        if (strcmp(format, "json") == 0) {
            mh_cli_json_bulk_end(out);
        } else {
            if (!csv) {
                fputc('\n', out);
            }
        }
        mh_cli_lines_free(&lines);
        return 0;
    }

    if (!mh_cli_require_arg(locator, "locator is required unless --file/--stdin is provided", err)) {
        mh_cli_lines_free(&lines);
        return 2;
    }

    mh_bbox bbox;
    mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
    mh_status st = mh_to_bbox(locator, &bbox, &ctx);
    if (st != MH_OK) {
        mh_cli_lines_free(&lines);
        return mh_cli_print_mh_error(err, &ctx);
    }

    if (strcmp(format, "json") == 0) {
        mh_cli_json_single_start_string(out, locator);
    }

    if (split) {
        mh_bbox parts[2];
        size_t parts_len = 0;
        st = mh_split_bbox_list(bbox, parts, &parts_len, &ctx);
        if (st != MH_OK) {
            mh_cli_lines_free(&lines);
            return mh_cli_print_mh_error(err, &ctx);
        }
        if (parts_len == 0) {
            parts[0] = bbox;
            parts_len = 1;
        }
        if (strcmp(format, "json") == 0) {
            fputc('[', out);
            for (size_t p = 0; p < parts_len; p++) {
                if (p > 0) {
                    fputc(',', out);
                }
                fprintf(
                    out,
                    "[%.*f,%.*f,%.*f,%.*f]",
                    digits,
                    parts[p].min_lat,
                    digits,
                    parts[p].min_lon,
                    digits,
                    parts[p].max_lat,
                    digits,
                    parts[p].max_lon
                );
            }
            fputc(']', out);
        } else {
            if (csv) {
                fputs("input,min_lat,min_lon,max_lat,max_lon\n", out);
            }
            if (csv) {
                for (size_t p = 0; p < parts_len; p++) {
                    char buf[128];
                    mh_cli_format_bbox(buf, sizeof(buf), &parts[p], digits, sep);
                    fprintf(out, "%s,%s\n", locator, buf);
                }
            } else {
                for (size_t p = 0; p < parts_len; p++) {
                    char buf[128];
                    mh_cli_format_bbox(buf, sizeof(buf), &parts[p], digits, sep);
                    fprintf(out, "%s%s", buf, p + 1 < parts_len ? "\n" : "");
                }
                fprintf(out, "\n");
            }
        }
    } else {
        if (strcmp(format, "json") == 0) {
            fprintf(
                out,
                "[%.*f,%.*f,%.*f,%.*f]",
                digits,
                bbox.min_lat,
                digits,
                bbox.min_lon,
                digits,
                bbox.max_lat,
                digits,
                bbox.max_lon
            );
        } else {
            if (csv) {
                fputs("input,min_lat,min_lon,max_lat,max_lon\n", out);
            }
            char buf[128];
            mh_cli_format_bbox(buf, sizeof(buf), &bbox, digits, sep);
            if (csv) {
                fprintf(out, "%s,%s\n", locator, buf);
            } else {
                fprintf(out, "%s\n", buf);
            }
        }
    }

    if (strcmp(format, "json") == 0) {
        mh_cli_json_single_end(out);
    }

    mh_cli_lines_free(&lines);
    return 0;
}

int mh_cli_handle_parts(int argc, char **argv, FILE *out, FILE *err) {
    const char *locator = NULL;
    for (int i = 2; i < argc; i++) {
        if (!locator) {
            locator = argv[i];
        } else {
            mh_cli_print_error(err, "unexpected argument");
            return 2;
        }
    }
    if (!mh_cli_require_arg(locator, "locator is required", err)) {
        return 2;
    }
    char norm[12];
    mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
    mh_status st = mh_normalize_locator(locator, norm, sizeof(norm), &ctx);
    if (st != MH_OK) {
        return mh_cli_print_mh_error(err, &ctx);
    }
    int precision = (int)strlen(norm);
    fprintf(out, "field=%.*s", 2, norm);
    if (precision >= 4) {
        fprintf(out, " square=%.*s", 2, norm + 2);
    }
    if (precision >= 6) {
        fprintf(out, " subsquare=%.*s", 2, norm + 4);
    }
    if (precision >= 8) {
        fprintf(out, " ext4=%.*s", 2, norm + 6);
    }
    if (precision >= 10) {
        fprintf(out, " ext5=%.*s", 2, norm + 8);
    }
    fprintf(out, "\n");
    return 0;
}

int mh_cli_handle_format(int argc, char **argv, FILE *out, FILE *err) {
    const char *locator = NULL;
    const char *mode = "center";
    int precision = -1;
    for (int i = 2; i < argc; i++) {
        if ((strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--precision") == 0) && i + 1 < argc) {
            precision = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
            mode = argv[++i];
        } else if (!locator) {
            locator = argv[i];
        } else {
            mh_cli_print_error(err, "unexpected argument");
            return 2;
        }
    }
    if (!locator || precision < 0) {
        mh_cli_print_error(err, "locator and precision are required");
        return 2;
    }
    int mode_code = 1;
    if (strcmp(mode, "truncate") == 0) {
        mode_code = 0;
    } else if (strcmp(mode, "center") == 0) {
        mode_code = 1;
    } else if (strcmp(mode, "error") == 0) {
        mode_code = 2;
    } else {
        mh_cli_print_error(err, "Unknown mode");
        return 2;
    }
    mh_grid grid;
    mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
    mh_status st = mh_format_locator(locator, precision, mode_code, &grid, &ctx);
    if (st != MH_OK) {
        return mh_cli_print_mh_error(err, &ctx);
    }
    fprintf(out, "%s\n", grid.locator);
    return 0;
}

int mh_cli_handle_geojson(int argc, char **argv, FILE *out, FILE *err) {
    const char *file_path = NULL;
    const char *format = "feature";
    const char *locator = NULL;
    int use_stdin = 0;
    int split = 0;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--file") == 0 && i + 1 < argc) {
            file_path = argv[++i];
        } else if (strcmp(argv[i], "--stdin") == 0) {
            use_stdin = 1;
        } else if (strcmp(argv[i], "--geojson-format") == 0 && i + 1 < argc) {
            format = argv[++i];
        } else if (strcmp(argv[i], "--split") == 0) {
            split = 1;
        } else if (!locator) {
            locator = argv[i];
        } else {
            mh_cli_print_error(err, "unexpected argument");
            return 2;
        }
    }

    if (strcmp(format, "feature") != 0 &&
        strcmp(format, "featurecollection") != 0 &&
        strcmp(format, "bbox") != 0 &&
        strcmp(format, "envelope") != 0) {
        mh_cli_print_error(err, "invalid geojson format");
        return 2;
    }

    mh_cli_lines lines;
    mh_cli_lines_init(&lines);
    int batch_err = mh_cli_read_lines(file_path, use_stdin, &lines, err);
    if (batch_err != 0) {
        mh_cli_lines_free(&lines);
        return batch_err;
    }

    if (lines.length > 0) {
        if (strcmp(format, "featurecollection") != 0 || split) {
            mh_cli_lines_free(&lines);
            mh_cli_print_error(err, "Batch geojson requires --geojson-format featurecollection");
            return 2;
        }
        const char **locs = (const char **)malloc(lines.length * sizeof(char *));
        if (!locs) {
            mh_cli_lines_free(&lines);
            mh_cli_print_error(err, "allocation failed");
            return 2;
        }
        for (size_t i = 0; i < lines.length; i++) {
            locs[i] = lines.items[i];
        }
        int rc = mh_cli_geojson_feature_collection(locs, lines.length, out, err);
        free(locs);
        mh_cli_lines_free(&lines);
        return rc;
    }

    if (!locator) {
        mh_cli_lines_free(&lines);
        mh_cli_print_error(err, "locator is required unless --file/--stdin is provided");
        return 2;
    }

    if (strcmp(format, "featurecollection") == 0) {
        const char *locs[1] = {locator};
        mh_cli_lines_free(&lines);
        return mh_cli_geojson_feature_collection(locs, 1, out, err);
    }
    if (strcmp(format, "bbox") == 0) {
        mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
        mh_bbox bbox;
        mh_status st = mh_to_bbox(locator, &bbox, &ctx);
        if (st != MH_OK) {
            mh_cli_lines_free(&lines);
            return mh_cli_print_mh_error(err, &ctx);
        }
        if (split) {
            mh_bbox parts[2];
            size_t parts_len = 2;
            st = mh_split_bbox_list(bbox, parts, &parts_len, &ctx);
            if (st != MH_OK) {
                mh_cli_lines_free(&lines);
                return mh_cli_print_mh_error(err, &ctx);
            }
            if (parts_len == 0) {
                parts[0] = bbox;
                parts_len = 1;
            }
            fputc('[', out);
            for (size_t i = 0; i < parts_len; i++) {
                double out_bbox[4] = {parts[i].min_lat, parts[i].min_lon, parts[i].max_lat, parts[i].max_lon};
                if (i > 0) {
                    fputc(',', out);
                }
                mh_cli_print_geojson_bbox(out, out_bbox);
            }
            fputc(']', out);
            fputc('\n', out);
            mh_cli_lines_free(&lines);
            return 0;
        }
        double out_bbox[4];
        st = mh_to_geojson_bbox(locator, out_bbox, &ctx);
        if (st != MH_OK) {
            mh_cli_lines_free(&lines);
            return mh_cli_print_mh_error(err, &ctx);
        }
        mh_cli_print_geojson_bbox(out, out_bbox);
        fputc('\n', out);
        mh_cli_lines_free(&lines);
        return 0;
    }
    if (strcmp(format, "envelope") == 0) {
        if (split) {
            mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
            mh_bbox bbox;
            mh_status st = mh_to_bbox(locator, &bbox, &ctx);
            if (st != MH_OK) {
                mh_cli_lines_free(&lines);
                return mh_cli_print_mh_error(err, &ctx);
            }
            mh_bbox parts[2];
            size_t parts_len = 2;
            st = mh_split_bbox_list(bbox, parts, &parts_len, &ctx);
            if (st != MH_OK) {
                mh_cli_lines_free(&lines);
                return mh_cli_print_mh_error(err, &ctx);
            }
            if (parts_len == 0) {
                parts[0] = bbox;
                parts_len = 1;
            }
            fputs("{\"type\":\"FeatureCollection\",\"features\":[", out);
            for (size_t i = 0; i < parts_len; i++) {
                if (i > 0) {
                    fputc(',', out);
                }
                fputs("{\"type\":\"Feature\",\"geometry\":", out);
                mh_cli_print_geojson_polygon(out, parts[i]);
                fputs(",\"properties\":{}}", out);
            }
            fputs("]}\n", out);
            mh_cli_lines_free(&lines);
            return 0;
        }
        mh_cli_lines_free(&lines);
        return mh_cli_geojson_with_buffer(locator, mh_to_geojson_envelope, out, err);
    }
    mh_cli_lines_free(&lines);
    return mh_cli_geojson_with_buffer(locator, mh_to_geojson_feature, out, err);
}

int mh_cli_handle_wkt(int argc, char **argv, FILE *out, FILE *err) {
    const char *file_path = NULL;
    const char *format = "plain";
    const char *locator = NULL;
    int use_stdin = 0;
    int precision = 6;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--file") == 0 && i + 1 < argc) {
            file_path = argv[++i];
        } else if (strcmp(argv[i], "--stdin") == 0) {
            use_stdin = 1;
        } else if (strcmp(argv[i], "--format") == 0 && i + 1 < argc) {
            format = argv[++i];
        } else if ((strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--precision") == 0) && i + 1 < argc) {
            precision = atoi(argv[++i]);
        } else if (!locator) {
            locator = argv[i];
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
            mh_bbox bbox;
            mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
            mh_status st;
            if (strchr(lines.items[i], ',')) {
                double lat = 0.0;
                double lon = 0.0;
                if (!mh_cli_parse_latlon_joined(lines.items[i], &lat, &lon)) {
                    mh_cli_lines_free(&lines);
                    mh_cli_print_error(err, "Latitude/longitude must be 'lat lon' or 'lat,lon'");
                    return 2;
                }
                mh_grid grid;
                st = mh_from_latlon(lat, lon, precision, 1, &grid, &ctx);
                if (st != MH_OK) {
                    mh_cli_lines_free(&lines);
                    return mh_cli_print_mh_error(err, &ctx);
                }
                st = mh_to_bbox(grid.locator, &bbox, &ctx);
            } else {
                st = mh_to_bbox(lines.items[i], &bbox, &ctx);
            }
            if (st != MH_OK) {
                mh_cli_lines_free(&lines);
                return mh_cli_print_mh_error(err, &ctx);
            }

            if (strcmp(format, "json") == 0) {
                char buf[256];
                snprintf(
                    buf,
                    sizeof(buf),
                    "POLYGON((%.17g %.17g, %.17g %.17g, %.17g %.17g, %.17g %.17g, %.17g %.17g))",
                    bbox.min_lon, bbox.min_lat,
                    bbox.max_lon, bbox.min_lat,
                    bbox.max_lon, bbox.max_lat,
                    bbox.min_lon, bbox.max_lat,
                    bbox.min_lon, bbox.min_lat
                );
                if (i > 0) {
                    fputc(',', out);
                }
                mh_cli_print_json_string(out, buf);
            } else {
                mh_cli_print_wkt_polygon(out, &bbox);
                fprintf(out, "%s", i + 1 < lines.length ? "\n" : "");
            }
        }
        if (strcmp(format, "json") == 0) {
            mh_cli_json_bulk_end(out);
        } else {
            fputc('\n', out);
        }
        mh_cli_lines_free(&lines);
        return 0;
    }

    if (!locator) {
        mh_cli_lines_free(&lines);
        mh_cli_print_error(err, "locator is required unless --file/--stdin is provided");
        return 2;
    }

    mh_bbox bbox;
    mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
    mh_status st;
    if (strchr(locator, ',')) {
        double lat = 0.0;
        double lon = 0.0;
        if (!mh_cli_parse_latlon_joined(locator, &lat, &lon)) {
            mh_cli_lines_free(&lines);
            mh_cli_print_error(err, "Latitude/longitude must be 'lat lon' or 'lat,lon'");
            return 2;
        }
        mh_grid grid;
        st = mh_from_latlon(lat, lon, precision, 1, &grid, &ctx);
        if (st != MH_OK) {
            mh_cli_lines_free(&lines);
            return mh_cli_print_mh_error(err, &ctx);
        }
        st = mh_to_bbox(grid.locator, &bbox, &ctx);
    } else {
        st = mh_to_bbox(locator, &bbox, &ctx);
    }
    if (st != MH_OK) {
        mh_cli_lines_free(&lines);
        return mh_cli_print_mh_error(err, &ctx);
    }
    if (strcmp(format, "json") == 0) {
        mh_cli_json_single_start_string(out, locator);
        char buf[256];
        snprintf(
            buf,
            sizeof(buf),
            "POLYGON((%.17g %.17g, %.17g %.17g, %.17g %.17g, %.17g %.17g, %.17g %.17g))",
            bbox.min_lon, bbox.min_lat,
            bbox.max_lon, bbox.min_lat,
            bbox.max_lon, bbox.max_lat,
            bbox.min_lon, bbox.max_lat,
            bbox.min_lon, bbox.min_lat
        );
        mh_cli_print_json_string(out, buf);
        mh_cli_json_single_end(out);
    } else {
        mh_cli_print_wkt_polygon(out, &bbox);
        fputc('\n', out);
    }
    mh_cli_lines_free(&lines);
    return 0;
}

int mh_cli_handle_bbox_split(int argc, char **argv, FILE *out, FILE *err) {
    double min_lat = 0.0;
    double min_lon = 0.0;
    double max_lat = 0.0;
    double max_lon = 0.0;
    int digits = MH_CLI_DEFAULT_DIGITS;
    int csv = 0;
    int have_bbox = 0;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--digits") == 0 && i + 1 < argc) {
            digits = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--csv") == 0) {
            csv = 1;
        } else if (!have_bbox && i + 3 < argc) {
            if (!mh_cli_parse_double(argv[i], &min_lat) ||
                !mh_cli_parse_double(argv[i + 1], &min_lon) ||
                !mh_cli_parse_double(argv[i + 2], &max_lat) ||
                !mh_cli_parse_double(argv[i + 3], &max_lon)) {
                mh_cli_print_error(err, "Expected lines: min_lat min_lon max_lat max_lon");
                return 2;
            }
            have_bbox = 1;
            i += 3;
        } else {
            mh_cli_print_error(err, "unexpected argument");
            return 2;
        }
    }

    if (!have_bbox) {
        mh_cli_print_error(err, "min_lat min_lon max_lat max_lon are required");
        return 2;
    }

    mh_bbox bbox = {min_lat, min_lon, max_lat, max_lon};
    mh_bbox parts[2];
    size_t parts_len = 2;
    mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
    mh_status st = mh_split_bbox_list(bbox, parts, &parts_len, &ctx);
    if (st != MH_OK) {
        return mh_cli_print_mh_error(err, &ctx);
    }
    if (parts_len == 0) {
        parts[0] = bbox;
        parts_len = 1;
    }

    const char *sep = csv ? "," : " ";
    if (csv) {
        fputs("min_lat,min_lon,max_lat,max_lon\n", out);
    }
    for (size_t i = 0; i < parts_len; i++) {
        char buf[128];
        mh_cli_format_bbox(buf, sizeof(buf), &parts[i], digits, sep);
        fprintf(out, "%s\n", buf);
    }
    if (!csv) {
        fprintf(out, "\n");
    }
    return 0;
}

int mh_cli_handle_bbox_split_list(int argc, char **argv, FILE *out, FILE *err) {
    const char *file_path = NULL;
    const char *format = "plain";
    int use_stdin = 0;
    int csv = 0;
    int digits = MH_CLI_DEFAULT_DIGITS;
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
        } else if (strcmp(argv[i], "--digits") == 0 && i + 1 < argc) {
            digits = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--csv") == 0) {
            csv = 1;
        } else if (!have_bbox && i + 3 < argc) {
            if (!mh_cli_parse_double(argv[i], &min_lat) ||
                !mh_cli_parse_double(argv[i + 1], &min_lon) ||
                !mh_cli_parse_double(argv[i + 2], &max_lat) ||
                !mh_cli_parse_double(argv[i + 3], &max_lon)) {
                mh_cli_print_error(err, "Expected lines: min_lat min_lon max_lat max_lon");
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

    if (strcmp(format, "json") != 0 && strcmp(format, "plain") != 0 && strcmp(format, "csv") != 0) {
        mh_cli_lines_free(&lines);
        mh_cli_print_error(err, "invalid format");
        return 2;
    }

    if (lines.length > 0) {
        if (strcmp(format, "json") == 0) {
            mh_cli_json_bulk_start(out, &lines);
        }
        if (strcmp(format, "csv") == 0) {
            fputs("min_lat,min_lon,max_lat,max_lon\n", out);
        }
        for (size_t i = 0; i < lines.length; i++) {
            mh_cli_tokens tokens;
            if (!mh_cli_split_line(lines.items[i], &tokens)) {
                mh_cli_lines_free(&lines);
                mh_cli_print_error(err, "allocation failed");
                return 2;
            }
            if (tokens.length != 4) {
                mh_cli_tokens_free(&tokens);
                mh_cli_lines_free(&lines);
                mh_cli_print_error(err, "Expected lines: min_lat min_lon max_lat max_lon");
                return 2;
            }
            mh_bbox bbox = {
                atof(tokens.items[0]),
                atof(tokens.items[1]),
                atof(tokens.items[2]),
                atof(tokens.items[3])
            };
            mh_bbox parts[2];
            size_t parts_len = 2;
            mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
            mh_status st = mh_split_bbox_list(bbox, parts, &parts_len, &ctx);
            if (st != MH_OK) {
                mh_cli_tokens_free(&tokens);
                mh_cli_lines_free(&lines);
                return mh_cli_print_mh_error(err, &ctx);
            }
            if (strcmp(format, "json") == 0) {
                if (i > 0) {
                    fputc(',', out);
                }
                fputs("{\"input\":[", out);
                fprintf(out, "%.17g,%.17g,%.17g,%.17g", bbox.min_lat, bbox.min_lon, bbox.max_lat, bbox.max_lon);
                fputs("],\"output\":", out);
                fputc('[', out);
                for (size_t p = 0; p < parts_len; p++) {
                    if (p > 0) {
                        fputc(',', out);
                    }
                    mh_cli_print_bbox_json(out, &parts[p]);
                }
                fputc(']', out);
                fputc('}', out);
            } else {
                const char *sep = strcmp(format, "csv") == 0 ? "," : " ";
                for (size_t p = 0; p < parts_len; p++) {
                    char buf[128];
                    mh_cli_format_bbox(buf, sizeof(buf), &parts[p], digits, sep);
                    fprintf(out, "%s\n", buf);
                }
            }
            mh_cli_tokens_free(&tokens);
        }
        if (strcmp(format, "json") == 0) {
            mh_cli_json_bulk_end(out);
        } else {
            if (strcmp(format, "csv") != 0) {
                fputc('\n', out);
            }
        }
        mh_cli_lines_free(&lines);
        return 0;
    }

    if (!have_bbox) {
        mh_cli_lines_free(&lines);
        mh_cli_print_error(err, "min_lat min_lon max_lat max_lon are required");
        return 2;
    }

    mh_bbox bbox = {min_lat, min_lon, max_lat, max_lon};
    mh_bbox parts[2];
    size_t parts_len = 2;
    mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
    mh_status st = mh_split_bbox_list(bbox, parts, &parts_len, &ctx);
    if (st != MH_OK) {
        mh_cli_lines_free(&lines);
        return mh_cli_print_mh_error(err, &ctx);
    }
    if (strcmp(format, "json") == 0) {
        fputs("{\"input\":[", out);
        fprintf(out, "%.17g,%.17g,%.17g,%.17g", min_lat, min_lon, max_lat, max_lon);
        fputs("],\"output\":", out);
        fputc('[', out);
        for (size_t p = 0; p < parts_len; p++) {
            if (p > 0) {
                fputc(',', out);
            }
            mh_cli_print_bbox_json(out, &parts[p]);
        }
        fputc(']', out);
        fputs("}\n", out);
        mh_cli_lines_free(&lines);
        return 0;
    }

    const char *sep = csv ? "," : " ";
    if (csv) {
        fputs("min_lat,min_lon,max_lat,max_lon\n", out);
    }
    for (size_t p = 0; p < parts_len; p++) {
        char buf[128];
        mh_cli_format_bbox(buf, sizeof(buf), &parts[p], digits, sep);
        fprintf(out, "%s\n", buf);
    }
    if (!csv) {
        fprintf(out, "\n");
    }
    mh_cli_lines_free(&lines);
    return 0;
}

int mh_cli_handle_from_latlon(int argc, char **argv, FILE *out, FILE *err) {
    const char *file_path = NULL;
    const char *format = "plain";
    int use_stdin = 0;
    int precision = 6;
    int clamp = 1;

    char **latlon_parts = NULL;
    size_t latlon_len = 0;

    mh_cli_lines lines;
    mh_cli_lines_init(&lines);

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--file") == 0 && i + 1 < argc) {
            file_path = argv[++i];
        } else if (strcmp(argv[i], "--stdin") == 0) {
            use_stdin = 1;
        } else if (strcmp(argv[i], "--format") == 0 && i + 1 < argc) {
            format = argv[++i];
        } else if ((strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--precision") == 0) && i + 1 < argc) {
            precision = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--no-clamp") == 0) {
            clamp = 0;
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
        if (strcmp(format, "csv") == 0) {
            fputs("locator\n", out);
        }
        for (size_t i = 0; i < lines.length; i++) {
            mh_cli_tokens tokens;
            if (!mh_cli_split_line(lines.items[i], &tokens)) {
                free(latlon_parts);
                mh_cli_lines_free(&lines);
                mh_cli_print_error(err, "allocation failed");
                return 2;
            }
            if (tokens.length != 2) {
                mh_cli_tokens_free(&tokens);
                free(latlon_parts);
                mh_cli_lines_free(&lines);
                mh_cli_print_error(err, "Latitude/longitude must be 'lat lon' or 'lat,lon'");
                return 2;
            }
            double lat = 0.0;
            double lon = 0.0;
            if (!mh_cli_parse_double(tokens.items[0], &lat) || !mh_cli_parse_double(tokens.items[1], &lon)) {
                mh_cli_tokens_free(&tokens);
                free(latlon_parts);
                mh_cli_lines_free(&lines);
                mh_cli_print_error(err, "Latitude/longitude must be 'lat lon' or 'lat,lon'");
                return 2;
            }
            mh_grid grid;
            mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
            mh_status st = mh_from_latlon(lat, lon, precision, clamp, &grid, &ctx);
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
                fputs("{\"input\":", out);
                mh_cli_json_print_latlon(out, lat, lon);
                fputs(",\"output\":", out);
                mh_cli_print_json_string(out, grid.locator);
                fputc('}', out);
            } else {
                fprintf(out, "%s%s", grid.locator, i + 1 < lines.length ? "\n" : "");
            }
            mh_cli_tokens_free(&tokens);
        }
        if (strcmp(format, "json") == 0) {
            fputc(']', out);
            fputc('\n', out);
        } else {
            fputc('\n', out);
        }
        free(latlon_parts);
        mh_cli_lines_free(&lines);
        return 0;
    }

    if (latlon_len == 0) {
        free(latlon_parts);
        mh_cli_lines_free(&lines);
        mh_cli_print_error(err, "lat/lon is required unless --file/--stdin is provided");
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
    mh_grid grid;
    mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
    mh_status st = mh_from_latlon(lat, lon, precision, clamp, &grid, &ctx);
    if (st != MH_OK) {
        free(latlon_parts);
        mh_cli_lines_free(&lines);
        return mh_cli_print_mh_error(err, &ctx);
    }
    if (strcmp(format, "json") == 0) {
        fputs("{\"input\":", out);
        mh_cli_json_print_latlon(out, lat, lon);
        fputs(",\"output\":", out);
        mh_cli_print_json_string(out, grid.locator);
        mh_cli_json_obj_end(out);
        fputc('\n', out);
    } else {
        fprintf(out, "%s\n", grid.locator);
    }
    free(latlon_parts);
    mh_cli_lines_free(&lines);
    return 0;
}

int mh_cli_handle_precision(int argc, char **argv, FILE *out, FILE *err) {
    const char *locator = NULL;
    for (int i = 2; i < argc; i++) {
        if (!locator) {
            locator = argv[i];
        } else {
            mh_cli_print_error(err, "unexpected argument");
            return 2;
        }
    }
    if (!mh_cli_require_arg(locator, "locator is required", err)) {
        return 2;
    }
    int precision = 0;
    mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
    mh_status st = mh_precision_of(locator, &precision, &ctx);
    if (st != MH_OK) {
        return mh_cli_print_mh_error(err, &ctx);
    }
    fprintf(out, "%d\n", precision);
    return 0;
}

int mh_cli_handle_children(int argc, char **argv, FILE *out, FILE *err) {
    const char *locator = NULL;
    int precision = -1;
    int csv = 0;
    int limit = -1;
    const char *format = "plain";

    for (int i = 2; i < argc; i++) {
        if ((strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--precision") == 0) && i + 1 < argc) {
            precision = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--limit") == 0 && i + 1 < argc) {
            limit = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--format") == 0 && i + 1 < argc) {
            format = argv[++i];
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

    mh_grid grid;
    mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
    mh_status st = mh_parse_locator(locator, &grid, &ctx);
    if (st != MH_OK) {
        return mh_cli_print_mh_error(err, &ctx);
    }
    if (precision < 0) {
        precision = grid.precision + 2;
    }

    mh_cli_lines lines;
    mh_cli_lines_init(&lines);
    mh_cli_children_ctx child_ctx = {&lines, (size_t)(limit > 0 ? limit : 0)};
    st = mh_children_iter(locator, precision, mh_cli_children_callback, &child_ctx, &ctx);
    if (st != MH_OK) {
        mh_cli_lines_free(&lines);
        return mh_cli_print_mh_error(err, &ctx);
    }

    if (strcmp(format, "json") == 0) {
        fputc('[', out);
        fputs("{\"input\":", out);
        mh_cli_print_json_string(out, locator);
        fputs(",\"output\":", out);
        fputc('[', out);
        for (size_t i = 0; i < lines.length; i++) {
            if (i > 0) {
                fputc(',', out);
            }
            mh_cli_print_json_string(out, lines.items[i]);
        }
        fputs("]}]\n", out);
    } else {
        if (csv) {
            fputs("locator\n", out);
            for (size_t i = 0; i < lines.length; i++) {
                fprintf(out, "%s\n", lines.items[i]);
            }
        } else {
            const char *sep = " ";
            for (size_t i = 0; i < lines.length; i++) {
                fprintf(out, "%s%s", lines.items[i], i + 1 < lines.length ? sep : "");
            }
            fprintf(out, "\n");
        }
    }
    mh_cli_lines_free(&lines);
    return 0;
}

int mh_cli_handle_parent(int argc, char **argv, FILE *out, FILE *err) {
    const char *locator = NULL;
    int precision = -1;
    const char *format = "plain";

    for (int i = 2; i < argc; i++) {
        if ((strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--precision") == 0) && i + 1 < argc) {
            precision = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--format") == 0 && i + 1 < argc) {
            format = argv[++i];
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

    mh_grid grid;
    mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
    mh_status st = mh_parent(locator, precision, &grid, &ctx);
    if (st != MH_OK) {
        return mh_cli_print_mh_error(err, &ctx);
    }
    if (strcmp(format, "json") == 0) {
        fputc('[', out);
        fputs("{\"input\":", out);
        mh_cli_print_json_string(out, locator);
        fputs(",\"output\":", out);
        mh_cli_print_json_string(out, grid.locator);
        fputs("}]\n", out);
    } else {
        fprintf(out, "%s\n", grid.locator);
    }
    return 0;
}

int mh_cli_handle_utm(int argc, char **argv, FILE *out, FILE *err) {
    const char *locator = NULL;
    for (int i = 2; i < argc; i++) {
        if (!locator) {
            locator = argv[i];
        } else {
            mh_cli_print_error(err, "unexpected argument");
            return 2;
        }
    }
    if (!mh_cli_require_arg(locator, "locator is required", err)) {
        return 2;
    }
    char zone[8];
    mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
    mh_status st = mh_to_utm_zone(locator, zone, sizeof(zone), &ctx);
    if (st != MH_OK) {
        return mh_cli_print_mh_error(err, &ctx);
    }
    fprintf(out, "%s\n", zone);
    return 0;
}

int mh_cli_handle_step(int argc, char **argv, FILE *out, FILE *err) {
    const char *locator = NULL;
    int dlat = 0;
    int dlon = 0;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--dlat-cells") == 0 && i + 1 < argc) {
            dlat = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--dlon-cells") == 0 && i + 1 < argc) {
            dlon = atoi(argv[++i]);
        } else if (!locator) {
            locator = argv[i];
        } else {
            mh_cli_print_error(err, "unexpected argument");
            return 2;
        }
    }
    if (!mh_cli_require_arg(locator, "locator is required", err)) {
        return 2;
    }
    mh_grid grid;
    mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
    mh_status st = mh_step(locator, dlat, dlon, &grid, &ctx);
    if (st != MH_OK) {
        return mh_cli_print_mh_error(err, &ctx);
    }
    fprintf(out, "%s\n", grid.locator);
    return 0;
}

int mh_cli_handle_corners(int argc, char **argv, FILE *out, FILE *err) {
    const char *locator = NULL;
    int digits = MH_CLI_DEFAULT_DIGITS;
    int csv = 0;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--digits") == 0 && i + 1 < argc) {
            digits = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--csv") == 0) {
            csv = 1;
        } else if (!locator) {
            locator = argv[i];
        } else {
            mh_cli_print_error(err, "unexpected argument");
            return 2;
        }
    }
    if (!mh_cli_require_arg(locator, "locator is required", err)) {
        return 2;
    }
    mh_corners_t corners;
    mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
    mh_status st = mh_corners(locator, &corners, &ctx);
    if (st != MH_OK) {
        return mh_cli_print_mh_error(err, &ctx);
    }
    const char *sep = csv ? "," : " ";
    if (csv) {
        fputs("lat,lon\n", out);
    }
    char buf[128];
    mh_cli_format_latlon(buf, sizeof(buf), corners.nw.lat, corners.nw.lon, digits, sep);
    fprintf(out, "%s\n", buf);
    mh_cli_format_latlon(buf, sizeof(buf), corners.ne.lat, corners.ne.lon, digits, sep);
    fprintf(out, "%s\n", buf);
    mh_cli_format_latlon(buf, sizeof(buf), corners.sw.lat, corners.sw.lon, digits, sep);
    fprintf(out, "%s\n", buf);
    mh_cli_format_latlon(buf, sizeof(buf), corners.se.lat, corners.se.lon, digits, sep);
    fprintf(out, "%s\n", buf);
    return 0;
}

int mh_cli_handle_size(int argc, char **argv, FILE *out, FILE *err) {
    const char *locator = NULL;
    const char *unit = "deg";
    const char *method = "spherical";
    double at_lat = 0.0;
    int use_at_lat = 0;
    int digits = MH_CLI_DEFAULT_DIGITS;
    int csv = 0;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--unit") == 0 && i + 1 < argc) {
            unit = argv[++i];
        } else if (strcmp(argv[i], "--lon-at") == 0 && i + 1 < argc) {
            if (!mh_cli_parse_double(argv[++i], &at_lat)) {
                mh_cli_print_error(err, "invalid latitude");
                return 2;
            }
            use_at_lat = 1;
        } else if (strcmp(argv[i], "--at-lat") == 0 && i + 1 < argc) {
            if (!mh_cli_parse_double(argv[++i], &at_lat)) {
                mh_cli_print_error(err, "invalid latitude");
                return 2;
            }
            use_at_lat = 1;
        } else if (strcmp(argv[i], "--method") == 0 && i + 1 < argc) {
            method = argv[++i];
        } else if (strcmp(argv[i], "--digits") == 0 && i + 1 < argc) {
            digits = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--csv") == 0) {
            csv = 1;
        } else if (!locator) {
            locator = argv[i];
        } else {
            mh_cli_print_error(err, "unexpected argument");
            return 2;
        }
    }

    if (!mh_cli_require_arg(locator, "locator is required", err)) {
        return 2;
    }

    mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
    int precision = 0;
    mh_status st = mh_precision_of(locator, &precision, &ctx);
    if (st != MH_OK) {
        return mh_cli_print_mh_error(err, &ctx);
    }
    double lon_step = 0.0;
    double lat_step = 0.0;
    st = mh_cli_step_size_for_precision(precision, &lon_step, &lat_step, &ctx);
    if (st != MH_OK) {
        return mh_cli_print_mh_error(err, &ctx);
    }

    double width = lon_step;
    double height = lat_step;
    if (strcmp(unit, "deg") != 0) {
        double lat = 0.0;
        double lon = 0.0;
        st = mh_to_center_latlon(locator, &lat, &lon, &ctx);
        if (st != MH_OK) {
            return mh_cli_print_mh_error(err, &ctx);
        }
        double half_lon = lon_step / 2.0;
        double half_lat = lat_step / 2.0;
        mh_point a = {use_at_lat ? at_lat : lat, lon - half_lon};
        mh_point b = {use_at_lat ? at_lat : lat, lon + half_lon};
        mh_point c = {lat - half_lat, lon};
        mh_point d = {lat + half_lat, lon};
        int method_code = strcmp(method, "geodesic") == 0 ? MH_DISTANCE_GEODESIC : MH_DISTANCE_HAVERSINE;
        double width_km = 0.0;
        double height_km = 0.0;
        st = mh_distance_km(&a, &b, method_code, &width_km, &ctx);
        if (st != MH_OK) {
            return mh_cli_print_mh_error(err, &ctx);
        }
        st = mh_distance_km(&c, &d, method_code, &height_km, &ctx);
        if (st != MH_OK) {
            return mh_cli_print_mh_error(err, &ctx);
        }
        if (strcmp(unit, "miles") == 0) {
            const double miles_per_km = 0.621371;
            width_km *= miles_per_km;
            height_km *= miles_per_km;
        }
        width = width_km;
        height = height_km;
    } else if (use_at_lat) {
        mh_cli_print_error(err, "--at-lat requires --unit km or miles");
        return 2;
    }

    const char *sep = csv ? "," : " ";
    if (csv) {
        fputs("input,width,height\n", out);
        fprintf(out, "%s,%.*f,%.*f\n", locator, digits, width, digits, height);
    } else {
        fprintf(out, "%.*f%s%.*f\n", digits, width, sep, digits, height);
    }
    return 0;
}

int mh_cli_handle_area(int argc, char **argv, FILE *out, FILE *err) {
    const char *locator = NULL;
    const char *method = "spherical";
    int digits = MH_CLI_DEFAULT_DIGITS;
    const char *format = "plain";
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--method") == 0 && i + 1 < argc) {
            method = argv[++i];
        } else if (strcmp(argv[i], "--digits") == 0 && i + 1 < argc) {
            digits = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--format") == 0 && i + 1 < argc) {
            format = argv[++i];
        } else if (!locator) {
            locator = argv[i];
        } else {
            mh_cli_print_error(err, "unexpected argument");
            return 2;
        }
    }
    if (!mh_cli_require_arg(locator, "locator is required", err)) {
        return 2;
    }

    double area = 0.0;
    mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
    if (strcmp(method, "geodesic") == 0) {
        mh_bbox bbox;
        mh_status st = mh_to_bbox(locator, &bbox, &ctx);
        if (st != MH_OK) {
            return mh_cli_print_mh_error(err, &ctx);
        }
        mh_point poly[4] = {
            {bbox.min_lat, bbox.min_lon},
            {bbox.min_lat, bbox.max_lon},
            {bbox.max_lat, bbox.max_lon},
            {bbox.max_lat, bbox.min_lon},
        };
        st = mh_geodesic_area_km2(poly, 4, &area, &ctx);
        if (st != MH_OK) {
            return mh_cli_print_mh_error(err, &ctx);
        }
        if (area < 0.0) {
            area = -area;
        }
    } else {
        int precision = 0;
        mh_status st = mh_precision_of(locator, &precision, &ctx);
        if (st != MH_OK) {
            return mh_cli_print_mh_error(err, &ctx);
        }
        double lon_step = 0.0;
        double lat_step = 0.0;
        st = mh_cli_step_size_for_precision(precision, &lon_step, &lat_step, &ctx);
        if (st != MH_OK) {
            return mh_cli_print_mh_error(err, &ctx);
        }
        double lat = 0.0;
        double lon = 0.0;
        st = mh_to_center_latlon(locator, &lat, &lon, &ctx);
        if (st != MH_OK) {
            return mh_cli_print_mh_error(err, &ctx);
        }
        double half_lon = lon_step / 2.0;
        double half_lat = lat_step / 2.0;
        mh_point a = {lat, lon - half_lon};
        mh_point b = {lat, lon + half_lon};
        mh_point c = {lat - half_lat, lon};
        mh_point d = {lat + half_lat, lon};
        double width_km = 0.0;
        double height_km = 0.0;
        st = mh_distance_km(&a, &b, MH_DISTANCE_HAVERSINE, &width_km, &ctx);
        if (st != MH_OK) {
            return mh_cli_print_mh_error(err, &ctx);
        }
        st = mh_distance_km(&c, &d, MH_DISTANCE_HAVERSINE, &height_km, &ctx);
        if (st != MH_OK) {
            return mh_cli_print_mh_error(err, &ctx);
        }
        area = width_km * height_km;
    }

    if (strcmp(format, "json") == 0) {
        fputs("{\"input\":", out);
        mh_cli_print_json_string(out, locator);
        fputs(",\"output\":", out);
        fprintf(out, "%.*f", digits, area);
        fputs("}\n", out);
    } else {
        fprintf(out, "%.*f\n", digits, area);
    }
    return 0;
}

int mh_cli_handle_diagonal(int argc, char **argv, FILE *out, FILE *err) {
    const char *locator = NULL;
    const char *method = "spherical";
    int digits = MH_CLI_DEFAULT_DIGITS;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--method") == 0 && i + 1 < argc) {
            method = argv[++i];
        } else if (strcmp(argv[i], "--digits") == 0 && i + 1 < argc) {
            digits = atoi(argv[++i]);
        } else if (!locator) {
            locator = argv[i];
        } else {
            mh_cli_print_error(err, "unexpected argument");
            return 2;
        }
    }
    if (!mh_cli_require_arg(locator, "locator is required", err)) {
        return 2;
    }

    mh_bbox bbox;
    mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
    mh_status st = mh_to_bbox(locator, &bbox, &ctx);
    if (st != MH_OK) {
        return mh_cli_print_mh_error(err, &ctx);
    }
    mh_point a = {bbox.min_lat, bbox.min_lon};
    mh_point b = {bbox.max_lat, bbox.max_lon};
    double dist = 0.0;
    int method_code = strcmp(method, "geodesic") == 0 ? MH_DISTANCE_GEODESIC : MH_DISTANCE_HAVERSINE;
    st = mh_distance_km(&a, &b, method_code, &dist, &ctx);
    if (st != MH_OK) {
        return mh_cli_print_mh_error(err, &ctx);
    }
    fprintf(out, "%.*f\n", digits, dist);
    return 0;
}

