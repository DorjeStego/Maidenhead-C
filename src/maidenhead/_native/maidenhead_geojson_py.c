#include "maidenhead_geojson_py.h"

#include <string.h>

#include "core.h"
#include "geojson.h"
#include "maidenhead_errors.h"
#include "maidenhead_parse.h"

PyObject *py_mh_to_wkt_many(PyObject *self, PyObject *args) {
    PyObject *locators_obj = NULL;
    if (!PyArg_ParseTuple(args, "O", &locators_obj)) {
        return NULL;
    }
    const char **locators = NULL;
    Py_ssize_t n = 0;
    if (mh_parse_locator_sequence(locators_obj, &locators, &n) != 0) {
        return NULL;
    }
    PyObject *out = PyList_New(n);
    if (!out) {
        PyMem_Free((void *)locators);
        return NULL;
    }
    for (Py_ssize_t i = 0; i < n; i++) {
        mh_bbox bbox;
        mh_error_context err = {MH_OK, NULL, NULL, NULL};
        mh_status st = mh_to_bbox(locators[i], &bbox, &err);
        if (st != MH_OK) {
            PyMem_Free((void *)locators);
            Py_DECREF(out);
            mh_raise_py_error(&err);
            return NULL;
        }
        PyObject *min_lon = PyFloat_FromDouble(bbox.min_lon);
        PyObject *min_lat = PyFloat_FromDouble(bbox.min_lat);
        PyObject *max_lon = PyFloat_FromDouble(bbox.max_lon);
        PyObject *max_lat = PyFloat_FromDouble(bbox.max_lat);
        if (!min_lon || !min_lat || !max_lon || !max_lat) {
            Py_XDECREF(min_lon);
            Py_XDECREF(min_lat);
            Py_XDECREF(max_lon);
            Py_XDECREF(max_lat);
            PyMem_Free((void *)locators);
            Py_DECREF(out);
            return NULL;
        }
        PyObject *item = PyUnicode_FromFormat(
            "POLYGON((%R %R, %R %R, %R %R, %R %R, %R %R))",
            min_lon, min_lat,
            max_lon, min_lat,
            max_lon, max_lat,
            min_lon, max_lat,
            min_lon, min_lat
        );
        Py_DECREF(min_lon);
        Py_DECREF(min_lat);
        Py_DECREF(max_lon);
        Py_DECREF(max_lat);
        if (!item) {
            PyMem_Free((void *)locators);
            Py_DECREF(out);
            return NULL;
        }
        PyList_SET_ITEM(out, i, item);
    }
    PyMem_Free((void *)locators);
    return out;
}

PyObject *py_mh_to_geojson_polygon_many(PyObject *self, PyObject *args) {
    PyObject *items_obj = NULL;
    if (!PyArg_ParseTuple(args, "O", &items_obj)) {
        return NULL;
    }
    PyObject *seq = PySequence_Fast(items_obj, "expected a sequence of locators or points");
    if (!seq) {
        return NULL;
    }
    Py_ssize_t n = PySequence_Fast_GET_SIZE(seq);
    PyObject *out = PyList_New(n);
    if (!out) {
        Py_DECREF(seq);
        return NULL;
    }
    PyObject **items = PySequence_Fast_ITEMS(seq);
    for (Py_ssize_t i = 0; i < n; i++) {
        PyObject *obj = items[i];
        char buf[2048];
        mh_error_context err = {MH_OK, NULL, NULL, NULL};
        mh_status st;
        if (PyUnicode_Check(obj)) {
            const char *locator = PyUnicode_AsUTF8(obj);
            if (!locator) {
                Py_DECREF(seq);
                Py_DECREF(out);
                return NULL;
            }
            st = mh_to_geojson_polygon(locator, buf, sizeof(buf), &err);
        } else {
            mh_point pt;
            if (mh_parse_point(obj, &pt) != 0) {
                Py_DECREF(seq);
                Py_DECREF(out);
                return NULL;
            }
            st = mh_to_geojson_point(pt.lat, pt.lon, buf, sizeof(buf), &err);
        }
        if (st != MH_OK) {
            Py_DECREF(seq);
            Py_DECREF(out);
            mh_raise_py_error(&err);
            return NULL;
        }
        PyObject *item = PyUnicode_FromString(buf);
        if (!item) {
            Py_DECREF(seq);
            Py_DECREF(out);
            return NULL;
        }
        PyList_SET_ITEM(out, i, item);
    }
    Py_DECREF(seq);
    return out;
}

PyObject *py_mh_to_geojson_feature_many(PyObject *self, PyObject *args) {
    PyObject *items_obj = NULL;
    if (!PyArg_ParseTuple(args, "O", &items_obj)) {
        return NULL;
    }
    PyObject *seq = PySequence_Fast(items_obj, "expected a sequence of locators or points");
    if (!seq) {
        return NULL;
    }
    Py_ssize_t n = PySequence_Fast_GET_SIZE(seq);
    PyObject *out = PyList_New(n);
    if (!out) {
        Py_DECREF(seq);
        return NULL;
    }
    PyObject **items = PySequence_Fast_ITEMS(seq);
    for (Py_ssize_t i = 0; i < n; i++) {
        PyObject *obj = items[i];
        char buf[2048];
        mh_error_context err = {MH_OK, NULL, NULL, NULL};
        mh_status st;
        if (PyUnicode_Check(obj)) {
            const char *locator = PyUnicode_AsUTF8(obj);
            if (!locator) {
                Py_DECREF(seq);
                Py_DECREF(out);
                return NULL;
            }
            st = mh_to_geojson_feature(locator, buf, sizeof(buf), &err);
        } else {
            mh_point pt;
            if (mh_parse_point(obj, &pt) != 0) {
                Py_DECREF(seq);
                Py_DECREF(out);
                return NULL;
            }
            st = mh_to_geojson_feature_point(pt.lat, pt.lon, buf, sizeof(buf), &err);
        }
        if (st != MH_OK) {
            Py_DECREF(seq);
            Py_DECREF(out);
            mh_raise_py_error(&err);
            return NULL;
        }
        PyObject *item = PyUnicode_FromString(buf);
        if (!item) {
            Py_DECREF(seq);
            Py_DECREF(out);
            return NULL;
        }
        PyList_SET_ITEM(out, i, item);
    }
    Py_DECREF(seq);
    return out;
}

PyObject *py_mh_to_geojson_bbox_many(PyObject *self, PyObject *args) {
    PyObject *items_obj = NULL;
    if (!PyArg_ParseTuple(args, "O", &items_obj)) {
        return NULL;
    }
    PyObject *seq = PySequence_Fast(items_obj, "expected a sequence of locators or points");
    if (!seq) {
        return NULL;
    }
    Py_ssize_t n = PySequence_Fast_GET_SIZE(seq);
    PyObject *out = PyList_New(n);
    if (!out) {
        Py_DECREF(seq);
        return NULL;
    }
    PyObject **items = PySequence_Fast_ITEMS(seq);
    for (Py_ssize_t i = 0; i < n; i++) {
        PyObject *obj = items[i];
        double bbox[4];
        mh_error_context err = {MH_OK, NULL, NULL, NULL};
        mh_status st;
        if (PyUnicode_Check(obj)) {
            const char *locator = PyUnicode_AsUTF8(obj);
            if (!locator) {
                Py_DECREF(seq);
                Py_DECREF(out);
                return NULL;
            }
            st = mh_to_geojson_bbox(locator, bbox, &err);
        } else {
            mh_point pt;
            if (mh_parse_point(obj, &pt) != 0) {
                Py_DECREF(seq);
                Py_DECREF(out);
                return NULL;
            }
            st = mh_to_geojson_bbox_point(pt.lat, pt.lon, bbox, &err);
        }
        if (st != MH_OK) {
            Py_DECREF(seq);
            Py_DECREF(out);
            mh_raise_py_error(&err);
            return NULL;
        }
        PyObject *item = PyList_New(4);
        if (!item) {
            Py_DECREF(seq);
            Py_DECREF(out);
            return NULL;
        }
        for (int j = 0; j < 4; j++) {
            PyList_SET_ITEM(item, j, PyFloat_FromDouble(bbox[j]));
        }
        PyList_SET_ITEM(out, i, item);
    }
    Py_DECREF(seq);
    return out;
}

PyObject *py_mh_to_geojson_envelope_many(PyObject *self, PyObject *args) {
    PyObject *items_obj = NULL;
    if (!PyArg_ParseTuple(args, "O", &items_obj)) {
        return NULL;
    }
    PyObject *seq = PySequence_Fast(items_obj, "expected a sequence of locators or points");
    if (!seq) {
        return NULL;
    }
    Py_ssize_t n = PySequence_Fast_GET_SIZE(seq);
    PyObject *out = PyList_New(n);
    if (!out) {
        Py_DECREF(seq);
        return NULL;
    }
    PyObject **items = PySequence_Fast_ITEMS(seq);
    for (Py_ssize_t i = 0; i < n; i++) {
        PyObject *obj = items[i];
        char buf[2048];
        mh_error_context err = {MH_OK, NULL, NULL, NULL};
        mh_status st;
        if (PyUnicode_Check(obj)) {
            const char *locator = PyUnicode_AsUTF8(obj);
            if (!locator) {
                Py_DECREF(seq);
                Py_DECREF(out);
                return NULL;
            }
            st = mh_to_geojson_envelope(locator, buf, sizeof(buf), &err);
        } else {
            mh_point pt;
            if (mh_parse_point(obj, &pt) != 0) {
                Py_DECREF(seq);
                Py_DECREF(out);
                return NULL;
            }
            st = mh_to_geojson_envelope_point(pt.lat, pt.lon, buf, sizeof(buf), &err);
        }
        if (st != MH_OK) {
            Py_DECREF(seq);
            Py_DECREF(out);
            mh_raise_py_error(&err);
            return NULL;
        }
        PyObject *item = PyUnicode_FromString(buf);
        if (!item) {
            Py_DECREF(seq);
            Py_DECREF(out);
            return NULL;
        }
        PyList_SET_ITEM(out, i, item);
    }
    Py_DECREF(seq);
    return out;
}

static PyObject *py_mh_geojson_with_buffer(
    mh_status (*fn)(const char *, char *, size_t, mh_error_context *),
    const char *locator
) {
    size_t cap = 2048;
    for (int attempt = 0; attempt < 4; attempt++) {
        char *buf = (char *)PyMem_Malloc(cap);
        if (!buf) {
            return PyErr_NoMemory();
        }
        mh_error_context err = {MH_OK, NULL, NULL, NULL};
        mh_status st = fn(locator, buf, cap, &err);
        if (st == MH_OK) {
            PyObject *out = PyUnicode_FromString(buf);
            PyMem_Free(buf);
            return out;
        }
        PyMem_Free(buf);
        if (err.code == MH_ERR_INTERNAL && err.message && strcmp(err.message, "output buffer too small") == 0) {
            cap *= 2;
            continue;
        }
        mh_raise_py_error(&err);
        return NULL;
    }
    PyErr_SetString(PyExc_RuntimeError, "output buffer too small");
    return NULL;
}

PyObject *py_mh_to_geojson_polygon(PyObject *self, PyObject *args) {
    PyObject *obj = NULL;
    if (!PyArg_ParseTuple(args, "O", &obj)) {
        return NULL;
    }
    if (PyUnicode_Check(obj)) {
        const char *locator = PyUnicode_AsUTF8(obj);
        if (!locator) {
            return NULL;
        }
        return py_mh_geojson_with_buffer(mh_to_geojson_polygon, locator);
    }
    mh_point pt;
    if (mh_parse_point(obj, &pt) != 0) {
        return NULL;
    }
    size_t cap = 256;
    for (int attempt = 0; attempt < 4; attempt++) {
        char *buf = (char *)PyMem_Malloc(cap);
        if (!buf) {
            return PyErr_NoMemory();
        }
        mh_error_context err = {MH_OK, NULL, NULL, NULL};
        mh_status st = mh_to_geojson_point(pt.lat, pt.lon, buf, cap, &err);
        if (st == MH_OK) {
            PyObject *out = PyUnicode_FromString(buf);
            PyMem_Free(buf);
            return out;
        }
        PyMem_Free(buf);
        if (err.code == MH_ERR_INTERNAL && err.message && strcmp(err.message, "output buffer too small") == 0) {
            cap *= 2;
            continue;
        }
        mh_raise_py_error(&err);
        return NULL;
    }
    PyErr_SetString(PyExc_RuntimeError, "output buffer too small");
    return NULL;
}

PyObject *py_mh_to_geojson_feature(PyObject *self, PyObject *args) {
    PyObject *obj = NULL;
    if (!PyArg_ParseTuple(args, "O", &obj)) {
        return NULL;
    }
    if (PyUnicode_Check(obj)) {
        const char *locator = PyUnicode_AsUTF8(obj);
        if (!locator) {
            return NULL;
        }
        return py_mh_geojson_with_buffer(mh_to_geojson_feature, locator);
    }
    mh_point pt;
    if (mh_parse_point(obj, &pt) != 0) {
        return NULL;
    }
    size_t cap = 256;
    for (int attempt = 0; attempt < 4; attempt++) {
        char *buf = (char *)PyMem_Malloc(cap);
        if (!buf) {
            return PyErr_NoMemory();
        }
        mh_error_context err = {MH_OK, NULL, NULL, NULL};
        mh_status st = mh_to_geojson_feature_point(pt.lat, pt.lon, buf, cap, &err);
        if (st == MH_OK) {
            PyObject *out = PyUnicode_FromString(buf);
            PyMem_Free(buf);
            return out;
        }
        PyMem_Free(buf);
        if (err.code == MH_ERR_INTERNAL && err.message && strcmp(err.message, "output buffer too small") == 0) {
            cap *= 2;
            continue;
        }
        mh_raise_py_error(&err);
        return NULL;
    }
    PyErr_SetString(PyExc_RuntimeError, "output buffer too small");
    return NULL;
}

PyObject *py_mh_to_geojson_envelope(PyObject *self, PyObject *args) {
    PyObject *obj = NULL;
    if (!PyArg_ParseTuple(args, "O", &obj)) {
        return NULL;
    }
    if (PyUnicode_Check(obj)) {
        const char *locator = PyUnicode_AsUTF8(obj);
        if (!locator) {
            return NULL;
        }
        return py_mh_geojson_with_buffer(mh_to_geojson_envelope, locator);
    }
    mh_point pt;
    if (mh_parse_point(obj, &pt) != 0) {
        return NULL;
    }
    size_t cap = 256;
    for (int attempt = 0; attempt < 4; attempt++) {
        char *buf = (char *)PyMem_Malloc(cap);
        if (!buf) {
            return PyErr_NoMemory();
        }
        mh_error_context err = {MH_OK, NULL, NULL, NULL};
        mh_status st = mh_to_geojson_envelope_point(pt.lat, pt.lon, buf, cap, &err);
        if (st == MH_OK) {
            PyObject *out = PyUnicode_FromString(buf);
            PyMem_Free(buf);
            return out;
        }
        PyMem_Free(buf);
        if (err.code == MH_ERR_INTERNAL && err.message && strcmp(err.message, "output buffer too small") == 0) {
            cap *= 2;
            continue;
        }
        mh_raise_py_error(&err);
        return NULL;
    }
    PyErr_SetString(PyExc_RuntimeError, "output buffer too small");
    return NULL;
}

PyObject *py_mh_to_geojson_feature_collection(PyObject *self, PyObject *args) {
    PyObject *seq_obj = NULL;
    if (!PyArg_ParseTuple(args, "O", &seq_obj)) {
        return NULL;
    }
    PyObject *seq = PySequence_Fast(seq_obj, "locators must be a sequence");
    if (!seq) {
        return NULL;
    }
    Py_ssize_t n = PySequence_Fast_GET_SIZE(seq);
    if (n < 0) {
        Py_DECREF(seq);
        return NULL;
    }
    const char **locators = (const char **)PyMem_Malloc(sizeof(char *) * (size_t)n);
    if (!locators) {
        Py_DECREF(seq);
        return PyErr_NoMemory();
    }
    PyObject **items = PySequence_Fast_ITEMS(seq);
    for (Py_ssize_t i = 0; i < n; i++) {
        const char *loc = PyUnicode_AsUTF8(items[i]);
        if (!loc) {
            PyMem_Free(locators);
            Py_DECREF(seq);
            return NULL;
        }
        locators[i] = loc;
    }
    Py_DECREF(seq);

    size_t cap = 512 + (size_t)n * 512;
    for (int attempt = 0; attempt < 4; attempt++) {
        char *buf = (char *)PyMem_Malloc(cap);
        if (!buf) {
            PyMem_Free(locators);
            return PyErr_NoMemory();
        }
        mh_error_context err = {MH_OK, NULL, NULL, NULL};
        mh_status st = mh_to_geojson_feature_collection(locators, (size_t)n, buf, cap, &err);
        if (st == MH_OK) {
            PyObject *out = PyUnicode_FromString(buf);
            PyMem_Free(buf);
            PyMem_Free(locators);
            return out;
        }
        PyMem_Free(buf);
        if (err.code == MH_ERR_INTERNAL && err.message && strcmp(err.message, "output buffer too small") == 0) {
            cap *= 2;
            continue;
        }
        mh_raise_py_error(&err);
        PyMem_Free(locators);
        return NULL;
    }
    PyMem_Free(locators);
    PyErr_SetString(PyExc_RuntimeError, "output buffer too small");
    return NULL;
}

PyObject *py_mh_to_geojson_bbox(PyObject *self, PyObject *args) {
    PyObject *obj = NULL;
    if (!PyArg_ParseTuple(args, "O", &obj)) {
        return NULL;
    }
    double out_bbox[4];
    mh_error_context err = {MH_OK, NULL, NULL, NULL};
    mh_status st;
    if (PyUnicode_Check(obj)) {
        const char *locator = PyUnicode_AsUTF8(obj);
        if (!locator) {
            return NULL;
        }
        st = mh_to_geojson_bbox(locator, out_bbox, &err);
    } else {
        mh_point pt;
        if (mh_parse_point(obj, &pt) != 0) {
            return NULL;
        }
        st = mh_to_geojson_bbox_point(pt.lat, pt.lon, out_bbox, &err);
    }
    if (st != MH_OK) {
        mh_raise_py_error(&err);
        return NULL;
    }
    PyObject *list = PyList_New(4);
    if (!list) {
        return NULL;
    }
    for (int i = 0; i < 4; i++) {
        PyObject *val = PyFloat_FromDouble(out_bbox[i]);
        if (!val) {
            Py_DECREF(list);
            return NULL;
        }
        PyList_SET_ITEM(list, i, val);
    }
    return list;
}

PyObject *py_mh_to_wkt(PyObject *self, PyObject *args) {
    PyObject *obj = NULL;
    if (!PyArg_ParseTuple(args, "O", &obj)) {
        return NULL;
    }
    if (PyUnicode_Check(obj)) {
        const char *locator = PyUnicode_AsUTF8(obj);
        if (!locator) {
            return NULL;
        }
        mh_bbox bbox;
        mh_error_context err = {MH_OK, NULL, NULL, NULL};
        mh_status st = mh_to_bbox(locator, &bbox, &err);
        if (st != MH_OK) {
            mh_raise_py_error(&err);
            return NULL;
        }
        PyObject *min_lon = PyFloat_FromDouble(bbox.min_lon);
        PyObject *min_lat = PyFloat_FromDouble(bbox.min_lat);
        PyObject *max_lon = PyFloat_FromDouble(bbox.max_lon);
        PyObject *max_lat = PyFloat_FromDouble(bbox.max_lat);
        if (!min_lon || !min_lat || !max_lon || !max_lat) {
            Py_XDECREF(min_lon);
            Py_XDECREF(min_lat);
            Py_XDECREF(max_lon);
            Py_XDECREF(max_lat);
            return NULL;
        }
        PyObject *out = PyUnicode_FromFormat(
            "POLYGON((%R %R, %R %R, %R %R, %R %R, %R %R))",
            min_lon, min_lat,
            max_lon, min_lat,
            max_lon, max_lat,
            min_lon, max_lat,
            min_lon, min_lat
        );
        Py_DECREF(min_lon);
        Py_DECREF(min_lat);
        Py_DECREF(max_lon);
        Py_DECREF(max_lat);
        return out;
    }

    mh_point pt;
    if (mh_parse_point(obj, &pt) != 0) {
        return NULL;
    }
    PyObject *lon = PyFloat_FromDouble(pt.lon);
    PyObject *lat = PyFloat_FromDouble(pt.lat);
    if (!lon || !lat) {
        Py_XDECREF(lon);
        Py_XDECREF(lat);
        return NULL;
    }
    PyObject *out = PyUnicode_FromFormat("POINT(%R %R)", lon, lat);
    Py_DECREF(lon);
    Py_DECREF(lat);
    return out;
}
