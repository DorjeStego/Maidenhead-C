#include "maidenhead_geo_py.h"

#include <string.h>

#include "core.h"
#include "geo.h"
#include "coverage.h"
#include "maidenhead_errors.h"
#include "maidenhead_parse.h"

PyObject *py_mh_azimuth_many(PyObject *self, PyObject *args, PyObject *kwargs) {
    PyObject *a_obj = NULL;
    PyObject *b_obj = NULL;
    int range_mode = 0;
    static char *kwlist[] = {"points_a", "points_b", "range_mode", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "OO|p", kwlist, &a_obj, &b_obj, &range_mode)) {
        return NULL;
    }
    const char **points_a = NULL;
    const char **points_b = NULL;
    Py_ssize_t n_a = 0;
    Py_ssize_t n_b = 0;
    if (mh_parse_locator_sequence(a_obj, &points_a, &n_a) != 0) {
        return NULL;
    }
    if (mh_parse_locator_sequence(b_obj, &points_b, &n_b) != 0) {
        PyMem_Free((void *)points_a);
        return NULL;
    }
    if (n_a != n_b) {
        PyMem_Free((void *)points_a);
        PyMem_Free((void *)points_b);
        PyErr_SetString(PyExc_ValueError, "points_a and points_b must have the same length");
        return NULL;
    }
    PyObject *out = PyList_New(n_a);
    if (!out) {
        PyMem_Free((void *)points_a);
        PyMem_Free((void *)points_b);
        return NULL;
    }
    for (Py_ssize_t i = 0; i < n_a; i++) {
        double lat_a = 0.0;
        double lon_a = 0.0;
        double lat_b = 0.0;
        double lon_b = 0.0;
        mh_error_context err = {MH_OK, NULL, NULL, NULL};
        mh_status st = mh_to_center_latlon(points_a[i], &lat_a, &lon_a, &err);
        if (st != MH_OK) {
            PyMem_Free((void *)points_a);
            PyMem_Free((void *)points_b);
            Py_DECREF(out);
            mh_raise_py_error(&err);
            return NULL;
        }
        st = mh_to_center_latlon(points_b[i], &lat_b, &lon_b, &err);
        if (st != MH_OK) {
            PyMem_Free((void *)points_a);
            PyMem_Free((void *)points_b);
            Py_DECREF(out);
            mh_raise_py_error(&err);
            return NULL;
        }
        mh_point a = {lat_a, lon_a};
        mh_point b = {lat_b, lon_b};
        double bearing = 0.0;
        double center_dist = 0.0;
        st = mh_bearing_deg(&a, &b, &bearing, &err);
        if (st != MH_OK) {
            PyMem_Free((void *)points_a);
            PyMem_Free((void *)points_b);
            Py_DECREF(out);
            mh_raise_py_error(&err);
            return NULL;
        }
        st = mh_distance_km(&a, &b, MH_DISTANCE_HAVERSINE, &center_dist, &err);
        if (st != MH_OK) {
            PyMem_Free((void *)points_a);
            PyMem_Free((void *)points_b);
            Py_DECREF(out);
            mh_raise_py_error(&err);
            return NULL;
        }
        if (!range_mode) {
            PyObject *item = Py_BuildValue("(dd)", bearing, center_dist);
            if (!item) {
                PyMem_Free((void *)points_a);
                PyMem_Free((void *)points_b);
                Py_DECREF(out);
                return NULL;
            }
            PyList_SET_ITEM(out, i, item);
            continue;
        }
        mh_corners_t corners_a;
        mh_corners_t corners_b;
        st = mh_corners(points_a[i], &corners_a, &err);
        if (st != MH_OK) {
            PyMem_Free((void *)points_a);
            PyMem_Free((void *)points_b);
            Py_DECREF(out);
            mh_raise_py_error(&err);
            return NULL;
        }
        st = mh_corners(points_b[i], &corners_b, &err);
        if (st != MH_OK) {
            PyMem_Free((void *)points_a);
            PyMem_Free((void *)points_b);
            Py_DECREF(out);
            mh_raise_py_error(&err);
            return NULL;
        }
        mh_point ca[4] = {corners_a.nw, corners_a.ne, corners_a.sw, corners_a.se};
        mh_point cb[4] = {corners_b.nw, corners_b.ne, corners_b.sw, corners_b.se};
        double min_d = 0.0;
        double max_d = 0.0;
        int first = 1;
        for (int ia = 0; ia < 4; ia++) {
            for (int ib = 0; ib < 4; ib++) {
                double dist = 0.0;
                st = mh_distance_km(&ca[ia], &cb[ib], MH_DISTANCE_HAVERSINE, &dist, &err);
                if (st != MH_OK) {
                    PyMem_Free((void *)points_a);
                    PyMem_Free((void *)points_b);
                    Py_DECREF(out);
                    mh_raise_py_error(&err);
                    return NULL;
                }
                if (first) {
                    min_d = dist;
                    max_d = dist;
                    first = 0;
                } else {
                    if (dist < min_d) {
                        min_d = dist;
                    }
                    if (dist > max_d) {
                        max_d = dist;
                    }
                }
            }
        }
        PyObject *item = Py_BuildValue("(ddd)", bearing, min_d, max_d);
        if (!item) {
            PyMem_Free((void *)points_a);
            PyMem_Free((void *)points_b);
            Py_DECREF(out);
            return NULL;
        }
        PyList_SET_ITEM(out, i, item);
    }
    PyMem_Free((void *)points_a);
    PyMem_Free((void *)points_b);
    return out;
}

PyObject *py_mh_initial_bearing_many(PyObject *self, PyObject *args) {
    PyObject *a_obj = NULL;
    PyObject *b_obj = NULL;
    if (!PyArg_ParseTuple(args, "OO", &a_obj, &b_obj)) {
        return NULL;
    }
    const char **points_a = NULL;
    const char **points_b = NULL;
    Py_ssize_t n_a = 0;
    Py_ssize_t n_b = 0;
    if (mh_parse_locator_sequence(a_obj, &points_a, &n_a) != 0) {
        return NULL;
    }
    if (mh_parse_locator_sequence(b_obj, &points_b, &n_b) != 0) {
        PyMem_Free((void *)points_a);
        return NULL;
    }
    if (n_a != n_b) {
        PyMem_Free((void *)points_a);
        PyMem_Free((void *)points_b);
        PyErr_SetString(PyExc_ValueError, "points_a and points_b must have the same length");
        return NULL;
    }
    PyObject *out = PyList_New(n_a);
    if (!out) {
        PyMem_Free((void *)points_a);
        PyMem_Free((void *)points_b);
        return NULL;
    }
    for (Py_ssize_t i = 0; i < n_a; i++) {
        double lat_a = 0.0;
        double lon_a = 0.0;
        double lat_b = 0.0;
        double lon_b = 0.0;
        mh_error_context err = {MH_OK, NULL, NULL, NULL};
        mh_status st = mh_to_center_latlon(points_a[i], &lat_a, &lon_a, &err);
        if (st != MH_OK) {
            PyMem_Free((void *)points_a);
            PyMem_Free((void *)points_b);
            Py_DECREF(out);
            mh_raise_py_error(&err);
            return NULL;
        }
        st = mh_to_center_latlon(points_b[i], &lat_b, &lon_b, &err);
        if (st != MH_OK) {
            PyMem_Free((void *)points_a);
            PyMem_Free((void *)points_b);
            Py_DECREF(out);
            mh_raise_py_error(&err);
            return NULL;
        }
        mh_point a = {lat_a, lon_a};
        mh_point b = {lat_b, lon_b};
        double bearing = 0.0;
        st = mh_bearing_deg(&a, &b, &bearing, &err);
        if (st != MH_OK) {
            PyMem_Free((void *)points_a);
            PyMem_Free((void *)points_b);
            Py_DECREF(out);
            mh_raise_py_error(&err);
            return NULL;
        }
        PyList_SET_ITEM(out, i, PyFloat_FromDouble(bearing));
    }
    PyMem_Free((void *)points_a);
    PyMem_Free((void *)points_b);
    return out;
}

PyObject *py_mh_distance_km(PyObject *self, PyObject *args, PyObject *kwargs) {
    PyObject *a_obj = NULL;
    PyObject *b_obj = NULL;
    const char *method = "haversine";
    static char *kwlist[] = {"a", "b", "method", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "OO|s", kwlist, &a_obj, &b_obj, &method)) {
        return NULL;
    }
    mh_point a;
    mh_point b;
    if (mh_parse_point(a_obj, &a) != 0 || mh_parse_point(b_obj, &b) != 0) {
        return NULL;
    }
    int method_code = MH_DISTANCE_HAVERSINE;
    if (strcmp(method, "geodesic") == 0) {
        method_code = MH_DISTANCE_GEODESIC;
    }
    double out = 0.0;
    mh_error_context err = {MH_OK, NULL, NULL, NULL};
    mh_status st = mh_distance_km(&a, &b, method_code, &out, &err);
    if (st != MH_OK) {
        mh_raise_py_error(&err);
        return NULL;
    }
    return PyFloat_FromDouble(out);
}

PyObject *py_mh_bearing_deg(PyObject *self, PyObject *args) {
    PyObject *a_obj = NULL;
    PyObject *b_obj = NULL;
    if (!PyArg_ParseTuple(args, "OO", &a_obj, &b_obj)) {
        return NULL;
    }
    mh_point a;
    mh_point b;
    if (mh_parse_point(a_obj, &a) != 0 || mh_parse_point(b_obj, &b) != 0) {
        return NULL;
    }
    double out = 0.0;
    mh_error_context err = {MH_OK, NULL, NULL, NULL};
    mh_status st = mh_bearing_deg(&a, &b, &out, &err);
    if (st != MH_OK) {
        mh_raise_py_error(&err);
        return NULL;
    }
    return PyFloat_FromDouble(out);
}

PyObject *py_mh_midpoint(PyObject *self, PyObject *args) {
    PyObject *a_obj = NULL;
    PyObject *b_obj = NULL;
    if (!PyArg_ParseTuple(args, "OO", &a_obj, &b_obj)) {
        return NULL;
    }
    mh_point a;
    mh_point b;
    if (mh_parse_point(a_obj, &a) != 0 || mh_parse_point(b_obj, &b) != 0) {
        return NULL;
    }
    mh_point out;
    mh_error_context err = {MH_OK, NULL, NULL, NULL};
    mh_status st = mh_midpoint(&a, &b, &out, &err);
    if (st != MH_OK) {
        mh_raise_py_error(&err);
        return NULL;
    }
    return Py_BuildValue("(dd)", out.lat, out.lon);
}

PyObject *py_mh_great_circle_path(PyObject *self, PyObject *args, PyObject *kwargs) {
    PyObject *a_obj = NULL;
    PyObject *b_obj = NULL;
    int n = 100;
    static char *kwlist[] = {"a", "b", "n", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "OO|i", kwlist, &a_obj, &b_obj, &n)) {
        return NULL;
    }
    mh_point a;
    mh_point b;
    if (mh_parse_point(a_obj, &a) != 0 || mh_parse_point(b_obj, &b) != 0) {
        return NULL;
    }
    if (n < 2) {
        PyErr_SetString(PyExc_ValueError, "n must be >= 2");
        return NULL;
    }
    mh_point *points = (mh_point *)PyMem_Malloc(sizeof(mh_point) * (size_t)n);
    if (!points) {
        return PyErr_NoMemory();
    }
    mh_error_context err = {MH_OK, NULL, NULL, NULL};
    mh_status st = mh_great_circle_path(&a, &b, (size_t)n, points, &err);
    if (st != MH_OK) {
        PyMem_Free(points);
        mh_raise_py_error(&err);
        return NULL;
    }
    PyObject *list = PyList_New(n);
    if (!list) {
        PyMem_Free(points);
        return NULL;
    }
    for (int i = 0; i < n; i++) {
        PyObject *item = Py_BuildValue("(dd)", points[i].lat, points[i].lon);
        if (!item) {
            PyMem_Free(points);
            Py_DECREF(list);
            return NULL;
        }
        PyList_SET_ITEM(list, i, item);
    }
    PyMem_Free(points);
    return list;
}

PyObject *py_mh_bearing_bin(PyObject *self, PyObject *args, PyObject *kwargs) {
    PyObject *a_obj = NULL;
    PyObject *b_obj = NULL;
    double bin_size = 5.0;
    static char *kwlist[] = {"a", "b", "bin_size", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "OO|d", kwlist, &a_obj, &b_obj, &bin_size)) {
        return NULL;
    }
    mh_point a;
    mh_point b;
    if (mh_parse_point(a_obj, &a) != 0 || mh_parse_point(b_obj, &b) != 0) {
        return NULL;
    }
    double out = 0.0;
    mh_error_context err = {MH_OK, NULL, NULL, NULL};
    mh_status st = mh_bearing_bin(&a, &b, bin_size, &out, &err);
    if (st != MH_OK) {
        mh_raise_py_error(&err);
        return NULL;
    }
    return PyFloat_FromDouble(out);
}

PyObject *py_mh_azimuthal_sector(PyObject *self, PyObject *args, PyObject *kwargs) {
    PyObject *a_obj = NULL;
    PyObject *b_obj = NULL;
    double width_deg = 0.0;
    static char *kwlist[] = {"a", "b", "width_deg", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "OOd", kwlist, &a_obj, &b_obj, &width_deg)) {
        return NULL;
    }
    mh_point a;
    mh_point b;
    if (mh_parse_point(a_obj, &a) != 0 || mh_parse_point(b_obj, &b) != 0) {
        return NULL;
    }
    double start = 0.0;
    double end = 0.0;
    mh_error_context err = {MH_OK, NULL, NULL, NULL};
    mh_status st = mh_azimuthal_sector(&a, &b, width_deg, &start, &end, &err);
    if (st != MH_OK) {
        mh_raise_py_error(&err);
        return NULL;
    }
    return Py_BuildValue("(dd)", start, end);
}

PyObject *py_mh_geodesic_midpoint(PyObject *self, PyObject *args) {
    PyObject *a_obj = NULL;
    PyObject *b_obj = NULL;
    if (!PyArg_ParseTuple(args, "OO", &a_obj, &b_obj)) {
        return NULL;
    }
    mh_point a;
    mh_point b;
    if (mh_parse_point(a_obj, &a) != 0 || mh_parse_point(b_obj, &b) != 0) {
        return NULL;
    }
    mh_point out;
    mh_error_context err = {MH_OK, NULL, NULL, NULL};
    mh_status st = mh_geodesic_midpoint(&a, &b, &out, &err);
    if (st != MH_OK) {
        mh_raise_py_error(&err);
        return NULL;
    }
    return Py_BuildValue("(dd)", out.lat, out.lon);
}

PyObject *py_mh_cover_circle(PyObject *self, PyObject *args, PyObject *kwargs) {
    PyObject *center_obj = NULL;
    double radius_km = 0.0;
    int precision = 0;
    static char *kwlist[] = {"center", "radius_km", "precision", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "Odi", kwlist, &center_obj, &radius_km, &precision)) {
        return NULL;
    }
    mh_point center;
    if (mh_parse_point(center_obj, &center) != 0) {
        return NULL;
    }
    mh_list list = {NULL, 0};
    mh_error_context err = {MH_OK, NULL, NULL, NULL};
    mh_status st = mh_cover_circle(&center, radius_km, precision, &list, &err);
    if (st != MH_OK) {
        mh_raise_py_error(&err);
        return NULL;
    }
    PyObject *out = PyList_New((Py_ssize_t)list.length);
    if (!out) {
        mh_free_list(&list);
        return NULL;
    }
    for (size_t i = 0; i < list.length; i++) {
        PyObject *item = PyUnicode_FromString(list.items[i]);
        if (!item) {
            mh_free_list(&list);
            Py_DECREF(out);
            return NULL;
        }
        PyList_SET_ITEM(out, (Py_ssize_t)i, item);
    }
    mh_free_list(&list);
    return out;
}

PyObject *py_mh_cover_line(PyObject *self, PyObject *args, PyObject *kwargs) {
    PyObject *a_obj = NULL;
    PyObject *b_obj = NULL;
    int precision = 0;
    const char *method = "greatcircle";
    static char *kwlist[] = {"a", "b", "precision", "method", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "OOi|s", kwlist, &a_obj, &b_obj, &precision, &method)) {
        return NULL;
    }
    mh_point a;
    mh_point b;
    if (mh_parse_point(a_obj, &a) != 0 || mh_parse_point(b_obj, &b) != 0) {
        return NULL;
    }
    int method_code = MH_LINE_GREATCIRCLE;
    if (strcmp(method, "geodesic") == 0) {
        method_code = MH_LINE_GEODESIC;
    }
    mh_list list = {NULL, 0};
    mh_error_context err = {MH_OK, NULL, NULL, NULL};
    mh_status st = mh_cover_line(&a, &b, precision, method_code, &list, &err);
    if (st != MH_OK) {
        mh_raise_py_error(&err);
        return NULL;
    }
    PyObject *out = PyList_New((Py_ssize_t)list.length);
    if (!out) {
        mh_free_list(&list);
        return NULL;
    }
    for (size_t i = 0; i < list.length; i++) {
        PyObject *item = PyUnicode_FromString(list.items[i]);
        if (!item) {
            mh_free_list(&list);
            Py_DECREF(out);
            return NULL;
        }
        PyList_SET_ITEM(out, (Py_ssize_t)i, item);
    }
    mh_free_list(&list);
    return out;
}
