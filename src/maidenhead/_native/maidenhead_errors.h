#ifndef MH_MAIDENHEAD_ERRORS_H
#define MH_MAIDENHEAD_ERRORS_H

#include <Python.h>

#include "errors.h"

int mh_import_exceptions(void);
PyObject *mh_error_type_for_status(mh_status code);
int mh_raise_py_error(const mh_error_context *err);
PyObject *mh_missing_dep_exception(void);

#endif
