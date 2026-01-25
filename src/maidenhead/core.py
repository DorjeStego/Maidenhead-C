from typing import Callable, Iterable, Literal, Sequence

from . import _native as _native_mh  # type: ignore
from . import constants as C
from .errors import InvalidLocatorError, require
from .mh_types import GridSquare, LocatorLike, validate_precision

if not hasattr(_native_mh, "normalize"):
    raise ImportError("maidenhead native extension is required")


def precision_of(locator: str | GridSquare) -> int:
    """Return locator precision (character length)."""
    if isinstance(locator, GridSquare):
        return locator.precision
    if not isinstance(locator, str):
        raise InvalidLocatorError(
            f"locator must be str or GridSquare, got {type(locator).__name__}",
            locator=locator,
        )
    return int(_native_mh.precision_of(locator))


def normalize(locator: str) -> str:
    """Normalize a Maidenhead locator to canonical casing."""
    require(
        isinstance(locator, str),
        InvalidLocatorError,
        f"locator must be str, got {type(locator).__name__}",
        locator=locator,
    )
    return str(_native_mh.normalize(locator))


def is_valid(locator: str) -> bool:
    """Return True if locator parses and validates."""
    try:
        _ = normalize(locator)
        return True
    except Exception:
        return False


def parse(locator: str) -> GridSquare:
    """Parse and validate a locator string, returning a GridSquare."""
    return GridSquare(_native_mh.parse(locator))


def format_locator(
    locator: LocatorLike,
    *,
    precision: int,
    mode: Literal["truncate", "center", "error"] = "center",
) -> GridSquare:
    """
    Format locator precision.

    mode:
      - "truncate": drop extra precision
      - "center": center within target precision
      - "error": raise if precision differs
    """
    s = str(locator)
    if not isinstance(s, str):
        raise InvalidLocatorError("locator must be str or GridSquare", locator=locator)
    return GridSquare(_native_mh.format_locator(s, precision=precision, mode=mode))


def to_bbox(locator: LocatorLike) -> tuple[float, float, float, float]:
    s = str(locator)
    return tuple(_native_mh.to_bbox(s))  # type: ignore[return-value]


def to_bbox_split(
    locator: LocatorLike,
) -> tuple[tuple[float, float, float, float], tuple[float, float, float, float]] | tuple[float, float, float, float] | None:
    min_lat, min_lon, max_lat, max_lon = to_bbox(locator)
    parts = split_bbox_list((min_lat, min_lon, max_lat, max_lon))
    if len(parts) == 2:
        return (parts[0], parts[1])
    if max_lon == 180.0 or min_lon == -180.0:
        return (min_lat, min_lon, max_lat, max_lon)
    return None


def split_bbox_list(
    bbox: tuple[float, float, float, float],
) -> list[tuple[float, float, float, float]]:
    return list(_native_mh.split_bbox_list(*bbox))


def split_bbox(
    bbox: tuple[float, float, float, float],
) -> tuple[tuple[float, float, float, float], tuple[float, float, float, float]] | None:
    parts = split_bbox_list(bbox)
    if len(parts) == 2:
        return (parts[0], parts[1])
    return None


def contains_point(locator: LocatorLike, lat: float, lon: float) -> bool:
    s = str(locator)
    return bool(_native_mh.contains_point(s, float(lat), float(lon)))


def intersects_bbox(
    locator: LocatorLike,
    bbox: tuple[float, float, float, float],
) -> bool:
    s = str(locator)
    return bool(_native_mh.intersects_bbox(s, *bbox))


def intersects_polygon(
    locator: LocatorLike,
    polygon: Sequence[tuple[float, float]],
) -> bool:
    s = str(locator)
    return bool(_native_mh.intersects_polygon(s, list(polygon)))


def cover_circle(
    center: tuple[float, float],
    radius_km: float,
    precision: int,
) -> list[str]:
    lat_c, lon_c = center
    out = _native_mh.cover_circle((lat_c, lon_c), float(radius_km), precision)
    return [GridSquare(x) for x in out]


def cover_line(
    p1: tuple[float, float],
    p2: tuple[float, float],
    precision: int,
    *,
    method: Literal["great-circle", "rhumb"] = "great-circle",
) -> list[str]:
    out = _native_mh.cover_line(p1, p2, precision, method=method)
    return [GridSquare(x) for x in out]


def to_utm_zone(locator: LocatorLike) -> str:
    s = str(locator)
    return str(_native_mh.to_utm_zone(s))


def to_center_latlon(locator: LocatorLike) -> tuple[float, float]:
    s = str(locator)
    return tuple(_native_mh.to_center_latlon(s))  # type: ignore[return-value]


def to_geojson_polygon(locator: LocatorLike | tuple[float, float]) -> dict:
    if isinstance(locator, tuple):
        payload = _native_mh.to_geojson_polygon((float(locator[0]), float(locator[1])))
    else:
        payload = _native_mh.to_geojson_polygon(str(locator))
    return _native_mh.json_loads(payload)


def to_geojson_feature(
    locator: LocatorLike | tuple[float, float],
    *,
    properties: dict | None = None,
) -> dict:
    if properties is not None:
        return {
            "type": "Feature",
            "geometry": to_geojson_polygon(locator),
            "properties": properties,
        }
    if isinstance(locator, tuple):
        payload = _native_mh.to_geojson_feature(locator)
    else:
        payload = _native_mh.to_geojson_feature(str(locator))
    return _native_mh.json_loads(payload)


def to_geojson_feature_collection(
    locators: Iterable[LocatorLike | tuple[float, float]],
    *,
    properties_fn: Callable[[LocatorLike | tuple[float, float]], dict] | None = None,
) -> dict:
    locs = []
    for loc in locators:
        if isinstance(loc, tuple):
            locs.append((float(loc[0]), float(loc[1])))
        else:
            locs.append(str(str(loc)))
    if properties_fn is None:
        payload = _native_mh.to_geojson_feature_collection(locs)
        return _native_mh.json_loads(payload)
    features = [to_geojson_feature(loc, properties=properties_fn(loc)) for loc in locs]
    return {"type": "FeatureCollection", "features": features}


def to_geojson_bbox(locator: LocatorLike | tuple[float, float]) -> list[float]:
    if isinstance(locator, tuple):
        return list(_native_mh.to_geojson_bbox((float(locator[0]), float(locator[1]))))
    return list(_native_mh.to_geojson_bbox(str(locator)))


def to_geojson_envelope(locator: LocatorLike | tuple[float, float]) -> dict:
    if isinstance(locator, tuple):
        payload = _native_mh.to_geojson_envelope(locator)
    else:
        payload = _native_mh.to_geojson_envelope(str(locator))
    return _native_mh.json_loads(payload)


def to_wkt(locator: LocatorLike | tuple[float, float]) -> str:
    if isinstance(locator, tuple):
        return str(_native_mh.to_wkt((float(locator[0]), float(locator[1]))))
    return str(_native_mh.to_wkt(str(locator)))


def corners(locator: LocatorLike) -> tuple[tuple[float, float], tuple[float, float], tuple[float, float], tuple[float, float]]:
    s = str(locator)
    return tuple(_native_mh.corners(s))  # type: ignore[return-value]


def azimuth(
    locator_a: LocatorLike,
    locator_b: LocatorLike,
    *,
    range_mode: bool = False,
) -> tuple[float, float] | tuple[float, float, float]:
    out = _native_mh.azimuth_many(
        [str(str(locator_a))],
        [str(str(locator_b))],
        range_mode=range_mode,
    )
    return tuple(out[0])  # type: ignore[return-value]


def initial_bearing(locator_a: LocatorLike, locator_b: LocatorLike) -> float:
    out = _native_mh.initial_bearing_many(
        [str(str(locator_a))],
        [str(str(locator_b))],
    )
    return float(out[0])


def cell_size(
    locator_or_precision: LocatorLike | int,
    *,
    unit: Literal["deg", "km", "miles"] = "deg",
    at_lat: float | None = None,
    method: Literal["spherical", "geodesic"] = "spherical",
) -> tuple[float, float]:
    if isinstance(locator_or_precision, int):
        precision = validate_precision(locator_or_precision)
        if unit != "deg":
            raise ValueError("unit must be 'deg' when using precision only")
        step = C.step_size_for_pair(precision // 2)
        return (step.lon_step_deg, step.lat_step_deg)
    locator = str(locator_or_precision)
    if unit == "deg":
        return tuple(_native_mh.cell_size_deg(locator))  # type: ignore[return-value]
    if unit in ("km", "miles"):
        width_km, height_km = _native_mh.cell_size_km(locator, at_lat=at_lat, method=method)
        if unit == "km":
            return (float(width_km), float(height_km))
        miles_per_km = 0.621371
        return (float(width_km) * miles_per_km, float(height_km) * miles_per_km)
    raise ValueError(f"Unknown unit: {unit!r}")


def cell_size_deg(locator: LocatorLike) -> tuple[float, float]:
    return cell_size(locator, unit="deg")


def cell_size_km(
    locator: LocatorLike,
    *,
    at_lat: float | None = None,
    method: Literal["spherical", "geodesic"] = "spherical",
) -> tuple[float, float]:
    return cell_size(locator, unit="km", at_lat=at_lat, method=method)


def area_km2(
    locator: LocatorLike,
    *,
    method: Literal["spherical", "geodesic"] = "spherical",
) -> float:
    return float(_native_mh.area_km2(str(locator), method=method))


def diagonal_km(
    locator: LocatorLike,
    *,
    method: Literal["spherical", "geodesic"] = "spherical",
) -> float:
    return float(_native_mh.diagonal_km(str(locator), method=method))


def from_latlon(
    lat: float,
    lon: float,
    *,
    precision: int = 6,
    clamp: bool = True,
    resolution_deg: float | tuple[float, float] | None = None,
) -> GridSquare:
    validate_precision(precision)
    if resolution_deg is not None:
        raise ValueError("resolution_deg is not supported in native-only mode")
    return GridSquare(_native_mh.from_latlon(float(lat), float(lon), precision=precision, clamp=clamp))


def parent(locator: LocatorLike, *, precision: int | None = None) -> GridSquare:
    s = str(locator)
    return GridSquare(_native_mh.parent(s, precision=precision))


def children(
    locator: LocatorLike,
    *,
    precision: int | None = None,
) -> Iterable[GridSquare]:
    s = str(locator)
    if precision is None:
        precision = parse(str(s)).precision + 2
    for child in _native_mh.children(s, precision=precision):
        yield GridSquare(child)


def neighbors(
    locator: LocatorLike,
    *,
    ring: int = 1,
    diagonals: bool = True,
) -> list[GridSquare]:
    s = str(locator)
    return [GridSquare(x) for x in _native_mh.neighbors(s, ring=ring, diagonals=diagonals)]


def adjacent(
    locator: LocatorLike,
    *,
    diagonals: bool = False,
) -> dict[str, GridSquare]:
    s = str(locator)
    return {k: GridSquare(v) for k, v in _native_mh.adjacent(s, diagonals=diagonals).items()}


def step(
    locator: LocatorLike,
    *,
    dlat_cells: int = 0,
    dlon_cells: int = 0,
) -> GridSquare:
    s = str(locator)
    return GridSquare(_native_mh.step(s, dlat_cells=dlat_cells, dlon_cells=dlon_cells))


def contains(outer: LocatorLike, inner: LocatorLike) -> bool:
    o = str(outer)
    i = str(inner)
    return bool(_native_mh.contains(o, i))
