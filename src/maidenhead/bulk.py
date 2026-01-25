# maidenhead/bulk.py
"""Compatibility bulk helpers.

This module is kept to avoid import breakages. It delegates to the native
extension and provides a minimal wrapper for from_latlon_many.
"""

from typing import Sequence

from . import _native as _native_mh  # type: ignore

if not hasattr(_native_mh, "normalize"):
    raise ImportError("maidenhead native extension is required")


def from_latlon_many(
    lats: Sequence[float],
    lons: Sequence[float],
    *,
    precision: int = 6,
    clamp: bool = True,
    resolution_deg: float | tuple[float, float] | None = None,
) -> list[str]:
    if resolution_deg is not None:
        raise ValueError("resolution_deg is not supported in native-only mode")
    return _native_mh.from_latlon_many(list(lats), list(lons), precision=precision, clamp=clamp)


# direct native aliases

to_center_many = _native_mh.to_center_many
to_bbox_many = _native_mh.to_bbox_many
cell_size_deg_many = _native_mh.cell_size_deg_many
cell_size_km_many = _native_mh.cell_size_km_many
area_km2_many = _native_mh.area_km2_many
diagonal_km_many = _native_mh.diagonal_km_many
parent_many = _native_mh.parent_many
children_many = _native_mh.children_many
azimuth_many = _native_mh.azimuth_many
contains_point_many = _native_mh.contains_point_many
contains_many = _native_mh.contains_many
corners_many = _native_mh.corners_many
split_bbox_many = _native_mh.split_bbox_many
neighbors_many = _native_mh.neighbors_many
adjacent_many = _native_mh.adjacent_many
intersects_bbox_many = _native_mh.intersects_bbox_many
intersects_polygon_many = _native_mh.intersects_polygon_many
initial_bearing_many = _native_mh.initial_bearing_many
normalize_many = _native_mh.normalize_many
to_wkt_many = _native_mh.to_wkt_many
to_utm_zone_many = _native_mh.to_utm_zone_many
to_geojson_polygon_many = _native_mh.to_geojson_polygon_many
to_geojson_feature_many = _native_mh.to_geojson_feature_many
to_geojson_bbox_many = _native_mh.to_geojson_bbox_many
to_geojson_envelope_many = _native_mh.to_geojson_envelope_many

__all__ = [
    "from_latlon_many",
    "to_center_many",
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
    "normalize_many",
    "to_wkt_many",
    "to_utm_zone_many",
    "to_geojson_polygon_many",
    "to_geojson_feature_many",
    "to_geojson_bbox_many",
    "to_geojson_envelope_many",
]
