import pytest

from maidenhead.errors import MissingDependencyError
from maidenhead.geo import (
    azimuthal_sector,
    bearing_bin,
    bearing_deg,
    distance_km,
    geodesic_distance_km,
    geodesic_midpoint,
    great_circle_path,
    haversine_distance_km,
    midpoint,
)


def test_distance_symmetric():
    d1 = distance_km((0.0, 0.0), (10.0, 10.0))
    d2 = distance_km((10.0, 10.0), (0.0, 0.0))
    assert d1 == pytest.approx(d2)

def test_haversine_custom_radius():
    with pytest.raises(ValueError):
        haversine_distance_km((0.0, 0.0), (0.0, 90.0), radius_km=1.0)

def test_geodesic_distance():
    try:
        dist = geodesic_distance_km((0.0, 0.0), (0.0, 1.0))
    except MissingDependencyError as exc:
        assert "geodesic distance requires" in str(exc)
        return
    assert dist == pytest.approx(111.319, rel=1e-3)

def test_geodesic_midpoint():
    try:
        lat, lon = geodesic_midpoint((0.0, 0.0), (0.0, 10.0))
    except MissingDependencyError as exc:
        assert "geodesic midpoint requires" in str(exc)
        return
    assert lat == pytest.approx(0.0, abs=1e-6)
    assert lon == pytest.approx(5.0, abs=1e-6)


def test_bearing_due_north():
    br = bearing_deg((0.0, 0.0), (1.0, 0.0))
    assert br == pytest.approx(0.0, abs=1e-6)


def test_midpoint_on_equator():
    lat, lon = midpoint((0.0, 0.0), (0.0, 10.0))
    assert lat == pytest.approx(0.0, abs=1e-6)
    assert lon == pytest.approx(5.0, abs=1e-6)


def test_great_circle_path_endpoints():
    pts = great_circle_path((0.0, 0.0), (0.0, 10.0), n=3)
    assert pts[0] == pytest.approx((0.0, 0.0))
    assert pts[-1] == pytest.approx((0.0, 10.0))


def test_bearing_bin():
    b = bearing_bin((0.0, 0.0), (0.0, 10.0), bin_size=10.0)
    assert b == pytest.approx(90.0)


def test_azimuthal_sector():
    start, end = azimuthal_sector((0.0, 0.0), (0.0, 10.0), width_deg=20.0)
    assert start == pytest.approx(80.0)
    assert end == pytest.approx(100.0)
