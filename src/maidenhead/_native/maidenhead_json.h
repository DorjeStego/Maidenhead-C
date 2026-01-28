#ifndef MH_MAIDENHEAD_JSON_H
#define MH_MAIDENHEAD_JSON_H

#include <Python.h>

PyObject *py_mh_json_loads(PyObject *self, PyObject *args);

#ifdef MH_HAVE_SIMDJSON
PyObject *py_mh_json_loads_simdjson(PyObject *self, PyObject *args);
#endif

#endif
