#include <Python.h>
#include <string.h>

#include "core.h"
#include "geo.h"
#include "maidenhead_errors.h"
#include "maidenhead_parse.h"
#include "maidenhead_pyutils.h"
#include "maidenhead_math.h"

PyObject *py_mh_normalize(PyObject *self, PyObject *args) {
    const char *input = NULL;
    if (!PyArg_ParseTuple(args, "s", &input)) {
        return NULL;
    }
    char out[12];
    mh_error_context err = {MH_OK, NULL, NULL, NULL};
    mh_status st = mh_normalize_locator(input, out, sizeof(out), &err);
    if (st != MH_OK) {
        mh_raise_py_error(&err);
        return NULL;
    }
    return PyUnicode_FromString(out);
}

PyObject *py_mh_normalize_many(PyObject *self, PyObject *args) {
    PyObject *locators_obj = NULL;
    if (!PyArg_ParseTuple(args, "O", &locators_obj)) {
        return NULL;
    }
    const char **locators = NULL;
    Py_ssize_t n = 0;
    if (mh_parse_locator_sequence(locators_obj, &locators, &n) != 0) {
        return NULL;
    }
    mh_list list = {NULL, 0};
    mh_error_context err = {MH_OK, NULL, NULL, NULL};
    mh_status st = mh_normalize_many(locators, (size_t)n, &list, &err);
    PyMem_Free((void *)locators);
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

PyObject *py_mh_precision_of(PyObject *self, PyObject *args) {
    const char *input = NULL;
    int precision = 0;
    if (!PyArg_ParseTuple(args, "s", &input)) {
        return NULL;
    }
    mh_error_context err = {MH_OK, NULL, NULL, NULL};
    mh_status st = mh_precision_of(input, &precision, &err);
    if (st != MH_OK) {
        mh_raise_py_error(&err);
        return NULL;
    }
    return PyLong_FromLong(precision);
}

PyObject *py_mh_to_bbox(PyObject *self, PyObject *args) {
    const char *input = NULL;
    mh_bbox bbox;
    if (!PyArg_ParseTuple(args, "s", &input)) {
        return NULL;
    }
    mh_error_context err = {MH_OK, NULL, NULL, NULL};
    mh_status st = mh_to_bbox(input, &bbox, &err);
    if (st != MH_OK) {
        mh_raise_py_error(&err);
        return NULL;
    }
    return Py_BuildValue(
        "(dddd)",
        bbox.min_lat,
        bbox.min_lon,
        bbox.max_lat,
        bbox.max_lon
    );
}

PyObject *py_mh_to_center_latlon(PyObject *self, PyObject *args) {
    const char *input = NULL;
    double lat = 0.0;
    double lon = 0.0;
    if (!PyArg_ParseTuple(args, "s", &input)) {
        return NULL;
    }
    mh_error_context err = {MH_OK, NULL, NULL, NULL};
    mh_status st = mh_to_center_latlon(input, &lat, &lon, &err);
    if (st != MH_OK) {
        mh_raise_py_error(&err);
        return NULL;
    }
    return Py_BuildValue("(dd)", lat, lon);
}

PyObject *py_mh_from_latlon(PyObject *self, PyObject *args, PyObject *kwargs) {
    double lat = 0.0;
    double lon = 0.0;
    int precision = 6;
    int clamp = 1;
    static char *kwlist[] = {"lat", "lon", "precision", "clamp", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "dd|ip", kwlist, &lat, &lon, &precision, &clamp)) {
        return NULL;
    }

    mh_grid out;
    mh_error_context err = {MH_OK, NULL, NULL, NULL};
    mh_status st = mh_from_latlon(lat, lon, precision, clamp, &out, &err);
    if (st != MH_OK) {
        mh_raise_py_error(&err);
        return NULL;
    }
    return PyUnicode_FromString(out.locator);
}

PyObject *py_mh_format_locator(PyObject *self, PyObject *args, PyObject *kwargs) {
    const char *locator = NULL;
    int precision = 0;
    const char *mode = "center";
    static char *kwlist[] = {"locator", "precision", "mode", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "si|s", kwlist, &locator, &precision, &mode)) {
        return NULL;
    }

    int mode_code = 1;
    if (strcmp(mode, "truncate") == 0) {
        mode_code = 0;
    } else if (strcmp(mode, "center") == 0) {
        mode_code = 1;
    } else if (strcmp(mode, "error") == 0) {
        mode_code = 2;
    } else {
        PyErr_Format(PyExc_ValueError, "Unknown mode: %s", mode);
        return NULL;
    }

    mh_grid out;
    mh_error_context err = {MH_OK, NULL, NULL, NULL};
    mh_status st = mh_format_locator(locator, precision, mode_code, &out, &err);
    if (st != MH_OK) {
        mh_raise_py_error(&err);
        return NULL;
    }
    return PyUnicode_FromString(out.locator);
}

PyObject *py_mh_to_bbox_split(PyObject *self, PyObject *args) {
    const char *locator = NULL;
    if (!PyArg_ParseTuple(args, "s", &locator)) {
        return NULL;
    }
    mh_bbox parts[2];
    size_t parts_len = 0;
    mh_error_context err = {MH_OK, NULL, NULL, NULL};
    mh_status st = mh_to_bbox_split(locator, parts, &parts_len, &err);
    if (st != MH_OK) {
        mh_raise_py_error(&err);
        return NULL;
    }
    if (parts_len == 0) {
        Py_RETURN_NONE;
    }
    if (parts_len == 1) {
        return Py_BuildValue(
            "(dddd)",
            parts[0].min_lat,
            parts[0].min_lon,
            parts[0].max_lat,
            parts[0].max_lon
        );
    }
    PyObject *a = Py_BuildValue(
        "(dddd)",
        parts[0].min_lat,
        parts[0].min_lon,
        parts[0].max_lat,
        parts[0].max_lon
    );
    PyObject *b = Py_BuildValue(
        "(dddd)",
        parts[1].min_lat,
        parts[1].min_lon,
        parts[1].max_lat,
        parts[1].max_lon
    );
    if (!a || !b) {
        Py_XDECREF(a);
        Py_XDECREF(b);
        return NULL;
    }
    return PyTuple_Pack(2, a, b);
}

PyObject *py_mh_split_bbox_list(PyObject *self, PyObject *args) {
    double min_lat = 0.0;
    double min_lon = 0.0;
    double max_lat = 0.0;
    double max_lon = 0.0;
    if (!PyArg_ParseTuple(args, "dddd", &min_lat, &min_lon, &max_lat, &max_lon)) {
        return NULL;
    }
    mh_bbox bbox = {min_lat, min_lon, max_lat, max_lon};
    mh_bbox parts[2];
    size_t parts_len = 0;
    mh_error_context err = {MH_OK, NULL, NULL, NULL};
    mh_status st = mh_split_bbox_list(bbox, parts, &parts_len, &err);
    if (st != MH_OK) {
        mh_raise_py_error(&err);
        return NULL;
    }
    PyObject *list = PyList_New((Py_ssize_t)parts_len);
    if (!list) {
        return NULL;
    }
    for (size_t i = 0; i < parts_len; i++) {
        PyObject *item = Py_BuildValue(
            "(dddd)",
            parts[i].min_lat,
            parts[i].min_lon,
            parts[i].max_lat,
            parts[i].max_lon
        );
        if (!item) {
            Py_DECREF(list);
            return NULL;
        }
        PyList_SET_ITEM(list, (Py_ssize_t)i, item);
    }
    return list;
}

PyObject *py_mh_parse(PyObject *self, PyObject *args) {
    const char *locator = NULL;
    if (!PyArg_ParseTuple(args, "s", &locator)) {
        return NULL;
    }
    mh_grid out;
    mh_error_context err = {MH_OK, NULL, NULL, NULL};
    mh_status st = mh_parse_locator(locator, &out, &err);
    if (st != MH_OK) {
        mh_raise_py_error(&err);
        return NULL;
    }
    return PyUnicode_FromString(out.locator);
}

PyObject *py_mh_corners(PyObject *self, PyObject *args) {
    const char *locator = NULL;
    if (!PyArg_ParseTuple(args, "s", &locator)) {
        return NULL;
    }
    mh_corners_t corners;
    mh_error_context err = {MH_OK, NULL, NULL, NULL};
    mh_status st = mh_corners(locator, &corners, &err);
    if (st != MH_OK) {
        mh_raise_py_error(&err);
        return NULL;
    }
    PyObject *nw = Py_BuildValue("(dd)", corners.nw.lat, corners.nw.lon);
    PyObject *ne = Py_BuildValue("(dd)", corners.ne.lat, corners.ne.lon);
    PyObject *sw = Py_BuildValue("(dd)", corners.sw.lat, corners.sw.lon);
    PyObject *se = Py_BuildValue("(dd)", corners.se.lat, corners.se.lon);
    if (!nw || !ne || !sw || !se) {
        Py_XDECREF(nw);
        Py_XDECREF(ne);
        Py_XDECREF(sw);
        Py_XDECREF(se);
        return NULL;
    }
    return Py_BuildValue("(OOOO)", nw, ne, sw, se);
}

PyObject *py_mh_cell_size_deg(PyObject *self, PyObject *args) {
    const char *locator = NULL;
    if (!PyArg_ParseTuple(args, "s", &locator)) {
        return NULL;
    }
    double width = 0.0;
    double height = 0.0;
    mh_error_context err = {MH_OK, NULL, NULL, NULL};
    mh_status st = mh_cell_size_deg(locator, &width, &height, &err);
    if (st != MH_OK) {
        mh_raise_py_error(&err);
        return NULL;
    }
    return Py_BuildValue("(dd)", width, height);
}

PyObject *py_mh_cell_size_km(PyObject *self, PyObject *args, PyObject *kwargs) {
    const char *locator = NULL;
    PyObject *at_lat_obj = Py_None;
    const char *method = "spherical";
    static char *kwlist[] = {"locator", "at_lat", "method", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "s|Os", kwlist, &locator, &at_lat_obj, &method)) {
        return NULL;
    }
    int method_code = MH_DISTANCE_HAVERSINE;
    if (strcmp(method, "spherical") == 0) {
        method_code = MH_DISTANCE_HAVERSINE;
    } else if (strcmp(method, "geodesic") == 0) {
        method_code = MH_DISTANCE_GEODESIC;
    } else {
        PyErr_Format(PyExc_ValueError, "Unknown method: %s", method);
        return NULL;
    }
    double at_lat = 0.0;
    int use_at_lat = 0;
    if (at_lat_obj != Py_None) {
        at_lat = PyFloat_AsDouble(at_lat_obj);
        if (PyErr_Occurred()) {
            return NULL;
        }
        use_at_lat = 1;
    }
    double width = 0.0;
    double height = 0.0;
    mh_error_context err = {MH_OK, NULL, NULL, NULL};
    mh_status st = mh_cell_size_km(locator, use_at_lat, at_lat, method_code, &width, &height, &err);
    if (st != MH_OK) {
        mh_raise_py_error(&err);
        return NULL;
    }
    return Py_BuildValue("(dd)", width, height);
}

PyObject *py_mh_area_km2(PyObject *self, PyObject *args, PyObject *kwargs) {
    const char *locator = NULL;
    const char *method = "spherical";
    static char *kwlist[] = {"locator", "method", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "s|s", kwlist, &locator, &method)) {
        return NULL;
    }
    int method_code = MH_DISTANCE_HAVERSINE;
    if (strcmp(method, "spherical") == 0) {
        method_code = MH_DISTANCE_HAVERSINE;
    } else if (strcmp(method, "geodesic") == 0) {
        method_code = MH_DISTANCE_GEODESIC;
    } else {
        PyErr_Format(PyExc_ValueError, "Unknown method: %s", method);
        return NULL;
    }
    double area = 0.0;
    mh_error_context err = {MH_OK, NULL, NULL, NULL};
    mh_status st = mh_area_km2(locator, method_code, &area, &err);
    if (st != MH_OK) {
        mh_raise_py_error(&err);
        return NULL;
    }
    return PyFloat_FromDouble(area);
}

PyObject *py_mh_diagonal_km(PyObject *self, PyObject *args, PyObject *kwargs) {
    const char *locator = NULL;
    const char *method = "spherical";
    static char *kwlist[] = {"locator", "method", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "s|s", kwlist, &locator, &method)) {
        return NULL;
    }
    int method_code = MH_DISTANCE_HAVERSINE;
    if (strcmp(method, "spherical") == 0) {
        method_code = MH_DISTANCE_HAVERSINE;
    } else if (strcmp(method, "geodesic") == 0) {
        method_code = MH_DISTANCE_GEODESIC;
    } else {
        PyErr_Format(PyExc_ValueError, "Unknown method: %s", method);
        return NULL;
    }
    double dist = 0.0;
    mh_error_context err = {MH_OK, NULL, NULL, NULL};
    mh_status st = mh_diagonal_km(locator, method_code, &dist, &err);
    if (st != MH_OK) {
        mh_raise_py_error(&err);
        return NULL;
    }
    return PyFloat_FromDouble(dist);
}

PyObject *py_mh_step(PyObject *self, PyObject *args, PyObject *kwargs) {
    const char *locator = NULL;
    int dlat = 0;
    int dlon = 0;
    static char *kwlist[] = {"locator", "dlat_cells", "dlon_cells", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "s|ii", kwlist, &locator, &dlat, &dlon)) {
        return NULL;
    }
    mh_grid out;
    mh_error_context err = {MH_OK, NULL, NULL, NULL};
    mh_status st = mh_step(locator, dlat, dlon, &out, &err);
    if (st != MH_OK) {
        mh_raise_py_error(&err);
        return NULL;
    }
    return PyUnicode_FromString(out.locator);
}

PyObject *py_mh_contains_point(PyObject *self, PyObject *args) {
    const char *locator = NULL;
    double lat = 0.0;
    double lon = 0.0;
    if (!PyArg_ParseTuple(args, "sdd", &locator, &lat, &lon)) {
        return NULL;
    }
    int out = 0;
    mh_error_context err = {MH_OK, NULL, NULL, NULL};
    mh_status st = mh_contains_point(locator, lat, lon, &out, &err);
    if (st != MH_OK) {
        mh_raise_py_error(&err);
        return NULL;
    }
    return PyBool_FromLong(out ? 1 : 0);
}

PyObject *py_mh_contains(PyObject *self, PyObject *args) {
    const char *outer = NULL;
    const char *inner = NULL;
    if (!PyArg_ParseTuple(args, "ss", &outer, &inner)) {
        return NULL;
    }
    int out = 0;
    mh_error_context err = {MH_OK, NULL, NULL, NULL};
    mh_status st = mh_contains(outer, inner, &out, &err);
    if (st != MH_OK) {
        mh_raise_py_error(&err);
        return NULL;
    }
    return PyBool_FromLong(out ? 1 : 0);
}

PyObject *py_mh_intersects_bbox(PyObject *self, PyObject *args) {
    const char *locator = NULL;
    double min_lat = 0.0;
    double min_lon = 0.0;
    double max_lat = 0.0;
    double max_lon = 0.0;
    if (!PyArg_ParseTuple(args, "sdddd", &locator, &min_lat, &min_lon, &max_lat, &max_lon)) {
        return NULL;
    }
    mh_bbox bbox = {min_lat, min_lon, max_lat, max_lon};
    int out = 0;
    mh_error_context err = {MH_OK, NULL, NULL, NULL};
    mh_status st = mh_intersects_bbox(locator, bbox, &out, &err);
    if (st != MH_OK) {
        mh_raise_py_error(&err);
        return NULL;
    }
    return PyBool_FromLong(out ? 1 : 0);
}

PyObject *py_mh_intersects_polygon(PyObject *self, PyObject *args) {
    const char *locator = NULL;
    PyObject *points_obj = NULL;
    if (!PyArg_ParseTuple(args, "sO", &locator, &points_obj)) {
        return NULL;
    }
    mh_point *points = NULL;
    Py_ssize_t n = 0;
    if (mh_parse_point_sequence(points_obj, &points, &n) != 0) {
        return NULL;
    }
    int out = 0;
    mh_error_context err = {MH_OK, NULL, NULL, NULL};
    mh_status st = mh_intersects_polygon(locator, points, (size_t)n, &out, &err);
    PyMem_Free(points);
    if (st != MH_OK) {
        mh_raise_py_error(&err);
        return NULL;
    }
    return PyBool_FromLong(out ? 1 : 0);
}

PyObject *py_mh_to_utm_zone(PyObject *self, PyObject *args) {
    const char *locator = NULL;
    if (!PyArg_ParseTuple(args, "s", &locator)) {
        return NULL;
    }
    char out[8];
    mh_error_context err = {MH_OK, NULL, NULL, NULL};
    mh_status st = mh_to_utm_zone(locator, out, sizeof(out), &err);
    if (st != MH_OK) {
        mh_raise_py_error(&err);
        return NULL;
    }
    return PyUnicode_FromString(out);
}

PyObject *py_mh_to_utm_zone_many(PyObject *self, PyObject *args) {
    PyObject *locators_obj = NULL;
    if (!PyArg_ParseTuple(args, "O", &locators_obj)) {
        return NULL;
    }
    const char **locators = NULL;
    Py_ssize_t n = 0;
    if (mh_parse_locator_sequence(locators_obj, &locators, &n) != 0) {
        return NULL;
    }
    mh_list list = {NULL, 0};
    mh_error_context err = {MH_OK, NULL, NULL, NULL};
    mh_status st = mh_to_utm_zone_many(locators, (size_t)n, &list, &err);
    PyMem_Free((void *)locators);
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

PyObject *py_mh_contains_many(PyObject *self, PyObject *args) {
    PyObject *outers_obj = NULL;
    PyObject *inners_obj = NULL;
    if (!PyArg_ParseTuple(args, "OO", &outers_obj, &inners_obj)) {
        return NULL;
    }
    const char **outers = NULL;
    const char **inners = NULL;
    Py_ssize_t n_outers = 0;
    Py_ssize_t n_inners = 0;
    if (mh_parse_locator_sequence(outers_obj, &outers, &n_outers) != 0) {
        return NULL;
    }
    if (mh_parse_locator_sequence(inners_obj, &inners, &n_inners) != 0) {
        PyMem_Free((void *)outers);
        return NULL;
    }
    if (n_outers != n_inners) {
        PyMem_Free((void *)outers);
        PyMem_Free((void *)inners);
        PyErr_SetString(PyExc_ValueError, "outers and inners must have the same length");
        return NULL;
    }
    PyObject *out = PyList_New(n_outers);
    if (!out) {
        PyMem_Free((void *)outers);
        PyMem_Free((void *)inners);
        return NULL;
    }
    for (Py_ssize_t i = 0; i < n_outers; i++) {
        int contains = 0;
        mh_error_context err = {MH_OK, NULL, NULL, NULL};
        mh_status st = mh_contains(outers[i], inners[i], &contains, &err);
        if (st != MH_OK) {
            PyMem_Free((void *)outers);
            PyMem_Free((void *)inners);
            Py_DECREF(out);
            mh_raise_py_error(&err);
            return NULL;
        }
        PyList_SET_ITEM(out, i, PyBool_FromLong(contains ? 1 : 0));
    }
    PyMem_Free((void *)outers);
    PyMem_Free((void *)inners);
    return out;
}

PyObject *py_mh_contains_point_many(PyObject *self, PyObject *args) {
    PyObject *locators_obj = NULL;
    PyObject *lats_obj = NULL;
    PyObject *lons_obj = NULL;
    if (!PyArg_ParseTuple(args, "OOO", &locators_obj, &lats_obj, &lons_obj)) {
        return NULL;
    }
    const char **locators = NULL;
    Py_ssize_t n_loc = 0;
    if (mh_parse_locator_sequence(locators_obj, &locators, &n_loc) != 0) {
        return NULL;
    }
    double *lats = NULL;
    double *lons = NULL;
    Py_ssize_t n_lats = 0;
    Py_ssize_t n_lons = 0;
    if (mh_parse_double_sequence(lats_obj, &lats, &n_lats) != 0) {
        PyMem_Free((void *)locators);
        return NULL;
    }
    if (mh_parse_double_sequence(lons_obj, &lons, &n_lons) != 0) {
        PyMem_Free((void *)locators);
        PyMem_Free(lats);
        return NULL;
    }
    if (n_loc != n_lats || n_loc != n_lons) {
        PyMem_Free((void *)locators);
        PyMem_Free(lats);
        PyMem_Free(lons);
        PyErr_SetString(PyExc_ValueError, "locators, lats, and lons must have the same length");
        return NULL;
    }
    PyObject *out = PyList_New(n_loc);
    if (!out) {
        PyMem_Free((void *)locators);
        PyMem_Free(lats);
        PyMem_Free(lons);
        return NULL;
    }
    for (Py_ssize_t i = 0; i < n_loc; i++) {
        int contains = 0;
        mh_error_context err = {MH_OK, NULL, NULL, NULL};
        mh_status st = mh_contains_point(locators[i], lats[i], lons[i], &contains, &err);
        if (st != MH_OK) {
            PyMem_Free((void *)locators);
            PyMem_Free(lats);
            PyMem_Free(lons);
            Py_DECREF(out);
            mh_raise_py_error(&err);
            return NULL;
        }
        PyList_SET_ITEM(out, i, PyBool_FromLong(contains ? 1 : 0));
    }
    PyMem_Free((void *)locators);
    PyMem_Free(lats);
    PyMem_Free(lons);
    return out;
}

PyObject *py_mh_intersects_bbox_many(PyObject *self, PyObject *args) {
    PyObject *locators_obj = NULL;
    PyObject *bboxes_obj = NULL;
    if (!PyArg_ParseTuple(args, "OO", &locators_obj, &bboxes_obj)) {
        return NULL;
    }
    const char **locators = NULL;
    Py_ssize_t n_loc = 0;
    if (mh_parse_locator_sequence(locators_obj, &locators, &n_loc) != 0) {
        return NULL;
    }
    mh_bbox *bboxes = NULL;
    Py_ssize_t n_bboxes = 0;
    if (mh_parse_bbox_sequence(bboxes_obj, &bboxes, &n_bboxes) != 0) {
        PyMem_Free((void *)locators);
        return NULL;
    }
    if (n_loc != n_bboxes) {
        PyMem_Free((void *)locators);
        PyMem_Free(bboxes);
        PyErr_SetString(PyExc_ValueError, "locators and bboxes must have the same length");
        return NULL;
    }
    PyObject *out = PyList_New(n_loc);
    if (!out) {
        PyMem_Free((void *)locators);
        PyMem_Free(bboxes);
        return NULL;
    }
    for (Py_ssize_t i = 0; i < n_loc; i++) {
        int intersects = 0;
        mh_error_context err = {MH_OK, NULL, NULL, NULL};
        mh_status st = mh_intersects_bbox(locators[i], bboxes[i], &intersects, &err);
        if (st != MH_OK) {
            PyMem_Free((void *)locators);
            PyMem_Free(bboxes);
            Py_DECREF(out);
            mh_raise_py_error(&err);
            return NULL;
        }
        PyList_SET_ITEM(out, i, PyBool_FromLong(intersects ? 1 : 0));
    }
    PyMem_Free((void *)locators);
    PyMem_Free(bboxes);
    return out;
}

PyObject *py_mh_intersects_polygon_many(PyObject *self, PyObject *args) {
    PyObject *locators_obj = NULL;
    PyObject *polygons_obj = NULL;
    if (!PyArg_ParseTuple(args, "OO", &locators_obj, &polygons_obj)) {
        return NULL;
    }
    const char **locators = NULL;
    Py_ssize_t n_loc = 0;
    if (mh_parse_locator_sequence(locators_obj, &locators, &n_loc) != 0) {
        return NULL;
    }
    PyObject *poly_seq = PySequence_Fast(polygons_obj, "expected a sequence of polygons");
    if (!poly_seq) {
        PyMem_Free((void *)locators);
        return NULL;
    }
    Py_ssize_t n_poly = PySequence_Fast_GET_SIZE(poly_seq);
    if (n_loc != n_poly) {
        Py_DECREF(poly_seq);
        PyMem_Free((void *)locators);
        PyErr_SetString(PyExc_ValueError, "locators and polygons must have the same length");
        return NULL;
    }
    PyObject *out = PyList_New(n_loc);
    if (!out) {
        Py_DECREF(poly_seq);
        PyMem_Free((void *)locators);
        return NULL;
    }
    PyObject **items = PySequence_Fast_ITEMS(poly_seq);
    for (Py_ssize_t i = 0; i < n_loc; i++) {
        mh_point *points = NULL;
        Py_ssize_t n_points = 0;
        if (mh_parse_point_sequence(items[i], &points, &n_points) != 0) {
            Py_DECREF(poly_seq);
            PyMem_Free((void *)locators);
            Py_DECREF(out);
            return NULL;
        }
        int intersects = 0;
        mh_error_context err = {MH_OK, NULL, NULL, NULL};
        mh_status st = mh_intersects_polygon(locators[i], points, (size_t)n_points, &intersects, &err);
        PyMem_Free(points);
        if (st != MH_OK) {
            Py_DECREF(poly_seq);
            PyMem_Free((void *)locators);
            Py_DECREF(out);
            mh_raise_py_error(&err);
            return NULL;
        }
        PyList_SET_ITEM(out, i, PyBool_FromLong(intersects ? 1 : 0));
    }
    Py_DECREF(poly_seq);
    PyMem_Free((void *)locators);
    return out;
}

PyObject *py_mh_corners_many(PyObject *self, PyObject *args) {
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
        mh_corners_t corners;
        mh_error_context err = {MH_OK, NULL, NULL, NULL};
        mh_status st = mh_corners(locators[i], &corners, &err);
        if (st != MH_OK) {
            PyMem_Free((void *)locators);
            Py_DECREF(out);
            mh_raise_py_error(&err);
            return NULL;
        }
        PyObject *nw = Py_BuildValue("(dd)", corners.nw.lat, corners.nw.lon);
        PyObject *ne = Py_BuildValue("(dd)", corners.ne.lat, corners.ne.lon);
        PyObject *sw = Py_BuildValue("(dd)", corners.sw.lat, corners.sw.lon);
        PyObject *se = Py_BuildValue("(dd)", corners.se.lat, corners.se.lon);
        if (!nw || !ne || !sw || !se) {
            Py_XDECREF(nw);
            Py_XDECREF(ne);
            Py_XDECREF(sw);
            Py_XDECREF(se);
            PyMem_Free((void *)locators);
            Py_DECREF(out);
            return NULL;
        }
        PyObject *item = PyList_New(4);
        if (!item) {
            PyMem_Free((void *)locators);
            Py_DECREF(out);
            Py_DECREF(nw);
            Py_DECREF(ne);
            Py_DECREF(sw);
            Py_DECREF(se);
            return NULL;
        }
        PyList_SET_ITEM(item, 0, nw);
        PyList_SET_ITEM(item, 1, ne);
        PyList_SET_ITEM(item, 2, sw);
        PyList_SET_ITEM(item, 3, se);
        PyList_SET_ITEM(out, i, item);
    }
    PyMem_Free((void *)locators);
    return out;
}

PyObject *py_mh_parent(PyObject *self, PyObject *args, PyObject *kwargs) {
    const char *locator = NULL;
    PyObject *precision_obj = Py_None;
    static char *kwlist[] = {"locator", "precision", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "s|O", kwlist, &locator, &precision_obj)) {
        return NULL;
    }
    int precision = -1;
    if (precision_obj != Py_None) {
        long value = PyLong_AsLong(precision_obj);
        if (PyErr_Occurred()) {
            return NULL;
        }
        precision = (int)value;
    }
    mh_grid out;
    mh_error_context err = {MH_OK, NULL, NULL, NULL};
    mh_status st = mh_parent(locator, precision, &out, &err);
    if (st != MH_OK) {
        mh_raise_py_error(&err);
        return NULL;
    }
    return PyUnicode_FromString(out.locator);
}

typedef struct mh_py_list_ctx {
    PyObject *list;
} mh_py_list_ctx;

static int mh_children_callback(const char *locator, void *userdata) {
    mh_py_list_ctx *ctx = (mh_py_list_ctx *)userdata;
    PyObject *item = PyUnicode_FromString(locator);
    if (!item) {
        return -1;
    }
    int rc = PyList_Append(ctx->list, item);
    Py_DECREF(item);
    if (rc != 0) {
        return -1;
    }
    return 0;
}

PyObject *py_mh_children(PyObject *self, PyObject *args, PyObject *kwargs) {
    const char *locator = NULL;
    int precision = 0;
    static char *kwlist[] = {"locator", "precision", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "si", kwlist, &locator, &precision)) {
        return NULL;
    }
    PyObject *list = PyList_New(0);
    if (!list) {
        return NULL;
    }
    mh_py_list_ctx ctx = {list};
    mh_error_context err = {MH_OK, NULL, NULL, NULL};
    mh_status st = mh_children_iter(locator, precision, mh_children_callback, &ctx, &err);
    if (st != MH_OK) {
        if (PyErr_Occurred()) {
            Py_DECREF(list);
            return NULL;
        }
        mh_raise_py_error(&err);
        Py_DECREF(list);
        return NULL;
    }
    return list;
}

PyObject *py_mh_neighbors(PyObject *self, PyObject *args, PyObject *kwargs) {
    const char *locator = NULL;
    int ring = 1;
    int diagonals = 1;
    static char *kwlist[] = {"locator", "ring", "diagonals", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "s|ip", kwlist, &locator, &ring, &diagonals)) {
        return NULL;
    }
    mh_list list = {NULL, 0};
    mh_error_context err = {MH_OK, NULL, NULL, NULL};
    mh_status st = mh_neighbors(locator, ring, diagonals ? 1 : 0, &list, &err);
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

PyObject *py_mh_neighbors_many(PyObject *self, PyObject *args, PyObject *kwargs) {
    PyObject *locators_obj = NULL;
    int ring = 1;
    int diagonals = 1;
    static char *kwlist[] = {"locators", "ring", "diagonals", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|ip", kwlist, &locators_obj, &ring, &diagonals)) {
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
        mh_list list = {NULL, 0};
        mh_error_context err = {MH_OK, NULL, NULL, NULL};
        mh_status st = mh_neighbors(locators[i], ring, diagonals ? 1 : 0, &list, &err);
        if (st != MH_OK) {
            PyMem_Free((void *)locators);
            Py_DECREF(out);
            mh_raise_py_error(&err);
            return NULL;
        }
        PyObject *item = mh_list_to_pylist(&list);
        mh_free_list(&list);
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

PyObject *py_mh_split_bbox(PyObject *self, PyObject *args) {
    double min_lat = 0.0;
    double min_lon = 0.0;
    double max_lat = 0.0;
    double max_lon = 0.0;
    if (!PyArg_ParseTuple(args, "dddd", &min_lat, &min_lon, &max_lat, &max_lon)) {
        return NULL;
    }
    mh_bbox bbox = {min_lat, min_lon, max_lat, max_lon};
    mh_bbox parts[2];
    size_t parts_len = 0;
    mh_error_context err = {MH_OK, NULL, NULL, NULL};
    mh_status st = mh_split_bbox(bbox, parts, &parts_len, &err);
    if (st != MH_OK) {
        mh_raise_py_error(&err);
        return NULL;
    }
    if (parts_len == 0) {
        Py_RETURN_NONE;
    }
    if (parts_len == 1) {
        return Py_BuildValue(
            "(dddd)",
            parts[0].min_lat,
            parts[0].min_lon,
            parts[0].max_lat,
            parts[0].max_lon
        );
    }
    PyObject *west = Py_BuildValue(
        "(dddd)",
        parts[0].min_lat,
        parts[0].min_lon,
        parts[0].max_lat,
        parts[0].max_lon
    );
    PyObject *east = Py_BuildValue(
        "(dddd)",
        parts[1].min_lat,
        parts[1].min_lon,
        parts[1].max_lat,
        parts[1].max_lon
    );
    if (!west || !east) {
        Py_XDECREF(west);
        Py_XDECREF(east);
        return NULL;
    }
    PyObject *out = PyTuple_Pack(2, west, east);
    Py_DECREF(west);
    Py_DECREF(east);
    return out;
}

PyObject *py_mh_from_latlon_many(PyObject *self, PyObject *args, PyObject *kwargs) {
    PyObject *lats_obj = NULL;
    PyObject *lons_obj = NULL;
    int precision = 6;
    static char *kwlist[] = {"lats", "lons", "precision", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "OO|i", kwlist, &lats_obj, &lons_obj, &precision)) {
        return NULL;
    }
    double *lats = NULL;
    double *lons = NULL;
    Py_ssize_t n_lats = 0;
    Py_ssize_t n_lons = 0;
    if (mh_parse_double_sequence(lats_obj, &lats, &n_lats) != 0) {
        return NULL;
    }
    if (mh_parse_double_sequence(lons_obj, &lons, &n_lons) != 0) {
        PyMem_Free(lats);
        return NULL;
    }
    if (n_lats != n_lons) {
        PyMem_Free(lats);
        PyMem_Free(lons);
        PyErr_SetString(PyExc_ValueError, "lats and lons must have the same length");
        return NULL;
    }
    mh_list list = {NULL, 0};
    mh_error_context err = {MH_OK, NULL, NULL, NULL};
    mh_status st = mh_from_latlon_many(lats, lons, (size_t)n_lats, precision, &list, &err);
    PyMem_Free(lats);
    PyMem_Free(lons);
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

PyObject *py_mh_to_center_many(PyObject *self, PyObject *args) {
    PyObject *locators_obj = NULL;
    if (!PyArg_ParseTuple(args, "O", &locators_obj)) {
        return NULL;
    }
    const char **locators = NULL;
    Py_ssize_t n = 0;
    if (mh_parse_locator_sequence(locators_obj, &locators, &n) != 0) {
        return NULL;
    }
    double *lats = (double *)PyMem_Malloc((size_t)n * sizeof(double));
    double *lons = (double *)PyMem_Malloc((size_t)n * sizeof(double));
    if (!lats || !lons) {
        PyMem_Free(lats);
        PyMem_Free(lons);
        PyMem_Free((void *)locators);
        PyErr_NoMemory();
        return NULL;
    }
    mh_error_context err = {MH_OK, NULL, NULL, NULL};
    mh_status st = mh_to_center_many(locators, (size_t)n, lats, lons, &err);
    PyMem_Free((void *)locators);
    if (st != MH_OK) {
        PyMem_Free(lats);
        PyMem_Free(lons);
        mh_raise_py_error(&err);
        return NULL;
    }
    PyObject *lat_list = PyList_New(n);
    PyObject *lon_list = PyList_New(n);
    if (!lat_list || !lon_list) {
        Py_XDECREF(lat_list);
        Py_XDECREF(lon_list);
        PyMem_Free(lats);
        PyMem_Free(lons);
        return NULL;
    }
    for (Py_ssize_t i = 0; i < n; i++) {
        PyList_SET_ITEM(lat_list, i, PyFloat_FromDouble(lats[i]));
        PyList_SET_ITEM(lon_list, i, PyFloat_FromDouble(lons[i]));
    }
    PyMem_Free(lats);
    PyMem_Free(lons);
    PyObject *out = PyTuple_Pack(2, lat_list, lon_list);
    Py_DECREF(lat_list);
    Py_DECREF(lon_list);
    return out;
}

PyObject *py_mh_to_bbox_many(PyObject *self, PyObject *args) {
    PyObject *locators_obj = NULL;
    if (!PyArg_ParseTuple(args, "O", &locators_obj)) {
        return NULL;
    }
    const char **locators = NULL;
    Py_ssize_t n = 0;
    if (mh_parse_locator_sequence(locators_obj, &locators, &n) != 0) {
        return NULL;
    }
    mh_bbox *bboxes = (mh_bbox *)PyMem_Malloc((size_t)n * sizeof(mh_bbox));
    if (!bboxes) {
        PyMem_Free((void *)locators);
        PyErr_NoMemory();
        return NULL;
    }
    mh_error_context err = {MH_OK, NULL, NULL, NULL};
    mh_status st = mh_to_bbox_many(locators, (size_t)n, bboxes, &err);
    PyMem_Free((void *)locators);
    if (st != MH_OK) {
        PyMem_Free(bboxes);
        mh_raise_py_error(&err);
        return NULL;
    }
    PyObject *min_lats = PyList_New(n);
    PyObject *min_lons = PyList_New(n);
    PyObject *max_lats = PyList_New(n);
    PyObject *max_lons = PyList_New(n);
    if (!min_lats || !min_lons || !max_lats || !max_lons) {
        Py_XDECREF(min_lats);
        Py_XDECREF(min_lons);
        Py_XDECREF(max_lats);
        Py_XDECREF(max_lons);
        PyMem_Free(bboxes);
        return NULL;
    }
    for (Py_ssize_t i = 0; i < n; i++) {
        PyList_SET_ITEM(min_lats, i, PyFloat_FromDouble(bboxes[i].min_lat));
        PyList_SET_ITEM(min_lons, i, PyFloat_FromDouble(bboxes[i].min_lon));
        PyList_SET_ITEM(max_lats, i, PyFloat_FromDouble(bboxes[i].max_lat));
        PyList_SET_ITEM(max_lons, i, PyFloat_FromDouble(bboxes[i].max_lon));
    }
    PyMem_Free(bboxes);
    PyObject *out = PyTuple_Pack(4, min_lats, min_lons, max_lats, max_lons);
    Py_DECREF(min_lats);
    Py_DECREF(min_lons);
    Py_DECREF(max_lats);
    Py_DECREF(max_lons);
    return out;
}

PyObject *py_mh_adjacent(PyObject *self, PyObject *args, PyObject *kwargs) {
    const char *locator = NULL;
    int diagonals = 0;
    static char *kwlist[] = {"locator", "diagonals", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "s|p", kwlist, &locator, &diagonals)) {
        return NULL;
    }
    mh_kv_list list = {NULL, NULL, 0};
    mh_error_context err = {MH_OK, NULL, NULL, NULL};
    mh_status st = mh_adjacent(locator, diagonals ? 1 : 0, &list, &err);
    if (st != MH_OK) {
        mh_raise_py_error(&err);
        return NULL;
    }
    PyObject *dict = PyDict_New();
    if (!dict) {
        mh_free_kv_list(&list);
        return NULL;
    }
    for (size_t i = 0; i < list.length; i++) {
        PyObject *key = PyUnicode_FromString(list.keys[i]);
        PyObject *value = PyUnicode_FromString(list.values[i]);
        if (!key || !value) {
            Py_XDECREF(key);
            Py_XDECREF(value);
            mh_free_kv_list(&list);
            Py_DECREF(dict);
            return NULL;
        }
        if (PyDict_SetItem(dict, key, value) != 0) {
            Py_DECREF(key);
            Py_DECREF(value);
            mh_free_kv_list(&list);
            Py_DECREF(dict);
            return NULL;
        }
        Py_DECREF(key);
        Py_DECREF(value);
    }
    mh_free_kv_list(&list);
    return dict;
}

PyObject *py_mh_adjacent_many(PyObject *self, PyObject *args, PyObject *kwargs) {
    PyObject *locators_obj = NULL;
    int diagonals = 0;
    static char *kwlist[] = {"locators", "diagonals", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|p", kwlist, &locators_obj, &diagonals)) {
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
        mh_kv_list list = {NULL, NULL, 0};
        mh_error_context err = {MH_OK, NULL, NULL, NULL};
        mh_status st = mh_adjacent(locators[i], diagonals ? 1 : 0, &list, &err);
        if (st != MH_OK) {
            PyMem_Free((void *)locators);
            Py_DECREF(out);
            mh_raise_py_error(&err);
            return NULL;
        }
        PyObject *item = mh_kv_list_to_pydict(&list);
        mh_free_kv_list(&list);
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

PyObject *py_mh_parent_many(PyObject *self, PyObject *args, PyObject *kwargs) {
    PyObject *locators_obj = NULL;
    PyObject *precision_obj = Py_None;
    static char *kwlist[] = {"locators", "precision", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|O", kwlist, &locators_obj, &precision_obj)) {
        return NULL;
    }
    int precision = -1;
    if (precision_obj != Py_None) {
        long value = PyLong_AsLong(precision_obj);
        if (PyErr_Occurred()) {
            return NULL;
        }
        precision = (int)value;
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
        mh_grid grid;
        mh_error_context err = {MH_OK, NULL, NULL, NULL};
        mh_status st = mh_parent(locators[i], precision, &grid, &err);
        if (st != MH_OK) {
            PyMem_Free((void *)locators);
            Py_DECREF(out);
            mh_raise_py_error(&err);
            return NULL;
        }
        PyObject *item = PyUnicode_FromString(grid.locator);
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

typedef struct mh_py_children_ctx {
    PyObject *list;
} mh_py_children_ctx;

static int mh_children_list_callback(const char *locator, void *userdata) {
    mh_py_children_ctx *ctx = (mh_py_children_ctx *)userdata;
    PyObject *item = PyUnicode_FromString(locator);
    if (!item) {
        return -1;
    }
    int rc = PyList_Append(ctx->list, item);
    Py_DECREF(item);
    if (rc != 0) {
        return -1;
    }
    return 0;
}

PyObject *py_mh_children_many(PyObject *self, PyObject *args, PyObject *kwargs) {
    PyObject *locators_obj = NULL;
    PyObject *precision_obj = Py_None;
    PyObject *limit_obj = Py_None;
    static char *kwlist[] = {"locators", "precision", "limit", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|OO", kwlist, &locators_obj, &precision_obj, &limit_obj)) {
        return NULL;
    }
    int precision = -1;
    if (precision_obj != Py_None) {
        long value = PyLong_AsLong(precision_obj);
        if (PyErr_Occurred()) {
            return NULL;
        }
        precision = (int)value;
    }
    long limit = -1;
    if (limit_obj != Py_None) {
        long value = PyLong_AsLong(limit_obj);
        if (PyErr_Occurred()) {
            return NULL;
        }
        limit = value;
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
        int precision_value = precision;
        if (precision_value < 0) {
            int p = 0;
            mh_error_context err = {MH_OK, NULL, NULL, NULL};
            mh_status st = mh_precision_of(locators[i], &p, &err);
            if (st != MH_OK) {
                PyMem_Free((void *)locators);
                Py_DECREF(out);
                mh_raise_py_error(&err);
                return NULL;
            }
            precision_value = p + 2;
        }
        PyObject *list = PyList_New(0);
        if (!list) {
            PyMem_Free((void *)locators);
            Py_DECREF(out);
            return NULL;
        }
        mh_py_children_ctx ctx = {list};
        mh_error_context err = {MH_OK, NULL, NULL, NULL};
        mh_status st = mh_children_iter(locators[i], precision_value, mh_children_list_callback, &ctx, &err);
        if (st != MH_OK) {
            PyMem_Free((void *)locators);
            Py_DECREF(list);
            Py_DECREF(out);
            mh_raise_py_error(&err);
            return NULL;
        }
        if (limit >= 0) {
            Py_ssize_t list_len = PyList_Size(list);
            if (list_len > limit) {
                PyObject *trim = PyList_GetSlice(list, 0, (Py_ssize_t)limit);
                Py_DECREF(list);
                if (!trim) {
                    PyMem_Free((void *)locators);
                    Py_DECREF(out);
                    return NULL;
                }
                list = trim;
            }
        }
        PyList_SET_ITEM(out, i, list);
    }
    PyMem_Free((void *)locators);
    return out;
}

PyObject *py_mh_cell_size_deg_many(PyObject *self, PyObject *args) {
    PyObject *locators_obj = NULL;
    if (!PyArg_ParseTuple(args, "O", &locators_obj)) {
        return NULL;
    }
    const char **locators = NULL;
    Py_ssize_t n = 0;
    if (mh_parse_locator_sequence(locators_obj, &locators, &n) != 0) {
        return NULL;
    }
    PyObject *widths = PyList_New(n);
    PyObject *heights = PyList_New(n);
    if (!widths || !heights) {
        Py_XDECREF(widths);
        Py_XDECREF(heights);
        PyMem_Free((void *)locators);
        return NULL;
    }
    for (Py_ssize_t i = 0; i < n; i++) {
        int precision = 0;
        mh_error_context err = {MH_OK, NULL, NULL, NULL};
        mh_status st = mh_precision_of(locators[i], &precision, &err);
        if (st != MH_OK) {
            PyMem_Free((void *)locators);
            Py_DECREF(widths);
            Py_DECREF(heights);
            mh_raise_py_error(&err);
            return NULL;
        }
        double lon_step = 0.0;
        double lat_step = 0.0;
        st = mh_step_size_for_precision(precision, &lon_step, &lat_step, &err);
        if (st != MH_OK) {
            PyMem_Free((void *)locators);
            Py_DECREF(widths);
            Py_DECREF(heights);
            mh_raise_py_error(&err);
            return NULL;
        }
        PyList_SET_ITEM(widths, i, PyFloat_FromDouble(lon_step));
        PyList_SET_ITEM(heights, i, PyFloat_FromDouble(lat_step));
    }
    PyMem_Free((void *)locators);
    PyObject *out = PyTuple_Pack(2, widths, heights);
    Py_DECREF(widths);
    Py_DECREF(heights);
    return out;
}

PyObject *py_mh_cell_size_km_many(PyObject *self, PyObject *args, PyObject *kwargs) {
    PyObject *locators_obj = NULL;
    PyObject *at_lat_obj = Py_None;
    const char *method = "spherical";
    static char *kwlist[] = {"locators", "at_lat", "method", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|Os", kwlist, &locators_obj, &at_lat_obj, &method)) {
        return NULL;
    }
    int method_code = MH_DISTANCE_HAVERSINE;
    if (strcmp(method, "spherical") == 0) {
        method_code = MH_DISTANCE_HAVERSINE;
    } else if (strcmp(method, "geodesic") == 0) {
        method_code = MH_DISTANCE_GEODESIC;
    } else {
        PyErr_Format(PyExc_ValueError, "Unknown method: %s", method);
        return NULL;
    }
    double at_lat = 0.0;
    int use_at_lat = 0;
    if (at_lat_obj != Py_None) {
        at_lat = PyFloat_AsDouble(at_lat_obj);
        if (PyErr_Occurred()) {
            return NULL;
        }
        use_at_lat = 1;
    }
    const char **locators = NULL;
    Py_ssize_t n = 0;
    if (mh_parse_locator_sequence(locators_obj, &locators, &n) != 0) {
        return NULL;
    }
    PyObject *widths = PyList_New(n);
    PyObject *heights = PyList_New(n);
    if (!widths || !heights) {
        Py_XDECREF(widths);
        Py_XDECREF(heights);
        PyMem_Free((void *)locators);
        return NULL;
    }
    for (Py_ssize_t i = 0; i < n; i++) {
        int precision = 0;
        mh_error_context err = {MH_OK, NULL, NULL, NULL};
        mh_status st = mh_precision_of(locators[i], &precision, &err);
        if (st != MH_OK) {
            PyMem_Free((void *)locators);
            Py_DECREF(widths);
            Py_DECREF(heights);
            mh_raise_py_error(&err);
            return NULL;
        }
        double lon_step = 0.0;
        double lat_step = 0.0;
        st = mh_step_size_for_precision(precision, &lon_step, &lat_step, &err);
        if (st != MH_OK) {
            PyMem_Free((void *)locators);
            Py_DECREF(widths);
            Py_DECREF(heights);
            mh_raise_py_error(&err);
            return NULL;
        }
        double lat = 0.0;
        double lon = 0.0;
        st = mh_to_center_latlon(locators[i], &lat, &lon, &err);
        if (st != MH_OK) {
            PyMem_Free((void *)locators);
            Py_DECREF(widths);
            Py_DECREF(heights);
            mh_raise_py_error(&err);
            return NULL;
        }
        double half_lon = lon_step / 2.0;
        double half_lat = lat_step / 2.0;
        mh_point a = {use_at_lat ? at_lat : lat, lon - half_lon};
        mh_point b = {use_at_lat ? at_lat : lat, lon + half_lon};
        mh_point c = {lat - half_lat, lon};
        mh_point d = {lat + half_lat, lon};
        double width_km = 0.0;
        double height_km = 0.0;
        st = mh_distance_km(&a, &b, method_code, &width_km, &err);
        if (st != MH_OK) {
            PyMem_Free((void *)locators);
            Py_DECREF(widths);
            Py_DECREF(heights);
            mh_raise_py_error(&err);
            return NULL;
        }
        st = mh_distance_km(&c, &d, method_code, &height_km, &err);
        if (st != MH_OK) {
            PyMem_Free((void *)locators);
            Py_DECREF(widths);
            Py_DECREF(heights);
            mh_raise_py_error(&err);
            return NULL;
        }
        PyList_SET_ITEM(widths, i, PyFloat_FromDouble(width_km));
        PyList_SET_ITEM(heights, i, PyFloat_FromDouble(height_km));
    }
    PyMem_Free((void *)locators);
    PyObject *out = PyTuple_Pack(2, widths, heights);
    Py_DECREF(widths);
    Py_DECREF(heights);
    return out;
}

PyObject *py_mh_area_km2_many(PyObject *self, PyObject *args, PyObject *kwargs) {
    PyObject *locators_obj = NULL;
    const char *method = "spherical";
    static char *kwlist[] = {"locators", "method", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|s", kwlist, &locators_obj, &method)) {
        return NULL;
    }
    int method_code = MH_DISTANCE_HAVERSINE;
    if (strcmp(method, "spherical") == 0) {
        method_code = MH_DISTANCE_HAVERSINE;
    } else if (strcmp(method, "geodesic") == 0) {
        method_code = MH_DISTANCE_GEODESIC;
    } else {
        PyErr_Format(PyExc_ValueError, "Unknown method: %s", method);
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
        double area_km2 = 0.0;
        if (method_code == MH_DISTANCE_GEODESIC) {
            mh_bbox bbox;
            mh_error_context err = {MH_OK, NULL, NULL, NULL};
            mh_status st = mh_to_bbox(locators[i], &bbox, &err);
            if (st != MH_OK) {
                PyMem_Free((void *)locators);
                Py_DECREF(out);
                mh_raise_py_error(&err);
                return NULL;
            }
            mh_point poly[4] = {
                {bbox.min_lat, bbox.min_lon},
                {bbox.min_lat, bbox.max_lon},
                {bbox.max_lat, bbox.max_lon},
                {bbox.max_lat, bbox.min_lon},
            };
            st = mh_geodesic_area_km2(poly, 4, &area_km2, &err);
            if (st != MH_OK) {
                PyMem_Free((void *)locators);
                Py_DECREF(out);
                mh_raise_py_error(&err);
                return NULL;
            }
            if (area_km2 < 0.0) {
                area_km2 = -area_km2;
            }
        } else {
            int precision = 0;
            mh_error_context err = {MH_OK, NULL, NULL, NULL};
            mh_status st = mh_precision_of(locators[i], &precision, &err);
            if (st != MH_OK) {
                PyMem_Free((void *)locators);
                Py_DECREF(out);
                mh_raise_py_error(&err);
                return NULL;
            }
            double lon_step = 0.0;
            double lat_step = 0.0;
            st = mh_step_size_for_precision(precision, &lon_step, &lat_step, &err);
            if (st != MH_OK) {
                PyMem_Free((void *)locators);
                Py_DECREF(out);
                mh_raise_py_error(&err);
                return NULL;
            }
            double lat = 0.0;
            double lon = 0.0;
            st = mh_to_center_latlon(locators[i], &lat, &lon, &err);
            if (st != MH_OK) {
                PyMem_Free((void *)locators);
                Py_DECREF(out);
                mh_raise_py_error(&err);
                return NULL;
            }
            double half_lon = lon_step / 2.0;
            double half_lat = lat_step / 2.0;
            mh_point a = {lat, lon - half_lon};
            mh_point b = {lat, lon + half_lon};
            mh_point c = {lat - half_lat, lon};
            mh_point d = {lat + half_lat, lon};
            double width_km = 0.0;
            double height_km = 0.0;
            st = mh_distance_km(&a, &b, MH_DISTANCE_HAVERSINE, &width_km, &err);
            if (st != MH_OK) {
                PyMem_Free((void *)locators);
                Py_DECREF(out);
                mh_raise_py_error(&err);
                return NULL;
            }
            st = mh_distance_km(&c, &d, MH_DISTANCE_HAVERSINE, &height_km, &err);
            if (st != MH_OK) {
                PyMem_Free((void *)locators);
                Py_DECREF(out);
                mh_raise_py_error(&err);
                return NULL;
            }
            area_km2 = width_km * height_km;
        }
        PyList_SET_ITEM(out, i, PyFloat_FromDouble(area_km2));
    }
    PyMem_Free((void *)locators);
    return out;
}

PyObject *py_mh_diagonal_km_many(PyObject *self, PyObject *args, PyObject *kwargs) {
    PyObject *locators_obj = NULL;
    const char *method = "spherical";
    static char *kwlist[] = {"locators", "method", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|s", kwlist, &locators_obj, &method)) {
        return NULL;
    }
    int method_code = MH_DISTANCE_HAVERSINE;
    if (strcmp(method, "spherical") == 0) {
        method_code = MH_DISTANCE_HAVERSINE;
    } else if (strcmp(method, "geodesic") == 0) {
        method_code = MH_DISTANCE_GEODESIC;
    } else {
        PyErr_Format(PyExc_ValueError, "Unknown method: %s", method);
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
        mh_point a = {bbox.min_lat, bbox.min_lon};
        mh_point b = {bbox.max_lat, bbox.max_lon};
        double dist_km = 0.0;
        st = mh_distance_km(&a, &b, method_code, &dist_km, &err);
        if (st != MH_OK) {
            PyMem_Free((void *)locators);
            Py_DECREF(out);
            mh_raise_py_error(&err);
            return NULL;
        }
        PyList_SET_ITEM(out, i, PyFloat_FromDouble(dist_km));
    }
    PyMem_Free((void *)locators);
    return out;
}

PyObject *py_mh_split_bbox_many(PyObject *self, PyObject *args) {
    PyObject *bboxes_obj = NULL;
    if (!PyArg_ParseTuple(args, "O", &bboxes_obj)) {
        return NULL;
    }
    mh_bbox *bboxes = NULL;
    Py_ssize_t n = 0;
    if (mh_parse_bbox_sequence(bboxes_obj, &bboxes, &n) != 0) {
        return NULL;
    }
    PyObject *out = PyList_New(n);
    if (!out) {
        PyMem_Free(bboxes);
        return NULL;
    }
    for (Py_ssize_t i = 0; i < n; i++) {
        mh_bbox parts[2];
        size_t parts_len = 0;
        mh_error_context err = {MH_OK, NULL, NULL, NULL};
        mh_status st = mh_split_bbox(bboxes[i], parts, &parts_len, &err);
        if (st != MH_OK) {
            PyMem_Free(bboxes);
            Py_DECREF(out);
            mh_raise_py_error(&err);
            return NULL;
        }
        PyObject *item = PyList_New((Py_ssize_t)parts_len);
        if (!item) {
            PyMem_Free(bboxes);
            Py_DECREF(out);
            return NULL;
        }
        for (size_t j = 0; j < parts_len; j++) {
            PyObject *bbox = Py_BuildValue(
                "(dddd)",
                parts[j].min_lat,
                parts[j].min_lon,
                parts[j].max_lat,
                parts[j].max_lon
            );
            if (!bbox) {
                Py_DECREF(item);
                PyMem_Free(bboxes);
                Py_DECREF(out);
                return NULL;
            }
            PyList_SET_ITEM(item, (Py_ssize_t)j, bbox);
        }
        PyList_SET_ITEM(out, i, item);
    }
    PyMem_Free(bboxes);
    return out;
}
