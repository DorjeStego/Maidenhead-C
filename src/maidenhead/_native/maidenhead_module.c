#include <Python.h>

#include "maidenhead_errors.h"
#include "maidenhead_core_py.h"
#include "maidenhead_geojson_py.h"
#include "maidenhead_geo_py.h"
#include "maidenhead_json.h"

static PyMethodDef maidenhead_native_methods[] = {
    {"normalize", py_mh_normalize, METH_VARARGS, "Normalize a locator."},
    {"normalize_many", py_mh_normalize_many, METH_VARARGS, "Normalize locators."},
    {"precision_of", py_mh_precision_of, METH_VARARGS, "Return locator precision."},
    {"to_bbox", py_mh_to_bbox, METH_VARARGS, "Return locator bbox."},
    {"to_center_latlon", py_mh_to_center_latlon, METH_VARARGS, "Return locator center lat/lon."},
    {"from_latlon", (PyCFunction)py_mh_from_latlon, METH_VARARGS | METH_KEYWORDS, "Convert lat/lon to locator."},
    {"format_locator", (PyCFunction)py_mh_format_locator, METH_VARARGS | METH_KEYWORDS, "Format locator precision."},
    {"to_bbox_split", py_mh_to_bbox_split, METH_VARARGS, "Split locator bbox at antimeridian."},
    {"split_bbox", py_mh_split_bbox, METH_VARARGS, "Split bbox at antimeridian."},
    {"split_bbox_list", py_mh_split_bbox_list, METH_VARARGS, "Split bbox into parts list."},
    {"from_latlon_many", (PyCFunction)py_mh_from_latlon_many, METH_VARARGS | METH_KEYWORDS, "Bulk lat/lon to locator."},
    {"to_center_many", py_mh_to_center_many, METH_VARARGS, "Bulk locator centers."},
    {"to_bbox_many", py_mh_to_bbox_many, METH_VARARGS, "Bulk locator bboxes."},
    {"parse", py_mh_parse, METH_VARARGS, "Parse and normalize a locator."},
    {"corners", py_mh_corners, METH_VARARGS, "Locator corners."},
    {"cell_size_deg", py_mh_cell_size_deg, METH_VARARGS, "Locator cell size (deg)."},
    {"cell_size_km", (PyCFunction)py_mh_cell_size_km, METH_VARARGS | METH_KEYWORDS, "Locator cell size (km)."},
    {"area_km2", (PyCFunction)py_mh_area_km2, METH_VARARGS | METH_KEYWORDS, "Locator area (km2)."},
    {"diagonal_km", (PyCFunction)py_mh_diagonal_km, METH_VARARGS | METH_KEYWORDS, "Locator diagonal (km)."},
    {"step", (PyCFunction)py_mh_step, METH_VARARGS | METH_KEYWORDS, "Step a locator."},
    {"contains_point", py_mh_contains_point, METH_VARARGS, "Point containment."},
    {"contains", py_mh_contains, METH_VARARGS, "Locator containment."},
    {"intersects_bbox", py_mh_intersects_bbox, METH_VARARGS, "Intersect locator with bbox."},
    {"intersects_polygon", py_mh_intersects_polygon, METH_VARARGS, "Intersect locator with polygon."},
    {"to_utm_zone", py_mh_to_utm_zone, METH_VARARGS, "UTM zone from locator."},
    {"to_utm_zone_many", py_mh_to_utm_zone_many, METH_VARARGS, "Bulk UTM zones."},
    {"contains_many", py_mh_contains_many, METH_VARARGS, "Bulk containment checks."},
    {"contains_point_many", py_mh_contains_point_many, METH_VARARGS, "Bulk point containment checks."},
    {"intersects_bbox_many", py_mh_intersects_bbox_many, METH_VARARGS, "Bulk bbox intersection checks."},
    {"intersects_polygon_many", py_mh_intersects_polygon_many, METH_VARARGS, "Bulk polygon intersection checks."},
    {"corners_many", py_mh_corners_many, METH_VARARGS, "Bulk locator corners."},
    {"parent", (PyCFunction)py_mh_parent, METH_VARARGS | METH_KEYWORDS, "Return parent locator."},
    {"children", (PyCFunction)py_mh_children, METH_VARARGS | METH_KEYWORDS, "Return child locators."},
    {"neighbors", (PyCFunction)py_mh_neighbors, METH_VARARGS | METH_KEYWORDS, "Neighbors at given ring."},
    {"neighbors_many", (PyCFunction)py_mh_neighbors_many, METH_VARARGS | METH_KEYWORDS, "Bulk neighbors."},
    {"adjacent", (PyCFunction)py_mh_adjacent, METH_VARARGS | METH_KEYWORDS, "Adjacent locators."},
    {"adjacent_many", (PyCFunction)py_mh_adjacent_many, METH_VARARGS | METH_KEYWORDS, "Bulk adjacent."},
    {"parent_many", (PyCFunction)py_mh_parent_many, METH_VARARGS | METH_KEYWORDS, "Bulk parent locators."},
    {"children_many", (PyCFunction)py_mh_children_many, METH_VARARGS | METH_KEYWORDS, "Bulk child locators."},
    {"cell_size_deg_many", py_mh_cell_size_deg_many, METH_VARARGS, "Bulk cell sizes (deg)."},
    {"cell_size_km_many", (PyCFunction)py_mh_cell_size_km_many, METH_VARARGS | METH_KEYWORDS, "Bulk cell sizes (km)."},
    {"area_km2_many", (PyCFunction)py_mh_area_km2_many, METH_VARARGS | METH_KEYWORDS, "Bulk area (km2)."},
    {"diagonal_km_many", (PyCFunction)py_mh_diagonal_km_many, METH_VARARGS | METH_KEYWORDS, "Bulk diagonal (km)."},
    {"azimuth_many", (PyCFunction)py_mh_azimuth_many, METH_VARARGS | METH_KEYWORDS, "Bulk azimuth."},
    {"initial_bearing_many", py_mh_initial_bearing_many, METH_VARARGS, "Bulk initial bearing."},
    {"to_wkt_many", py_mh_to_wkt_many, METH_VARARGS, "Bulk WKT."},
    {"to_geojson_polygon_many", py_mh_to_geojson_polygon_many, METH_VARARGS, "Bulk GeoJSON polygons."},
    {"to_geojson_feature_many", py_mh_to_geojson_feature_many, METH_VARARGS, "Bulk GeoJSON features."},
    {"to_geojson_bbox_many", py_mh_to_geojson_bbox_many, METH_VARARGS, "Bulk GeoJSON bboxes."},
    {"to_geojson_envelope_many", py_mh_to_geojson_envelope_many, METH_VARARGS, "Bulk GeoJSON envelopes."},
    {"split_bbox_many", py_mh_split_bbox_many, METH_VARARGS, "Bulk split bbox."},
    {"distance_km", (PyCFunction)py_mh_distance_km, METH_VARARGS | METH_KEYWORDS, "Distance between two points."},
    {"bearing_deg", py_mh_bearing_deg, METH_VARARGS, "Initial bearing between two points."},
    {"midpoint", py_mh_midpoint, METH_VARARGS, "Great-circle midpoint between points."},
    {"great_circle_path", (PyCFunction)py_mh_great_circle_path, METH_VARARGS | METH_KEYWORDS, "Great-circle path points."},
    {"bearing_bin", (PyCFunction)py_mh_bearing_bin, METH_VARARGS | METH_KEYWORDS, "Bearing bin start angle."},
    {"azimuthal_sector", (PyCFunction)py_mh_azimuthal_sector, METH_VARARGS | METH_KEYWORDS, "Azimuthal sector from A to B."},
    {"geodesic_midpoint", py_mh_geodesic_midpoint, METH_VARARGS, "Geodesic midpoint (native WGS84)."},
    {"cover_circle", (PyCFunction)py_mh_cover_circle, METH_VARARGS | METH_KEYWORDS, "Cover circle with grid squares."},
    {"cover_line", (PyCFunction)py_mh_cover_line, METH_VARARGS | METH_KEYWORDS, "Cover line with grid squares."},
    {"json_loads", py_mh_json_loads, METH_VARARGS, "Parse JSON using simdjson when available."},
    {"to_geojson_polygon", py_mh_to_geojson_polygon, METH_VARARGS, "GeoJSON polygon string for locator."},
    {"to_geojson_feature", py_mh_to_geojson_feature, METH_VARARGS, "GeoJSON feature string for locator."},
    {"to_geojson_feature_collection", py_mh_to_geojson_feature_collection, METH_VARARGS, "GeoJSON feature collection string."},
    {"to_geojson_bbox", py_mh_to_geojson_bbox, METH_VARARGS, "GeoJSON bbox array."},
    {"to_geojson_envelope", py_mh_to_geojson_envelope, METH_VARARGS, "GeoJSON envelope string for locator."},
    {"to_wkt", py_mh_to_wkt, METH_VARARGS, "WKT polygon string for locator."},
    {NULL, NULL, 0, NULL},
};

static struct PyModuleDef maidenhead_native_module = {
    PyModuleDef_HEAD_INIT,
    "_native",
    "Native C extension for maidenhead (stub).",
    -1,
    maidenhead_native_methods,
};

PyMODINIT_FUNC PyInit__native(void) {
    PyObject *module = PyModule_Create(&maidenhead_native_module);
    if (!module) {
        return NULL;
    }
    if (mh_import_exceptions() != 0) {
        Py_DECREF(module);
        return NULL;
    }
    return module;
}
