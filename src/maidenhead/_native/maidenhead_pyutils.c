#include "maidenhead_pyutils.h"

PyObject *mh_list_to_pylist(const mh_list *list) {
    PyObject *out = PyList_New((Py_ssize_t)list->length);
    if (!out) {
        return NULL;
    }
    for (size_t i = 0; i < list->length; i++) {
        PyObject *item = PyUnicode_FromString(list->items[i]);
        if (!item) {
            Py_DECREF(out);
            return NULL;
        }
        PyList_SET_ITEM(out, (Py_ssize_t)i, item);
    }
    return out;
}

PyObject *mh_kv_list_to_pydict(const mh_kv_list *list) {
    PyObject *dict = PyDict_New();
    if (!dict) {
        return NULL;
    }
    for (size_t i = 0; i < list->length; i++) {
        PyObject *key = PyUnicode_FromString(list->keys[i]);
        PyObject *value = PyUnicode_FromString(list->values[i]);
        if (!key || !value) {
            Py_XDECREF(key);
            Py_XDECREF(value);
            Py_DECREF(dict);
            return NULL;
        }
        if (PyDict_SetItem(dict, key, value) != 0) {
            Py_DECREF(key);
            Py_DECREF(value);
            Py_DECREF(dict);
            return NULL;
        }
        Py_DECREF(key);
        Py_DECREF(value);
    }
    return dict;
}
