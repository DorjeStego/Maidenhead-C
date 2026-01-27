#ifndef MH_MAIDENHEAD_PYUTILS_H
#define MH_MAIDENHEAD_PYUTILS_H

#include <Python.h>

#include "core.h"

PyObject *mh_list_to_pylist(const mh_list *list);
PyObject *mh_kv_list_to_pydict(const mh_kv_list *list);

#endif
