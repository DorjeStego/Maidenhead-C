import pytest

from maidenhead import core

from maidenhead import vector

try:
    from maidenhead import _native as _native_mh  # type: ignore
except Exception:  # pragma: no cover - optional native module
    _native_mh = None


@pytest.mark.skipif(_native_mh is None, reason="native extension not available")
def test_vector_from_latlon_many_native():
    lats = [0.0, 45.5, -12.3]
    lons = [0.0, 179.9, -179.9]
    expected = _native_mh.from_latlon_many(lats, lons, precision=6)
    assert vector.from_latlon_many(lats, lons, precision=6) == expected


@pytest.mark.skipif(_native_mh is None, reason="native extension not available")
def test_vector_to_center_latlon_many_native():
    locators = ["AA00aa", "CM98jw", "FN31pr"]
    exp_lat, exp_lon = _native_mh.to_center_many(locators)
    got_lat, got_lon = vector.to_center_latlon_many(locators, return_type="tuple")
    assert got_lat == pytest.approx(exp_lat)
    assert got_lon == pytest.approx(exp_lon)


@pytest.mark.skipif(_native_mh is None, reason="native extension not available")
def test_vector_to_bbox_many_native():
    locators = ["AA00aa", "CM98jw", "FN31pr"]
    exp_min_lat, exp_min_lon, exp_max_lat, exp_max_lon = _native_mh.to_bbox_many(locators)
    got_min_lat, got_min_lon, got_max_lat, got_max_lon = vector.to_bbox_many(locators, return_type="tuple")
    assert got_min_lat == pytest.approx(exp_min_lat)
    assert got_min_lon == pytest.approx(exp_min_lon)
    assert got_max_lat == pytest.approx(exp_max_lat)
    assert got_max_lon == pytest.approx(exp_max_lon)


@pytest.mark.skipif(_native_mh is None, reason="native extension not available")
def test_vector_normalize_many_native():
    locators = ["aa00aa", "CM98jw", "fn31pr"]
    expected = _native_mh.normalize_many(locators)
    assert vector.normalize_many(locators, return_type="list") == expected


@pytest.mark.skipif(_native_mh is None, reason="native extension not available")
def test_vector_to_utm_zone_many_native():
    locators = ["AA00aa", "CM98jw", "FN31pr"]
    expected = _native_mh.to_utm_zone_many(locators)
    assert vector.to_utm_zone_many(locators, return_type="list") == expected


@pytest.mark.skipif(_native_mh is None, reason="native extension not available")
def test_vector_contains_many_native():
    locs = ["AA00aa", "CM98jw", "FN31pr"]
    outers = vector.parent_many(locs, precision=4, return_type="list")
    expected = _native_mh.contains_many(outers, locs)
    assert vector.contains_many(outers, locs, return_type="list") == expected


@pytest.mark.skipif(_native_mh is None, reason="native extension not available")
def test_vector_contains_point_many_native():
    locators = ["AA00aa", "CM98jw", "FN31pr"]
    lats = [0.0, 45.0, -20.0]
    lons = [0.0, 170.0, -170.0]
    expected = _native_mh.contains_point_many(locators, lats, lons)
    assert vector.contains_point_many(locators, lats, lons, return_type="list") == expected


@pytest.mark.skipif(_native_mh is None, reason="native extension not available")
def test_vector_intersects_bbox_many_native():
    locators = ["AA00aa", "CM98jw", "FN31pr"]
    bboxes = [vector.to_bbox_many([loc], return_type="list")[0] for loc in locators]
    expected = _native_mh.intersects_bbox_many(locators, bboxes)
    assert vector.intersects_bbox_many(locators, bboxes, return_type="list") == expected


@pytest.mark.skipif(_native_mh is None, reason="native extension not available")
def test_vector_corners_many_native():
    locators = ["AA00aa", "CM98jw", "FN31pr"]
    expected = _native_mh.corners_many(locators)
    assert vector.corners_many(locators, return_type="list") == expected


@pytest.mark.skipif(_native_mh is None, reason="native extension not available")
def test_vector_neighbors_many_native():
    locators = ["AA00aa", "CM98jw"]
    expected = _native_mh.neighbors_many(locators, ring=1, diagonals=True)
    assert vector.neighbors_many(locators, ring=1, diagonals=True, return_type="list") == expected


@pytest.mark.skipif(_native_mh is None, reason="native extension not available")
def test_vector_adjacent_many_native():
    locators = ["AA00aa", "CM98jw"]
    expected = _native_mh.adjacent_many(locators, diagonals=True)
    assert vector.adjacent_many(locators, diagonals=True, return_type="list") == expected


@pytest.mark.skipif(_native_mh is None, reason="native extension not available")
def test_vector_intersects_polygon_many_native():
    locators = ["AA00aa", "CM98jw"]
    polygons = []
    for loc in locators:
        min_lat, min_lon, max_lat, max_lon = vector.to_bbox_many([loc], return_type="list")[0]
        polygons.append(
            [
                (min_lat, min_lon),
                (min_lat, max_lon),
                (max_lat, max_lon),
                (max_lat, min_lon),
            ]
        )
    expected = _native_mh.intersects_polygon_many(locators, polygons)
    assert vector.intersects_polygon_many(locators, polygons, return_type="list") == expected


@pytest.mark.skipif(_native_mh is None, reason="native extension not available")
def test_vector_parent_many_native():
    locators = ["AA00aa", "CM98jw", "FN31pr"]
    expected = _native_mh.parent_many(locators, precision=4)
    assert vector.parent_many(locators, precision=4, return_type="list") == expected


@pytest.mark.skipif(_native_mh is None, reason="native extension not available")
def test_vector_children_many_native():
    locators = ["AA00aa", "CM98jw"]
    expected = _native_mh.children_many(locators, precision=8, limit=3)
    assert vector.children_many(locators, precision=8, limit=3, return_type="list") == expected


@pytest.mark.skipif(_native_mh is None, reason="native extension not available")
def test_vector_cell_size_deg_many_native():
    locators = ["AA00aa", "CM98jw", "FN31pr"]
    widths, heights = _native_mh.cell_size_deg_many(locators)
    expected = list(zip(widths, heights))
    assert vector.cell_size_deg_many(locators, return_type="list") == expected


@pytest.mark.skipif(_native_mh is None, reason="native extension not available")
def test_vector_cell_size_km_many_native():
    locators = ["AA00aa", "CM98jw", "FN31pr"]
    widths, heights = _native_mh.cell_size_km_many(locators, method="spherical")
    expected = list(zip(widths, heights))
    assert vector.cell_size_km_many(locators, method="spherical", return_type="list") == expected


@pytest.mark.skipif(_native_mh is None, reason="native extension not available")
def test_vector_area_km2_many_native():
    locators = ["AA00aa", "CM98jw", "FN31pr"]
    expected = _native_mh.area_km2_many(locators, method="spherical")
    assert vector.area_km2_many(locators, method="spherical", return_type="list") == expected


@pytest.mark.skipif(_native_mh is None, reason="native extension not available")
def test_vector_diagonal_km_many_native():
    locators = ["AA00aa", "CM98jw", "FN31pr"]
    expected = _native_mh.diagonal_km_many(locators, method="spherical")
    assert vector.diagonal_km_many(locators, method="spherical", return_type="list") == expected


@pytest.mark.skipif(_native_mh is None, reason="native extension not available")
def test_vector_to_wkt_many_native():
    locators = ["AA00aa", "CM98jw", "FN31pr"]
    expected = _native_mh.to_wkt_many(locators)
    assert vector.to_wkt_many(locators, return_type="list") == expected


@pytest.mark.skipif(_native_mh is None, reason="native extension not available")
def test_vector_azimuth_many_native():
    locators_a = ["AA00aa", "CM98jw", "FN31pr"]
    locators_b = ["AB00aa", "CM99jw", "FN32pr"]
    expected = _native_mh.azimuth_many(locators_a, locators_b, range_mode=True)
    assert vector.azimuth_many(locators_a, locators_b, range_mode=True, return_type="list") == expected


@pytest.mark.skipif(_native_mh is None, reason="native extension not available")
def test_vector_geojson_many_native():
    items = ["AA00aa", (0.0, 0.0)]
    expected_poly = [core.to_geojson_polygon(loc) for loc in items]
    assert vector.to_geojson_polygon_many(items, return_type="list") == expected_poly

    expected_feat = [core.to_geojson_feature(loc) for loc in items]
    assert vector.to_geojson_feature_many(items, return_type="list") == expected_feat

    expected_feats = [core.to_geojson_feature(loc) for loc in items]
    assert vector.to_geojson_features_many(items, return_type="list") == expected_feats

    expected_bbox = [core.to_geojson_bbox(loc) for loc in items]
    assert vector.to_geojson_bbox_many(items, return_type="list") == expected_bbox

    expected_env = [core.to_geojson_envelope(loc) for loc in items]
    assert vector.to_geojson_envelope_many(items, return_type="list") == expected_env


@pytest.mark.skipif(_native_mh is None, reason="native extension not available")
def test_vector_split_bbox_many_native():
    bboxes = [
        (10.0, 170.0, 20.0, -170.0),
        (10.0, 10.0, 20.0, 20.0),
    ]
    expected = [core.split_bbox_list(b) for b in bboxes]
    assert vector.split_bbox_many(bboxes, return_type="list") == expected
