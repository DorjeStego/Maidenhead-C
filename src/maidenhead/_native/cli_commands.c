#include "cli_commands.h"

#include <string.h>

#include "cli_handlers.h"

static const mh_cli_command MH_CLI_COMMANDS[] = {
    {"normalize", mh_cli_handle_normalize},
    {"validate", mh_cli_handle_validate},
    {"center", mh_cli_handle_center},
    {"bbox", mh_cli_handle_bbox},
    {"parts", mh_cli_handle_parts},
    {"geojson", mh_cli_handle_geojson},
    {"wkt", mh_cli_handle_wkt},
    {"bbox-split", mh_cli_handle_bbox_split},
    {"bbox-split-list", mh_cli_handle_bbox_split_list},
    {"bulk", mh_cli_handle_bulk},
    {"format", mh_cli_handle_format},
    {"from-latlon", mh_cli_handle_from_latlon},
    {"precision", mh_cli_handle_precision},
    {"children", mh_cli_handle_children},
    {"parent", mh_cli_handle_parent},
    {"contains", mh_cli_handle_contains},
    {"contains-point", mh_cli_handle_contains_point},
    {"intersects-bbox", mh_cli_handle_intersects_bbox},
    {"intersects-polygon", mh_cli_handle_intersects_polygon},
    {"neighbors", mh_cli_handle_neighbors},
    {"adjacent", mh_cli_handle_adjacent},
    {"utm", mh_cli_handle_utm},
    {"step", mh_cli_handle_step},
    {"corners", mh_cli_handle_corners},
    {"size", mh_cli_handle_size},
    {"area", mh_cli_handle_area},
    {"diagonal", mh_cli_handle_diagonal},
    {"distance", mh_cli_handle_distance},
    {"bearing", mh_cli_handle_bearing},
    {"midpoint", mh_cli_handle_midpoint},
    {"great-circle", mh_cli_handle_great_circle},
    {"bearing-bin", mh_cli_handle_bearing_bin},
    {"azimuthal-sector", mh_cli_handle_azimuthal_sector},
    {"azimuth", mh_cli_handle_azimuth},
    {"initial-bearing", mh_cli_handle_initial_bearing},
    {"cover-circle", mh_cli_handle_cover_circle},
    {"cover-line", mh_cli_handle_cover_line},
};

const mh_cli_command *mh_cli_find_command(const char *name) {
    if (!name || !*name) {
        return NULL;
    }
    for (size_t i = 0; i < sizeof(MH_CLI_COMMANDS) / sizeof(MH_CLI_COMMANDS[0]); i++) {
        if (strcmp(name, MH_CLI_COMMANDS[i].name) == 0) {
            return &MH_CLI_COMMANDS[i];
        }
    }
    return NULL;
}
