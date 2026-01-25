# maidenhead/geo.py
from typing import TYPE_CHECKING, Literal, Tuple, Union

from . import _native as _native_mh  # type: ignore

if not hasattr(_native_mh, "distance_km"):
    raise ImportError("maidenhead native extension is required")

if TYPE_CHECKING:
    from .mh_types import GridSquare, LocatorLike
else:
    LocatorLike = Union[str, object]

EARTH_RADIUS_KM: float = 6371.0088
DistanceMethod = Literal["haversine", "geodesic"]
PointLike = Union[LocatorLike, Tuple[float, float]]


def _resolve_point(p: PointLike) -> tuple[float, float]:
    from .mh_types import GridSquare
    from .core import to_center_latlon

    if isinstance(p, GridSquare):
        return to_center_latlon(p)
    if isinstance(p, str):
        return to_center_latlon(p)
    if (
        isinstance(p, tuple)
        and len(p) == 2
        and isinstance(p[0], (int, float))
        and isinstance(p[1], (int, float))
    ):
        return (float(p[0]), float(p[1]))
    raise TypeError(f"Unsupported point type: {type(p).__name__}")


def haversine_distance_km(
    a: PointLike,
    b: PointLike,
    *,
    radius_km: float = EARTH_RADIUS_KM,
) -> float:
    lat1, lon1 = _resolve_point(a)
    lat2, lon2 = _resolve_point(b)
    if radius_km != EARTH_RADIUS_KM:
        raise ValueError("custom radius is not supported in native-only mode")
    return float(_native_mh.distance_km((lat1, lon1), (lat2, lon2), method="haversine"))


def geodesic_distance_km(a: PointLike, b: PointLike) -> float:
    lat1, lon1 = _resolve_point(a)
    lat2, lon2 = _resolve_point(b)
    return float(_native_mh.distance_km((lat1, lon1), (lat2, lon2), method="geodesic"))


def distance_km(
    a: PointLike,
    b: PointLike,
    *,
    method: DistanceMethod = "haversine",
) -> float:
    lat1, lon1 = _resolve_point(a)
    lat2, lon2 = _resolve_point(b)
    return float(_native_mh.distance_km((lat1, lon1), (lat2, lon2), method=method))


def bearing_deg(a: PointLike, b: PointLike) -> float:
    lat1, lon1 = _resolve_point(a)
    lat2, lon2 = _resolve_point(b)
    return float(_native_mh.bearing_deg((lat1, lon1), (lat2, lon2)))


def midpoint(a: PointLike, b: PointLike) -> tuple[float, float]:
    lat1, lon1 = _resolve_point(a)
    lat2, lon2 = _resolve_point(b)
    lat, lon = _native_mh.midpoint((lat1, lon1), (lat2, lon2))
    return (float(lat), float(lon))


def great_circle_path(a: PointLike, b: PointLike, *, n: int = 64) -> list[tuple[float, float]]:
    lat1, lon1 = _resolve_point(a)
    lat2, lon2 = _resolve_point(b)
    return [(float(lat), float(lon)) for lat, lon in _native_mh.great_circle_path((lat1, lon1), (lat2, lon2), n=n)]


def bearing_bin(a: PointLike, b: PointLike, *, bins: int = 16, bin_size: float | None = None) -> float:
    lat1, lon1 = _resolve_point(a)
    lat2, lon2 = _resolve_point(b)
    if bin_size is not None:
        return float(_native_mh.bearing_bin((lat1, lon1), (lat2, lon2), bin_size=float(bin_size)))
    width_deg = 360.0 / float(bins)
    return float(_native_mh.bearing_bin((lat1, lon1), (lat2, lon2), bin_size=width_deg))


def azimuthal_sector(a: PointLike, b: PointLike, *, bins: int = 16, width_deg: float | None = None) -> tuple[float, float]:
    lat1, lon1 = _resolve_point(a)
    lat2, lon2 = _resolve_point(b)
    if width_deg is None:
        width_deg = 360.0 / float(bins)
    start, end = _native_mh.azimuthal_sector((lat1, lon1), (lat2, lon2), width_deg=float(width_deg))
    return (float(start), float(end))


def geodesic_midpoint(a: PointLike, b: PointLike) -> tuple[float, float]:
    lat1, lon1 = _resolve_point(a)
    lat2, lon2 = _resolve_point(b)
    lat, lon = _native_mh.geodesic_midpoint((lat1, lon1), (lat2, lon2))
    return (float(lat), float(lon))
