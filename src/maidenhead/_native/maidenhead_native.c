#include <Python.h>
#include <string.h>

#include "errors.h"
#include "core.h"
#include "geo.h"
#include "geojson.h"
#include "coverage.h"

static PyObject *mh_exc_maidenhead = NULL;
static PyObject *mh_exc_precision = NULL;
static PyObject *mh_exc_invalid_locator = NULL;
static PyObject *mh_exc_out_of_range = NULL;
static PyObject *mh_exc_unsupported = NULL;
static PyObject *mh_exc_missing_dep = NULL;

static int mh_parse_point_sequence(PyObject *obj, mh_point **out_points, Py_ssize_t *out_len);
static int mh_parse_double_sequence(PyObject *obj, double **out_vals, Py_ssize_t *out_len);
static int mh_parse_locator_sequence(PyObject *obj, const char ***out_vals, Py_ssize_t *out_len);
static int mh_parse_bbox_sequence(PyObject *obj, mh_bbox **out_bboxes, Py_ssize_t *out_len);
static int mh_parse_point(PyObject *obj, mh_point *out);

#ifdef MH_HAVE_SIMDJSON
PyObject *py_mh_json_loads_simdjson(PyObject *self, PyObject *args);
#endif

static PyObject *py_mh_json_loads(PyObject *self, PyObject *args) {
#ifdef MH_HAVE_SIMDJSON
    return py_mh_json_loads_simdjson(self, args);
#else
    (void)self;
    (void)args;
    if (mh_exc_missing_dep) {
        PyErr_SetString(mh_exc_missing_dep, "simdjson not available");
    } else {
        PyErr_SetString(PyExc_ImportError, "simdjson not available");
    }
    return NULL;
#endif
}

static PyObject *mh_list_to_pylist(const mh_list *list);
static PyObject *mh_kv_list_to_pydict(const mh_kv_list *list);
static int mh_step_size_for_precision(int precision, double *lon_step, double *lat_step, mh_error_context *err);

static int mh_import_exceptions(void) {
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

static PyObject *mh_error_type_for_status(mh_status code) {
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

static int mh_raise_py_error(const mh_error_context *err) {
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

static PyObject *py_mh_normalize(PyObject *self, PyObject *args) {
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

static PyObject *py_mh_normalize_many(PyObject *self, PyObject *args) {
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

static PyObject *py_mh_precision_of(PyObject *self, PyObject *args) {
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

static PyObject *py_mh_to_bbox(PyObject *self, PyObject *args) {
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

static PyObject *py_mh_to_center_latlon(PyObject *self, PyObject *args) {
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

static PyObject *py_mh_from_latlon(PyObject *self, PyObject *args, PyObject *kwargs) {
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

static PyObject *py_mh_format_locator(PyObject *self, PyObject *args, PyObject *kwargs) {
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

static PyObject *py_mh_to_bbox_split(PyObject *self, PyObject *args) {
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

static PyObject *py_mh_split_bbox_list(PyObject *self, PyObject *args) {
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

static PyObject *py_mh_parse(PyObject *self, PyObject *args) {
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

static PyObject *py_mh_corners(PyObject *self, PyObject *args) {
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

static PyObject *py_mh_cell_size_deg(PyObject *self, PyObject *args) {
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

static PyObject *py_mh_cell_size_km(PyObject *self, PyObject *args, PyObject *kwargs) {
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

static PyObject *py_mh_area_km2(PyObject *self, PyObject *args, PyObject *kwargs) {
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

static PyObject *py_mh_diagonal_km(PyObject *self, PyObject *args, PyObject *kwargs) {
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

static PyObject *py_mh_step(PyObject *self, PyObject *args, PyObject *kwargs) {
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

static PyObject *py_mh_contains_point(PyObject *self, PyObject *args) {
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

static PyObject *py_mh_contains(PyObject *self, PyObject *args) {
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

static PyObject *py_mh_intersects_bbox(PyObject *self, PyObject *args) {
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

static PyObject *py_mh_intersects_polygon(PyObject *self, PyObject *args) {
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

static PyObject *py_mh_to_utm_zone(PyObject *self, PyObject *args) {
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

static PyObject *py_mh_to_utm_zone_many(PyObject *self, PyObject *args) {
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

static PyObject *py_mh_contains_many(PyObject *self, PyObject *args) {
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

static PyObject *py_mh_contains_point_many(PyObject *self, PyObject *args) {
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

static PyObject *py_mh_intersects_bbox_many(PyObject *self, PyObject *args) {
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

static PyObject *py_mh_intersects_polygon_many(PyObject *self, PyObject *args) {
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

static PyObject *py_mh_corners_many(PyObject *self, PyObject *args) {
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

static PyObject *py_mh_parent(PyObject *self, PyObject *args, PyObject *kwargs) {
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

static PyObject *py_mh_children(PyObject *self, PyObject *args, PyObject *kwargs) {
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

static PyObject *py_mh_neighbors(PyObject *self, PyObject *args, PyObject *kwargs) {
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

static PyObject *py_mh_neighbors_many(PyObject *self, PyObject *args, PyObject *kwargs) {
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

static int mh_parse_double_sequence(PyObject *obj, double **out_vals, Py_ssize_t *out_len) {
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

static int mh_parse_locator_sequence(PyObject *obj, const char ***out_vals, Py_ssize_t *out_len) {
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

static int mh_parse_bbox_sequence(PyObject *obj, mh_bbox **out_bboxes, Py_ssize_t *out_len) {
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

static PyObject *mh_list_to_pylist(const mh_list *list) {
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

static PyObject *mh_kv_list_to_pydict(const mh_kv_list *list) {
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

static int mh_step_size_for_precision(int precision, double *lon_step, double *lat_step, mh_error_context *err) {
    mh_status st = mh_validate_precision(precision, err);
    if (st != MH_OK) {
        return st;
    }
    int pairs = precision / 2;
    double lon_cell = 360.0;
    double lat_cell = 180.0;
    for (int i = 1; i <= pairs; i++) {
        int base = 0;
        if (i == 1) {
            base = 18;
        } else if (i % 2 == 0) {
            base = 10;
        } else {
            base = 24;
        }
        lon_cell /= (double)base;
        lat_cell /= (double)base;
    }
    *lon_step = lon_cell;
    *lat_step = lat_cell;
    return MH_OK;
}

static PyObject *py_mh_split_bbox(PyObject *self, PyObject *args) {
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

static PyObject *py_mh_from_latlon_many(PyObject *self, PyObject *args, PyObject *kwargs) {
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

static PyObject *py_mh_to_center_many(PyObject *self, PyObject *args) {
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

static PyObject *py_mh_to_bbox_many(PyObject *self, PyObject *args) {
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

static PyObject *py_mh_adjacent(PyObject *self, PyObject *args, PyObject *kwargs) {
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

static PyObject *py_mh_adjacent_many(PyObject *self, PyObject *args, PyObject *kwargs) {
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

static PyObject *py_mh_parent_many(PyObject *self, PyObject *args, PyObject *kwargs) {
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

static PyObject *py_mh_children_many(PyObject *self, PyObject *args, PyObject *kwargs) {
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

static PyObject *py_mh_cell_size_deg_many(PyObject *self, PyObject *args) {
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

static PyObject *py_mh_cell_size_km_many(PyObject *self, PyObject *args, PyObject *kwargs) {
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

static PyObject *py_mh_area_km2_many(PyObject *self, PyObject *args, PyObject *kwargs) {
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

static PyObject *py_mh_diagonal_km_many(PyObject *self, PyObject *args, PyObject *kwargs) {
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

static PyObject *py_mh_to_wkt_many(PyObject *self, PyObject *args) {
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

static PyObject *py_mh_azimuth_many(PyObject *self, PyObject *args, PyObject *kwargs) {
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

static PyObject *py_mh_initial_bearing_many(PyObject *self, PyObject *args) {
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

static PyObject *py_mh_to_geojson_polygon_many(PyObject *self, PyObject *args) {
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

static PyObject *py_mh_to_geojson_feature_many(PyObject *self, PyObject *args) {
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

static PyObject *py_mh_to_geojson_bbox_many(PyObject *self, PyObject *args) {
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

static PyObject *py_mh_to_geojson_envelope_many(PyObject *self, PyObject *args) {
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

static PyObject *py_mh_split_bbox_many(PyObject *self, PyObject *args) {
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
static int mh_parse_point(PyObject *obj, mh_point *out) {
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

static int mh_parse_point_sequence(PyObject *obj, mh_point **out_points, Py_ssize_t *out_len) {
    PyObject *seq = PySequence_Fast(obj, "points must be a sequence");
    if (!seq) {
        return -1;
    }
    Py_ssize_t n = PySequence_Fast_GET_SIZE(seq);
    if (n < 0) {
        Py_DECREF(seq);
        return -1;
    }
    mh_point *points = (mh_point *)PyMem_Malloc(sizeof(mh_point) * (size_t)n);
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

static PyObject *py_mh_distance_km(PyObject *self, PyObject *args, PyObject *kwargs) {
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

static PyObject *py_mh_bearing_deg(PyObject *self, PyObject *args) {
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

static PyObject *py_mh_midpoint(PyObject *self, PyObject *args) {
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

static PyObject *py_mh_great_circle_path(PyObject *self, PyObject *args, PyObject *kwargs) {
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

static PyObject *py_mh_bearing_bin(PyObject *self, PyObject *args, PyObject *kwargs) {
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

static PyObject *py_mh_azimuthal_sector(PyObject *self, PyObject *args, PyObject *kwargs) {
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

static PyObject *py_mh_geodesic_midpoint(PyObject *self, PyObject *args) {
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

static PyObject *py_mh_cover_circle(PyObject *self, PyObject *args, PyObject *kwargs) {
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

static PyObject *py_mh_cover_line(PyObject *self, PyObject *args, PyObject *kwargs) {
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

static PyObject *py_mh_to_geojson_polygon(PyObject *self, PyObject *args) {
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

static PyObject *py_mh_to_geojson_feature(PyObject *self, PyObject *args) {
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

static PyObject *py_mh_to_geojson_envelope(PyObject *self, PyObject *args) {
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

static PyObject *py_mh_to_geojson_feature_collection(PyObject *self, PyObject *args) {
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

static PyObject *py_mh_to_geojson_bbox(PyObject *self, PyObject *args) {
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

static PyObject *py_mh_to_wkt(PyObject *self, PyObject *args) {
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

static PyMethodDef maidenhead_native_methods[] = {
    {"normalize", py_mh_normalize, METH_VARARGS, "Normalize a locator."},
    {"normalize_many", py_mh_normalize_many, METH_VARARGS, "Normalize locators."},
    {"precision_of", py_mh_precision_of, METH_VARARGS, "Return locator precision."},
    {"to_bbox", py_mh_to_bbox, METH_VARARGS, "Return locator bbox."},
    {"to_center_latlon", py_mh_to_center_latlon, METH_VARARGS, "Return locator center lat/lon."},
    {"from_latlon", (PyCFunction)py_mh_from_latlon, METH_VARARGS | METH_KEYWORDS, "Convert lat/lon to locator."},
    {"format_locator", (PyCFunction)py_mh_format_locator, METH_VARARGS | METH_KEYWORDS, "Format locator precision."},
    {"to_bbox_split", py_mh_to_bbox_split, METH_VARARGS, "Split locator bbox at antimeridian."},
    {"split_bbox", py_mh_split_bbox, METH_VARARGS, "Split bbox at antimeridian."},
    {"split_bbox_list", py_mh_split_bbox_list, METH_VARARGS, "Split bbox into parts list."},
    {"from_latlon_many", (PyCFunction)py_mh_from_latlon_many, METH_VARARGS | METH_KEYWORDS, "Bulk lat/lon to locator."},
    {"to_center_many", py_mh_to_center_many, METH_VARARGS, "Bulk locator centers."},
    {"to_bbox_many", py_mh_to_bbox_many, METH_VARARGS, "Bulk locator bboxes."},
    {"parse", py_mh_parse, METH_VARARGS, "Parse and normalize a locator."},
    {"corners", py_mh_corners, METH_VARARGS, "Locator corners."},
    {"cell_size_deg", py_mh_cell_size_deg, METH_VARARGS, "Locator cell size (deg)."},
    {"cell_size_km", (PyCFunction)py_mh_cell_size_km, METH_VARARGS | METH_KEYWORDS, "Locator cell size (km)."},
    {"area_km2", (PyCFunction)py_mh_area_km2, METH_VARARGS | METH_KEYWORDS, "Locator area (km2)."},
    {"diagonal_km", (PyCFunction)py_mh_diagonal_km, METH_VARARGS | METH_KEYWORDS, "Locator diagonal (km)."},
    {"step", (PyCFunction)py_mh_step, METH_VARARGS | METH_KEYWORDS, "Step a locator."},
    {"contains_point", py_mh_contains_point, METH_VARARGS, "Point containment."},
    {"contains", py_mh_contains, METH_VARARGS, "Locator containment."},
    {"intersects_bbox", py_mh_intersects_bbox, METH_VARARGS, "Intersect locator with bbox."},
    {"intersects_polygon", py_mh_intersects_polygon, METH_VARARGS, "Intersect locator with polygon."},
    {"to_utm_zone", py_mh_to_utm_zone, METH_VARARGS, "UTM zone from locator."},
    {"to_utm_zone_many", py_mh_to_utm_zone_many, METH_VARARGS, "Bulk UTM zones."},
    {"contains_many", py_mh_contains_many, METH_VARARGS, "Bulk containment checks."},
    {"contains_point_many", py_mh_contains_point_many, METH_VARARGS, "Bulk point containment checks."},
    {"intersects_bbox_many", py_mh_intersects_bbox_many, METH_VARARGS, "Bulk bbox intersection checks."},
    {"intersects_polygon_many", py_mh_intersects_polygon_many, METH_VARARGS, "Bulk polygon intersection checks."},
    {"corners_many", py_mh_corners_many, METH_VARARGS, "Bulk locator corners."},
    {"parent", (PyCFunction)py_mh_parent, METH_VARARGS | METH_KEYWORDS, "Return parent locator."},
    {"children", (PyCFunction)py_mh_children, METH_VARARGS | METH_KEYWORDS, "Return child locators."},
    {"neighbors", (PyCFunction)py_mh_neighbors, METH_VARARGS | METH_KEYWORDS, "Neighbors at given ring."},
    {"neighbors_many", (PyCFunction)py_mh_neighbors_many, METH_VARARGS | METH_KEYWORDS, "Bulk neighbors."},
    {"adjacent", (PyCFunction)py_mh_adjacent, METH_VARARGS | METH_KEYWORDS, "Adjacent locators."},
    {"adjacent_many", (PyCFunction)py_mh_adjacent_many, METH_VARARGS | METH_KEYWORDS, "Bulk adjacent."},
    {"parent_many", (PyCFunction)py_mh_parent_many, METH_VARARGS | METH_KEYWORDS, "Bulk parent locators."},
    {"children_many", (PyCFunction)py_mh_children_many, METH_VARARGS | METH_KEYWORDS, "Bulk child locators."},
    {"cell_size_deg_many", py_mh_cell_size_deg_many, METH_VARARGS, "Bulk cell sizes (deg)."},
    {"cell_size_km_many", (PyCFunction)py_mh_cell_size_km_many, METH_VARARGS | METH_KEYWORDS, "Bulk cell sizes (km)."},
    {"area_km2_many", (PyCFunction)py_mh_area_km2_many, METH_VARARGS | METH_KEYWORDS, "Bulk area (km2)."},
    {"diagonal_km_many", (PyCFunction)py_mh_diagonal_km_many, METH_VARARGS | METH_KEYWORDS, "Bulk diagonal (km)."},
    {"azimuth_many", (PyCFunction)py_mh_azimuth_many, METH_VARARGS | METH_KEYWORDS, "Bulk azimuth."},
    {"initial_bearing_many", py_mh_initial_bearing_many, METH_VARARGS, "Bulk initial bearing."},
    {"to_wkt_many", py_mh_to_wkt_many, METH_VARARGS, "Bulk WKT."},
    {"to_geojson_polygon_many", py_mh_to_geojson_polygon_many, METH_VARARGS, "Bulk GeoJSON polygons."},
    {"to_geojson_feature_many", py_mh_to_geojson_feature_many, METH_VARARGS, "Bulk GeoJSON features."},
    {"to_geojson_bbox_many", py_mh_to_geojson_bbox_many, METH_VARARGS, "Bulk GeoJSON bboxes."},
    {"to_geojson_envelope_many", py_mh_to_geojson_envelope_many, METH_VARARGS, "Bulk GeoJSON envelopes."},
    {"split_bbox_many", py_mh_split_bbox_many, METH_VARARGS, "Bulk split bbox."},
    {"distance_km", (PyCFunction)py_mh_distance_km, METH_VARARGS | METH_KEYWORDS, "Distance between two points."},
    {"bearing_deg", py_mh_bearing_deg, METH_VARARGS, "Initial bearing between two points."},
    {"midpoint", py_mh_midpoint, METH_VARARGS, "Great-circle midpoint between points."},
    {"great_circle_path", (PyCFunction)py_mh_great_circle_path, METH_VARARGS | METH_KEYWORDS, "Great-circle path points."},
    {"bearing_bin", (PyCFunction)py_mh_bearing_bin, METH_VARARGS | METH_KEYWORDS, "Bearing bin start angle."},
    {"azimuthal_sector", (PyCFunction)py_mh_azimuthal_sector, METH_VARARGS | METH_KEYWORDS, "Azimuthal sector from A to B."},
    {"geodesic_midpoint", py_mh_geodesic_midpoint, METH_VARARGS, "Geodesic midpoint (native WGS84)."},
    {"cover_circle", (PyCFunction)py_mh_cover_circle, METH_VARARGS | METH_KEYWORDS, "Cover circle with grid squares."},
    {"cover_line", (PyCFunction)py_mh_cover_line, METH_VARARGS | METH_KEYWORDS, "Cover line with grid squares."},
    {"json_loads", py_mh_json_loads, METH_VARARGS, "Parse JSON using simdjson when available."},
    {"to_geojson_polygon", py_mh_to_geojson_polygon, METH_VARARGS, "GeoJSON polygon string for locator."},
    {"to_geojson_feature", py_mh_to_geojson_feature, METH_VARARGS, "GeoJSON feature string for locator."},
    {"to_geojson_feature_collection", py_mh_to_geojson_feature_collection, METH_VARARGS, "GeoJSON feature collection string."},
    {"to_geojson_bbox", py_mh_to_geojson_bbox, METH_VARARGS, "GeoJSON bbox array."},
    {"to_geojson_envelope", py_mh_to_geojson_envelope, METH_VARARGS, "GeoJSON envelope string for locator."},
    {"to_wkt", py_mh_to_wkt, METH_VARARGS, "WKT polygon string for locator."},
    {NULL, NULL, 0, NULL},
};

static struct PyModuleDef maidenhead_native_module = {
    PyModuleDef_HEAD_INIT,
    "_native",
    "Native C extension for maidenhead (stub).",
    -1,
    maidenhead_native_methods,
};

PyMODINIT_FUNC PyInit__native(void) {
    PyObject *module = PyModule_Create(&maidenhead_native_module);
    if (!module) {
        return NULL;
    }
    if (mh_import_exceptions() != 0) {
        Py_DECREF(module);
        return NULL;
    }
    return module;
}
