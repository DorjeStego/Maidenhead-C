#ifndef MH_MAIDENHEAD_GEO_PY_H
#define MH_MAIDENHEAD_GEO_PY_H

#include <Python.h>

PyObject *py_mh_azimuth_many(PyObject *self, PyObject *args, PyObject *kwargs);
PyObject *py_mh_initial_bearing_many(PyObject *self, PyObject *args);
PyObject *py_mh_distance_km(PyObject *self, PyObject *args, PyObject *kwargs);
PyObject *py_mh_bearing_deg(PyObject *self, PyObject *args);
PyObject *py_mh_midpoint(PyObject *self, PyObject *args);
PyObject *py_mh_great_circle_path(PyObject *self, PyObject *args, PyObject *kwargs);
PyObject *py_mh_bearing_bin(PyObject *self, PyObject *args, PyObject *kwargs);
PyObject *py_mh_azimuthal_sector(PyObject *self, PyObject *args, PyObject *kwargs);
PyObject *py_mh_geodesic_midpoint(PyObject *self, PyObject *args);
PyObject *py_mh_cover_circle(PyObject *self, PyObject *args, PyObject *kwargs);
PyObject *py_mh_cover_line(PyObject *self, PyObject *args, PyObject *kwargs);

#endif
