#include "cli.h"

#include "cli_commands.h"
#include "cli_utils.h"

#include <stdio.h>
#include <string.h>

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
        fprintf(out, "mh 1.0.0rc2-005\n");
        return 0;
    }
    const mh_cli_command *cmd = mh_cli_find_command(argv[1]);
    if (cmd) {
        return cmd->handler(argc, argv, out, err);
    }

    mh_cli_print_error(err, "unknown command");
    return 2;
}

int mh_cli_main(int argc, char **argv) {
    return mh_cli_main_io(argc, argv, stdout, stderr);
}

