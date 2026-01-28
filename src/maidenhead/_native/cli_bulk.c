#include "cli_utils.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core.h"
#include "coverage.h"
#include "geo.h"
#include "geojson.h"
#include "types.h"

int mh_cli_handle_bulk(int argc, char **argv, FILE *out, FILE *err) {
    const char *op = NULL;
    const char *file_path = NULL;
    const char *format = "plain";
    const char *unit = "deg";
    const char *method = "spherical";
    const char *geojson_format = "feature";
    int use_stdin = 0;
    int precision = -1;
    int range_mode = 0;
    int digits = MH_CLI_DEFAULT_DIGITS;
    int diagonals = 1;
    int ring = 1;
    int limit = -1;
    int split = 0;
    int have_at_lat = 0;
    double at_lat = 0.0;

    if (argc < 3) {
        mh_cli_print_error(err, "bulk requires an operation");
        return 2;
    }
    op = argv[2];

    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--file") == 0 && i + 1 < argc) {
            file_path = argv[++i];
        } else if (strcmp(argv[i], "--stdin") == 0) {
            use_stdin = 1;
        } else if (strcmp(argv[i], "--format") == 0 && i + 1 < argc) {
            format = argv[++i];
        } else if ((strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--precision") == 0) && i + 1 < argc) {
            precision = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--range") == 0) {
            range_mode = 1;
        } else if (strcmp(argv[i], "--digits") == 0 && i + 1 < argc) {
            digits = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--diagonals") == 0) {
            diagonals = 1;
        } else if (strcmp(argv[i], "--no-diagonals") == 0) {
            diagonals = 0;
        } else if (strcmp(argv[i], "--ring") == 0 && i + 1 < argc) {
            ring = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--limit") == 0 && i + 1 < argc) {
            limit = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--unit") == 0 && i + 1 < argc) {
            unit = argv[++i];
        } else if (strcmp(argv[i], "--at-lat") == 0 && i + 1 < argc) {
            if (!mh_cli_parse_double(argv[++i], &at_lat)) {
                mh_cli_print_error(err, "invalid latitude");
                return 2;
            }
            have_at_lat = 1;
        } else if (strcmp(argv[i], "--method") == 0 && i + 1 < argc) {
            method = argv[++i];
        } else if (strcmp(argv[i], "--geojson-format") == 0 && i + 1 < argc) {
            geojson_format = argv[++i];
        } else if (strcmp(argv[i], "--split") == 0) {
            split = 1;
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
    if (lines.length == 0) {
        mh_cli_lines_free(&lines);
        mh_cli_print_error(err, "bulk requires --file or --stdin input");
        return 2;
    }

    const char *csv_header = NULL;
    if (strcmp(format, "csv") == 0) {
        if (strcmp(op, "normalize") == 0) {
            csv_header = "locator";
        } else if (strcmp(op, "from-latlon") == 0) {
            csv_header = "input_lat,input_lon,locator";
        } else if (strcmp(op, "center") == 0) {
            csv_header = "input,lat,lon";
        } else if (strcmp(op, "bbox") == 0 || strcmp(op, "bbox-split") == 0 || strcmp(op, "bbox-split-list") == 0) {
            csv_header = "input,min_lat,min_lon,max_lat,max_lon";
        } else if (strcmp(op, "wkt") == 0) {
            csv_header = "input,wkt";
        } else if (strcmp(op, "contains") == 0 || strcmp(op, "contains-point") == 0) {
            csv_header = "contains";
        } else if (strcmp(op, "intersects-bbox") == 0 || strcmp(op, "intersects-polygon") == 0) {
            csv_header = "intersects";
        } else if (strcmp(op, "azimuth") == 0) {
            csv_header = range_mode
                ? "input_a,input_b,bearing_deg,min_distance_km,max_distance_km"
                : "input_a,input_b,bearing_deg,distance_km";
        } else if (strcmp(op, "initial-bearing") == 0) {
            csv_header = "bearing_deg";
        } else if (strcmp(op, "neighbors") == 0) {
            csv_header = "input,neighbor";
        } else if (strcmp(op, "adjacent") == 0) {
            csv_header = "input,direction,locator";
        } else if (strcmp(op, "corners") == 0) {
            csv_header = "input,lat,lon";
        } else if (strcmp(op, "precision") == 0) {
            csv_header = "input,precision";
        } else if (strcmp(op, "parent") == 0) {
            csv_header = "input,locator";
        } else if (strcmp(op, "children") == 0) {
            csv_header = "input,child";
        } else if (strcmp(op, "size") == 0) {
            csv_header = "input,width,height";
        } else if (strcmp(op, "area") == 0) {
            csv_header = "input,area_km2";
        } else if (strcmp(op, "diagonal") == 0) {
            csv_header = "input,diagonal_km";
        } else if (strcmp(op, "utm") == 0) {
            csv_header = "input,utm_zone";
        } else if (strcmp(op, "geojson") == 0) {
            if (strcmp(geojson_format, "point") == 0) {
                csv_header = "input_lat,input_lon,grid,geojson";
            } else {
                csv_header = "input,geojson";
            }
        }
        if (csv_header) {
            fprintf(out, "%s\n", csv_header);
        }
    }

    if (strcmp(op, "normalize") == 0) {
        if (strcmp(format, "json") == 0) {
            mh_cli_json_bulk_start(out, &lines);
        }
        for (size_t i = 0; i < lines.length; i++) {
            char norm[12];
            mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
            mh_status st = mh_normalize_locator(lines.items[i], norm, sizeof(norm), &ctx);
            if (st != MH_OK) {
                mh_cli_lines_free(&lines);
                return mh_cli_print_mh_error_line(err, &ctx, i + 1);
            }
            if (strcmp(format, "json") == 0) {
                if (i > 0) {
                    fputc(',', out);
                }
                mh_cli_json_obj_start_input_string(out, lines.items[i]);
                mh_cli_print_json_string(out, norm);
                mh_cli_json_obj_end(out);
            } else {
                fprintf(out, "%s%s", norm, i + 1 < lines.length ? "\n" : "");
            }
        }
        if (strcmp(format, "json") == 0) {
            mh_cli_json_bulk_end(out);
        }
        fputc('\n', out);
        mh_cli_lines_free(&lines);
        return 0;
    }

    if (strcmp(op, "from-latlon") == 0) {
        int out_precision = precision >= 0 ? precision : 6;
        if (strcmp(format, "json") == 0) {
            fputc('[', out);
        }
        for (size_t i = 0; i < lines.length; i++) {
            mh_cli_tokens tokens;
            if (!mh_cli_split_line(lines.items[i], &tokens)) {
                mh_cli_lines_free(&lines);
                mh_cli_print_error(err, "allocation failed");
                return 2;
            }
            if (tokens.length != 2) {
                mh_cli_tokens_free(&tokens);
                mh_cli_lines_free(&lines);
                mh_cli_print_error(err, "Expected lines: lat lon");
                return 2;
            }
            double lat = 0.0;
            double lon = 0.0;
            if (!mh_cli_parse_double(tokens.items[0], &lat) || !mh_cli_parse_double(tokens.items[1], &lon)) {
                mh_cli_tokens_free(&tokens);
                mh_cli_lines_free(&lines);
                mh_cli_print_error(err, "Latitude/longitude must be 'lat lon' or 'lat,lon'");
                return 2;
            }
            mh_grid grid;
            mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
            mh_status st = mh_from_latlon(lat, lon, out_precision, 1, &grid, &ctx);
            if (st != MH_OK) {
                mh_cli_tokens_free(&tokens);
                mh_cli_lines_free(&lines);
                return mh_cli_print_mh_error_line(err, &ctx, i + 1);
            }
            if (strcmp(format, "json") == 0) {
                if (i > 0) {
                    fputc(',', out);
                }
                fputs("{\"input\":", out);
                mh_cli_json_print_latlon(out, lat, lon);
                fputs(",\"output\":", out);
                mh_cli_print_json_string(out, grid.locator);
                mh_cli_json_obj_end(out);
            } else if (strcmp(format, "csv") == 0) {
                fprintf(out, "%.17g,%.17g,%s\n", lat, lon, grid.locator);
            } else {
                fprintf(out, "%s%s", grid.locator, i + 1 < lines.length ? "\n" : "");
            }
            mh_cli_tokens_free(&tokens);
        }
        if (strcmp(format, "json") == 0) {
            fputc(']', out);
            fputc('\n', out);
        } else {
            if (strcmp(format, "csv") != 0) {
                fputc('\n', out);
            }
        }
        mh_cli_lines_free(&lines);
        return 0;
    }

    if (strcmp(op, "center") == 0) {
        if (strcmp(format, "json") == 0) {
            mh_cli_json_bulk_start(out, &lines);
        }
        for (size_t i = 0; i < lines.length; i++) {
            double lat = 0.0;
            double lon = 0.0;
            mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
            mh_status st = mh_to_center_latlon(lines.items[i], &lat, &lon, &ctx);
            if (st != MH_OK) {
                mh_cli_lines_free(&lines);
                return mh_cli_print_mh_error_line(err, &ctx, i + 1);
            }
            if (strcmp(format, "json") == 0) {
                if (i > 0) {
                    fputc(',', out);
                }
                mh_cli_json_obj_start_input_string(out, lines.items[i]);
                fputc('[', out);
                mh_cli_print_json_float(out, lat);
                fputc(',', out);
                mh_cli_print_json_float(out, lon);
                fputc(']', out);
                mh_cli_json_obj_end(out);
            } else if (strcmp(format, "csv") == 0) {
                fprintf(out, "%s,%.17g,%.17g\n", lines.items[i], lat, lon);
            } else {
                const char *sep = " ";
                char buf[128];
                mh_cli_format_latlon(buf, sizeof(buf), lat, lon, digits, sep);
                fprintf(out, "%s%s", buf, i + 1 < lines.length ? "\n" : "");
            }
        }
        if (strcmp(format, "json") == 0) {
            mh_cli_json_bulk_end(out);
        }
        fputc('\n', out);
        mh_cli_lines_free(&lines);
        return 0;
    }

    if (strcmp(op, "bbox") == 0) {
        if (strcmp(format, "json") == 0) {
            mh_cli_json_bulk_start(out, &lines);
        }
        for (size_t i = 0; i < lines.length; i++) {
            mh_bbox bbox;
            mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
            mh_status st = mh_to_bbox(lines.items[i], &bbox, &ctx);
            if (st != MH_OK) {
                mh_cli_lines_free(&lines);
                return mh_cli_print_mh_error_line(err, &ctx, i + 1);
            }
            if (strcmp(format, "json") == 0) {
                if (i > 0) {
                    fputc(',', out);
                }
                mh_cli_json_obj_start_input_string(out, lines.items[i]);
                mh_cli_print_bbox_json(out, &bbox);
                mh_cli_json_obj_end(out);
            } else if (strcmp(format, "csv") == 0) {
                char buf[128];
                mh_cli_format_bbox(buf, sizeof(buf), &bbox, digits, ",");
                fprintf(out, "%s,%s\n", lines.items[i], buf);
            } else {
                const char *sep = " ";
                char buf[128];
                mh_cli_format_bbox(buf, sizeof(buf), &bbox, digits, sep);
                fprintf(out, "%s%s", buf, i + 1 < lines.length ? "\n" : "");
            }
        }
        if (strcmp(format, "json") == 0) {
            mh_cli_json_bulk_end(out);
        }
        fputc('\n', out);
        mh_cli_lines_free(&lines);
        return 0;
    }

    if (strcmp(op, "wkt") == 0) {
        int out_precision = precision >= 0 ? precision : 6;
        if (strcmp(format, "json") == 0) {
            mh_cli_json_bulk_start(out, &lines);
        }
        for (size_t i = 0; i < lines.length; i++) {
            mh_bbox bbox;
            mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
            mh_status st;
            int input_is_latlon = 0;
            double input_lat = 0.0;
            double input_lon = 0.0;
            if (strchr(lines.items[i], ',')) {
                if (!mh_cli_parse_latlon_joined(lines.items[i], &input_lat, &input_lon)) {
                    mh_cli_lines_free(&lines);
                    mh_cli_print_error(err, "Latitude/longitude must be 'lat lon' or 'lat,lon'");
                    return 2;
                }
                input_is_latlon = 1;
                mh_grid grid;
                st = mh_from_latlon(input_lat, input_lon, out_precision, 1, &grid, &ctx);
                if (st != MH_OK) {
                    mh_cli_lines_free(&lines);
                    return mh_cli_print_mh_error_line(err, &ctx, i + 1);
                }
                st = mh_to_bbox(grid.locator, &bbox, &ctx);
            } else {
                st = mh_to_bbox(lines.items[i], &bbox, &ctx);
            }
            if (st != MH_OK) {
                mh_cli_lines_free(&lines);
                return mh_cli_print_mh_error_line(err, &ctx, i + 1);
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
                if (input_is_latlon) {
                    fputs("{\"input\":", out);
                    mh_cli_json_print_latlon(out, input_lat, input_lon);
                    fputs(",\"output\":", out);
                } else {
                    mh_cli_json_obj_start_input_string(out, lines.items[i]);
                }
                mh_cli_print_json_string(out, buf);
                mh_cli_json_obj_end(out);
            } else if (strcmp(format, "csv") == 0) {
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
                fprintf(out, "%s,%s\n", lines.items[i], buf);
            } else {
                mh_cli_print_wkt_polygon(out, &bbox);
                fprintf(out, "%s", i + 1 < lines.length ? "\n" : "");
            }
        }
        if (strcmp(format, "json") == 0) {
            mh_cli_json_bulk_end(out);
        }
        fputc('\n', out);
        mh_cli_lines_free(&lines);
        return 0;
    }

    if (strcmp(op, "contains-point") == 0) {
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
            if (tokens.length < 2) {
                mh_cli_tokens_free(&tokens);
                mh_cli_lines_free(&lines);
                mh_cli_print_error(err, "Expected lines: locator lat lon");
                return 2;
            }
            double lat = 0.0;
            double lon = 0.0;
            if (!mh_cli_split_latlon_parts((const char **)&tokens.items[1], tokens.length - 1, &lat, &lon)) {
                mh_cli_tokens_free(&tokens);
                mh_cli_lines_free(&lines);
                mh_cli_print_error(err, "Latitude/longitude must be 'lat lon' or 'lat,lon'");
                return 2;
            }
            int hit = 0;
            mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
            mh_status st = mh_contains_point(tokens.items[0], lat, lon, &hit, &ctx);
            if (st != MH_OK) {
                mh_cli_tokens_free(&tokens);
                mh_cli_lines_free(&lines);
                return mh_cli_print_mh_error_line(err, &ctx, i + 1);
            }
            if (strcmp(format, "json") == 0) {
                if (i > 0) {
                    fputc(',', out);
                }
                fputs("{\"input\":[", out);
                mh_cli_print_json_string(out, tokens.items[0]);
                fputc(',', out);
                mh_cli_json_print_latlon(out, lat, lon);
                fputs("],\"output\":", out);
                fputs(hit ? "true" : "false", out);
                mh_cli_json_obj_end(out);
            } else if (strcmp(format, "csv") == 0) {
                fprintf(out, "%s\n", hit ? "true" : "false");
            } else {
                fprintf(out, "%s%s", hit ? "true" : "false", i + 1 < lines.length ? "\n" : "");
            }
            mh_cli_tokens_free(&tokens);
        }
        if (strcmp(format, "json") == 0) {
            mh_cli_json_bulk_end(out);
        }
        if (strcmp(format, "csv") != 0) {
            fputc('\n', out);
        }
        mh_cli_lines_free(&lines);
        return 0;
    }

    if (strcmp(op, "contains") == 0) {
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
            if (tokens.length != 2) {
                mh_cli_tokens_free(&tokens);
                mh_cli_lines_free(&lines);
                mh_cli_print_error(err, "Expected lines: outer inner");
                return 2;
            }
            int hit = 0;
            mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
            mh_status st = mh_contains(tokens.items[0], tokens.items[1], &hit, &ctx);
            if (st != MH_OK) {
                mh_cli_tokens_free(&tokens);
                mh_cli_lines_free(&lines);
                return mh_cli_print_mh_error_line(err, &ctx, i + 1);
            }
            if (strcmp(format, "json") == 0) {
                if (i > 0) {
                    fputc(',', out);
                }
                const char *inputs[] = {tokens.items[0], tokens.items[1]};
                mh_cli_json_obj_start_input_array(out, inputs, 2);
                fputs(hit ? "true" : "false", out);
                mh_cli_json_obj_end(out);
            } else if (strcmp(format, "csv") == 0) {
                fprintf(out, "%s\n", hit ? "true" : "false");
            } else {
                fprintf(out, "%s%s", hit ? "true" : "false", i + 1 < lines.length ? "\n" : "");
            }
            mh_cli_tokens_free(&tokens);
        }
        if (strcmp(format, "json") == 0) {
            mh_cli_json_bulk_end(out);
        }
        if (strcmp(format, "csv") != 0) {
            fputc('\n', out);
        }
        mh_cli_lines_free(&lines);
        return 0;
    }

    if (strcmp(op, "intersects-bbox") == 0) {
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
                return mh_cli_print_mh_error_line(err, &ctx, i + 1);
            }
            if (strcmp(format, "json") == 0) {
                if (i > 0) {
                    fputc(',', out);
                }
                fputs("{\"input\":[", out);
                mh_cli_print_json_string(out, tokens.items[0]);
                fputc(',', out);
                fprintf(out, "%.17g,%.17g,%.17g,%.17g", bbox.min_lat, bbox.min_lon, bbox.max_lat, bbox.max_lon);
                fputs("],\"output\":", out);
                fputs(hit ? "true" : "false", out);
                fputc('}', out);
            } else if (strcmp(format, "csv") == 0) {
                fprintf(out, "%s\n", hit ? "true" : "false");
            } else {
                fprintf(out, "%s%s", hit ? "true" : "false", i + 1 < lines.length ? "\n" : "");
            }
            mh_cli_tokens_free(&tokens);
        }
        if (strcmp(format, "json") == 0) {
            mh_cli_json_bulk_end(out);
        }
        if (strcmp(format, "csv") != 0) {
            fputc('\n', out);
        }
        mh_cli_lines_free(&lines);
        return 0;
    }

    if (strcmp(op, "intersects-polygon") == 0) {
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
            if (tokens.length < 4) {
                mh_cli_tokens_free(&tokens);
                mh_cli_lines_free(&lines);
                mh_cli_print_error(err, "Expected lines: locator lat,lon lat,lon lat,lon ...");
                return 2;
            }
            size_t poly_len = tokens.length - 1;
            mh_point *points = (mh_point *)calloc(poly_len, sizeof(mh_point));
            if (!points) {
                mh_cli_tokens_free(&tokens);
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
                mh_cli_lines_free(&lines);
                return mh_cli_print_mh_error_line(err, &ctx, i + 1);
            }
            if (strcmp(format, "json") == 0) {
                if (i > 0) {
                    fputc(',', out);
                }
                fputs("{\"input\":[", out);
                mh_cli_print_json_string(out, tokens.items[0]);
                for (size_t p = 0; p < poly_len; p++) {
                    fputc(',', out);
                    mh_cli_json_print_latlon(out, points[p].lat, points[p].lon);
                }
                fputs("],\"output\":", out);
                fputs(hit ? "true" : "false", out);
                mh_cli_json_obj_end(out);
            } else if (strcmp(format, "csv") == 0) {
                fprintf(out, "%s\n", hit ? "true" : "false");
            } else {
                fprintf(out, "%s%s", hit ? "true" : "false", i + 1 < lines.length ? "\n" : "");
            }
            mh_cli_tokens_free(&tokens);
        }
        if (strcmp(format, "json") == 0) {
            mh_cli_json_bulk_end(out);
        }
        if (strcmp(format, "csv") != 0) {
            fputc('\n', out);
        }
        mh_cli_lines_free(&lines);
        return 0;
    }

    if (strcmp(op, "azimuth") == 0) {
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
            if (tokens.length != 2) {
                mh_cli_tokens_free(&tokens);
                mh_cli_lines_free(&lines);
                mh_cli_print_error(err, "Expected lines: point_a point_b");
                return 2;
            }
            mh_point a;
            mh_point b;
            int a_is_loc = 0;
            int b_is_loc = 0;
            mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
            if (strchr(tokens.items[0], ',')) {
                if (!mh_cli_parse_latlon_joined(tokens.items[0], &a.lat, &a.lon)) {
                    mh_cli_tokens_free(&tokens);
                    mh_cli_lines_free(&lines);
                    mh_cli_print_error(err, "Point must be 'lat,lon' or a locator");
                    return 2;
                }
            } else {
                a_is_loc = 1;
                if (mh_to_center_latlon(tokens.items[0], &a.lat, &a.lon, &ctx) != MH_OK) {
                    mh_cli_tokens_free(&tokens);
                    mh_cli_lines_free(&lines);
                    return mh_cli_print_mh_error_line(err, &ctx, i + 1);
                }
            }
            if (strchr(tokens.items[1], ',')) {
                if (!mh_cli_parse_latlon_joined(tokens.items[1], &b.lat, &b.lon)) {
                    mh_cli_tokens_free(&tokens);
                    mh_cli_lines_free(&lines);
                    mh_cli_print_error(err, "Point must be 'lat,lon' or a locator");
                    return 2;
                }
            } else {
                b_is_loc = 1;
                if (mh_to_center_latlon(tokens.items[1], &b.lat, &b.lon, &ctx) != MH_OK) {
                    mh_cli_tokens_free(&tokens);
                    mh_cli_lines_free(&lines);
                    return mh_cli_print_mh_error_line(err, &ctx, i + 1);
                }
            }
            double bearing = 0.0;
            double dist = 0.0;
            mh_status st = mh_bearing_deg(&a, &b, &bearing, &ctx);
            if (st != MH_OK) {
                mh_cli_tokens_free(&tokens);
                mh_cli_lines_free(&lines);
                return mh_cli_print_mh_error_line(err, &ctx, i + 1);
            }
            st = mh_distance_km(&a, &b, MH_DISTANCE_HAVERSINE, &dist, &ctx);
            if (st != MH_OK) {
                mh_cli_tokens_free(&tokens);
                mh_cli_lines_free(&lines);
                return mh_cli_print_mh_error_line(err, &ctx, i + 1);
            }
            if (range_mode && a_is_loc && b_is_loc) {
                mh_corners_t corners_a;
                mh_corners_t corners_b;
                st = mh_corners(tokens.items[0], &corners_a, &ctx);
                if (st != MH_OK) {
                    mh_cli_tokens_free(&tokens);
                    mh_cli_lines_free(&lines);
                    return mh_cli_print_mh_error_line(err, &ctx, i + 1);
                }
                st = mh_corners(tokens.items[1], &corners_b, &ctx);
                if (st != MH_OK) {
                    mh_cli_tokens_free(&tokens);
                    mh_cli_lines_free(&lines);
                    return mh_cli_print_mh_error_line(err, &ctx, i + 1);
                }
                mh_point a_pts[4] = {corners_a.nw, corners_a.ne, corners_a.sw, corners_a.se};
                mh_point b_pts[4] = {corners_b.nw, corners_b.ne, corners_b.sw, corners_b.se};
                double min_km = 0.0;
                double max_km = 0.0;
                int first = 1;
                for (int ia = 0; ia < 4; ia++) {
                    for (int ib = 0; ib < 4; ib++) {
                        double d = 0.0;
                        st = mh_distance_km(&a_pts[ia], &b_pts[ib], MH_DISTANCE_HAVERSINE, &d, &ctx);
                        if (st != MH_OK) {
                            mh_cli_tokens_free(&tokens);
                            mh_cli_lines_free(&lines);
                            return mh_cli_print_mh_error_line(err, &ctx, i + 1);
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
            if (strcmp(format, "json") == 0) {
                if (i > 0) {
                    fputc(',', out);
                }
                fputs("{\"input\":[", out);
                mh_cli_json_print_point_input(out, tokens.items[0], a_is_loc, a.lat, a.lon);
                fputc(',', out);
                mh_cli_json_print_point_input(out, tokens.items[1], b_is_loc, b.lat, b.lon);
                fputs("],\"output\":", out);
                fputc('[', out);
                fprintf(out, "%.*f", digits, bearing);
                fputc(',', out);
                fprintf(out, "%.*f", digits, min_km);
                fputc(',', out);
                fprintf(out, "%.*f", digits, max_km);
                fputc(']', out);
                mh_cli_json_obj_end(out);
            } else if (strcmp(format, "csv") == 0) {
                fprintf(
                    out,
                    "%s,%s,%.*f,%.*f,%.*f\n",
                    tokens.items[0],
                    tokens.items[1],
                    digits,
                    bearing,
                    digits,
                    min_km,
                    digits,
                    max_km
                );
            } else {
                fprintf(
                    out,
                    "%.*f %.*f %.*f%s",
                    digits,
                    bearing,
                    digits,
                    min_km,
                    digits,
                    max_km,
                    i + 1 < lines.length ? "\n" : ""
                );
            }
            } else {
                if (strcmp(format, "json") == 0) {
                    if (i > 0) {
                        fputc(',', out);
                    }
                    fputs("{\"input\":[", out);
                    mh_cli_json_print_point_input(out, tokens.items[0], a_is_loc, a.lat, a.lon);
                    fputc(',', out);
                    mh_cli_json_print_point_input(out, tokens.items[1], b_is_loc, b.lat, b.lon);
                    fputs("],\"output\":", out);
                    fputc('[', out);
                    fprintf(out, "%.*f", digits, bearing);
                    fputc(',', out);
                    fprintf(out, "%.*f", digits, dist);
                    fputc(']', out);
                    mh_cli_json_obj_end(out);
                } else if (strcmp(format, "csv") == 0) {
                    fprintf(
                        out,
                        "%s,%s,%.*f,%.*f\n",
                        tokens.items[0],
                        tokens.items[1],
                        digits,
                        bearing,
                        digits,
                        dist
                    );
                } else {
                    fprintf(out, "%.*f %.*f%s", digits, bearing, digits, dist, i + 1 < lines.length ? "\n" : "");
                }
            }
            mh_cli_tokens_free(&tokens);
        }
        if (strcmp(format, "json") == 0) {
            mh_cli_json_bulk_end(out);
        }
        if (strcmp(format, "csv") != 0) {
            fputc('\n', out);
        }
        mh_cli_lines_free(&lines);
        return 0;
    }

    if (strcmp(op, "initial-bearing") == 0) {
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
            if (tokens.length != 2) {
                mh_cli_tokens_free(&tokens);
                mh_cli_lines_free(&lines);
                mh_cli_print_error(err, "Expected lines: locator_a locator_b");
                return 2;
            }
            double lat_a = 0.0;
            double lon_a = 0.0;
            double lat_b = 0.0;
            double lon_b = 0.0;
            mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
            mh_status st = mh_to_center_latlon(tokens.items[0], &lat_a, &lon_a, &ctx);
            if (st != MH_OK) {
                mh_cli_tokens_free(&tokens);
                mh_cli_lines_free(&lines);
                return mh_cli_print_mh_error_line(err, &ctx, i + 1);
            }
            st = mh_to_center_latlon(tokens.items[1], &lat_b, &lon_b, &ctx);
            if (st != MH_OK) {
                mh_cli_tokens_free(&tokens);
                mh_cli_lines_free(&lines);
                return mh_cli_print_mh_error_line(err, &ctx, i + 1);
            }
            mh_point a = {lat_a, lon_a};
            mh_point b = {lat_b, lon_b};
            double bearing = 0.0;
            st = mh_bearing_deg(&a, &b, &bearing, &ctx);
            if (st != MH_OK) {
                mh_cli_tokens_free(&tokens);
                mh_cli_lines_free(&lines);
                return mh_cli_print_mh_error_line(err, &ctx, i + 1);
            }
            if (strcmp(format, "json") == 0) {
                if (i > 0) {
                    fputc(',', out);
                }
                fputs("{\"input\":[", out);
                mh_cli_json_print_point_input(out, tokens.items[0], 1, lat_a, lon_a);
                fputc(',', out);
                mh_cli_json_print_point_input(out, tokens.items[1], 1, lat_b, lon_b);
                fputs("],\"output\":", out);
                fprintf(out, "%.*f", digits, bearing);
                mh_cli_json_obj_end(out);
            } else if (strcmp(format, "csv") == 0) {
                fprintf(out, "%.*f%s", digits, bearing, i + 1 < lines.length ? "," : "");
            } else {
                fprintf(out, "%.*f%s", digits, bearing, i + 1 < lines.length ? "\n" : "");
            }
            mh_cli_tokens_free(&tokens);
        }
        if (strcmp(format, "json") == 0) {
            mh_cli_json_bulk_end(out);
        }
        if (strcmp(format, "csv") != 0) {
            fputc('\n', out);
        }
        mh_cli_lines_free(&lines);
        return 0;
    }

    if (strcmp(op, "neighbors") == 0) {
        if (strcmp(format, "json") == 0) {
            mh_cli_json_bulk_start(out, &lines);
        }
        for (size_t i = 0; i < lines.length; i++) {
            mh_list list = {NULL, 0};
            mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
            mh_status st = mh_neighbors(lines.items[i], ring, diagonals, &list, &ctx);
            if (st != MH_OK) {
                mh_cli_lines_free(&lines);
                return mh_cli_print_mh_error_line(err, &ctx, i + 1);
            }
            if (strcmp(format, "json") == 0) {
                if (i > 0) {
                    fputc(',', out);
                }
                mh_cli_json_obj_start_input_string(out, lines.items[i]);
                fputc('[', out);
                for (size_t j = 0; j < list.length; j++) {
                    if (j > 0) {
                        fputc(',', out);
                    }
                    mh_cli_print_json_string(out, list.items[j]);
                }
                fputc(']', out);
                mh_cli_json_obj_end(out);
            } else if (strcmp(format, "csv") == 0) {
                for (size_t j = 0; j < list.length; j++) {
                    fprintf(out, "%s,%s\n", lines.items[i], list.items[j]);
                }
            } else {
                for (size_t j = 0; j < list.length; j++) {
                    fprintf(out, "%s%s", list.items[j], j + 1 < list.length ? " " : "");
                }
                fprintf(out, "%s", i + 1 < lines.length ? "\n" : "");
            }
            mh_free_list(&list);
        }
        if (strcmp(format, "json") == 0) {
            mh_cli_json_bulk_end(out);
        }
        if (strcmp(format, "csv") != 0) {
            fputc('\n', out);
        }
        mh_cli_lines_free(&lines);
        return 0;
    }

    if (strcmp(op, "adjacent") == 0) {
        if (strcmp(format, "json") == 0) {
            mh_cli_json_bulk_start(out, &lines);
        }
        for (size_t i = 0; i < lines.length; i++) {
            mh_kv_list list = {NULL, NULL, 0};
            mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
            mh_status st = mh_adjacent(lines.items[i], diagonals, &list, &ctx);
            if (st != MH_OK) {
                mh_cli_lines_free(&lines);
                return mh_cli_print_mh_error_line(err, &ctx, i + 1);
            }
            if (strcmp(format, "json") == 0) {
                if (i > 0) {
                    fputc(',', out);
                }
                mh_cli_json_obj_start_input_string(out, lines.items[i]);
                fputc('[', out);
                for (size_t j = 0; j < list.length; j++) {
                    if (j > 0) {
                        fputc(',', out);
                    }
                    fputc('[', out);
                    mh_cli_print_json_string(out, list.keys[j]);
                    fputc(',', out);
                    mh_cli_print_json_string(out, list.values[j]);
                    fputc(']', out);
                }
                fputc(']', out);
                mh_cli_json_obj_end(out);
            } else if (strcmp(format, "csv") == 0) {
                for (size_t j = 0; j < list.length; j++) {
                    fprintf(out, "%s,%s,%s\n", lines.items[i], list.keys[j], list.values[j]);
                }
            } else {
                for (size_t j = 0; j < list.length; j++) {
                    fprintf(out, "%s:%s%s", list.keys[j], list.values[j], j + 1 < list.length ? " " : "");
                }
                fprintf(out, "%s", i + 1 < lines.length ? "\n" : "");
            }
            mh_free_kv_list(&list);
        }
        if (strcmp(format, "json") == 0) {
            mh_cli_json_bulk_end(out);
        }
        if (strcmp(format, "csv") != 0) {
            fputc('\n', out);
        }
        mh_cli_lines_free(&lines);
        return 0;
    }

    if (strcmp(op, "corners") == 0) {
        if (strcmp(format, "json") == 0) {
            mh_cli_json_bulk_start(out, &lines);
        }
        for (size_t i = 0; i < lines.length; i++) {
            mh_corners_t corners;
            mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
            mh_status st = mh_corners(lines.items[i], &corners, &ctx);
            if (st != MH_OK) {
                mh_cli_lines_free(&lines);
                return mh_cli_print_mh_error_line(err, &ctx, i + 1);
            }
            if (strcmp(format, "json") == 0) {
                if (i > 0) {
                    fputc(',', out);
                }
                mh_cli_json_obj_start_input_string(out, lines.items[i]);
                fputc('[', out);
                mh_point pts[4] = {corners.nw, corners.ne, corners.sw, corners.se};
                for (int p = 0; p < 4; p++) {
                    if (p > 0) {
                        fputc(',', out);
                    }
                    fputc('[', out);
                    mh_cli_print_json_float(out, pts[p].lat);
                    fputc(',', out);
                    mh_cli_print_json_float(out, pts[p].lon);
                    fputc(']', out);
                }
                fputc(']', out);
                mh_cli_json_obj_end(out);
            } else if (strcmp(format, "csv") == 0) {
                fprintf(out, "%s,%.17g,%.17g\n", lines.items[i], corners.nw.lat, corners.nw.lon);
                fprintf(out, "%s,%.17g,%.17g\n", lines.items[i], corners.ne.lat, corners.ne.lon);
                fprintf(out, "%s,%.17g,%.17g\n", lines.items[i], corners.sw.lat, corners.sw.lon);
                fprintf(out, "%s,%.17g,%.17g\n", lines.items[i], corners.se.lat, corners.se.lon);
            } else {
                const char *point_sep = " ";
                char buf[128];
                mh_cli_format_latlon(buf, sizeof(buf), corners.nw.lat, corners.nw.lon, digits, point_sep);
                fprintf(out, "%s;", buf);
                mh_cli_format_latlon(buf, sizeof(buf), corners.ne.lat, corners.ne.lon, digits, point_sep);
                fprintf(out, "%s;", buf);
                mh_cli_format_latlon(buf, sizeof(buf), corners.sw.lat, corners.sw.lon, digits, point_sep);
                fprintf(out, "%s;", buf);
                mh_cli_format_latlon(buf, sizeof(buf), corners.se.lat, corners.se.lon, digits, point_sep);
                fprintf(out, "%s%s", buf, i + 1 < lines.length ? "\n" : "");
            }
        }
        if (strcmp(format, "json") == 0) {
            mh_cli_json_bulk_end(out);
        }
        if (strcmp(format, "csv") != 0) {
            fputc('\n', out);
        }
        mh_cli_lines_free(&lines);
        return 0;
    }

    if (strcmp(op, "precision") == 0) {
        if (strcmp(format, "json") == 0) {
            mh_cli_json_bulk_start(out, &lines);
        }
        for (size_t i = 0; i < lines.length; i++) {
            char norm[12];
            mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
            mh_status st = mh_normalize_locator(lines.items[i], norm, sizeof(norm), &ctx);
            if (st != MH_OK) {
                mh_cli_lines_free(&lines);
                return mh_cli_print_mh_error_line(err, &ctx, i + 1);
            }
            int p = (int)strlen(norm);
            if (strcmp(format, "json") == 0) {
                if (i > 0) {
                    fputc(',', out);
                }
                mh_cli_json_obj_start_input_string(out, lines.items[i]);
                fprintf(out, "%d", p);
                mh_cli_json_obj_end(out);
            } else if (strcmp(format, "csv") == 0) {
                fprintf(out, "%s,%d\n", lines.items[i], p);
            } else {
                fprintf(out, "%d%s", p, i + 1 < lines.length ? "\n" : "");
            }
        }
        if (strcmp(format, "json") == 0) {
            mh_cli_json_bulk_end(out);
        }
        if (strcmp(format, "csv") != 0) {
            fputc('\n', out);
        }
        mh_cli_lines_free(&lines);
        return 0;
    }

    if (strcmp(op, "parent") == 0) {
        if (strcmp(format, "json") == 0) {
            mh_cli_json_bulk_start(out, &lines);
        }
        for (size_t i = 0; i < lines.length; i++) {
            mh_grid grid;
            mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
            int target = precision >= 0 ? precision : -1;
            mh_status st = mh_parent(lines.items[i], target, &grid, &ctx);
            if (st != MH_OK) {
                mh_cli_lines_free(&lines);
                return mh_cli_print_mh_error_line(err, &ctx, i + 1);
            }
            if (strcmp(format, "json") == 0) {
                if (i > 0) {
                    fputc(',', out);
                }
                mh_cli_json_obj_start_input_string(out, lines.items[i]);
                mh_cli_print_json_string(out, grid.locator);
                mh_cli_json_obj_end(out);
            } else if (strcmp(format, "csv") == 0) {
                fprintf(out, "%s,%s\n", lines.items[i], grid.locator);
            } else {
                fprintf(out, "%s%s", grid.locator, i + 1 < lines.length ? "\n" : "");
            }
        }
        if (strcmp(format, "json") == 0) {
            mh_cli_json_bulk_end(out);
        }
        if (strcmp(format, "csv") != 0) {
            fputc('\n', out);
        }
        mh_cli_lines_free(&lines);
        return 0;
    }

    if (strcmp(op, "children") == 0) {
        if (strcmp(format, "json") == 0) {
            mh_cli_json_bulk_start(out, &lines);
        }
        for (size_t i = 0; i < lines.length; i++) {
            mh_grid grid;
            mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
            mh_status st = mh_parse_locator(lines.items[i], &grid, &ctx);
            if (st != MH_OK) {
                mh_cli_lines_free(&lines);
                return mh_cli_print_mh_error_line(err, &ctx, i + 1);
            }
            int target = precision >= 0 ? precision : (grid.precision + 2);
            mh_cli_lines children;
            mh_cli_lines_init(&children);
            mh_cli_children_ctx child_ctx = {&children, (size_t)(limit > 0 ? limit : 0)};
            st = mh_children_iter(lines.items[i], target, mh_cli_children_callback, &child_ctx, &ctx);
            if (st != MH_OK) {
                mh_cli_lines_free(&children);
                mh_cli_lines_free(&lines);
                return mh_cli_print_mh_error_line(err, &ctx, i + 1);
            }
            if (strcmp(format, "json") == 0) {
                if (i > 0) {
                    fputc(',', out);
                }
                mh_cli_json_obj_start_input_string(out, lines.items[i]);
                fputc('[', out);
                for (size_t j = 0; j < children.length; j++) {
                    if (j > 0) {
                        fputc(',', out);
                    }
                    mh_cli_print_json_string(out, children.items[j]);
                }
                fputc(']', out);
                mh_cli_json_obj_end(out);
            } else if (strcmp(format, "csv") == 0) {
                for (size_t j = 0; j < children.length; j++) {
                    fprintf(out, "%s,%s\n", lines.items[i], children.items[j]);
                }
            } else {
                for (size_t j = 0; j < children.length; j++) {
                    fprintf(out, "%s%s", children.items[j], j + 1 < children.length ? " " : "");
                }
                fprintf(out, "%s", i + 1 < lines.length ? "\n" : "");
            }
            mh_cli_lines_free(&children);
        }
        if (strcmp(format, "json") == 0) {
            mh_cli_json_bulk_end(out);
        }
        if (strcmp(format, "csv") != 0) {
            fputc('\n', out);
        }
        mh_cli_lines_free(&lines);
        return 0;
    }

    if (strcmp(op, "size") == 0) {
        if (strcmp(format, "json") == 0) {
            mh_cli_json_bulk_start(out, &lines);
        }
        for (size_t i = 0; i < lines.length; i++) {
            char norm[12];
            mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
            mh_status st = mh_normalize_locator(lines.items[i], norm, sizeof(norm), &ctx);
            if (st != MH_OK) {
                mh_cli_lines_free(&lines);
                return mh_cli_print_mh_error_line(err, &ctx, i + 1);
            }
            int prec = (int)strlen(norm);
            double lon_step = 0.0;
            double lat_step = 0.0;
            st = mh_cli_step_size_for_precision(prec, &lon_step, &lat_step, &ctx);
            if (st != MH_OK) {
                mh_cli_lines_free(&lines);
                return mh_cli_print_mh_error_line(err, &ctx, i + 1);
            }
            double width = lon_step;
            double height = lat_step;
            if (strcmp(unit, "deg") != 0) {
                double lat = 0.0;
                double lon = 0.0;
                st = mh_to_center_latlon(norm, &lat, &lon, &ctx);
                if (st != MH_OK) {
                    mh_cli_lines_free(&lines);
                    return mh_cli_print_mh_error_line(err, &ctx, i + 1);
                }
                double ref_lat = have_at_lat ? at_lat : lat;
                double half_lon = lon_step / 2.0;
                double half_lat = lat_step / 2.0;
                mh_point a = {ref_lat, lon - half_lon};
                mh_point b = {ref_lat, lon + half_lon};
                mh_point c = {lat - half_lat, lon};
                mh_point d = {lat + half_lat, lon};
                double width_km = 0.0;
                double height_km = 0.0;
                int method_code = strcmp(method, "geodesic") == 0 ? MH_DISTANCE_GEODESIC : MH_DISTANCE_HAVERSINE;
                st = mh_distance_km(&a, &b, method_code, &width_km, &ctx);
                if (st != MH_OK) {
                    mh_cli_lines_free(&lines);
                    return mh_cli_print_mh_error_line(err, &ctx, i + 1);
                }
                st = mh_distance_km(&c, &d, method_code, &height_km, &ctx);
                if (st != MH_OK) {
                    mh_cli_lines_free(&lines);
                    return mh_cli_print_mh_error_line(err, &ctx, i + 1);
                }
                width = width_km;
                height = height_km;
                if (strcmp(unit, "miles") == 0) {
                    width *= 0.621371;
                    height *= 0.621371;
                }
            }
            if (strcmp(format, "json") == 0) {
                if (i > 0) {
                    fputc(',', out);
                }
                mh_cli_json_obj_start_input_string(out, lines.items[i]);
                fputc('[', out);
                fprintf(out, "%.*f", digits, width);
                fputc(',', out);
                fprintf(out, "%.*f", digits, height);
                fputc(']', out);
                mh_cli_json_obj_end(out);
            } else if (strcmp(format, "csv") == 0) {
                fprintf(out, "%s,%.*f,%.*f\n", lines.items[i], digits, width, digits, height);
            } else {
                const char *sep = " ";
                fprintf(out, "%.*f%s%.*f%s", digits, width, sep, digits, height, i + 1 < lines.length ? "\n" : "");
            }
        }
        if (strcmp(format, "json") == 0) {
            mh_cli_json_bulk_end(out);
        }
        if (strcmp(format, "csv") != 0) {
            fputc('\n', out);
        }
        mh_cli_lines_free(&lines);
        return 0;
    }

    if (strcmp(op, "area") == 0) {
        if (strcmp(format, "json") == 0) {
            fputc('[', out);
        }
        for (size_t i = 0; i < lines.length; i++) {
            double area = 0.0;
            mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
            if (strcmp(method, "geodesic") == 0) {
                mh_bbox bbox;
                mh_status st = mh_to_bbox(lines.items[i], &bbox, &ctx);
                if (st != MH_OK) {
                    mh_cli_lines_free(&lines);
                    return mh_cli_print_mh_error_line(err, &ctx, i + 1);
                }
                mh_point poly[4] = {
                    {bbox.min_lat, bbox.min_lon},
                    {bbox.min_lat, bbox.max_lon},
                    {bbox.max_lat, bbox.max_lon},
                    {bbox.max_lat, bbox.min_lon},
                };
                mh_status st2 = mh_geodesic_area_km2(poly, 4, &area, &ctx);
                if (st2 != MH_OK) {
                    mh_cli_lines_free(&lines);
                    return mh_cli_print_mh_error_line(err, &ctx, i + 1);
                }
                if (area < 0.0) {
                    area = -area;
                }
            } else {
                double lon_step = 0.0;
                double lat_step = 0.0;
                char norm[12];
                mh_status st = mh_normalize_locator(lines.items[i], norm, sizeof(norm), &ctx);
                if (st != MH_OK) {
                    mh_cli_lines_free(&lines);
                    return mh_cli_print_mh_error_line(err, &ctx, i + 1);
                }
                int prec = (int)strlen(norm);
                st = mh_cli_step_size_for_precision(prec, &lon_step, &lat_step, &ctx);
                if (st != MH_OK) {
                    mh_cli_lines_free(&lines);
                    return mh_cli_print_mh_error_line(err, &ctx, i + 1);
                }
                double lat = 0.0;
                double lon = 0.0;
                st = mh_to_center_latlon(norm, &lat, &lon, &ctx);
                if (st != MH_OK) {
                    mh_cli_lines_free(&lines);
                    return mh_cli_print_mh_error_line(err, &ctx, i + 1);
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
                    mh_cli_lines_free(&lines);
                    return mh_cli_print_mh_error_line(err, &ctx, i + 1);
                }
                st = mh_distance_km(&c, &d, MH_DISTANCE_HAVERSINE, &height_km, &ctx);
                if (st != MH_OK) {
                    mh_cli_lines_free(&lines);
                    return mh_cli_print_mh_error_line(err, &ctx, i + 1);
                }
                area = width_km * height_km;
            }
            if (strcmp(format, "json") == 0) {
                if (i > 0) {
                    fputc(',', out);
                }
                fputs("{\"input\":", out);
                mh_cli_print_json_string(out, lines.items[i]);
                fputs(",\"output\":", out);
                fprintf(out, "%.*f", digits, area);
                fputc('}', out);
            } else if (strcmp(format, "csv") == 0) {
                fprintf(out, "%s,%.*f\n", lines.items[i], digits, area);
            } else {
                fprintf(out, "%.*f%s", digits, area, i + 1 < lines.length ? "\n" : "");
            }
        }
        if (strcmp(format, "json") == 0) {
            fputc(']', out);
            fputc('\n', out);
        } else {
            if (strcmp(format, "csv") != 0) {
                fputc('\n', out);
            }
        }
        mh_cli_lines_free(&lines);
        return 0;
    }

    if (strcmp(op, "diagonal") == 0) {
        if (strcmp(format, "json") == 0) {
            mh_cli_json_bulk_start(out, &lines);
        }
        for (size_t i = 0; i < lines.length; i++) {
            mh_bbox bbox;
            mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
            mh_status st = mh_to_bbox(lines.items[i], &bbox, &ctx);
            if (st != MH_OK) {
                mh_cli_lines_free(&lines);
                return mh_cli_print_mh_error_line(err, &ctx, i + 1);
            }
            mh_point a = {bbox.min_lat, bbox.min_lon};
            mh_point b = {bbox.max_lat, bbox.max_lon};
            double dist = 0.0;
            int method_code = strcmp(method, "geodesic") == 0 ? MH_DISTANCE_GEODESIC : MH_DISTANCE_HAVERSINE;
            st = mh_distance_km(&a, &b, method_code, &dist, &ctx);
            if (st != MH_OK) {
                mh_cli_lines_free(&lines);
                return mh_cli_print_mh_error_line(err, &ctx, i + 1);
            }
            if (strcmp(format, "json") == 0) {
                if (i > 0) {
                    fputc(',', out);
                }
                mh_cli_json_obj_start_input_string(out, lines.items[i]);
                fprintf(out, "%.*f", digits, dist);
                mh_cli_json_obj_end(out);
            } else if (strcmp(format, "csv") == 0) {
                fprintf(out, "%s,%.*f\n", lines.items[i], digits, dist);
            } else {
                fprintf(out, "%.*f%s", digits, dist, i + 1 < lines.length ? "\n" : "");
            }
        }
        if (strcmp(format, "json") == 0) {
            mh_cli_json_bulk_end(out);
        }
        if (strcmp(format, "csv") != 0) {
            fputc('\n', out);
        }
        mh_cli_lines_free(&lines);
        return 0;
    }

    if (strcmp(op, "utm") == 0) {
        if (strcmp(format, "json") == 0) {
            mh_cli_json_bulk_start(out, &lines);
        }
        for (size_t i = 0; i < lines.length; i++) {
            char buf[8];
            mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
            mh_status st = mh_to_utm_zone(lines.items[i], buf, sizeof(buf), &ctx);
            if (st != MH_OK) {
                mh_cli_lines_free(&lines);
                return mh_cli_print_mh_error_line(err, &ctx, i + 1);
            }
            if (strcmp(format, "json") == 0) {
                if (i > 0) {
                    fputc(',', out);
                }
                mh_cli_json_obj_start_input_string(out, lines.items[i]);
                mh_cli_print_json_string(out, buf);
                mh_cli_json_obj_end(out);
            } else if (strcmp(format, "csv") == 0) {
                fprintf(out, "%s,%s\n", lines.items[i], buf);
            } else {
                fprintf(out, "%s%s", buf, i + 1 < lines.length ? "\n" : "");
            }
        }
        if (strcmp(format, "json") == 0) {
            mh_cli_json_bulk_end(out);
            fputc('\n', out);
        } else if (strcmp(format, "csv") != 0) {
            fputc('\n', out);
        }
        mh_cli_lines_free(&lines);
        return 0;
    }

    if (strcmp(op, "geojson") == 0) {
        if (strcmp(geojson_format, "featurecollection") == 0) {
            const char **locs = (const char **)malloc(lines.length * sizeof(char *));
            if (!locs) {
                mh_cli_lines_free(&lines);
                mh_cli_print_error(err, "allocation failed");
                return 2;
            }
            for (size_t i = 0; i < lines.length; i++) {
                locs[i] = lines.items[i];
            }
            char *buf = mh_cli_geojson_feature_collection_alloc(locs, lines.length, err);
            free(locs);
            if (!buf) {
                mh_cli_lines_free(&lines);
                return 2;
            }
            if (strcmp(format, "csv") == 0) {
                mh_cli_print_csv_field(out, "featurecollection");
                fputc(',', out);
                mh_cli_print_csv_field(out, buf);
                fputc('\n', out);
            } else {
                fputc('[', out);
                mh_cli_json_obj_start_input_array(out, (const char **)lines.items, lines.length);
                fputs(buf, out);
                mh_cli_json_obj_end(out);
                fputs("]\n", out);
            }
            free(buf);
            mh_cli_lines_free(&lines);
            return 0;
        }
        if (strcmp(geojson_format, "feature") == 0) {
            if (strcmp(format, "csv") != 0) {
                fputc('[', out);
            }
            for (size_t i = 0; i < lines.length; i++) {
                size_t cap = 256;
                for (int attempt = 0; attempt < 4; attempt++) {
                    char *buf = (char *)malloc(cap);
                    if (!buf) {
                        mh_cli_lines_free(&lines);
                        mh_cli_print_error(err, "allocation failed");
                        return 2;
                    }
                    mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
                    mh_status st = mh_to_geojson_feature(lines.items[i], buf, cap, &ctx);
                    if (st == MH_OK) {
                        if (strcmp(format, "csv") == 0) {
                            mh_cli_print_csv_field(out, lines.items[i]);
                            fputc(',', out);
                            mh_cli_print_csv_field(out, buf);
                            fputc('\n', out);
                        } else {
                            if (i > 0) {
                                fputc(',', out);
                            }
                            mh_cli_json_obj_start_input_string(out, lines.items[i]);
                            fputs(buf, out);
                            mh_cli_json_obj_end(out);
                        }
                        free(buf);
                        break;
                    }
                    free(buf);
                    if (ctx.code == MH_ERR_INTERNAL && ctx.message && strcmp(ctx.message, "output buffer too small") == 0) {
                        cap *= 2;
                        continue;
                    }
                    mh_cli_lines_free(&lines);
                    return mh_cli_print_mh_error_line(err, &ctx, i + 1);
                }
            }
            if (strcmp(format, "csv") != 0) {
                fputs("]\n", out);
            }
            mh_cli_lines_free(&lines);
            return 0;
        }
        if (strcmp(geojson_format, "point") == 0) {
            if (strcmp(format, "csv") != 0 && strcmp(format, "json") != 0) {
                fputc('[', out);
            }
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
                if (tokens.length != 2) {
                    mh_cli_tokens_free(&tokens);
                    mh_cli_lines_free(&lines);
                    mh_cli_print_error(err, "Expected lines: lat lon");
                    return 2;
                }
                double lat = 0.0;
                double lon = 0.0;
                if (!mh_cli_parse_double(tokens.items[0], &lat) ||
                    !mh_cli_parse_double(tokens.items[1], &lon)) {
                    mh_cli_tokens_free(&tokens);
                    mh_cli_lines_free(&lines);
                    mh_cli_print_error(err, "Latitude/longitude must be 'lat lon' or 'lat,lon'");
                    return 2;
                }
                int precision_value = precision >= 0 ? precision : mh_cli_precision_from_decimals(
                    mh_cli_decimal_places(tokens.items[0]) > mh_cli_decimal_places(tokens.items[1])
                        ? mh_cli_decimal_places(tokens.items[0])
                        : mh_cli_decimal_places(tokens.items[1])
                );
                mh_grid grid;
                mh_error_context loc_ctx = {MH_OK, NULL, NULL, NULL};
                mh_status loc_st = mh_from_latlon(lat, lon, precision_value, 1, &grid, &loc_ctx);
                if (loc_st != MH_OK) {
                    mh_cli_tokens_free(&tokens);
                    mh_cli_lines_free(&lines);
                    return mh_cli_print_mh_error_line(err, &loc_ctx, i + 1);
                }
                size_t cap = 256;
                for (int attempt = 0; attempt < 4; attempt++) {
                    char *buf = (char *)malloc(cap);
                    if (!buf) {
                        mh_cli_tokens_free(&tokens);
                        mh_cli_lines_free(&lines);
                        mh_cli_print_error(err, "allocation failed");
                        return 2;
                    }
                    mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
                    mh_status st = mh_to_geojson_point(lat, lon, buf, cap, &ctx);
                    if (st == MH_OK) {
                        if (strcmp(format, "json") == 0) {
                            if (i > 0) {
                                fputc(',', out);
                            }
                            mh_cli_json_obj_start_input_string(out, grid.locator);
                            fputs(buf, out);
                            mh_cli_json_obj_end(out);
                        } else if (strcmp(format, "csv") == 0) {
                            mh_cli_print_csv_field(out, tokens.items[0]);
                            fputc(',', out);
                            mh_cli_print_csv_field(out, tokens.items[1]);
                            fputc(',', out);
                            mh_cli_print_csv_field(out, grid.locator);
                            fputc(',', out);
                            mh_cli_print_csv_field(out, buf);
                            fputc('\n', out);
                        } else {
                            if (i > 0) {
                                fputc(',', out);
                            }
                            fputs(buf, out);
                        }
                        free(buf);
                        break;
                    }
                    free(buf);
                    if (ctx.code == MH_ERR_INTERNAL && ctx.message &&
                        strcmp(ctx.message, "output buffer too small") == 0) {
                        cap *= 2;
                        continue;
                    }
                    mh_cli_tokens_free(&tokens);
                    mh_cli_lines_free(&lines);
                    return mh_cli_print_mh_error_line(err, &ctx, i + 1);
                }
                mh_cli_tokens_free(&tokens);
            }
            if (strcmp(format, "json") == 0) {
                mh_cli_json_bulk_end(out);
                fputc('\n', out);
            } else if (strcmp(format, "csv") != 0) {
                fputs("]\n", out);
            }
            mh_cli_lines_free(&lines);
            return 0;
        }
        if (strcmp(geojson_format, "bbox") == 0) {
            fputc('[', out);
            for (size_t i = 0; i < lines.length; i++) {
                if (i > 0) {
                    fputc(',', out);
                }
                mh_cli_json_obj_start_input_string(out, lines.items[i]);
                if (split) {
                    mh_bbox bbox;
                    mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
                    mh_status st = mh_to_bbox(lines.items[i], &bbox, &ctx);
                    if (st != MH_OK) {
                        mh_cli_lines_free(&lines);
                        return mh_cli_print_mh_error_line(err, &ctx, i + 1);
                    }
                    mh_bbox parts[2];
                    size_t parts_len = 2;
                    st = mh_split_bbox_list(bbox, parts, &parts_len, &ctx);
                    if (st != MH_OK) {
                        mh_cli_lines_free(&lines);
                        return mh_cli_print_mh_error_line(err, &ctx, i + 1);
                    }
                    if (parts_len == 0) {
                        parts[0] = bbox;
                        parts_len = 1;
                    }
                    fputc('[', out);
                    for (size_t p = 0; p < parts_len; p++) {
                        if (p > 0) {
                            fputc(',', out);
                        }
                        mh_cli_print_bbox_json(out, &parts[p]);
                    }
                    fputc(']', out);
                } else {
                    double out_bbox[4];
                    mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
                    mh_status st = mh_to_geojson_bbox(lines.items[i], out_bbox, &ctx);
                    if (st != MH_OK) {
                        mh_cli_lines_free(&lines);
                        return mh_cli_print_mh_error_line(err, &ctx, i + 1);
                    }
                    mh_cli_print_geojson_bbox(out, out_bbox);
                }
                mh_cli_json_obj_end(out);
            }
            fputs("]\n", out);
            mh_cli_lines_free(&lines);
            return 0;
        }
        if (strcmp(geojson_format, "envelope") == 0) {
            if (split) {
                fputc('[', out);
                for (size_t i = 0; i < lines.length; i++) {
                    if (i > 0) {
                        fputc(',', out);
                    }
                    mh_bbox bbox;
                    mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
                    mh_status st = mh_to_bbox(lines.items[i], &bbox, &ctx);
                    if (st != MH_OK) {
                        mh_cli_lines_free(&lines);
                        return mh_cli_print_mh_error_line(err, &ctx, i + 1);
                    }
                    mh_bbox parts[2];
                    size_t parts_len = 2;
                    st = mh_split_bbox_list(bbox, parts, &parts_len, &ctx);
                    if (st != MH_OK) {
                        mh_cli_lines_free(&lines);
                        return mh_cli_print_mh_error_line(err, &ctx, i + 1);
                    }
                    if (parts_len == 0) {
                        parts[0] = bbox;
                        parts_len = 1;
                    }
                    mh_cli_json_obj_start_input_string(out, lines.items[i]);
                    fputs("{\"type\":\"FeatureCollection\",\"features\":[", out);
                    for (size_t p = 0; p < parts_len; p++) {
                        if (p > 0) {
                            fputc(',', out);
                        }
                        fputs("{\"type\":\"Feature\",\"geometry\":", out);
                        mh_cli_print_geojson_polygon(out, parts[p]);
                        fputs(",\"properties\":{}}", out);
                    }
                    fputs("]}", out);
                    mh_cli_json_obj_end(out);
                }
                fputs("]\n", out);
                mh_cli_lines_free(&lines);
                return 0;
            }
            fputc('[', out);
            for (size_t i = 0; i < lines.length; i++) {
                size_t cap = 256;
                for (int attempt = 0; attempt < 4; attempt++) {
                    char *buf = (char *)malloc(cap);
                    if (!buf) {
                        mh_cli_lines_free(&lines);
                        mh_cli_print_error(err, "allocation failed");
                        return 2;
                    }
                    mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
                    mh_status st = mh_to_geojson_envelope(lines.items[i], buf, cap, &ctx);
                    if (st == MH_OK) {
                        if (i > 0) {
                            fputc(',', out);
                        }
                        mh_cli_json_obj_start_input_string(out, lines.items[i]);
                        fputs(buf, out);
                        mh_cli_json_obj_end(out);
                        free(buf);
                        break;
                    }
                    free(buf);
                    if (ctx.code == MH_ERR_INTERNAL && ctx.message && strcmp(ctx.message, "output buffer too small") == 0) {
                        cap *= 2;
                        continue;
                    }
                    mh_cli_lines_free(&lines);
                    return mh_cli_print_mh_error_line(err, &ctx, i + 1);
                }
            }
            fputs("]\n", out);
            mh_cli_lines_free(&lines);
            return 0;
        }
        mh_cli_lines_free(&lines);
        mh_cli_print_error(err, "invalid geojson format");
        return 2;
    }

    if (strcmp(op, "bbox-split") == 0) {
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
                return mh_cli_print_mh_error_line(err, &ctx, i + 1);
            }
            if (parts_len == 0) {
                parts[0] = bbox;
                parts_len = 1;
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
                    fprintf(out, "%s%s", buf, p + 1 < parts_len ? ";" : "");
                }
                fprintf(out, "%s", i + 1 < lines.length ? "\n" : "");
            }
            mh_cli_tokens_free(&tokens);
        }
        if (strcmp(format, "json") == 0) {
            mh_cli_json_bulk_end(out);
        }
        fputc('\n', out);
        mh_cli_lines_free(&lines);
        return 0;
    }

    if (strcmp(op, "bbox-split-list") == 0) {
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
                return mh_cli_print_mh_error_line(err, &ctx, i + 1);
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
                    fprintf(out, "%s%s", buf, p + 1 < parts_len ? ";" : "");
                }
                fprintf(out, "%s", i + 1 < lines.length ? "\n" : "");
            }
            mh_cli_tokens_free(&tokens);
        }
        if (strcmp(format, "json") == 0) {
            mh_cli_json_bulk_end(out);
        }
        fputc('\n', out);
        mh_cli_lines_free(&lines);
        return 0;
    }

    mh_cli_lines_free(&lines);
    mh_cli_print_error(err, "unknown bulk operation");
    return 2;
}
