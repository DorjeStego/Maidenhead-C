#ifndef MH_MAIDENHEAD_GEOJSON_PY_H
#define MH_MAIDENHEAD_GEOJSON_PY_H

#include <Python.h>

PyObject *py_mh_to_wkt_many(PyObject *self, PyObject *args);
PyObject *py_mh_to_geojson_polygon_many(PyObject *self, PyObject *args);
PyObject *py_mh_to_geojson_feature_many(PyObject *self, PyObject *args);
PyObject *py_mh_to_geojson_bbox_many(PyObject *self, PyObject *args);
PyObject *py_mh_to_geojson_envelope_many(PyObject *self, PyObject *args);

PyObject *py_mh_to_geojson_polygon(PyObject *self, PyObject *args);
PyObject *py_mh_to_geojson_feature(PyObject *self, PyObject *args);
PyObject *py_mh_to_geojson_envelope(PyObject *self, PyObject *args);
PyObject *py_mh_to_geojson_feature_collection(PyObject *self, PyObject *args);
PyObject *py_mh_to_geojson_bbox(PyObject *self, PyObject *args);
PyObject *py_mh_to_wkt(PyObject *self, PyObject *args);

#endif
