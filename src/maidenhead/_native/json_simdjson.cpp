#include <Python.h>
#include <climits>
#include <string_view>

#ifdef MH_HAVE_SIMDJSON
#include <simdjson.h>
#endif

#include "errors.h"

static PyObject *mh_missing_dep_error(const char *message) {
    PyObject *mod = PyImport_ImportModule("maidenhead.errors");
    if (!mod) {
        PyErr_SetString(PyExc_RuntimeError, message);
        return NULL;
    }
    PyObject *exc = PyObject_GetAttrString(mod, "MissingDependencyError");
    Py_DECREF(mod);
    if (!exc) {
        PyErr_SetString(PyExc_RuntimeError, message);
        return NULL;
    }
    PyErr_SetString(exc, message);
    Py_DECREF(exc);
    return NULL;
}

#ifdef MH_HAVE_SIMDJSON
static PyObject *mh_simdjson_to_py(const simdjson::dom::element &el);

static PyObject *mh_simdjson_to_py(const simdjson::dom::element &el) {
    using simdjson::dom::element_type;
    switch (el.type()) {
        case element_type::ARRAY: {
            simdjson::dom::array arr = el.get_array();
            PyObject *list = PyList_New(0);
            if (!list) {
                return NULL;
            }
            for (simdjson::dom::element child : arr) {
                PyObject *item = mh_simdjson_to_py(child);
                if (!item) {
                    Py_DECREF(list);
                    return NULL;
                }
                if (PyList_Append(list, item) != 0) {
                    Py_DECREF(item);
                    Py_DECREF(list);
                    return NULL;
                }
                Py_DECREF(item);
            }
            return list;
        }
        case element_type::OBJECT: {
            simdjson::dom::object obj = el.get_object();
            PyObject *dict = PyDict_New();
            if (!dict) {
                return NULL;
            }
            for (auto field : obj) {
                std::string_view key = field.key;
                PyObject *py_key = PyUnicode_FromStringAndSize(key.data(), (Py_ssize_t)key.size());
                if (!py_key) {
                    Py_DECREF(dict);
                    return NULL;
                }
                PyObject *py_val = mh_simdjson_to_py(field.value);
                if (!py_val) {
                    Py_DECREF(py_key);
                    Py_DECREF(dict);
                    return NULL;
                }
                if (PyDict_SetItem(dict, py_key, py_val) != 0) {
                    Py_DECREF(py_key);
                    Py_DECREF(py_val);
                    Py_DECREF(dict);
                    return NULL;
                }
                Py_DECREF(py_key);
                Py_DECREF(py_val);
            }
            return dict;
        }
        case element_type::INT64: {
            int64_t val = int64_t(el);
            return PyLong_FromLongLong(val);
        }
        case element_type::UINT64: {
            uint64_t val = uint64_t(el);
            if (val <= (uint64_t)LLONG_MAX) {
                return PyLong_FromLongLong((long long)val);
            }
            return PyLong_FromUnsignedLongLong(val);
        }
        case element_type::DOUBLE: {
            double val = double(el);
            return PyFloat_FromDouble(val);
        }
        case element_type::STRING: {
            std::string_view val = el.get_string();
            return PyUnicode_FromStringAndSize(val.data(), (Py_ssize_t)val.size());
        }
        case element_type::BOOL: {
            bool val = bool(el);
            return PyBool_FromLong(val ? 1 : 0);
        }
        case element_type::NULL_VALUE:
        default:
            Py_RETURN_NONE;
    }
}
#endif

extern "C" PyObject *py_mh_json_loads(PyObject *self, PyObject *args) {
    PyObject *obj = NULL;
    if (!PyArg_ParseTuple(args, "O", &obj)) {
        return NULL;
    }
#ifdef MH_HAVE_SIMDJSON
    const char *buf = NULL;
    Py_ssize_t len = 0;
    if (PyBytes_Check(obj)) {
        if (PyBytes_AsStringAndSize(obj, (char **)&buf, &len) != 0) {
            return NULL;
        }
    } else {
        buf = PyUnicode_AsUTF8AndSize(obj, &len);
        if (!buf) {
            return NULL;
        }
    }
    simdjson::padded_string json(buf, (size_t)len);
    simdjson::dom::parser parser;
    simdjson::dom::element element;
    auto error = parser.parse(json).get(element);
    if (error) {
        PyErr_Format(PyExc_ValueError, "invalid json: %s", simdjson::error_message(error));
        return NULL;
    }
    return mh_simdjson_to_py(element);
#else
    (void)self;
    PyObject *json_mod = PyImport_ImportModule("json");
    if (!json_mod) {
        return mh_missing_dep_error("native json parsing requires simdjson");
    }
    PyObject *loads = PyObject_GetAttrString(json_mod, "loads");
    Py_DECREF(json_mod);
    if (!loads) {
        return mh_missing_dep_error("native json parsing requires simdjson");
    }
    PyObject *call_args = PyTuple_Pack(1, obj);
    if (!call_args) {
        Py_DECREF(loads);
        return NULL;
    }
    PyObject *result = PyObject_CallObject(loads, call_args);
    Py_DECREF(call_args);
    Py_DECREF(loads);
    return result;
#endif
}
