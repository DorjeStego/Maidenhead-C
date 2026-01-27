#ifndef MH_MAIDENHEAD_PARSE_H
#define MH_MAIDENHEAD_PARSE_H

#include <Python.h>

#include "core.h"

int mh_parse_point_sequence(PyObject *obj, mh_point **out_points, Py_ssize_t *out_len);
int mh_parse_double_sequence(PyObject *obj, double **out_vals, Py_ssize_t *out_len);
int mh_parse_locator_sequence(PyObject *obj, const char ***out_vals, Py_ssize_t *out_len);
int mh_parse_bbox_sequence(PyObject *obj, mh_bbox **out_bboxes, Py_ssize_t *out_len);
int mh_parse_point(PyObject *obj, mh_point *out);

#endif
