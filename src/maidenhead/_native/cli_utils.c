#include "cli_utils.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core.h"
#include "geojson.h"

char *mh_cli_strdup(const char *s) {
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

char *mh_cli_strndup(const char *s, size_t n) {
    char *out = (char *)malloc(n + 1);
    if (!out) {
        return NULL;
    }
    memcpy(out, s, n);
    out[n] = '\0';
    return out;
}

void mh_cli_lines_init(mh_cli_lines *lines) {
    lines->items = NULL;
    lines->length = 0;
    lines->capacity = 0;
}

void mh_cli_lines_free(mh_cli_lines *lines) {
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

int mh_cli_lines_push(mh_cli_lines *lines, const char *value) {
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

void mh_cli_tokens_free(mh_cli_tokens *tokens) {
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

char *mh_cli_trim(char *s) {
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

int mh_cli_split_line(const char *line, mh_cli_tokens *out) {
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

int mh_cli_read_lines(const char *file_path, int use_stdin, mh_cli_lines *out, FILE *err) {
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

int mh_cli_parse_double(const char *s, double *out) {
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

int mh_cli_step_size_for_precision(int precision, double *lon_step, double *lat_step, mh_error_context *err) {
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

int mh_cli_parse_latlon_joined(const char *joined, double *lat, double *lon) {
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

int mh_cli_split_latlon_parts(const char **parts, size_t n, double *lat, double *lon) {
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

int mh_cli_parse_point_parts(
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

int mh_cli_split_two_points(
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

int mh_cli_children_callback(const char *locator, void *userdata) {
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

void mh_cli_print_json_string(FILE *out, const char *s) {
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

void mh_cli_print_error(FILE *err, const char *message);
int mh_cli_print_mh_error(FILE *err, const mh_error_context *ctx);

int mh_cli_geojson_with_buffer(
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

int mh_cli_geojson_feature_collection(
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

char *mh_cli_geojson_feature_collection_alloc(
    const char *const *locators,
    size_t count,
    FILE *err
) {
    size_t cap = 512 + count * 512;
    for (int attempt = 0; attempt < 4; attempt++) {
        char *buf = (char *)malloc(cap);
        if (!buf) {
            mh_cli_print_error(err, "allocation failed");
            return NULL;
        }
        mh_error_context ctx = {MH_OK, NULL, NULL, NULL};
        mh_status st = mh_to_geojson_feature_collection(locators, count, buf, cap, &ctx);
        if (st == MH_OK) {
            return buf;
        }
        free(buf);
        if (ctx.code == MH_ERR_INTERNAL && ctx.message && strcmp(ctx.message, "output buffer too small") == 0) {
            cap *= 2;
            continue;
        }
        mh_cli_print_mh_error(err, &ctx);
        return NULL;
    }
    mh_cli_print_error(err, "output buffer too small");
    return NULL;
}

void mh_cli_print_geojson_polygon(FILE *out, mh_bbox bbox) {
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

void mh_cli_print_geojson_bbox(FILE *out, const double bbox[4]) {
    fprintf(out, "[%.17g,%.17g,%.17g,%.17g]", bbox[0], bbox[1], bbox[2], bbox[3]);
}

void mh_cli_print_bbox_json(FILE *out, const mh_bbox *bbox) {
    fprintf(
        out,
        "[%.17g,%.17g,%.17g,%.17g]",
        bbox->min_lat,
        bbox->min_lon,
        bbox->max_lat,
        bbox->max_lon
    );
}

void mh_cli_print_json_float(FILE *out, double value) {
    fprintf(out, "%.17g", value);
}

int mh_cli_decimal_places(const char *value) {
    const char *dot = strchr(value, '.');
    if (!dot) {
        return 0;
    }
    int count = 0;
    for (const char *p = dot + 1; *p; p++) {
        if (!isdigit((unsigned char)*p)) {
            break;
        }
        count++;
    }
    return count;
}

int mh_cli_precision_from_decimals(int decimals) {
    if (decimals <= 0) {
        return 2;
    }
    if (decimals <= 2) {
        return 4;
    }
    if (decimals <= 4) {
        return 6;
    }
    if (decimals <= 6) {
        return 8;
    }
    return 10;
}

void mh_cli_json_bulk_start(FILE *out, const mh_cli_lines *lines) {
    (void)lines;
    fputc('[', out);
}

void mh_cli_json_bulk_end(FILE *out) {
    fputc(']', out);
}

void mh_cli_json_single_start_string(FILE *out, const char *input) {
    fputs("{\"input\":", out);
    mh_cli_print_json_string(out, input);
    fputs(",\"output\":", out);
}

void mh_cli_json_single_start_strings(FILE *out, const char **items, size_t count) {
    fputs("{\"input\":[", out);
    for (size_t i = 0; i < count; i++) {
        if (i > 0) {
            fputc(',', out);
        }
        mh_cli_print_json_string(out, items[i]);
    }
    fputs("],\"output\":", out);
}

void mh_cli_json_single_end(FILE *out) {
    fputs("}\n", out);
}

void mh_cli_json_obj_start_input_string(FILE *out, const char *input) {
    fputs("{\"input\":", out);
    mh_cli_print_json_string(out, input);
    fputs(",\"output\":", out);
}

void mh_cli_json_obj_start_input_array(FILE *out, const char **items, size_t count) {
    fputs("{\"input\":[", out);
    for (size_t i = 0; i < count; i++) {
        if (i > 0) {
            fputc(',', out);
        }
        mh_cli_print_json_string(out, items[i]);
    }
    fputs("],\"output\":", out);
}

void mh_cli_json_obj_end(FILE *out) {
    fputc('}', out);
}

void mh_cli_print_csv_field(FILE *out, const char *value) {
    fputc('"', out);
    for (const char *p = value; *p; ++p) {
        if (*p == '"') {
            fputc('"', out);
        }
        fputc(*p, out);
    }
    fputc('"', out);
}

void mh_cli_json_print_latlon(FILE *out, double lat, double lon) {
    fputc('[', out);
    mh_cli_print_json_float(out, lat);
    fputc(',', out);
    mh_cli_print_json_float(out, lon);
    fputc(']', out);
}

void mh_cli_json_print_point_input(
    FILE *out,
    const char *token,
    int is_locator,
    double lat,
    double lon
) {
    if (is_locator) {
        mh_cli_print_json_string(out, token);
    } else {
        mh_cli_json_print_latlon(out, lat, lon);
    }
}

void mh_cli_print_error(FILE *err, const char *message) {
    fprintf(err, "error: %s\n", message);
}

int mh_cli_print_mh_error(FILE *err, const mh_error_context *ctx) {
    if (ctx && ctx->message) {
        mh_cli_print_error(err, ctx->message);
    } else {
        mh_cli_print_error(err, "operation failed");
    }
    return 2;
}

int mh_cli_print_mh_error_line(FILE *err, const mh_error_context *ctx, size_t line_no) {
    if (ctx && ctx->message) {
        fprintf(err, "error: %s at line %zu\n", ctx->message, line_no);
    } else {
        fprintf(err, "error: operation failed at line %zu\n", line_no);
    }
    return 2;
}

int mh_cli_require_arg(const char *value, const char *message, FILE *err) {
    if (!value || !*value) {
        mh_cli_print_error(err, message);
        return 0;
    }
    return 1;
}

void mh_cli_format_bbox(char *buf, size_t buf_len, const mh_bbox *bbox, int digits, const char *sep) {
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

void mh_cli_print_wkt_polygon(FILE *out, const mh_bbox *bbox) {
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

void mh_cli_format_latlon(char *buf, size_t buf_len, double lat, double lon, int digits, const char *sep) {
    snprintf(buf, buf_len, "%.*f%s%.*f", digits, lat, sep, digits, lon);
}
