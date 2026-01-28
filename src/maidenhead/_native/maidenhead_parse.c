#include "maidenhead_parse.h"

int mh_parse_double_sequence(PyObject *obj, double **out_vals, Py_ssize_t *out_len) {
    PyObject *seq = PySequence_Fast(obj, "expected a sequence of numbers");
    if (!seq) {
        return -1;
    }
    Py_ssize_t n = PySequence_Fast_GET_SIZE(seq);
    double *vals = (double *)PyMem_Malloc((size_t)n * sizeof(double));
    if (!vals) {
        Py_DECREF(seq);
        PyErr_NoMemory();
        return -1;
    }
    PyObject **items = PySequence_Fast_ITEMS(seq);
    for (Py_ssize_t i = 0; i < n; i++) {
        double v = PyFloat_AsDouble(items[i]);
        if (PyErr_Occurred()) {
            PyMem_Free(vals);
            Py_DECREF(seq);
            return -1;
        }
        vals[i] = v;
    }
    Py_DECREF(seq);
    *out_vals = vals;
    *out_len = n;
    return 0;
}

int mh_parse_locator_sequence(PyObject *obj, const char ***out_vals, Py_ssize_t *out_len) {
    PyObject *seq = PySequence_Fast(obj, "expected a sequence of locators");
    if (!seq) {
        return -1;
    }
    Py_ssize_t n = PySequence_Fast_GET_SIZE(seq);
    const char **vals = (const char **)PyMem_Malloc((size_t)n * sizeof(char *));
    if (!vals) {
        Py_DECREF(seq);
        PyErr_NoMemory();
        return -1;
    }
    PyObject **items = PySequence_Fast_ITEMS(seq);
    for (Py_ssize_t i = 0; i < n; i++) {
        if (!PyUnicode_Check(items[i])) {
            PyMem_Free(vals);
            Py_DECREF(seq);
            PyErr_SetString(PyExc_TypeError, "locators must be strings");
            return -1;
        }
        const char *s = PyUnicode_AsUTF8(items[i]);
        if (!s) {
            PyMem_Free(vals);
            Py_DECREF(seq);
            return -1;
        }
        vals[i] = s;
    }
    Py_DECREF(seq);
    *out_vals = vals;
    *out_len = n;
    return 0;
}

int mh_parse_bbox_sequence(PyObject *obj, mh_bbox **out_bboxes, Py_ssize_t *out_len) {
    PyObject *seq = PySequence_Fast(obj, "expected a sequence of bboxes");
    if (!seq) {
        return -1;
    }
    Py_ssize_t n = PySequence_Fast_GET_SIZE(seq);
    mh_bbox *bboxes = (mh_bbox *)PyMem_Malloc((size_t)n * sizeof(mh_bbox));
    if (!bboxes) {
        Py_DECREF(seq);
        PyErr_NoMemory();
        return -1;
    }
    PyObject **items = PySequence_Fast_ITEMS(seq);
    for (Py_ssize_t i = 0; i < n; i++) {
        PyObject *item = items[i];
        if (!PyTuple_Check(item) && !PyList_Check(item)) {
            PyMem_Free(bboxes);
            Py_DECREF(seq);
            PyErr_SetString(PyExc_ValueError, "bbox must be (min_lat, min_lon, max_lat, max_lon)");
            return -1;
        }
        if (PySequence_Size(item) != 4) {
            PyMem_Free(bboxes);
            Py_DECREF(seq);
            PyErr_SetString(PyExc_ValueError, "bbox must be (min_lat, min_lon, max_lat, max_lon)");
            return -1;
        }
        PyObject *min_lat_obj = PySequence_GetItem(item, 0);
        PyObject *min_lon_obj = PySequence_GetItem(item, 1);
        PyObject *max_lat_obj = PySequence_GetItem(item, 2);
        PyObject *max_lon_obj = PySequence_GetItem(item, 3);
        if (!min_lat_obj || !min_lon_obj || !max_lat_obj || !max_lon_obj) {
            Py_XDECREF(min_lat_obj);
            Py_XDECREF(min_lon_obj);
            Py_XDECREF(max_lat_obj);
            Py_XDECREF(max_lon_obj);
            PyMem_Free(bboxes);
            Py_DECREF(seq);
            return -1;
        }
        double min_lat = PyFloat_AsDouble(min_lat_obj);
        double min_lon = PyFloat_AsDouble(min_lon_obj);
        double max_lat = PyFloat_AsDouble(max_lat_obj);
        double max_lon = PyFloat_AsDouble(max_lon_obj);
        Py_DECREF(min_lat_obj);
        Py_DECREF(min_lon_obj);
        Py_DECREF(max_lat_obj);
        Py_DECREF(max_lon_obj);
        if (PyErr_Occurred()) {
            PyMem_Free(bboxes);
            Py_DECREF(seq);
            return -1;
        }
        bboxes[i].min_lat = min_lat;
        bboxes[i].min_lon = min_lon;
        bboxes[i].max_lat = max_lat;
        bboxes[i].max_lon = max_lon;
    }
    Py_DECREF(seq);
    *out_bboxes = bboxes;
    *out_len = n;
    return 0;
}

int mh_parse_point(PyObject *obj, mh_point *out) {
    if (!PyTuple_Check(obj) && !PyList_Check(obj)) {
        PyErr_SetString(PyExc_ValueError, "point must be (lat, lon)");
        return -1;
    }
    if (PySequence_Size(obj) != 2) {
        PyErr_SetString(PyExc_ValueError, "point must be (lat, lon)");
        return -1;
    }
    PyObject *lat_obj = PySequence_GetItem(obj, 0);
    PyObject *lon_obj = PySequence_GetItem(obj, 1);
    if (!lat_obj || !lon_obj) {
        Py_XDECREF(lat_obj);
        Py_XDECREF(lon_obj);
        return -1;
    }
    double lat = PyFloat_AsDouble(lat_obj);
    double lon = PyFloat_AsDouble(lon_obj);
    Py_DECREF(lat_obj);
    Py_DECREF(lon_obj);
    if (PyErr_Occurred()) {
        return -1;
    }
    out->lat = lat;
    out->lon = lon;
    return 0;
}

int mh_parse_point_sequence(PyObject *obj, mh_point **out_points, Py_ssize_t *out_len) {
    PyObject *seq = PySequence_Fast(obj, "points must be a sequence");
    if (!seq) {
        return -1;
    }
    Py_ssize_t n = PySequence_Fast_GET_SIZE(seq);
    if (n < 0) {
        Py_DECREF(seq);
        return -1;
    }
    mh_point *points = (mh_point *)PyMem_Malloc((size_t)n * sizeof(mh_point));
    if (!points) {
        Py_DECREF(seq);
        PyErr_NoMemory();
        return -1;
    }
    PyObject **items = PySequence_Fast_ITEMS(seq);
    for (Py_ssize_t i = 0; i < n; i++) {
        if (mh_parse_point(items[i], &points[i]) != 0) {
            PyMem_Free(points);
            Py_DECREF(seq);
            return -1;
        }
    }
    Py_DECREF(seq);
    *out_points = points;
    *out_len = n;
    return 0;
}
