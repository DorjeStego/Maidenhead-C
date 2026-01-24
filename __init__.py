# maidenhead/__init__.py
from __future__ import annotations

"""
Maidenhead grid square utilities.

Public API is intentionally small:
- parsing/validation/normalization
- locator <-> lat/lon conversion
- bbox/center helpers
- neighborhood/topology helpers
- basic geodesy helpers (distance/bearing/midpoint)

Everything else is available from submodules (core, geo, vector, types).
"""

from importlib.metadata import PackageNotFoundError, version as _pkg_version

# ---- Version ----
try:
    __version__ = _pkg_version("maidenhead")
except PackageNotFoundError:  # pragma: no cover (common in editable/dev mode)
    __version__ = "0.0.0"


# ---- Public types/exceptions ----
try:
    from .errors import MaidenheadError, InvalidLocatorError, OutOfRangeError, PrecisionError
    from .types import GridSquare
except Exception:  # pragma: no cover - fallback when repo root isn't a package
    from maidenhead.errors import MaidenheadError, InvalidLocatorError, OutOfRangeError, PrecisionError
    from maidenhead.mh_types import GridSquare

# ---- Core API ----
try:
    from .core import (
        children,
        from_latlon,
        is_valid,
        neighbors,
        normalize,
        parent,
        parse,
        precision_of,
        to_bbox,
        to_center_latlon,
    )
except Exception:  # pragma: no cover - fallback when repo root isn't a package
    from maidenhead.core import (
        children,
        from_latlon,
        is_valid,
        neighbors,
        normalize,
        parent,
        parse,
        precision_of,
        to_bbox,
        to_center_latlon,
    )

# ---- Geodesy helpers ----
try:
    from .geo import bearing_deg, distance_km, midpoint
except Exception:  # pragma: no cover - fallback when repo root isn't a package
    from maidenhead.geo import bearing_deg, distance_km, midpoint

__all__ = [
    # meta
    "__version__",
    # types
    "GridSquare",
    # exceptions
    "MaidenheadError",
    "InvalidLocatorError",
    "PrecisionError",
    "OutOfRangeError",
    # core
    "parse",
    "is_valid",
    "normalize",
    "precision_of",
    "from_latlon",
    "to_center_latlon",
    "to_bbox",
    "neighbors",
    "parent",
    "children",
    # geo
    "distance_km",
    "bearing_deg",
    "midpoint",
]
