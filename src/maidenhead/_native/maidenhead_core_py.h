#ifndef MH_MAIDENHEAD_CORE_PY_H
#define MH_MAIDENHEAD_CORE_PY_H

#include <Python.h>

PyObject *py_mh_normalize(PyObject *self, PyObject *args);
PyObject *py_mh_normalize_many(PyObject *self, PyObject *args);
PyObject *py_mh_precision_of(PyObject *self, PyObject *args);
PyObject *py_mh_to_bbox(PyObject *self, PyObject *args);
PyObject *py_mh_to_center_latlon(PyObject *self, PyObject *args);
PyObject *py_mh_from_latlon(PyObject *self, PyObject *args, PyObject *kwargs);
PyObject *py_mh_format_locator(PyObject *self, PyObject *args, PyObject *kwargs);
PyObject *py_mh_to_bbox_split(PyObject *self, PyObject *args);
PyObject *py_mh_split_bbox_list(PyObject *self, PyObject *args);
PyObject *py_mh_parse(PyObject *self, PyObject *args);
PyObject *py_mh_corners(PyObject *self, PyObject *args);
PyObject *py_mh_cell_size_deg(PyObject *self, PyObject *args);
PyObject *py_mh_cell_size_km(PyObject *self, PyObject *args, PyObject *kwargs);
PyObject *py_mh_area_km2(PyObject *self, PyObject *args, PyObject *kwargs);
PyObject *py_mh_diagonal_km(PyObject *self, PyObject *args, PyObject *kwargs);
PyObject *py_mh_step(PyObject *self, PyObject *args, PyObject *kwargs);
PyObject *py_mh_contains_point(PyObject *self, PyObject *args);
PyObject *py_mh_contains(PyObject *self, PyObject *args);
PyObject *py_mh_intersects_bbox(PyObject *self, PyObject *args);
PyObject *py_mh_intersects_polygon(PyObject *self, PyObject *args);
PyObject *py_mh_to_utm_zone(PyObject *self, PyObject *args);
PyObject *py_mh_to_utm_zone_many(PyObject *self, PyObject *args);
PyObject *py_mh_contains_many(PyObject *self, PyObject *args);
PyObject *py_mh_contains_point_many(PyObject *self, PyObject *args);
PyObject *py_mh_intersects_bbox_many(PyObject *self, PyObject *args);
PyObject *py_mh_intersects_polygon_many(PyObject *self, PyObject *args);
PyObject *py_mh_corners_many(PyObject *self, PyObject *args);
PyObject *py_mh_parent(PyObject *self, PyObject *args, PyObject *kwargs);
PyObject *py_mh_children(PyObject *self, PyObject *args, PyObject *kwargs);
PyObject *py_mh_neighbors(PyObject *self, PyObject *args, PyObject *kwargs);
PyObject *py_mh_neighbors_many(PyObject *self, PyObject *args, PyObject *kwargs);
PyObject *py_mh_split_bbox(PyObject *self, PyObject *args);
PyObject *py_mh_from_latlon_many(PyObject *self, PyObject *args, PyObject *kwargs);
PyObject *py_mh_to_center_many(PyObject *self, PyObject *args);
PyObject *py_mh_to_bbox_many(PyObject *self, PyObject *args);
PyObject *py_mh_adjacent(PyObject *self, PyObject *args, PyObject *kwargs);
PyObject *py_mh_adjacent_many(PyObject *self, PyObject *args, PyObject *kwargs);
PyObject *py_mh_parent_many(PyObject *self, PyObject *args, PyObject *kwargs);
PyObject *py_mh_children_many(PyObject *self, PyObject *args, PyObject *kwargs);
PyObject *py_mh_cell_size_deg_many(PyObject *self, PyObject *args);
PyObject *py_mh_cell_size_km_many(PyObject *self, PyObject *args, PyObject *kwargs);
PyObject *py_mh_area_km2_many(PyObject *self, PyObject *args, PyObject *kwargs);
PyObject *py_mh_diagonal_km_many(PyObject *self, PyObject *args, PyObject *kwargs);
PyObject *py_mh_split_bbox_many(PyObject *self, PyObject *args);

#endif
