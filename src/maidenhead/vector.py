# maidenhead/vector.py
"""Compatibility vector helpers.

This module is retained to avoid import breakages. It currently forwards to
bulk helpers and returns native list outputs (no pandas/numpy adapters).
"""

from . import bulk as _bulk


def _unsupported_return_type(return_type: str) -> None:
    if return_type not in ("auto", "list", "tuple"):
        raise ImportError("numpy/pandas adapters are removed in native-only mode")


# thin wrappers to preserve prior signatures where possible

def from_latlon_many(*args, **kwargs):
    _unsupported_return_type(kwargs.get("return_type", "auto"))
    return _bulk.from_latlon_many(*args, **kwargs)


def normalize_many(locators, *, return_type: str = "auto"):
    _unsupported_return_type(return_type)
    return _bulk.normalize_many(locators)


def to_center_latlon_many(locators, *, return_type: str = "auto"):
    _unsupported_return_type(return_type)
    return _bulk.to_center_many(locators)


def to_bbox_many(locators, *, return_type: str = "auto"):
    _unsupported_return_type(return_type)
    return _bulk.to_bbox_many(locators)


# re-export common bulk helpers
cell_size_deg_many = _bulk.cell_size_deg_many
cell_size_km_many = _bulk.cell_size_km_many
area_km2_many = _bulk.area_km2_many
diagonal_km_many = _bulk.diagonal_km_many
parent_many = _bulk.parent_many
children_many = _bulk.children_many
azimuth_many = _bulk.azimuth_many
contains_point_many = _bulk.contains_point_many
contains_many = _bulk.contains_many
corners_many = _bulk.corners_many
split_bbox_many = _bulk.split_bbox_many
neighbors_many = _bulk.neighbors_many
adjacent_many = _bulk.adjacent_many
intersects_bbox_many = _bulk.intersects_bbox_many
intersects_polygon_many = _bulk.intersects_polygon_many
initial_bearing_many = _bulk.initial_bearing_many
to_wkt_many = _bulk.to_wkt_many
to_utm_zone_many = _bulk.to_utm_zone_many
to_geojson_polygon_many = _bulk.to_geojson_polygon_many
to_geojson_feature_many = _bulk.to_geojson_feature_many
to_geojson_features_many = _bulk.to_geojson_feature_many
to_geojson_bbox_many = _bulk.to_geojson_bbox_many
to_geojson_envelope_many = _bulk.to_geojson_envelope_many

__all__ = [
    "from_latlon_many",
    "normalize_many",
    "to_center_latlon_many",
    "to_bbox_many",
    "cell_size_deg_many",
    "cell_size_km_many",
    "area_km2_many",
    "diagonal_km_many",
    "parent_many",
    "children_many",
    "azimuth_many",
    "contains_point_many",
    "contains_many",
    "corners_many",
    "split_bbox_many",
    "neighbors_many",
    "adjacent_many",
    "intersects_bbox_many",
    "intersects_polygon_many",
    "initial_bearing_many",
    "to_wkt_many",
    "to_utm_zone_many",
    "to_geojson_polygon_many",
    "to_geojson_feature_many",
    "to_geojson_features_many",
    "to_geojson_bbox_many",
    "to_geojson_envelope_many",
]
