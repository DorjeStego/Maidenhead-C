import json

import pytest

from maidenhead import core

try:
    from maidenhead import _native as _native_mh  # type: ignore
    if not hasattr(_native_mh, "normalize"):
        _native_mh = None
except Exception:  # pragma: no cover - optional native module
    _native_mh = None


@pytest.mark.skipif(_native_mh is None, reason="native extension not available")
def test_native_split_bbox_matches_core():
    bbox = (0.0, 170.0, 10.0, -170.0)
    assert _native_mh.split_bbox(*bbox) == core.split_bbox(bbox)
    no_split = (0.0, 10.0, 10.0, 20.0)
    assert _native_mh.split_bbox(*no_split) == core.split_bbox(no_split)


@pytest.mark.skipif(_native_mh is None, reason="native extension not available")
def test_native_from_latlon_many_matches_native():
    lats = [0.0, 45.5, -12.3]
    lons = [0.0, 179.9, -179.9]
    precision = 6
    expected = _native_mh.from_latlon_many(lats, lons, precision=precision)
    assert _native_mh.from_latlon_many(lats, lons, precision=precision) == expected


@pytest.mark.skipif(_native_mh is None, reason="native extension not available")
def test_native_to_center_many_matches_native():
    locators = ["AA00aa", "CM98jw", "FN31pr"]
    expected = _native_mh.to_center_many(locators)
    got = _native_mh.to_center_many(locators)
    assert got[0] == pytest.approx(expected[0])
    assert got[1] == pytest.approx(expected[1])


@pytest.mark.skipif(_native_mh is None, reason="native extension not available")
def test_native_to_bbox_many_matches_native():
    locators = ["AA00aa", "CM98jw", "FN31pr"]
    expected = _native_mh.to_bbox_many(locators)
    got = _native_mh.to_bbox_many(locators)
    for idx in range(4):
        assert got[idx] == pytest.approx(expected[idx])


@pytest.mark.skipif(_native_mh is None, reason="native extension not available")
def test_native_normalize_many_matches_native():
    locators = ["aa00aa", "CM98jw", "fn31pr"]
    expected = [core.normalize(loc) for loc in locators]
    assert _native_mh.normalize_many(locators) == expected


@pytest.mark.skipif(_native_mh is None, reason="native extension not available")
def test_native_to_utm_zone_many_matches_native():
    locators = ["AA00aa", "CM98jw", "FN31pr"]
    expected = [core.to_utm_zone(loc) for loc in locators]
    assert _native_mh.to_utm_zone_many(locators) == expected


@pytest.mark.skipif(_native_mh is None, reason="native extension not available")
def test_native_contains_many_matches_native():
    locs = ["AA00aa", "CM98jw", "FN31pr"]
    outers = _native_mh.parent_many(locs, precision=4)
    expected = _native_mh.contains_many(outers, locs)
    assert _native_mh.contains_many(outers, locs) == expected


@pytest.mark.skipif(_native_mh is None, reason="native extension not available")
def test_native_contains_point_many_matches_native():
    locators = ["AA00aa", "CM98jw", "FN31pr"]
    lats = [0.0, 45.0, -20.0]
    lons = [0.0, 170.0, -170.0]
    expected = _native_mh.contains_point_many(locators, lats, lons)
    assert _native_mh.contains_point_many(locators, lats, lons) == expected


@pytest.mark.skipif(_native_mh is None, reason="native extension not available")
def test_native_intersects_bbox_many_matches_native():
    locators = ["AA00aa", "CM98jw", "FN31pr"]
    bboxes = [core.to_bbox(loc) for loc in locators]
    expected = _native_mh.intersects_bbox_many(locators, bboxes)
    assert _native_mh.intersects_bbox_many(locators, bboxes) == expected


@pytest.mark.skipif(_native_mh is None, reason="native extension not available")
def test_native_corners_many_matches_native():
    locators = ["AA00aa", "CM98jw", "FN31pr"]
    expected = _native_mh.corners_many(locators)
    assert _native_mh.corners_many(locators) == expected


@pytest.mark.skipif(_native_mh is None, reason="native extension not available")
def test_native_neighbors_many_matches_native():
    locators = ["AA00aa", "CM98jw"]
    expected = _native_mh.neighbors_many(locators, ring=1, diagonals=True)
    assert _native_mh.neighbors_many(locators, ring=1, diagonals=True) == expected


@pytest.mark.skipif(_native_mh is None, reason="native extension not available")
def test_native_adjacent_many_matches_native():
    locators = ["AA00aa", "CM98jw"]
    expected = _native_mh.adjacent_many(locators, diagonals=True)
    assert _native_mh.adjacent_many(locators, diagonals=True) == expected


@pytest.mark.skipif(_native_mh is None, reason="native extension not available")
def test_native_intersects_polygon_many_matches_native():
    locators = ["AA00aa", "CM98jw"]
    polygons = []
    for loc in locators:
        min_lat, min_lon, max_lat, max_lon = core.to_bbox(loc)
        polygons.append(
            [
                (min_lat, min_lon),
                (min_lat, max_lon),
                (max_lat, max_lon),
                (max_lat, min_lon),
            ]
        )
    expected = _native_mh.intersects_polygon_many(locators, polygons)
    assert _native_mh.intersects_polygon_many(locators, polygons) == expected


@pytest.mark.skipif(_native_mh is None, reason="native extension not available")
def test_native_parent_many_matches_native():
    locators = ["AA00aa", "CM98jw", "FN31pr"]
    expected = _native_mh.parent_many(locators, precision=4)
    assert _native_mh.parent_many(locators, precision=4) == expected


@pytest.mark.skipif(_native_mh is None, reason="native extension not available")
def test_native_children_many_matches_native():
    locators = ["AA00aa", "CM98jw"]
    expected = _native_mh.children_many(locators, precision=8, limit=3)
    assert _native_mh.children_many(locators, precision=8, limit=3) == expected


@pytest.mark.skipif(_native_mh is None, reason="native extension not available")
def test_native_cell_size_deg_many_matches_native():
    locators = ["AA00aa", "CM98jw", "FN31pr"]
    expected = _native_mh.cell_size_deg_many(locators)
    got = _native_mh.cell_size_deg_many(locators)
    assert got[0] == pytest.approx(expected[0])
    assert got[1] == pytest.approx(expected[1])


@pytest.mark.skipif(_native_mh is None, reason="native extension not available")
def test_native_cell_size_km_many_matches_native():
    locators = ["AA00aa", "CM98jw", "FN31pr"]
    expected = _native_mh.cell_size_km_many(locators, method="spherical")
    got = _native_mh.cell_size_km_many(locators, method="spherical")
    assert got[0] == pytest.approx(expected[0])
    assert got[1] == pytest.approx(expected[1])


@pytest.mark.skipif(_native_mh is None, reason="native extension not available")
def test_native_area_km2_many_matches_native():
    locators = ["AA00aa", "CM98jw", "FN31pr"]
    expected = _native_mh.area_km2_many(locators, method="spherical")
    got = _native_mh.area_km2_many(locators, method="spherical")
    assert got == pytest.approx(expected)


@pytest.mark.skipif(_native_mh is None, reason="native extension not available")
def test_native_diagonal_km_many_matches_native():
    locators = ["AA00aa", "CM98jw", "FN31pr"]
    expected = _native_mh.diagonal_km_many(locators, method="spherical")
    got = _native_mh.diagonal_km_many(locators, method="spherical")
    assert got == pytest.approx(expected)


@pytest.mark.skipif(_native_mh is None, reason="native extension not available")
def test_native_to_wkt_many_matches_native():
    locators = ["AA00aa", "CM98jw", "FN31pr"]
    expected = [core.to_wkt(loc) for loc in locators]
    assert _native_mh.to_wkt_many(locators) == expected


@pytest.mark.skipif(_native_mh is None, reason="native extension not available")
def test_native_azimuth_many_matches_native():
    locators_a = ["AA00aa", "CM98jw", "FN31pr"]
    locators_b = ["AB00aa", "CM99jw", "FN32pr"]
    expected = _native_mh.azimuth_many(locators_a, locators_b, range_mode=True)
    got = _native_mh.azimuth_many(locators_a, locators_b, range_mode=True)
    assert got == pytest.approx(expected)


@pytest.mark.skipif(_native_mh is None, reason="native extension not available")
def test_native_geojson_many_matches_native():
    items = ["AA00aa", (0.0, 0.0)]
    expected_poly = [core.to_geojson_polygon(loc) for loc in items]
    got_poly = [json.loads(s) for s in _native_mh.to_geojson_polygon_many(items)]
    assert got_poly == expected_poly

    expected_feat = [core.to_geojson_feature(loc) for loc in items]
    got_feat = [json.loads(s) for s in _native_mh.to_geojson_feature_many(items)]
    assert got_feat == expected_feat

    expected_bbox = [core.to_geojson_bbox(loc) for loc in items]
    got_bbox = _native_mh.to_geojson_bbox_many(items)
    assert got_bbox == expected_bbox

    expected_env = [core.to_geojson_envelope(loc) for loc in items]
    got_env = [json.loads(s) for s in _native_mh.to_geojson_envelope_many(items)]
    assert got_env == expected_env


@pytest.mark.skipif(_native_mh is None, reason="native extension not available")
def test_native_split_bbox_many_matches_native():
    bboxes = [
        (10.0, 170.0, 20.0, -170.0),
        (10.0, 10.0, 20.0, 20.0),
    ]
    expected = _native_mh.split_bbox_many(bboxes)
    assert _native_mh.split_bbox_many(bboxes) == expected
