#ifndef MAIDENHEAD_NATIVE_CLI_HANDLERS_H
#define MAIDENHEAD_NATIVE_CLI_HANDLERS_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

int mh_cli_handle_normalize(int argc, char **argv, FILE *out, FILE *err);
int mh_cli_handle_validate(int argc, char **argv, FILE *out, FILE *err);
int mh_cli_handle_center(int argc, char **argv, FILE *out, FILE *err);
int mh_cli_handle_bbox(int argc, char **argv, FILE *out, FILE *err);
int mh_cli_handle_parts(int argc, char **argv, FILE *out, FILE *err);
int mh_cli_handle_format(int argc, char **argv, FILE *out, FILE *err);
int mh_cli_handle_geojson(int argc, char **argv, FILE *out, FILE *err);
int mh_cli_handle_wkt(int argc, char **argv, FILE *out, FILE *err);
int mh_cli_handle_bbox_split(int argc, char **argv, FILE *out, FILE *err);
int mh_cli_handle_bbox_split_list(int argc, char **argv, FILE *out, FILE *err);
int mh_cli_handle_bulk(int argc, char **argv, FILE *out, FILE *err);
int mh_cli_handle_from_latlon(int argc, char **argv, FILE *out, FILE *err);
int mh_cli_handle_precision(int argc, char **argv, FILE *out, FILE *err);
int mh_cli_handle_children(int argc, char **argv, FILE *out, FILE *err);
int mh_cli_handle_contains(int argc, char **argv, FILE *out, FILE *err);
int mh_cli_handle_contains_point(int argc, char **argv, FILE *out, FILE *err);
int mh_cli_handle_parent(int argc, char **argv, FILE *out, FILE *err);
int mh_cli_handle_intersects_bbox(int argc, char **argv, FILE *out, FILE *err);
int mh_cli_handle_intersects_polygon(int argc, char **argv, FILE *out, FILE *err);
int mh_cli_handle_neighbors(int argc, char **argv, FILE *out, FILE *err);
int mh_cli_handle_adjacent(int argc, char **argv, FILE *out, FILE *err);
int mh_cli_handle_utm(int argc, char **argv, FILE *out, FILE *err);
int mh_cli_handle_step(int argc, char **argv, FILE *out, FILE *err);
int mh_cli_handle_corners(int argc, char **argv, FILE *out, FILE *err);
int mh_cli_handle_size(int argc, char **argv, FILE *out, FILE *err);
int mh_cli_handle_area(int argc, char **argv, FILE *out, FILE *err);
int mh_cli_handle_diagonal(int argc, char **argv, FILE *out, FILE *err);
int mh_cli_handle_distance(int argc, char **argv, FILE *out, FILE *err);
int mh_cli_handle_bearing(int argc, char **argv, FILE *out, FILE *err);
int mh_cli_handle_midpoint(int argc, char **argv, FILE *out, FILE *err);
int mh_cli_handle_great_circle(int argc, char **argv, FILE *out, FILE *err);
int mh_cli_handle_bearing_bin(int argc, char **argv, FILE *out, FILE *err);
int mh_cli_handle_azimuthal_sector(int argc, char **argv, FILE *out, FILE *err);
int mh_cli_handle_azimuth(int argc, char **argv, FILE *out, FILE *err);
int mh_cli_handle_initial_bearing(int argc, char **argv, FILE *out, FILE *err);
int mh_cli_handle_cover_circle(int argc, char **argv, FILE *out, FILE *err);
int mh_cli_handle_cover_line(int argc, char **argv, FILE *out, FILE *err);

#ifdef __cplusplus
}
#endif

#endif
