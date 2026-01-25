import pytest

from maidenhead import core, vector

try:
    from maidenhead import _native as _native_mh  # type: ignore
    if not hasattr(_native_mh, "normalize"):
        _native_mh = None
except Exception:  # pragma: no cover - optional native module
    _native_mh = None


@pytest.mark.skipif(_native_mh is None, reason="native extension not available")
def test_vector_from_latlon_many_forwards():
    lats = [0.0, 45.5, -12.3]
    lons = [0.0, 179.9, -179.9]
    expected = _native_mh.from_latlon_many(lats, lons, precision=6)
    assert vector.from_latlon_many(lats, lons, precision=6) == expected


@pytest.mark.skipif(_native_mh is None, reason="native extension not available")
def test_vector_to_center_latlon_many_forwards():
    locators = ["AA00aa", "CM98jw", "FN31pr"]
    expected = _native_mh.to_center_many(locators)
    got = vector.to_center_latlon_many(locators)
    assert got[0] == pytest.approx(expected[0])
    assert got[1] == pytest.approx(expected[1])


@pytest.mark.skipif(_native_mh is None, reason="native extension not available")
def test_vector_to_bbox_many_forwards():
    locators = ["AA00aa", "CM98jw", "FN31pr"]
    expected = _native_mh.to_bbox_many(locators)
    got = vector.to_bbox_many(locators)
    for idx in range(4):
        assert got[idx] == pytest.approx(expected[idx])


@pytest.mark.skipif(_native_mh is None, reason="native extension not available")
def test_vector_geojson_many_forwards():
    items = ["AA00aa", (0.0, 0.0)]
    expected_poly = [core.to_geojson_polygon(loc) for loc in items]
    got_poly = vector.to_geojson_polygon_many(items)
    assert got_poly == expected_poly

    expected_feat = [core.to_geojson_feature(loc) for loc in items]
    got_feat = vector.to_geojson_feature_many(items)
    assert got_feat == expected_feat

    expected_bbox = [core.to_geojson_bbox(loc) for loc in items]
    got_bbox = vector.to_geojson_bbox_many(items)
    assert got_bbox == expected_bbox

    expected_env = [core.to_geojson_envelope(loc) for loc in items]
    got_env = vector.to_geojson_envelope_many(items)
    assert got_env == expected_env


def test_vector_rejects_numpy_pandas_return_type():
    with pytest.raises(ImportError):
        vector.normalize_many(["IO83ri"], return_type="numpy")
    with pytest.raises(ImportError):
        vector.normalize_many(["IO83ri"], return_type="pandas")
