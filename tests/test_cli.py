import json
import os
import random
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
if str(SRC) not in sys.path:
    sys.path.insert(0, str(SRC))

try:
    import orjson  # type: ignore
except Exception:  # pragma: no cover - optional in local env
    orjson = None

from maidenhead import normalize, step
from maidenhead.core import to_bbox, to_center_latlon, to_geojson_bbox, to_utm_zone
from maidenhead.geo import bearing_deg, distance_km
FIXTURES_PATH = Path(__file__).with_name("fixtures_cli.json")


def _load_fixtures():
    with FIXTURES_PATH.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def _find_native_cli() -> Path | None:
    env_path = os.environ.get("MAIDENHEAD_CLI_PATH")
    candidates = []
    if env_path:
        candidates.append(Path(env_path))
    candidates.extend(
        [
            ROOT / "cmake-build-native-313" / "mh_cli",
            ROOT / "cmake-build-debug" / "mh_cli",
            ROOT / "cmake-build-default" / "mh_cli",
        ]
    )
    candidates.extend(Path(path) for path in ROOT.glob("cmake-build-native-*/mh_cli"))
    candidates.extend(Path(path) for path in ROOT.glob("build/temp.*/*/mh_cli"))
    valid = [path for path in candidates if path.is_file() and os.access(path, os.X_OK)]
    if not valid:
        return None
    return max(valid, key=lambda p: p.stat().st_mtime)


def _cli_env() -> dict[str, str]:
    env = os.environ.copy()
    env["PYTHONPATH"] = f"{SRC}:{env.get('PYTHONPATH', '')}"
    native_cli = _find_native_cli()
    if native_cli is not None:
        env["MAIDENHEAD_CLI_PATH"] = str(native_cli)
    return env


def _run_cli(args: list[str], stdin: str | None = None) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, "-m", "maidenhead.cli_native", *args],
        input=stdin,
        text=True,
        capture_output=True,
        env=_cli_env(),
    )


def _run_cli_ok(args: list[str], stdin: str | None = None) -> str:
    proc = _run_cli(args, stdin=stdin)
    assert proc.returncode == 0, proc.stderr
    return proc.stdout.strip()


def test_cli_normalize(valid_locators):
    rng = random.Random(5)
    loc = rng.choice(valid_locators)
    out = _run_cli_ok(["normalize", loc.swapcase()])
    assert out == normalize(loc)


def test_cli_center_literal_expected():
    out = _run_cli_ok(["center", "IO83ri", "--digits", "6"])
    assert out == "53.354167 -2.541667"


def test_cli_bbox_literal_expected():
    out = _run_cli_ok(["bbox", "IO83ri", "--digits", "6"])
    assert out == "53.333333 -2.583333 53.375000 -2.500000"


def test_cli_roundtrip_center_within_bbox():
    loc = _run_cli_ok(["from-latlon", "53.365418,-2.574069"])
    center = _run_cli_ok(["center", loc, "--digits", "6"])
    bbox = _run_cli_ok(["bbox", loc, "--digits", "6"])
    lat, lon = [float(v) for v in center.split()]
    min_lat, min_lon, max_lat, max_lon = [float(v) for v in bbox.split()]
    assert min_lat <= lat <= max_lat
    assert min_lon <= lon <= max_lon


def test_cli_golden_fixtures():
    fixtures = _load_fixtures()
    for item in fixtures["center"]:
        out = _run_cli_ok(
            ["center", item["locator"], "--digits", str(item["digits"])]
        )
        assert out == item["output"]
    for item in fixtures["bbox"]:
        out = _run_cli_ok(
            ["bbox", item["locator"], "--digits", str(item["digits"])]
        )
        assert out == item["output"]
    for item in fixtures["from_latlon"]:
        out = _run_cli_ok(
            ["from-latlon", item["latlon"], "--precision", str(item["precision"])]
        )
        assert out == item["output"]
    for item in fixtures["distance"]:
        out = _run_cli_ok(
            ["distance", *item["points"], "--digits", str(item["digits"])]
        )
        assert out == item["output"]
    for item in fixtures["bearing"]:
        out = _run_cli_ok(
            ["bearing", *item["points"], "--digits", str(item["digits"])]
        )
        assert out == item["output"]


def test_cli_center_csv(sample_valid_locators):
    loc = sample_valid_locators(lengths=[6], seed=101)[0]
    proc = _run_cli(["center", loc, "--digits", "4", "--csv"])
    assert proc.returncode == 0
    lat, lon = to_center_latlon(loc)
    lines = proc.stdout.strip().splitlines()
    assert lines[0] == "input,lat,lon"
    out_lat, out_lon = [float(v) for v in lines[1].split(",")[1:]]
    assert out_lat == pytest.approx(round(lat, 4))
    assert out_lon == pytest.approx(round(lon, 4))


def test_cli_bbox_csv(sample_valid_locators):
    loc = sample_valid_locators(lengths=[6], seed=102)[0]
    proc = _run_cli(["bbox", loc, "--digits", "4", "--csv"])
    assert proc.returncode == 0
    min_lat, min_lon, max_lat, max_lon = to_bbox(loc)
    lines = proc.stdout.strip().splitlines()
    assert lines[0].startswith("input,")
    out_vals = [float(v) for v in lines[1].split(",")[1:]]
    expected = [min_lat, min_lon, max_lat, max_lon]
    for out_val, exp in zip(out_vals, expected):
        assert out_val == pytest.approx(round(exp, 4))


def test_cli_bbox_split_single():
    proc = _run_cli(["bbox", "IO83ri", "--split", "--digits", "4", "--csv"])
    assert proc.returncode == 0
    lines = proc.stdout.strip().splitlines()
    assert len(lines) == 2
    out_vals = [float(v) for v in lines[1].split(",")[1:]]
    expected = list(to_bbox("IO83ri"))
    for out_val, exp in zip(out_vals, expected):
        assert out_val == pytest.approx(round(exp, 4))


def test_cli_from_latlon_single_arg():
    out = _run_cli_ok(["from-latlon", "53.365418,-2.574069"])
    assert out == "IO83ri"


def test_cli_from_latlon_space_separated():
    out = _run_cli_ok(["from-latlon", "53.073219", "-3.934023"])
    assert out == "IO83ab"


def test_cli_from_latlon_comma_space():
    out = _run_cli_ok(["from-latlon", "53.073219,", "-3.934023"])
    assert out == "IO83ab"


def test_cli_from_latlon_precision_10():
    out = _run_cli_ok(["from-latlon", "53.365418,-2.574069", "--precision", "10"])
    assert len(out) == 10


def test_cli_parts_output(sample_valid_locators):
    loc = sample_valid_locators(lengths=[8], seed=103)[0]
    out = _run_cli_ok(["parts", loc])
    assert "field=" in out


def test_cli_size_output(sample_valid_locators):
    loc = sample_valid_locators(lengths=[6], seed=104)[0]
    proc = _run_cli(["size", loc, "--unit", "km", "--csv"])
    assert proc.returncode == 0
    lines = proc.stdout.strip().splitlines()
    assert lines[0] == "input,width,height"
    out = [float(v) for v in lines[1].split(",")[1:]]
    assert len(out) == 2
    assert out[0] > 0.0
    assert out[1] > 0.0


def test_cli_step_output(sample_valid_locators):
    loc = sample_valid_locators(lengths=[6], seed=105)[0]
    out = _run_cli_ok(["step", loc, "--dlat-cells", "1"])
    assert out == step(loc, dlat_cells=1).locator


def test_cli_normalize_batch_json_stdin():
    if orjson is None:
        pytest.skip("orjson not installed")
    data = "io83ri\nfn31pr\n"
    proc = _run_cli(["normalize", "--stdin", "--format", "json"], stdin=data)
    assert proc.returncode == 0
    assert orjson.loads(proc.stdout.strip()) == ["IO83ri", "FN31pr"]


def test_cli_batch_conflicting_inputs():
    proc = _run_cli(["normalize", "--stdin", "--file", "locators.txt"], stdin="IO83rj\n")
    assert proc.returncode == 2
    assert "error:" in proc.stderr


def test_cli_normalize_requires_locator():
    proc = _run_cli(["normalize"])
    assert proc.returncode == 2
    assert "error:" in proc.stderr


def test_cli_from_latlon_batch_invalid_line():
    proc = _run_cli(["from-latlon", "--stdin", "--format", "plain"], stdin="53.36,-2.57,1\n")
    assert proc.returncode == 2
    assert "error:" in proc.stderr


def test_cli_geojson_batch_requires_featurecollection():
    proc = _run_cli(["geojson", "--stdin"], stdin="IO83rj\n")
    assert proc.returncode == 2
    assert "error:" in proc.stderr


def test_cli_center_batch_csv_file(tmp_path, sample_valid_locators):
    locs = sample_valid_locators(lengths=[4, 6], seed=113, count=1)
    file_path = tmp_path / "locs.txt"
    file_path.write_text("\n".join(locs))
    proc = _run_cli(["center", "--file", str(file_path), "--format", "csv", "--digits", "4"])
    assert proc.returncode == 0
    lines = proc.stdout.strip().splitlines()
    assert len(lines) == 3
    assert lines[0] == "input,lat,lon"
    assert all("," in line for line in lines[1:])


def test_cli_from_latlon_batch_json_stdin():
    if orjson is None:
        pytest.skip("orjson not installed")
    data = "53.365418,-2.574069\n52.069654,4.271870\n"
    proc = _run_cli(["from-latlon", "--stdin", "--format", "json"], stdin=data)
    assert proc.returncode == 0
    out = orjson.loads(proc.stdout.strip())
    assert [item["output"] for item in out] == ["IO83ri", "JO22db"]


def test_cli_format_truncate(sample_valid_locators):
    loc = sample_valid_locators(lengths=[6], seed=106)[0]
    out = _run_cli_ok(["format", loc, "--precision", "2", "--mode", "truncate"])
    assert len(out) == 2


def test_cli_size_at_lat(sample_valid_locators):
    loc = sample_valid_locators(lengths=[6], seed=107)[0]
    proc = _run_cli(["size", loc, "--unit", "km", "--at-lat", "10"])
    assert proc.returncode == 0
    out = [float(v) for v in proc.stdout.strip().split()]
    assert len(out) == 2
    assert out[0] > 0.0
    assert out[1] > 0.0


def test_cli_area_diagonal(sample_valid_locators):
    loc = sample_valid_locators(lengths=[6], seed=108)[0]
    out = _run_cli_ok(["area", loc])
    assert float(out) > 0.0


def test_cli_distance_comma_space():
    out = float(_run_cli_ok(["distance", "53.073219,", "-3.934023", "51.5074,-0.1278"]))
    expected = distance_km((53.073219, -3.934023), (51.5074, -0.1278))
    assert out == pytest.approx(expected)


def test_cli_distance_space_separated():
    out = float(_run_cli_ok(["distance", "53.073219", "-3.934023", "51.5074", "-0.1278"]))
    expected = distance_km((53.073219, -3.934023), (51.5074, -0.1278))
    assert out == pytest.approx(expected)


def test_cli_utm(sample_valid_locators):
    loc = sample_valid_locators(lengths=[6], seed=109)[0]
    out = _run_cli_ok(["utm", loc])
    assert out == to_utm_zone(loc)


def test_cli_corners():
    proc = _run_cli(["corners", "IO83ri", "--digits", "4"])
    assert proc.returncode == 0
    lines = proc.stdout.strip().splitlines()
    assert len(lines) == 4


def test_cli_precision():
    out = _run_cli_ok(["precision", "IO83ri"])
    assert out == "6"


def test_cli_neighbors():
    out = _run_cli_ok(["neighbors", "IO83ri", "--no-diagonals"]).split()
    assert len(out) == 4


def test_cli_adjacent():
    lines = _run_cli_ok(["adjacent", "IO83ri"]).splitlines()
    assert len(lines) == 4


def test_cli_wkt_locator():
    out = _run_cli_ok(["wkt", "IO83ri"])
    assert out.startswith("POLYGON((")


def test_cli_wkt_latlon():
    out = _run_cli_ok(["wkt", "53.365418,-2.574069", "--precision", "6"])
    assert out.startswith("POLYGON((")


def test_cli_cover_circle():
    locs = _run_cli_ok(["cover-circle", "0.0,0.0", "5", "--precision", "4"]).split()
    assert "JJ00" in locs


def test_cli_cover_line():
    locs = _run_cli_ok(["cover-line", "0.0,0.0", "1.0,1.0", "--precision", "4"]).split()
    assert "JJ00" in locs


def test_cli_great_circle():
    lines = _run_cli_ok(["great-circle", "0.0,0.0", "1.0,1.0", "--points-count", "3"]).splitlines()
    assert len(lines) == 3


def test_cli_bearing_bin():
    out = float(_run_cli_ok(["bearing-bin", "0.0,0.0", "0.0,10.0", "--bin-size", "10"]))
    assert out == pytest.approx(90.0)


def test_cli_azimuthal_sector():
    start, end = [float(v) for v in _run_cli_ok(["azimuthal-sector", "0.0,0.0", "0.0,10.0", "--width", "20"]).split()]
    assert start == pytest.approx(80.0)
    assert end == pytest.approx(100.0)


def test_cli_midpoint():
    assert _run_cli_ok(["midpoint", "0.0,0.0", "0.0,10.0"])


def test_cli_midpoint_geodesic_missing_dependency():
    proc = _run_cli(["midpoint", "0.0,0.0", "0.0,10.0", "--method", "geodesic"])
    if proc.returncode == 0:
        assert proc.stdout.strip()
    else:
        assert proc.returncode == 2
        assert "geodesic midpoint requires" in proc.stderr


def test_cli_azimuth_basic():
    parts = _run_cli_ok(["azimuth", "IO83ri", "IO84aa", "--digits", "4"]).split()
    assert len(parts) == 2


def test_cli_azimuth_range():
    lines = _run_cli_ok(["azimuth", "IO83ri", "IO84aa", "--range", "--digits", "4", "--csv"]).splitlines()
    assert lines[0] == "bearing_deg,min_distance_km,max_distance_km"
    parts = lines[1].split(",")
    assert len(parts) == 3


def test_cli_initial_bearing():
    assert _run_cli_ok(["initial-bearing", "IO83ri", "IO84aa", "--digits", "4"])


def test_cli_parent_default():
    out = _run_cli_ok(["parent", "IO83ri"])
    assert out == "IO83"


def test_cli_parent_precision():
    out = _run_cli_ok(["parent", "IO83ri", "--precision", "2"])
    assert out == "IO"


def test_cli_children_default():
    out = _run_cli_ok(["children", "IO83", "--limit", "5"]).split()
    assert len(out) == 5


def test_cli_children_precision():
    out = _run_cli_ok(["children", "IO", "--precision", "6", "--limit", "3"]).split()
    assert len(out) == 3


def test_cli_contains():
    out = _run_cli_ok(["contains", "IO83", "IO83ri"])
    assert out == "true"


def test_cli_contains_point():
    out = _run_cli_ok(["contains-point", "IO83ri", "53.354167,-2.541667"])
    assert out == "true"


def test_cli_intersects_bbox():
    out = _run_cli_ok(["intersects-bbox", "IO83ri", "53.0", "-3.0", "54.0", "-2.0"])
    assert out == "true"


def test_cli_intersects_polygon():
    proc = _run_cli(
        [
            "intersects-polygon",
            "IO83ri",
            "53.0,-3.0",
            "53.0,-2.0",
            "54.0,-2.0",
            "54.0,-3.0",
        ]
    )
    assert proc.returncode == 0
    assert proc.stdout.strip() == "true"


def test_cli_geojson_feature(sample_valid_locators):
    if orjson is None:
        pytest.skip("orjson not installed")
    loc = sample_valid_locators(lengths=[6], seed=110)[0]
    proc = _run_cli(["geojson", loc])
    assert proc.returncode == 0
    data = orjson.loads(proc.stdout.strip())
    assert data["type"] == "Feature"


def test_cli_geojson_bbox_format():
    if orjson is None:
        pytest.skip("orjson not installed")
    proc = _run_cli(["geojson", "IO83ri", "--geojson-format", "bbox"])
    assert proc.returncode == 0
    out = orjson.loads(proc.stdout.strip())
    expected = to_geojson_bbox("IO83ri")
    assert out == pytest.approx(expected)


def test_cli_geojson_featurecollection_stdin(sample_valid_locators):
    if orjson is None:
        pytest.skip("orjson not installed")
    data = "\n".join(sample_valid_locators(lengths=[4, 6], seed=114, count=1)) + "\n"
    proc = _run_cli(["geojson", "--stdin", "--geojson-format", "featurecollection"], stdin=data)
    assert proc.returncode == 0
    out = orjson.loads(proc.stdout.strip())
    assert out["type"] == "FeatureCollection"


def test_cli_cover_circle_batch_csv_stdin():
    data = "JJ00 5 4\n"
    proc = _run_cli(["cover-circle", "0.0,0.0", "5", "--precision", "4", "--stdin", "--format", "csv"], stdin=data)
    assert proc.returncode == 0
    line = proc.stdout.strip()
    assert line
    assert " " not in line


def test_cli_cover_line_batch_csv_stdin():
    data = "JJ00 JJ11 4\n"
    proc = _run_cli(["cover-line", "0.0,0.0", "1.0,1.0", "--precision", "4", "--stdin", "--format", "csv"], stdin=data)
    assert proc.returncode == 0
    lines = proc.stdout.strip().splitlines()
    assert lines[0] == "locator"
    assert len(lines) > 1


def test_cli_cover_circle_batch_json_stdin():
    if orjson is None:
        pytest.skip("orjson not installed")
    data = "JJ00 5 4\n"
    proc = _run_cli(["cover-circle", "0.0,0.0", "5", "--precision", "4", "--stdin", "--format", "json"], stdin=data)
    assert proc.returncode == 0
    out = orjson.loads(proc.stdout.strip())
    assert isinstance(out, list)
    assert out and isinstance(out[0], dict)


def test_cli_cover_line_batch_json_stdin():
    if orjson is None:
        pytest.skip("orjson not installed")
    data = "JJ00 JJ11 4\n"
    proc = _run_cli(["cover-line", "0.0,0.0", "1.0,1.0", "--precision", "4", "--stdin", "--format", "json"], stdin=data)
    assert proc.returncode == 0
    out = orjson.loads(proc.stdout.strip())
    assert isinstance(out, list)
    assert out and isinstance(out[0], dict)



def test_cli_bbox_split_command():
    proc = _run_cli(["bbox-split", "0", "170", "10", "-170", "--csv", "--digits", "4"])
    assert proc.returncode == 0
    lines = proc.stdout.strip().splitlines()
    assert len(lines) == 3


def test_cli_bbox_split_list_json():
    if orjson is None:
        pytest.skip("orjson not installed")
    proc = _run_cli(["bbox-split-list", "0", "170", "10", "-170", "--format", "json"])
    assert proc.returncode == 0
    out = orjson.loads(proc.stdout.strip())
    assert len(out) == 2


def test_cli_validate_invalid(invalid_locators):
    for loc in invalid_locators:
        proc = _run_cli(["validate", loc])
        assert proc.returncode == 2


def test_cli_validate_valid_lengths(valid_locators):
    for loc in valid_locators:
        proc = _run_cli(["validate", loc])
        assert proc.returncode == 0


def test_cli_validate_print_valid(sample_valid_locators):
    loc = sample_valid_locators(lengths=[6], seed=111)[0]
    out = _run_cli_ok(["validate", loc, "--print"])
    assert out == "valid"


def test_cli_validate_print_invalid(sample_invalid_locators):
    loc = sample_invalid_locators(seed=112)[0]
    proc = _run_cli(["validate", loc, "--print"])
    assert proc.returncode == 2
    assert proc.stdout.strip() == "invalid"
