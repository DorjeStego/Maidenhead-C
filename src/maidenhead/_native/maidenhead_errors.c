#include "maidenhead_errors.h"

#include <string.h>

static PyObject *mh_exc_maidenhead = NULL;
static PyObject *mh_exc_precision = NULL;
static PyObject *mh_exc_invalid_locator = NULL;
static PyObject *mh_exc_out_of_range = NULL;
static PyObject *mh_exc_unsupported = NULL;
static PyObject *mh_exc_missing_dep = NULL;

int mh_import_exceptions(void) {
    PyObject *mod = PyImport_ImportModule("maidenhead.errors");
    if (!mod) {
        return -1;
    }

    mh_exc_maidenhead = PyObject_GetAttrString(mod, "MaidenheadError");
    mh_exc_precision = PyObject_GetAttrString(mod, "PrecisionError");
    mh_exc_invalid_locator = PyObject_GetAttrString(mod, "InvalidLocatorError");
    mh_exc_out_of_range = PyObject_GetAttrString(mod, "OutOfRangeError");
    mh_exc_unsupported = PyObject_GetAttrString(mod, "UnsupportedError");
    mh_exc_missing_dep = PyObject_GetAttrString(mod, "MissingDependencyError");

    Py_DECREF(mod);

    if (!mh_exc_maidenhead || !mh_exc_precision || !mh_exc_invalid_locator ||
        !mh_exc_out_of_range || !mh_exc_unsupported || !mh_exc_missing_dep) {
        return -1;
    }
    return 0;
}

PyObject *mh_error_type_for_status(mh_status code) {
    switch (code) {
        case MH_ERR_PRECISION:
            return mh_exc_precision;
        case MH_ERR_INVALID_LOCATOR:
            return mh_exc_invalid_locator;
        case MH_ERR_OUT_OF_RANGE:
            return mh_exc_out_of_range;
        case MH_ERR_UNSUPPORTED:
            return mh_exc_unsupported;
        case MH_ERR_MISSING_DEP:
            return mh_exc_missing_dep;
        case MH_ERR_INTERNAL:
        default:
            return mh_exc_maidenhead;
    }
}

int mh_raise_py_error(const mh_error_context *err) {
    const char *message = "native error";
    PyObject *exc_type = mh_exc_maidenhead;

    if (err) {
        if (err->message) {
            message = err->message;
        }
        exc_type = mh_error_type_for_status(err->code);
    }

    if (!exc_type) {
        PyErr_SetString(PyExc_RuntimeError, message);
        return -1;
    }
    PyErr_SetString(exc_type, message);
    return -1;
}

PyObject *mh_missing_dep_exception(void) {
    return mh_exc_missing_dep;
}
