#include "maidenhead_json.h"

#include "maidenhead_errors.h"

PyObject *py_mh_json_loads(PyObject *self, PyObject *args) {
#ifdef MH_HAVE_SIMDJSON
    return py_mh_json_loads_simdjson(self, args);
#else
    (void)self;
    (void)args;
    PyObject *missing_dep = mh_missing_dep_exception();
    if (missing_dep) {
        PyErr_SetString(missing_dep, "simdjson not available");
    } else {
        PyErr_SetString(PyExc_ImportError, "simdjson not available");
    }
    return NULL;
#endif
}
