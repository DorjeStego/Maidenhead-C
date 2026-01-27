#ifndef MAIDENHEAD_NATIVE_CLI_UTILS_H
#define MAIDENHEAD_NATIVE_CLI_UTILS_H

#include <stddef.h>
#include <stdio.h>

#include "errors.h"
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

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

typedef struct {
    mh_cli_lines *lines;
    size_t limit;
} mh_cli_children_ctx;

char *mh_cli_strdup(const char *s);
char *mh_cli_strndup(const char *s, size_t n);
void mh_cli_lines_init(mh_cli_lines *lines);
void mh_cli_lines_free(mh_cli_lines *lines);
int mh_cli_lines_push(mh_cli_lines *lines, const char *value);
void mh_cli_tokens_free(mh_cli_tokens *tokens);
char *mh_cli_trim(char *s);
int mh_cli_split_line(const char *line, mh_cli_tokens *out);
int mh_cli_read_lines(const char *file_path, int use_stdin, mh_cli_lines *out, FILE *err);
int mh_cli_parse_double(const char *s, double *out);
int mh_cli_step_size_for_precision(int precision, double *lon_step, double *lat_step, mh_error_context *err);
int mh_cli_parse_latlon_joined(const char *joined, double *lat, double *lon);
int mh_cli_split_latlon_parts(const char **parts, size_t n, double *lat, double *lon);
int mh_cli_parse_point_parts(
    const char **parts,
    size_t n,
    mh_point *out,
    char *locator_buf,
    size_t locator_len,
    int *is_locator,
    mh_error_context *err
);
int mh_cli_split_two_points(
    char **args,
    size_t n,
    const char ***a_parts,
    size_t *a_len,
    const char ***b_parts,
    size_t *b_len
);
int mh_cli_children_callback(const char *locator, void *userdata);
void mh_cli_print_json_string(FILE *out, const char *s);
int mh_cli_geojson_with_buffer(
    const char *locator,
    mh_status (*fn)(const char *, char *, size_t, mh_error_context *),
    FILE *out,
    FILE *err
);
int mh_cli_geojson_feature_collection(
    const char *const *locators,
    size_t count,
    FILE *out,
    FILE *err
);
char *mh_cli_geojson_feature_collection_alloc(
    const char *const *locators,
    size_t count,
    FILE *err
);
void mh_cli_print_geojson_polygon(FILE *out, mh_bbox bbox);
void mh_cli_print_geojson_bbox(FILE *out, const double bbox[4]);
void mh_cli_print_bbox_json(FILE *out, const mh_bbox *bbox);
void mh_cli_print_json_float(FILE *out, double value);
int mh_cli_decimal_places(const char *value);
int mh_cli_precision_from_decimals(int decimals);
void mh_cli_json_bulk_start(FILE *out, const mh_cli_lines *lines);
void mh_cli_json_bulk_end(FILE *out);
void mh_cli_json_single_start_string(FILE *out, const char *input);
void mh_cli_json_single_start_strings(FILE *out, const char **items, size_t count);
void mh_cli_json_single_end(FILE *out);
void mh_cli_json_obj_start_input_string(FILE *out, const char *input);
void mh_cli_json_obj_start_input_array(FILE *out, const char **items, size_t count);
void mh_cli_json_obj_end(FILE *out);
void mh_cli_print_csv_field(FILE *out, const char *value);
void mh_cli_json_print_latlon(FILE *out, double lat, double lon);
void mh_cli_json_print_point_input(
    FILE *out,
    const char *token,
    int is_locator,
    double lat,
    double lon
);
void mh_cli_print_error(FILE *err, const char *message);
int mh_cli_print_mh_error(FILE *err, const mh_error_context *ctx);
int mh_cli_print_mh_error_line(FILE *err, const mh_error_context *ctx, size_t line_no);
int mh_cli_require_arg(const char *value, const char *message, FILE *err);
void mh_cli_format_bbox(char *buf, size_t buf_len, const mh_bbox *bbox, int digits, const char *sep);
void mh_cli_print_wkt_polygon(FILE *out, const mh_bbox *bbox);
void mh_cli_format_latlon(char *buf, size_t buf_len, double lat, double lon, int digits, const char *sep);

#ifdef __cplusplus
}
#endif

#endif
