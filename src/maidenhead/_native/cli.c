#include "cli.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core.h"
#include "coverage.h"
#include "errors.h"
#include "geo.h"
#include "geojson.h"
#include "types.h"

#define MH_CLI_DEFAULT_DIGITS (6)

typedef struct {
    char **items;
    size_t length;
    size_t capacity;
} mh_cli_lines;

typedef struct {
    char **items;
    size_t length;
} mh_cli_tokens;

static char *mh_cli_strdup(const char *s) {
    if (!s) {
        return NULL;
    }
    size_t len = strlen(s);
    char *out = (char *)malloc(len + 1);
    if (!out) {
        return NULL;
    }
    memcpy(out, s, len + 1);
    return out;
}

static char *mh_cli_strndup(const char *s, size_t n) {
    char *out = (char *)malloc(n + 1);
    if (!out) {
        return NULL;
    }
    memcpy(out, s, n);
    out[n] = '\0';
    return out;
}

static void mh_cli_lines_init(mh_cli_lines *lines) {
    lines->items = NULL;
    lines->length = 0;
    lines->capacity = 0;
}

static void mh_cli_lines_free(mh_cli_lines *lines) {
    if (!lines) {
        return;
    }
    for (size_t i = 0; i < lines->length; i++) {
        free(lines->items[i]);
    }
    free(lines->items);
    lines->items = NULL;
    lines->length = 0;
    lines->capacity = 0;
}

static int mh_cli_lines_push(mh_cli_lines *lines, const char *value) {
    if (lines->length == lines->capacity) {
        size_t next = lines->capacity == 0 ? 16 : lines->capacity * 2;
        char **items = (char **)realloc(lines->items, next * sizeof(char *));
        if (!items) {
            return 0;
        }
        lines->items = items;
        lines->capacity = next;
    }
    lines->items[lines->length] = mh_cli_strdup(value);
    if (!lines->items[lines->length]) {
        return 0;
    }
    lines->length++;
    return 1;
}

static void mh_cli_tokens_free(mh_cli_tokens *tokens) {
    if (!tokens) {
        return;
    }
    for (size_t i = 0; i < tokens->length; i++) {
        free(tokens->items[i]);
    }
    free(tokens->items);
    tokens->items = NULL;
    tokens->length = 0;
}

static char *mh_cli_trim(char *s) {
    while (*s && isspace((unsigned char)*s)) {
        s++;
    }
    if (!*s) {
        return s;
    }
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) {
        *end = '\0';
        end--;
    }
    return s;
}

static int mh_cli_split_line(const char *line, mh_cli_tokens *out) {
    out->items = NULL;
    out->length = 0;
    if (!line) {
        return 0;
    }
    int has_space = 0;
    for (const char *p = line; *p; p++) {
        if (isspace((unsigned char)*p)) {
            has_space = 1;
            break;
        }
    }
    char *copy = mh_cli_strdup(line);
    if (!copy) {
        return 0;
    }
    const char *delim = has_space ? " \t\r\n" : ",";
    char *save = NULL;
    char *tok = strtok_r(copy, delim, &save);
    size_t cap = 0;
    while (tok) {
        char *clean = mh_cli_trim(tok);
        if (*clean) {
            if (out->length == cap) {
                size_t next = cap == 0 ? 8 : cap * 2;
                char **items = (char **)realloc(out->items, next * sizeof(char *));
                if (!items) {
                    free(copy);
                    mh_cli_tokens_free(out);
                    return 0;
                }
                out->items = items;
                cap = next;
            }
            out->items[out->length] = mh_cli_strdup(clean);
            if (!out->items[out->length]) {
                free(copy);
                mh_cli_tokens_free(out);
                return 0;
            }
            out->length++;
        }
        tok = strtok_r(NULL, delim, &save);
    }
    free(copy);
    return 1;
}

static int mh_cli_read_lines(const char *file_path, int use_stdin, mh_cli_lines *out, FILE *err) {
    if (file_path && use_stdin) {
        fprintf(err, "error: Use only one of --file or --stdin\n");
        return 2;
    }
    FILE *handle = NULL;
    if (file_path) {
        handle = fopen(file_path, "r");
        if (!handle) {
            fprintf(err, "error: failed to open file: %s\n", file_path);
            return 2;
        }
    } else if (use_stdin) {
        handle = stdin;
    } else {
        return 0;
    }

    char *line = NULL;
    size_t cap = 0;
    ssize_t read = 0;
    while ((read = getline(&line, &cap, handle)) != -1) {
        if (read <= 0) {
            continue;
        }
        while (read > 0 && (line[read - 1] == '\n' || line[read - 1] == '\r')) {
            line[read - 1] = '\0';
            read--;
        }
        char *trimmed = mh_cli_trim(line);
        if (*trimmed) {
            if (!mh_cli_lines_push(out, trimmed)) {
                free(line);
                if (handle != stdin) {
                    fclose(handle);
                }
                fprintf(err, "error: allocation failed\n");
                return 2;
            }
        }
    }
    free(line);
    if (handle != stdin) {
        fclose(handle);
    }
    return 0;
}

static int mh_cli_parse_double(const char *s, double *out) {
    if (!s) {
        return 0;
    }
    while (isspace((unsigned char)*s)) {
        s++;
    }
    if (!*s) {
        return 0;
    }
    errno = 0;
    char *end = NULL;
    double val = strtod(s, &end);
    if (errno != 0 || end == s) {
        return 0;
    }
    while (end && isspace((unsigned char)*end)) {
        end++;
    }
    if (end && *end) {
        return 0;
    }
    *out = val;
    return 1;
}

static int mh_cli_step_size_for_precision(int precision, double *lon_step, double *lat_step, mh_error_context *err) {
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

static int mh_cli_parse_latlon_joined(const char *joined, double *lat, double *lon) {
    const char *comma = strchr(joined, ',');
    if (!comma) {
        return 0;
    }
    char *left = mh_cli_strndup(joined, (size_t)(comma - joined));
    char *right = mh_cli_strdup(comma + 1);
    if (!left || !right) {
        free(left);
        free(right);
        return 0;
    }
    char *ltrim = mh_cli_trim(left);
    char *rtrim = mh_cli_trim(right);
    if (!*ltrim || !*rtrim) {
        free(left);
        free(right);
        return 0;
    }
    if (!mh_cli_parse_double(ltrim, lat) || !mh_cli_parse_double(rtrim, lon)) {
        free(left);
        free(right);
        return 0;
    }
    free(left);
    free(right);
    return 1;
}

static int mh_cli_split_latlon_parts(const char **parts, size_t n, double *lat, double *lon) {
    if (n == 0) {
        return 0;
    }
    size_t total = 0;
    for (size_t i = 0; i < n; i++) {
        total += strlen(parts[i]) + 1;
    }
    char *joined = (char *)malloc(total + 1);
    if (!joined) {
        return 0;
    }
    joined[0] = '\0';
    for (size_t i = 0; i < n; i++) {
        strcat(joined, parts[i]);
        if (i + 1 < n) {
            strcat(joined, " ");
        }
    }
    int ok = 0;
    if (strchr(joined, ',')) {
        ok = mh_cli_parse_latlon_joined(joined, lat, lon);
        free(joined);
        return ok;
    }
    if (n == 2) {
        ok = mh_cli_parse_double(parts[0], lat) && mh_cli_parse_double(parts[1], lon);
        free(joined);
        return ok;
    }
    free(joined);
    return 0;
}

static int mh_cli_parse_point_parts(
    const char **parts,
    size_t n,
    mh_point *out,
    char *locator_buf,
    size_t locator_len,
    int *is_locator,
    mh_error_context *err
) {
    double lat = 0.0;
    double lon = 0.0;
    if (mh_cli_split_latlon_parts(parts, n, &lat, &lon)) {
        out->lat = lat;
        out->lon = lon;
        if (is_locator) {
            *is_locator = 0;
        }
        return 1;
    }
    if (n == 1) {
        mh_status st = mh_normalize_locator(parts[0], locator_buf, locator_len, err);
        if (st != MH_OK) {
            return 0;
        }
        st = mh_to_center_latlon(locator_buf, &out->lat, &out->lon, err);
        if (st != MH_OK) {
            return 0;
        }
        if (is_locator) {
            *is_locator = 1;
        }
        return 1;
    }
    return 0;
}

static int mh_cli_split_two_points(
    char **args,
    size_t n,
    const char ***a_parts,
    size_t *a_len,
    const char ***b_parts,
    size_t *b_len
) {
    if (n == 2) {
        *a_parts = (const char **)&args[0];
        *a_len = 1;
        *b_parts = (const char **)&args[1];
        *b_len = 1;
        return 1;
    }
    if (n == 3) {
        double lat = 0.0;
        double lon = 0.0;
        const char *first_two[2] = {args[0], args[1]};
        if (mh_cli_split_latlon_parts(first_two, 2, &lat, &lon)) {
            *a_parts = (const char **)args;
            *a_len = 2;
            *b_parts = (const char **)&args[2];
            *b_len = 1;
            return 1;
        }
        const char *last_two[2] = {args[1], args[2]};
        if (mh_cli_split_latlon_parts(last_two, 2, &lat, &lon)) {
            *a_parts = (const char **)&args[0];
            *a_len = 1;
            *b_parts = (const char **)&args[1];
            *b_len = 2;
            return 1;
        }
        return 0;
    }
    if (n == 4) {
        *a_parts = (const char **)&args[0];
        *a_len = 2;
        *b_parts = (const char **)&args[2];
        *b_len = 2;
        return 1;
    }
    return 0;
}

static void mh_cli_print_json_string(FILE *out, const char *s) {
    fputc('"', out);
    for (const char *p = s; *p; p++) {
        if (*p == '"' || *p == '\\') {
            fputc('\\', out);
            fputc(*p, out);
        } else if (*p == '\n') {
            fputs("\\n", out);
        } else if (*p == '\r') {
            fputs("\\r", out);
        } else if (*p == '\t') {
            fputs("\\t", out);
        } else {
            fputc(*p, out);
        }
    }
    fputc('"', out);
}

static void mh_cli_print_error(FILE *err, const char *message);
static int mh_cli_print_mh_error(FILE *err, const mh_error_context *ctx);

static int mh_cli_geojson_with_buffer(
    const char *locator,
    mh_status (*fn)(const char *, char *, size_t, mh_error_context *),
    FILE *out,
    FILE *err
) {
    size_t cap = 256;
    for (int attempt = 0; attempt < 4; attempt++) {
        char *buf = (char *)malloc(cap);
        if (!buf) {
            mh_cli_print_error(err, "allocation failed");
            return 2;
        }
        mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
        mh_status st = fn(locator, buf, cap, &ctx);
        if (st == MH_OK) {
            fputs(buf, out);
            fputc('\n', out);
            free(buf);
            return 0;
        }
        free(buf);
        if (ctx.code == MH_ERR_INTERNAL && ctx.message && strcmp(ctx.message, "output buffer too small") == 0) {
            cap *= 2;
            continue;
        }
        return mh_cli_print_mh_error(err, &ctx);
    }
    mh_cli_print_error(err, "output buffer too small");
    return 2;
}

static int mh_cli_geojson_feature_collection(
    const char *const *locators,
    size_t count,
    FILE *out,
    FILE *err
) {
    size_t cap = 512 + count * 512;
    for (int attempt = 0; attempt < 4; attempt++) {
        char *buf = (char *)malloc(cap);
        if (!buf) {
            mh_cli_print_error(err, "allocation failed");
            return 2;
        }
        mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
        mh_status st = mh_to_geojson_feature_collection(locators, count, buf, cap, &ctx);
        if (st == MH_OK) {
            fputs(buf, out);
            fputc('\n', out);
            free(buf);
            return 0;
        }
        free(buf);
        if (ctx.code == MH_ERR_INTERNAL && ctx.message && strcmp(ctx.message, "output buffer too small") == 0) {
            cap *= 2;
            continue;
        }
        return mh_cli_print_mh_error(err, &ctx);
    }
    mh_cli_print_error(err, "output buffer too small");
    return 2;
}

static void mh_cli_print_geojson_polygon(FILE *out, mh_bbox bbox) {
    fprintf(
        out,
        "{\"type\":\"Polygon\",\"coordinates\":[[[%.17g,%.17g],[%.17g,%.17g],[%.17g,%.17g],[%.17g,%.17g],[%.17g,%.17g]]]}",
        bbox.min_lon, bbox.min_lat,
        bbox.max_lon, bbox.min_lat,
        bbox.max_lon, bbox.max_lat,
        bbox.min_lon, bbox.max_lat,
        bbox.min_lon, bbox.min_lat
    );
}

static void mh_cli_print_geojson_bbox(FILE *out, const double bbox[4]) {
    fprintf(out, "[%.17g,%.17g,%.17g,%.17g]", bbox[0], bbox[1], bbox[2], bbox[3]);
}

static void mh_cli_print_bbox_json(FILE *out, const mh_bbox *bbox) {
    fprintf(
        out,
        "[%.17g,%.17g,%.17g,%.17g]",
        bbox->min_lat,
        bbox->min_lon,
        bbox->max_lat,
        bbox->max_lon
    );
}

static void mh_cli_print_json_float(FILE *out, double value) {
    fprintf(out, "%.17g", value);
}

static void mh_cli_json_bulk_start(FILE *out, const mh_cli_lines *lines) {
    fputs("{\"input\":[", out);
    for (size_t i = 0; i < lines->length; i++) {
        if (i > 0) {
            fputc(',', out);
        }
        mh_cli_print_json_string(out, lines->items[i]);
    }
    fputs("],\"output\":{\"items\":[", out);
}

static void mh_cli_json_bulk_end(FILE *out) {
    fputs("]}}\n", out);
}

static void mh_cli_json_single_start_string(FILE *out, const char *input) {
    fputs("{\"input\":", out);
    mh_cli_print_json_string(out, input);
    fputs(",\"output\":{", out);
}

static void mh_cli_json_single_start_strings(FILE *out, const char **items, size_t count) {
    fputs("{\"input\":[", out);
    for (size_t i = 0; i < count; i++) {
        if (i > 0) {
            fputc(',', out);
        }
        mh_cli_print_json_string(out, items[i]);
    }
    fputs("],\"output\":{", out);
}

static void mh_cli_json_single_end(FILE *out) {
    fputs("}}\n", out);
}

static void mh_cli_print_error(FILE *err, const char *message) {
    fprintf(err, "error: %s\n", message);
}

static int mh_cli_print_mh_error(FILE *err, const mh_error_context *ctx) {
    if (ctx && ctx->message) {
        mh_cli_print_error(err, ctx->message);
    } else {
        mh_cli_print_error(err, "operation failed");
    }
    return 2;
}

static int mh_cli_print_mh_error_line(FILE *err, const mh_error_context *ctx, size_t line_no) {
    if (ctx && ctx->message) {
        fprintf(err, "error: %s at line %zu\n", ctx->message, line_no);
    } else {
        fprintf(err, "error: operation failed at line %zu\n", line_no);
    }
    return 2;
}

static int mh_cli_require_arg(const char *value, const char *message, FILE *err) {
    if (!value || !*value) {
        mh_cli_print_error(err, message);
        return 0;
    }
    return 1;
}

static void mh_cli_format_bbox(char *buf, size_t buf_len, const mh_bbox *bbox, int digits, const char *sep) {
    snprintf(
        buf,
        buf_len,
        "%.*f%s%.*f%s%.*f%s%.*f",
        digits,
        bbox->min_lat,
        sep,
        digits,
        bbox->min_lon,
        sep,
        digits,
        bbox->max_lat,
        sep,
        digits,
        bbox->max_lon
    );
}

static void mh_cli_print_wkt_polygon(FILE *out, const mh_bbox *bbox) {
    fprintf(
        out,
        "POLYGON((%.17g %.17g, %.17g %.17g, %.17g %.17g, %.17g %.17g, %.17g %.17g))",
        bbox->min_lon, bbox->min_lat,
        bbox->max_lon, bbox->min_lat,
        bbox->max_lon, bbox->max_lat,
        bbox->min_lon, bbox->max_lat,
        bbox->min_lon, bbox->min_lat
    );
}

static void mh_cli_format_latlon(char *buf, size_t buf_len, double lat, double lon, int digits, const char *sep) {
    snprintf(buf, buf_len, "%.*f%s%.*f", digits, lat, sep, digits, lon);
}

static int mh_cli_handle_normalize(int argc, char **argv, FILE *out, FILE *err) {
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
            mh_cli_json_bulk_start(out, &lines);
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
        fputs("\"value\":", out);
        mh_cli_print_json_string(out, norm);
        mh_cli_json_single_end(out);
    } else {
        fprintf(out, "%s\n", norm);
    }
    mh_cli_lines_free(&lines);
    return 0;
}

static int mh_cli_handle_validate(int argc, char **argv, FILE *out, FILE *err) {
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

static int mh_cli_handle_center(int argc, char **argv, FILE *out, FILE *err) {
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
                fprintf(out, "%.*f%s%.*f%s", digits, lat, sep, digits, lon, i + 1 < lines.length ? "\n" : "");
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
        fputs("\"value\":[", out);
        fprintf(out, "%.*f,%.*f", digits, lat, digits, lon);
        fputs("]", out);
        mh_cli_json_single_end(out);
    } else {
        fprintf(out, "%.*f%s%.*f\n", digits, lat, sep, digits, lon);
    }
    mh_cli_lines_free(&lines);
    return 0;
}

static int mh_cli_handle_bbox(int argc, char **argv, FILE *out, FILE *err) {
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

    const char *sep = csv ? "," : " ";
    if (lines.length > 0) {
        if (strcmp(format, "json") == 0) {
            mh_cli_json_bulk_start(out, &lines);
        }
        int first = 1;
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

            for (size_t p = 0; p < parts_len; p++) {
                if (strcmp(format, "json") == 0) {
                    if (!first) {
                        fputc(',', out);
                    }
                    first = 0;
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
                } else {
                    char buf[128];
                    mh_cli_format_bbox(buf, sizeof(buf), &parts[p], digits, sep);
                    fprintf(out, "%s\n", buf);
                }
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

    mh_bbox bbox;
    mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
    mh_status st = mh_to_bbox(locator, &bbox, &ctx);
    if (st != MH_OK) {
        mh_cli_lines_free(&lines);
        return mh_cli_print_mh_error(err, &ctx);
    }

    if (strcmp(format, "json") == 0) {
        mh_cli_json_single_start_string(out, locator);
        fputs("\"value\":", out);
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
            for (size_t p = 0; p < parts_len; p++) {
                char buf[128];
                mh_cli_format_bbox(buf, sizeof(buf), &parts[p], digits, sep);
                fprintf(out, "%s%s", buf, p + 1 < parts_len ? "\n" : "");
            }
            fprintf(out, "\n");
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
            char buf[128];
            mh_cli_format_bbox(buf, sizeof(buf), &bbox, digits, sep);
            fprintf(out, "%s\n", buf);
        }
    }

    if (strcmp(format, "json") == 0) {
        mh_cli_json_single_end(out);
    }

    mh_cli_lines_free(&lines);
    return 0;
}

static int mh_cli_handle_parts(int argc, char **argv, FILE *out, FILE *err) {
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

static int mh_cli_handle_format(int argc, char **argv, FILE *out, FILE *err) {
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

static int mh_cli_handle_geojson(int argc, char **argv, FILE *out, FILE *err) {
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

static int mh_cli_handle_wkt(int argc, char **argv, FILE *out, FILE *err) {
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
        fputs("\"value\":", out);
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

static int mh_cli_handle_bbox_split(int argc, char **argv, FILE *out, FILE *err) {
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
    for (size_t i = 0; i < parts_len; i++) {
        char buf[128];
        mh_cli_format_bbox(buf, sizeof(buf), &parts[i], digits, sep);
        fprintf(out, "%s%s", buf, i + 1 < parts_len ? "\n" : "");
    }
    fprintf(out, "\n");
    return 0;
}

static int mh_cli_handle_bbox_split_list(int argc, char **argv, FILE *out, FILE *err) {
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
                fputc('[', out);
                for (size_t p = 0; p < parts_len; p++) {
                    if (p > 0) {
                        fputc(',', out);
                    }
                    mh_cli_print_bbox_json(out, &parts[p]);
                }
                fputc(']', out);
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
        } else {
            fputc('\n', out);
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
        char input0[64];
        char input1[64];
        char input2[64];
        char input3[64];
        snprintf(input0, sizeof(input0), "%.17g", min_lat);
        snprintf(input1, sizeof(input1), "%.17g", min_lon);
        snprintf(input2, sizeof(input2), "%.17g", max_lat);
        snprintf(input3, sizeof(input3), "%.17g", max_lon);
        const char *input_items[] = {input0, input1, input2, input3};
        mh_cli_json_single_start_strings(out, input_items, 4);
        fputs("\"value\":", out);
        fputc('[', out);
        for (size_t p = 0; p < parts_len; p++) {
            if (p > 0) {
                fputc(',', out);
            }
            mh_cli_print_bbox_json(out, &parts[p]);
        }
        fputc(']', out);
        mh_cli_json_single_end(out);
        mh_cli_lines_free(&lines);
        return 0;
    }

    const char *sep = csv ? "," : " ";
    for (size_t p = 0; p < parts_len; p++) {
        char buf[128];
        mh_cli_format_bbox(buf, sizeof(buf), &parts[p], digits, sep);
        fprintf(out, "%s%s", buf, p + 1 < parts_len ? "\n" : "");
    }
    fprintf(out, "\n");
    mh_cli_lines_free(&lines);
    return 0;
}

typedef struct {
    mh_cli_lines *lines;
    size_t limit;
} mh_cli_children_ctx;

static int mh_cli_children_callback(const char *locator, void *userdata) {
    mh_cli_children_ctx *ctx = (mh_cli_children_ctx *)userdata;
    if (!ctx || !ctx->lines) {
        return 0;
    }
    if (ctx->limit > 0 && ctx->lines->length >= ctx->limit) {
        return 0;
    }
    if (!mh_cli_lines_push(ctx->lines, locator)) {
        return 1;
    }
    return 0;
}

static int mh_cli_handle_bulk(int argc, char **argv, FILE *out, FILE *err) {
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
                mh_cli_print_json_string(out, norm);
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
                mh_cli_print_json_string(out, grid.locator);
            } else if (strcmp(format, "csv") == 0) {
                fprintf(out, "%s%s", grid.locator, i + 1 < lines.length ? "," : "");
            } else {
                fprintf(out, "%s%s", grid.locator, i + 1 < lines.length ? "\n" : "");
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
                fputc('[', out);
                mh_cli_print_json_float(out, lat);
                fputc(',', out);
                mh_cli_print_json_float(out, lon);
                fputc(']', out);
            } else {
                const char *sep = strcmp(format, "csv") == 0 ? "," : " ";
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
                mh_cli_print_bbox_json(out, &bbox);
            } else {
                const char *sep = strcmp(format, "csv") == 0 ? "," : " ";
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
            if (strchr(lines.items[i], ',')) {
                double lat = 0.0;
                double lon = 0.0;
                if (!mh_cli_parse_latlon_joined(lines.items[i], &lat, &lon)) {
                    mh_cli_lines_free(&lines);
                    mh_cli_print_error(err, "Latitude/longitude must be 'lat lon' or 'lat,lon'");
                    return 2;
                }
                mh_grid grid;
                st = mh_from_latlon(lat, lon, out_precision, 1, &grid, &ctx);
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
                mh_cli_print_json_string(out, buf);
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
                fputs(hit ? "true" : "false", out);
            } else if (strcmp(format, "csv") == 0) {
                fprintf(out, "%s%s", hit ? "true" : "false", i + 1 < lines.length ? "," : "");
            } else {
                fprintf(out, "%s%s", hit ? "true" : "false", i + 1 < lines.length ? "\n" : "");
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
                fputs(hit ? "true" : "false", out);
            } else if (strcmp(format, "csv") == 0) {
                fprintf(out, "%s%s", hit ? "true" : "false", i + 1 < lines.length ? "," : "");
            } else {
                fprintf(out, "%s%s", hit ? "true" : "false", i + 1 < lines.length ? "\n" : "");
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
                fputs(hit ? "true" : "false", out);
            } else if (strcmp(format, "csv") == 0) {
                fprintf(out, "%s%s", hit ? "true" : "false", i + 1 < lines.length ? "," : "");
            } else {
                fprintf(out, "%s%s", hit ? "true" : "false", i + 1 < lines.length ? "\n" : "");
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
                fputs(hit ? "true" : "false", out);
            } else if (strcmp(format, "csv") == 0) {
                fprintf(out, "%s%s", hit ? "true" : "false", i + 1 < lines.length ? "," : "");
            } else {
                fprintf(out, "%s%s", hit ? "true" : "false", i + 1 < lines.length ? "\n" : "");
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
                    fputc('[', out);
                    fprintf(out, "%.*f", digits, bearing);
                    fputc(',', out);
                    fprintf(out, "%.*f", digits, min_km);
                    fputc(',', out);
                    fprintf(out, "%.*f", digits, max_km);
                    fputc(']', out);
                } else {
                    const char *sep = strcmp(format, "csv") == 0 ? "," : " ";
                    fprintf(out, "%.*f%s%.*f%s%.*f%s", digits, bearing, sep, digits, min_km, sep, digits, max_km, i + 1 < lines.length ? "\n" : "");
                }
            } else {
                if (strcmp(format, "json") == 0) {
                    if (i > 0) {
                        fputc(',', out);
                    }
                    fputc('[', out);
                    fprintf(out, "%.*f", digits, bearing);
                    fputc(',', out);
                    fprintf(out, "%.*f", digits, dist);
                    fputc(']', out);
                } else {
                    const char *sep = strcmp(format, "csv") == 0 ? "," : " ";
                    fprintf(out, "%.*f%s%.*f%s", digits, bearing, sep, digits, dist, i + 1 < lines.length ? "\n" : "");
                }
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
                fprintf(out, "%.*f", digits, bearing);
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
        fputc('\n', out);
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
                fputc('[', out);
                for (size_t j = 0; j < list.length; j++) {
                    if (j > 0) {
                        fputc(',', out);
                    }
                    mh_cli_print_json_string(out, list.items[j]);
                }
                fputc(']', out);
            } else if (strcmp(format, "csv") == 0) {
                for (size_t j = 0; j < list.length; j++) {
                    fprintf(out, "%s%s", list.items[j], j + 1 < list.length ? "," : "");
                }
                fprintf(out, "%s", i + 1 < lines.length ? "\n" : "");
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
        fputc('\n', out);
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
            } else if (strcmp(format, "csv") == 0) {
                for (size_t j = 0; j < list.length; j++) {
                    fprintf(out, "%s:%s%s", list.keys[j], list.values[j], j + 1 < list.length ? "," : "");
                }
                fprintf(out, "%s", i + 1 < lines.length ? "\n" : "");
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
        fputc('\n', out);
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
            } else {
                const char *point_sep = strcmp(format, "csv") == 0 ? "," : " ";
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
        fputc('\n', out);
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
                fprintf(out, "%d", p);
            } else if (strcmp(format, "csv") == 0) {
                fprintf(out, "%d%s", p, i + 1 < lines.length ? "," : "");
            } else {
                fprintf(out, "%d%s", p, i + 1 < lines.length ? "\n" : "");
            }
        }
        if (strcmp(format, "json") == 0) {
            mh_cli_json_bulk_end(out);
        }
        fputc('\n', out);
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
                mh_cli_print_json_string(out, grid.locator);
            } else if (strcmp(format, "csv") == 0) {
                fprintf(out, "%s%s", grid.locator, i + 1 < lines.length ? "," : "");
            } else {
                fprintf(out, "%s%s", grid.locator, i + 1 < lines.length ? "\n" : "");
            }
        }
        if (strcmp(format, "json") == 0) {
            mh_cli_json_bulk_end(out);
        }
        fputc('\n', out);
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
                fputc('[', out);
                for (size_t j = 0; j < children.length; j++) {
                    if (j > 0) {
                        fputc(',', out);
                    }
                    mh_cli_print_json_string(out, children.items[j]);
                }
                fputc(']', out);
            } else if (strcmp(format, "csv") == 0) {
                for (size_t j = 0; j < children.length; j++) {
                    fprintf(out, "%s%s", children.items[j], j + 1 < children.length ? "," : "");
                }
                fprintf(out, "%s", i + 1 < lines.length ? "\n" : "");
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
        fputc('\n', out);
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
                fputc('[', out);
                fprintf(out, "%.*f", digits, width);
                fputc(',', out);
                fprintf(out, "%.*f", digits, height);
                fputc(']', out);
            } else {
                const char *sep = strcmp(format, "csv") == 0 ? "," : " ";
                fprintf(out, "%.*f%s%.*f%s", digits, width, sep, digits, height, i + 1 < lines.length ? "\n" : "");
            }
        }
        if (strcmp(format, "json") == 0) {
            mh_cli_json_bulk_end(out);
        }
        fputc('\n', out);
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
                fprintf(out, "%.*f%s", digits, area, i + 1 < lines.length ? "," : "");
            } else {
                fprintf(out, "%.*f%s", digits, area, i + 1 < lines.length ? "\n" : "");
            }
        }
        if (strcmp(format, "json") == 0) {
            fputc(']', out);
            fputc('\n', out);
        } else {
            fputc('\n', out);
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
                fprintf(out, "%.*f", digits, dist);
            } else if (strcmp(format, "csv") == 0) {
                fprintf(out, "%.*f%s", digits, dist, i + 1 < lines.length ? "," : "");
            } else {
                fprintf(out, "%.*f%s", digits, dist, i + 1 < lines.length ? "\n" : "");
            }
        }
        if (strcmp(format, "json") == 0) {
            mh_cli_json_bulk_end(out);
        }
        fputc('\n', out);
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
                mh_cli_print_json_string(out, buf);
            } else if (strcmp(format, "csv") == 0) {
                fprintf(out, "%s%s", buf, i + 1 < lines.length ? "," : "");
            } else {
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
            int rc = mh_cli_geojson_feature_collection(locs, lines.length, out, err);
            free(locs);
            mh_cli_lines_free(&lines);
            return rc;
        }
        if (strcmp(geojson_format, "feature") == 0) {
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
                    mh_status st = mh_to_geojson_feature(lines.items[i], buf, cap, &ctx);
                    if (st == MH_OK) {
                        if (i > 0) {
                            fputc(',', out);
                        }
                        fputs(buf, out);
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
        if (strcmp(geojson_format, "bbox") == 0) {
            fputc('[', out);
            for (size_t i = 0; i < lines.length; i++) {
                if (i > 0) {
                    fputc(',', out);
                }
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
            }
            fputs("]\n", out);
            mh_cli_lines_free(&lines);
            return 0;
        }
        if (strcmp(geojson_format, "envelope") == 0) {
            if (split) {
                fputs("{\"type\":\"FeatureCollection\",\"features\":[", out);
                int first = 1;
                for (size_t i = 0; i < lines.length; i++) {
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
                    for (size_t p = 0; p < parts_len; p++) {
                        if (!first) {
                            fputc(',', out);
                        }
                        first = 0;
                        fputs("{\"type\":\"Feature\",\"geometry\":", out);
                        mh_cli_print_geojson_polygon(out, parts[p]);
                        fputs(",\"properties\":{}}", out);
                    }
                }
                fputs("]}\n", out);
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
                        fputs(buf, out);
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
                fputc('[', out);
                for (size_t p = 0; p < parts_len; p++) {
                    if (p > 0) {
                        fputc(',', out);
                    }
                    mh_cli_print_bbox_json(out, &parts[p]);
                }
                fputc(']', out);
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
                fputc('[', out);
                for (size_t p = 0; p < parts_len; p++) {
                    if (p > 0) {
                        fputc(',', out);
                    }
                    mh_cli_print_bbox_json(out, &parts[p]);
                }
                fputc(']', out);
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

static int mh_cli_handle_from_latlon(int argc, char **argv, FILE *out, FILE *err) {
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
                mh_cli_print_json_string(out, grid.locator);
            } else {
                fprintf(out, "%s%s", grid.locator, i + 1 < lines.length ? "\n" : "");
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
        char input0[64];
        char input1[64];
        snprintf(input0, sizeof(input0), "%.17g", lat);
        snprintf(input1, sizeof(input1), "%.17g", lon);
        const char *inputs[] = {input0, input1};
        mh_cli_json_single_start_strings(out, inputs, 2);
        fputs("\"value\":", out);
        mh_cli_print_json_string(out, grid.locator);
        mh_cli_json_single_end(out);
    } else {
        fprintf(out, "%s\n", grid.locator);
    }
    free(latlon_parts);
    mh_cli_lines_free(&lines);
    return 0;
}

static int mh_cli_handle_precision(int argc, char **argv, FILE *out, FILE *err) {
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

static int mh_cli_handle_children(int argc, char **argv, FILE *out, FILE *err) {
    const char *locator = NULL;
    int precision = -1;
    int csv = 0;
    int limit = -1;

    for (int i = 2; i < argc; i++) {
        if ((strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--precision") == 0) && i + 1 < argc) {
            precision = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--limit") == 0 && i + 1 < argc) {
            limit = atoi(argv[++i]);
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

    const char *sep = csv ? "," : " ";
    for (size_t i = 0; i < lines.length; i++) {
        fprintf(out, "%s%s", lines.items[i], i + 1 < lines.length ? sep : "");
    }
    fprintf(out, "\n");
    mh_cli_lines_free(&lines);
    return 0;
}

static int mh_cli_handle_contains(int argc, char **argv, FILE *out, FILE *err) {
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

static int mh_cli_handle_contains_point(int argc, char **argv, FILE *out, FILE *err) {
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
        char input0[64];
        char input1[64];
        char input2[64];
        snprintf(input0, sizeof(input0), "%s", locator);
        snprintf(input1, sizeof(input1), "%.17g", lat);
        snprintf(input2, sizeof(input2), "%.17g", lon);
        const char *inputs[] = {input0, input1, input2};
        mh_cli_json_single_start_strings(out, inputs, 3);
        fputs("\"value\":", out);
        fputs(hit ? "true" : "false", out);
        mh_cli_json_single_end(out);
    } else {
        fprintf(out, "%s\n", hit ? "true" : "false");
    }
    free(latlon_parts);
    mh_cli_lines_free(&lines);
    return 0;
}

static int mh_cli_handle_parent(int argc, char **argv, FILE *out, FILE *err) {
    const char *locator = NULL;
    int precision = -1;

    for (int i = 2; i < argc; i++) {
        if ((strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--precision") == 0) && i + 1 < argc) {
            precision = atoi(argv[++i]);
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
    fprintf(out, "%s\n", grid.locator);
    return 0;
}

static int mh_cli_handle_intersects_bbox(int argc, char **argv, FILE *out, FILE *err) {
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
        char input0[64];
        char input1[64];
        char input2[64];
        char input3[64];
        char input4[64];
        snprintf(input0, sizeof(input0), "%s", locator);
        snprintf(input1, sizeof(input1), "%.17g", min_lat);
        snprintf(input2, sizeof(input2), "%.17g", min_lon);
        snprintf(input3, sizeof(input3), "%.17g", max_lat);
        snprintf(input4, sizeof(input4), "%.17g", max_lon);
        const char *inputs[] = {input0, input1, input2, input3, input4};
        mh_cli_json_single_start_strings(out, inputs, 5);
        fputs("\"value\":", out);
        fputs(hit ? "true" : "false", out);
        mh_cli_json_single_end(out);
    } else {
        fprintf(out, "%s\n", hit ? "true" : "false");
    }
    mh_cli_lines_free(&lines);
    return 0;
}

static int mh_cli_handle_intersects_polygon(int argc, char **argv, FILE *out, FILE *err) {
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
        size_t input_count = point_len + 1;
        const char **inputs = (const char **)malloc(input_count * sizeof(char *));
        if (!inputs) {
            free(point_parts);
            mh_cli_lines_free(&lines);
            mh_cli_print_error(err, "allocation failed");
            return 2;
        }
        inputs[0] = locator;
        for (size_t p = 0; p < point_len; p++) {
            inputs[p + 1] = point_parts[p];
        }
        mh_cli_json_single_start_strings(out, inputs, input_count);
        fputs("\"value\":", out);
        fputs(hit ? "true" : "false", out);
        mh_cli_json_single_end(out);
        free(inputs);
    } else {
        fprintf(out, "%s\n", hit ? "true" : "false");
    }
    free(point_parts);
    mh_cli_lines_free(&lines);
    return 0;
}

static int mh_cli_handle_neighbors(int argc, char **argv, FILE *out, FILE *err) {
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

static int mh_cli_handle_adjacent(int argc, char **argv, FILE *out, FILE *err) {
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
    const char *sep = csv ? "," : " ";
    for (size_t i = 0; i < list.length; i++) {
        fprintf(out, "%s%s%s%s", list.keys[i], sep, list.values[i], i + 1 < list.length ? "\n" : "");
    }
    fprintf(out, "\n");
    mh_free_kv_list(&list);
    return 0;
}

static int mh_cli_handle_utm(int argc, char **argv, FILE *out, FILE *err) {
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

static int mh_cli_handle_step(int argc, char **argv, FILE *out, FILE *err) {
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

static int mh_cli_handle_corners(int argc, char **argv, FILE *out, FILE *err) {
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

static int mh_cli_handle_size(int argc, char **argv, FILE *out, FILE *err) {
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
    fprintf(out, "%.*f%s%.*f\n", digits, width, sep, digits, height);
    return 0;
}

static int mh_cli_handle_area(int argc, char **argv, FILE *out, FILE *err) {
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

static int mh_cli_handle_diagonal(int argc, char **argv, FILE *out, FILE *err) {
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

static int mh_cli_handle_distance(int argc, char **argv, FILE *out, FILE *err) {
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

static int mh_cli_handle_bearing(int argc, char **argv, FILE *out, FILE *err) {
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

static int mh_cli_handle_midpoint(int argc, char **argv, FILE *out, FILE *err) {
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
    fprintf(out, "%.*f%s%.*f\n", digits, out_pt.lat, sep, digits, out_pt.lon);
    return 0;
}

static int mh_cli_handle_great_circle(int argc, char **argv, FILE *out, FILE *err) {
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
    for (int i = 0; i < points_count; i++) {
        fprintf(out, "%.*f%s%.*f%s", digits, points[i].lat, sep, digits, points[i].lon,
                i + 1 < points_count ? "\n" : "");
    }
    fprintf(out, "\n");
    free(points);
    free(point_parts);
    return 0;
}

static int mh_cli_handle_bearing_bin(int argc, char **argv, FILE *out, FILE *err) {
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

static int mh_cli_handle_azimuthal_sector(int argc, char **argv, FILE *out, FILE *err) {
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
    fprintf(out, "%.*f%s%.*f\n", digits, start, sep, digits, end);
    return 0;
}

static int mh_cli_handle_azimuth(int argc, char **argv, FILE *out, FILE *err) {
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

static int mh_cli_handle_initial_bearing(int argc, char **argv, FILE *out, FILE *err) {
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

static int mh_cli_handle_cover_circle(int argc, char **argv, FILE *out, FILE *err) {
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
                fputc('[', out);
                for (size_t j = 0; j < list.length; j++) {
                    if (j > 0) {
                        fputc(',', out);
                    }
                    mh_cli_print_json_string(out, list.items[j]);
                }
                fputc(']', out);
            } else {
                const char *sep = strcmp(format, "csv") == 0 ? "," : " ";
                for (size_t j = 0; j < list.length; j++) {
                    fprintf(out, "%s%s", list.items[j], j + 1 < list.length ? sep : "");
                }
                fprintf(out, "%s", i + 1 < lines.length ? "\n" : "");
            }
            mh_free_list(&list);
            mh_cli_tokens_free(&tokens);
        }
        if (strcmp(format, "json") == 0) {
            mh_cli_json_bulk_end(out);
        } else {
            fputc('\n', out);
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
        char radius_buf[64];
        char precision_buf[32];
        snprintf(radius_buf, sizeof(radius_buf), "%.17g", radius_km);
        snprintf(precision_buf, sizeof(precision_buf), "%d", precision);
        size_t input_count = center_len + 2;
        const char **inputs = (const char **)malloc(input_count * sizeof(char *));
        if (!inputs) {
            mh_free_list(&list);
            free(center_parts);
            mh_cli_lines_free(&lines);
            mh_cli_print_error(err, "allocation failed");
            return 2;
        }
        for (size_t i = 0; i < center_len; i++) {
            inputs[i] = center_parts[i];
        }
        inputs[center_len] = radius_buf;
        inputs[center_len + 1] = precision_buf;
        mh_cli_json_single_start_strings(out, inputs, input_count);
        fputs("\"value\":", out);
        fputc('[', out);
        for (size_t j = 0; j < list.length; j++) {
            if (j > 0) {
                fputc(',', out);
            }
            mh_cli_print_json_string(out, list.items[j]);
        }
        fputc(']', out);
        mh_cli_json_single_end(out);
        free(inputs);
    } else {
        const char *sep = csv ? "," : " ";
        for (size_t j = 0; j < list.length; j++) {
            fprintf(out, "%s%s", list.items[j], j + 1 < list.length ? sep : "");
        }
        fprintf(out, "\n");
    }
    mh_free_list(&list);
    free(center_parts);
    mh_cli_lines_free(&lines);
    return 0;
}

static int mh_cli_handle_cover_line(int argc, char **argv, FILE *out, FILE *err) {
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
            int is_loc = 0;
            mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
            if (!mh_cli_parse_point_parts((const char **)&tokens.items[0], 1, &a, loc_a, sizeof(loc_a), &is_loc, &ctx)) {
                mh_cli_tokens_free(&tokens);
                free(point_parts);
                mh_cli_lines_free(&lines);
                mh_cli_print_error(err, "Point must be 'lat,lon' or a locator");
                return 2;
            }
            if (!mh_cli_parse_point_parts((const char **)&tokens.items[1], 1, &b, loc_b, sizeof(loc_b), &is_loc, &ctx)) {
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
                fputc('[', out);
                for (size_t j = 0; j < list.length; j++) {
                    if (j > 0) {
                        fputc(',', out);
                    }
                    mh_cli_print_json_string(out, list.items[j]);
                }
                fputc(']', out);
            } else {
                const char *sep = strcmp(format, "csv") == 0 ? "," : " ";
                for (size_t j = 0; j < list.length; j++) {
                    fprintf(out, "%s%s", list.items[j], j + 1 < list.length ? sep : "");
                }
                fprintf(out, "%s", i + 1 < lines.length ? "\n" : "");
            }
            mh_free_list(&list);
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
    int is_loc = 0;
    mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
    if (!mh_cli_parse_point_parts(a_parts, a_len, &a, loc_buf, sizeof(loc_buf), &is_loc, &ctx)) {
        free(point_parts);
        mh_cli_lines_free(&lines);
        mh_cli_print_error(err, "Point must be 'lat,lon' or a locator");
        return 2;
    }
    if (!mh_cli_parse_point_parts(b_parts, b_len, &b, loc_buf, sizeof(loc_buf), &is_loc, &ctx)) {
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
        char precision_buf[32];
        snprintf(precision_buf, sizeof(precision_buf), "%d", precision);
        size_t input_count = point_len + 1;
        const char **inputs = (const char **)malloc(input_count * sizeof(char *));
        if (!inputs) {
            mh_free_list(&list);
            free(point_parts);
            mh_cli_lines_free(&lines);
            mh_cli_print_error(err, "allocation failed");
            return 2;
        }
        for (size_t i = 0; i < point_len; i++) {
            inputs[i] = point_parts[i];
        }
        inputs[point_len] = precision_buf;
        mh_cli_json_single_start_strings(out, inputs, input_count);
        fputs("\"value\":", out);
        fputc('[', out);
        for (size_t j = 0; j < list.length; j++) {
            if (j > 0) {
                fputc(',', out);
            }
            mh_cli_print_json_string(out, list.items[j]);
        }
        fputc(']', out);
        mh_cli_json_single_end(out);
        free(inputs);
    } else {
        const char *sep = csv ? "," : " ";
        for (size_t j = 0; j < list.length; j++) {
            fprintf(out, "%s%s", list.items[j], j + 1 < list.length ? sep : "");
        }
        fprintf(out, "\n");
    }
    mh_free_list(&list);
    free(point_parts);
    mh_cli_lines_free(&lines);
    return 0;
}

int mh_cli_main_io(int argc, char **argv, FILE *out, FILE *err) {
    if (argc <= 1 || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        fprintf(out, "usage: mh [-h] [--version]\n");
        fprintf(out, "          {normalize,validate,center,bbox,parts,geojson,size,step,from-latlon,format,area,diagonal,utm,corners,precision,neighbors,adjacent,wkt,bbox-split,bbox-split-list,bulk,cover-circle,cover-line,distance,bearing,midpoint,great-circle,bearing-bin,azimuthal-sector,azimuth,initial-bearing,parent,children,contains,contains-point,intersects-bbox,intersects-polygon} ...\n\n");
        fprintf(out, "Maidenhead grid square utilities for encoding and decoding locators.\n\n");
        fprintf(out, "positional arguments:\n");
        fprintf(out, "  {normalize,validate,center,bbox,parts,geojson,size,step,from-latlon,format,area,diagonal,utm,corners,precision,neighbors,adjacent,wkt,bbox-split,bbox-split-list,bulk,cover-circle,cover-line,distance,bearing,midpoint,great-circle,bearing-bin,azimuthal-sector,azimuth,initial-bearing,parent,children,contains,contains-point,intersects-bbox,intersects-polygon}\n");
        fprintf(out, "    normalize           Normalize locator casing and validate.\n");
        fprintf(out, "    validate            Validate a locator. Exit code 0 if valid, 2 if invalid.\n");
        fprintf(out, "    center              Print the center lat/lon for a locator.\n");
        fprintf(out, "    bbox                Print bbox for a locator: min_lat min_lon max_lat max_lon.\n");
        fprintf(out, "    parts               Print locator components (field/square/subsquare/etc).\n");
        fprintf(out, "    geojson             Emit GeoJSON for a locator or batch.\n");
        fprintf(out, "    size                Print cell size (width height).\n");
        fprintf(out, "    step                Move a locator by a number of cells.\n");
        fprintf(out, "    from-latlon         Convert lat/lon to a locator.\n");
        fprintf(out, "    format              Coerce locator precision.\n");
        fprintf(out, "    area                Print cell area (km^2).\n");
        fprintf(out, "    diagonal            Print cell diagonal length (km).\n");
        fprintf(out, "    utm                 Print UTM zone for locator.\n");
        fprintf(out, "    corners             Print NW NE SW SE corners of a locator.\n");
        fprintf(out, "    precision           Print locator precision (character length).\n");
        fprintf(out, "    neighbors           List neighboring locators.\n");
        fprintf(out, "    adjacent            List adjacent locators with directions.\n");
        fprintf(out, "    wkt                 Print WKT polygon for a locator or lat/lon.\n");
        fprintf(out, "    bbox-split          Split a bbox that crosses the antimeridian.\n");
        fprintf(out, "    bbox-split-list     List non-degenerate bbox parts after antimeridian split.\n");
        fprintf(out, "    bulk                Bulk operations for locators/latlon.\n");
        fprintf(out, "    cover-circle        Cover circle with grid squares.\n");
        fprintf(out, "    cover-line          Cover line with grid squares.\n");
        fprintf(out, "    distance            Distance (km) between two locators or points.\n");
        fprintf(out, "    bearing             Initial bearing (deg) from A to B.\n");
        fprintf(out, "    midpoint            Great-circle midpoint between A and B (lat lon).\n");
        fprintf(out, "    great-circle        Great-circle path points from A to B.\n");
        fprintf(out, "    bearing-bin         Bearing bin start angle from A to B.\n");
        fprintf(out, "    azimuthal-sector    Bearing sector from A to B.\n");
        fprintf(out, "    azimuth             Bearing and distance between A and B.\n");
        fprintf(out, "    initial-bearing     Initial bearing between two locators.\n");
        fprintf(out, "    parent              Return parent locator at lower precision.\n");
        fprintf(out, "    children            List child locators at higher precision.\n");
        fprintf(out, "    contains            Check if one locator contains another.\n");
        fprintf(out, "    contains-point      Check if a locator contains a lat/lon point.\n");
        fprintf(out, "    intersects-bbox     Check if locator intersects a bbox.\n");
        fprintf(out, "    intersects-polygon  Check if locator intersects a polygon.\n\n");
        fprintf(out, "options:\n");
        fprintf(out, "  -h, --help            show this help message and exit\n");
        fprintf(out, "  --version             show program's version number and exit\n\n");
        fprintf(out, "Examples:\n");
        fprintf(out, "  mh normalize IO91wm\n");
        fprintf(out, "  mh center IO83ri\n");
        fprintf(out, "  mh from-latlon 53.365418,-2.574069\n");
        return 0;
    }
    if (strcmp(argv[1], "--version") == 0) {
        fprintf(out, "mh 1.0.0rc1\n");
        return 0;
    }
    if (strcmp(argv[1], "normalize") == 0) {
        return mh_cli_handle_normalize(argc, argv, out, err);
    }
    if (strcmp(argv[1], "validate") == 0) {
        return mh_cli_handle_validate(argc, argv, out, err);
    }
    if (strcmp(argv[1], "center") == 0) {
        return mh_cli_handle_center(argc, argv, out, err);
    }
    if (strcmp(argv[1], "bbox") == 0) {
        return mh_cli_handle_bbox(argc, argv, out, err);
    }
    if (strcmp(argv[1], "parts") == 0) {
        return mh_cli_handle_parts(argc, argv, out, err);
    }
    if (strcmp(argv[1], "geojson") == 0) {
        return mh_cli_handle_geojson(argc, argv, out, err);
    }
    if (strcmp(argv[1], "wkt") == 0) {
        return mh_cli_handle_wkt(argc, argv, out, err);
    }
    if (strcmp(argv[1], "bbox-split") == 0) {
        return mh_cli_handle_bbox_split(argc, argv, out, err);
    }
    if (strcmp(argv[1], "bbox-split-list") == 0) {
        return mh_cli_handle_bbox_split_list(argc, argv, out, err);
    }
    if (strcmp(argv[1], "bulk") == 0) {
        return mh_cli_handle_bulk(argc, argv, out, err);
    }
    if (strcmp(argv[1], "format") == 0) {
        return mh_cli_handle_format(argc, argv, out, err);
    }
    if (strcmp(argv[1], "from-latlon") == 0) {
        return mh_cli_handle_from_latlon(argc, argv, out, err);
    }
    if (strcmp(argv[1], "precision") == 0) {
        return mh_cli_handle_precision(argc, argv, out, err);
    }
    if (strcmp(argv[1], "children") == 0) {
        return mh_cli_handle_children(argc, argv, out, err);
    }
    if (strcmp(argv[1], "parent") == 0) {
        return mh_cli_handle_parent(argc, argv, out, err);
    }
    if (strcmp(argv[1], "contains") == 0) {
        return mh_cli_handle_contains(argc, argv, out, err);
    }
    if (strcmp(argv[1], "contains-point") == 0) {
        return mh_cli_handle_contains_point(argc, argv, out, err);
    }
    if (strcmp(argv[1], "intersects-bbox") == 0) {
        return mh_cli_handle_intersects_bbox(argc, argv, out, err);
    }
    if (strcmp(argv[1], "intersects-polygon") == 0) {
        return mh_cli_handle_intersects_polygon(argc, argv, out, err);
    }
    if (strcmp(argv[1], "neighbors") == 0) {
        return mh_cli_handle_neighbors(argc, argv, out, err);
    }
    if (strcmp(argv[1], "adjacent") == 0) {
        return mh_cli_handle_adjacent(argc, argv, out, err);
    }
    if (strcmp(argv[1], "utm") == 0) {
        return mh_cli_handle_utm(argc, argv, out, err);
    }
    if (strcmp(argv[1], "step") == 0) {
        return mh_cli_handle_step(argc, argv, out, err);
    }
    if (strcmp(argv[1], "corners") == 0) {
        return mh_cli_handle_corners(argc, argv, out, err);
    }
    if (strcmp(argv[1], "size") == 0) {
        return mh_cli_handle_size(argc, argv, out, err);
    }
    if (strcmp(argv[1], "area") == 0) {
        return mh_cli_handle_area(argc, argv, out, err);
    }
    if (strcmp(argv[1], "diagonal") == 0) {
        return mh_cli_handle_diagonal(argc, argv, out, err);
    }
    if (strcmp(argv[1], "distance") == 0) {
        return mh_cli_handle_distance(argc, argv, out, err);
    }
    if (strcmp(argv[1], "bearing") == 0) {
        return mh_cli_handle_bearing(argc, argv, out, err);
    }
    if (strcmp(argv[1], "midpoint") == 0) {
        return mh_cli_handle_midpoint(argc, argv, out, err);
    }
    if (strcmp(argv[1], "great-circle") == 0) {
        return mh_cli_handle_great_circle(argc, argv, out, err);
    }
    if (strcmp(argv[1], "bearing-bin") == 0) {
        return mh_cli_handle_bearing_bin(argc, argv, out, err);
    }
    if (strcmp(argv[1], "azimuthal-sector") == 0) {
        return mh_cli_handle_azimuthal_sector(argc, argv, out, err);
    }
    if (strcmp(argv[1], "azimuth") == 0) {
        return mh_cli_handle_azimuth(argc, argv, out, err);
    }
    if (strcmp(argv[1], "initial-bearing") == 0) {
        return mh_cli_handle_initial_bearing(argc, argv, out, err);
    }
    if (strcmp(argv[1], "cover-circle") == 0) {
        return mh_cli_handle_cover_circle(argc, argv, out, err);
    }
    if (strcmp(argv[1], "cover-line") == 0) {
        return mh_cli_handle_cover_line(argc, argv, out, err);
    }

    mh_cli_print_error(err, "unknown command");
    return 2;
}

int mh_cli_main(int argc, char **argv) {
    return mh_cli_main_io(argc, argv, stdout, stderr);
}
