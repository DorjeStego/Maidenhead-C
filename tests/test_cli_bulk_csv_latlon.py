import csv
import io
import json
import os
import subprocess
import sys
import shutil
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
if str(SRC) not in sys.path:
    sys.path.insert(0, str(SRC))

from maidenhead import core


LATLON_LINES = [
    "51.4779,-0.0015",
    "40.6892,-74.0445",
    "48.8584,2.2945",
    "35.6586,139.7454",
    "-33.8568,151.2153",
    "-37.8136,144.9631",
    "-23.5505,-46.6333",
    "-22.9068,-43.1729",
    "-34.6037,-58.3816",
    "-12.0464,-77.0428",
    "19.4326,-99.1332",
    "14.6349,-90.5069",
    "9.9281,-84.0907",
    "4.7110,-74.0721",
    "10.4806,-66.9036",
    "18.4655,-66.1057",
    "18.2208,-66.5901",
    "25.7617,-80.1918",
    "34.0522,-118.2437",
    "37.7749,-122.4194",
    "47.6062,-122.3321",
    "41.8781,-87.6298",
    "29.7604,-95.3698",
    "39.7392,-104.9903",
    "33.4484,-112.0740",
    "44.9778,-93.2650",
    "42.3601,-71.0589",
    "38.9072,-77.0369",
    "45.5152,-122.6784",
    "49.2827,-123.1207",
    "43.6532,-79.3832",
    "45.5019,-73.5674",
    "46.8139,-71.2080",
    "53.5461,-113.4938",
    "61.2181,-149.9003",
    "64.1466,-21.9426",
    "59.3293,18.0686",
    "60.1699,24.9384",
    "55.6761,12.5683",
    "52.5200,13.4050",
    "50.1109,8.6821",
    "48.1351,11.5820",
    "45.4642,9.1900",
    "41.9028,12.4964",
    "40.4168,-3.7038",
    "38.7223,-9.1393",
    "51.5074,-0.1278",
    "53.3498,-6.2603",
    "55.9533,-3.1883",
    "52.4862,-1.8904",
    "50.8503,4.3517",
    "52.3676,4.9041",
    "48.2082,16.3738",
    "50.0755,14.4378",
    "52.2297,21.0122",
    "47.4979,19.0402",
    "44.4268,26.1025",
    "42.6977,23.3219",
    "37.9838,23.7275",
    "41.0082,28.9784",
    "55.7558,37.6173",
    "59.9343,30.3351",
    "64.9631,40.2030",
    "24.7136,46.6753",
    "25.2048,55.2708",
    "31.7683,35.2137",
    "32.0853,34.7818",
    "30.0444,31.2357",
    "33.8938,35.5018",
    "35.6892,51.3890",
    "34.5553,69.2075",
    "33.3152,44.3661",
    "19.0760,72.8777",
    "28.6139,77.2090",
    "13.7563,100.5018",
    "21.0278,105.8342",
    "10.8231,106.6297",
    "1.3521,103.8198",
    "3.1390,101.6869",
    "-6.2088,106.8456",
    "14.5995,120.9842",
    "31.2304,121.4737",
    "39.9042,116.4074",
    "22.3193,114.1694",
    "37.5665,126.9780",
    "35.1796,129.0756",
    "47.8864,106.9057",
    "-1.2921,36.8219",
    "-6.7924,39.2083",
    "0.3476,32.5825",
    "9.1450,40.4897",
    "30.5595,22.9375",
    "36.8065,10.1815",
    "33.5731,-7.5898",
    "-26.2041,28.0473",
    "-33.9249,18.4241",
    "-25.7479,28.2293",
    "-17.8249,31.0493",
    "-15.3875,28.3228",
    "-22.5609,17.0658",
    "-34.9285,138.6007",
    "-27.4698,153.0251",
    "-31.9505,115.8605",
    "-36.8485,174.7633",
    "-41.2865,174.7762",
    "-9.4438,147.1803",
    "-17.5334,-149.5667",
    "-13.8333,-171.7500",
    "-18.1248,178.4501",
    "-21.1393,-175.2049",
    "64.9631,-19.0208",
    "78.2232,15.6469",
    "-75.2500,0.0000",
    "0.0000,0.0000",
    "0.0000,179.9000",
    "0.0000,-179.9000",
    "89.9000,0.0000",
    "-89.9000,0.0000",
    "66.5622,25.7482",
    "-54.8019,-68.3030",
    "52.2053,0.1218",
    "43.7696,11.2558",
    "45.0703,7.6869",
    "44.4949,11.3426",
    "43.7102,7.2620",
    "47.3769,8.5417",
    "46.2044,6.1432",
    "47.5596,7.5886",
    "43.2965,5.3698",
    "43.6047,1.4442",
    "44.8378,-0.5792",
    "48.5734,7.7521",
    "49.6116,6.1319",
    "50.6292,3.0573",
    "51.2194,4.4025",
    "51.9244,4.4777",
    "52.0705,4.3007",
    "51.4416,5.4697",
    "52.0907,5.1214",
    "53.2194,6.5665",
    "54.3520,18.6466",
    "50.0647,19.9450",
    "51.1079,17.0385",
    "54.6872,25.2797",
    "56.9496,24.1052",
    "59.4370,24.7536",
    "58.3776,26.7290",
    "63.4305,10.3951",
    "58.9690,5.7331",
    "60.3913,5.3221",
    "70.6634,23.6821",
    "69.6492,18.9553",
    "65.0121,25.4651",
    "68.9585,33.0827",
    "56.3269,44.0065",
    "54.7104,20.4522",
    "43.1155,131.8855",
    "41.1152,16.8719",
    "37.3891,-5.9845",
    "36.7213,-4.4214",
    "39.4699,-0.3763",
    "43.2630,-2.9350",
    "41.3851,2.1734",
    "39.5696,2.6502",
    "28.1235,-15.4363",
    "32.6500,-16.9089",
    "5.6037,-0.1870",
    "6.5244,3.3792",
    "9.0765,7.3986",
    "5.5600,-0.2050",
    "14.7167,-17.4677",
    "31.6295,-8.0083",
    "30.4278,-9.5981",
    "35.7595,-5.8340",
    "36.7538,3.0588",
    "34.0209,-6.8416",
    "13.5127,2.1128",
    "12.3714,-1.5197",
    "6.1319,1.2228",
    "7.3775,3.9470",
    "4.8156,7.0498",
    "11.0168,29.7183",
    "15.5007,32.5599",
    "8.9806,38.7578",
    "-3.3869,29.3619",
    "-1.9441,30.0619",
    "-4.4419,15.2663",
    "-8.8390,13.2894",
    "-20.3484,57.5522",
    "-18.8792,47.5079",
    "-24.6544,25.9089",
    "-29.8587,31.0218",
    "-33.9608,25.6022",
    "-47.0674,28.4959",
    "-4.3250,15.3222",
    "-12.9711,-38.5108",
    "-8.0543,-34.8813",
    "-3.1190,-60.0217",
    "-1.4558,-48.4902",
    "-15.7942,-47.8822",
    "-16.6869,-49.2648",
    "-19.9167,-43.9345",
    "-30.0346,-51.2177",
    "-32.0580,-52.0986",
    "-25.4284,-49.2733",
    "-27.5954,-48.5480",
    "-3.7319,-38.5267",
    "0.1807,-78.4678",
    "-2.1700,-79.9224",
    "-0.9677,-80.7089",
    "-4.2658,-79.2253",
    "-16.4090,-71.5375",
    "-13.5226,-71.9673",
    "-8.1117,-79.0288",
    "-17.7833,-63.1821",
    "-19.0470,-65.2595",
    "-25.2637,-57.5759",
    "-31.4201,-64.1888",
    "-38.0055,-57.5426",
    "-32.9442,-60.6505",
    "-34.9011,-56.1645",
    "3.4516,-76.5320",
    "6.2442,-75.5812",
    "12.11499,-86.2362",
    "8.9824,-79.5199",
    "17.2510,-88.7590",
    "18.4861,-69.9312",
    "18.4663,-69.9506",
    "10.6549,-61.5019",
    "13.1939,-59.5432",
    "12.1067,-68.9335",
    "15.5000,-88.0333",
    "64.8378,-147.7164",
    "58.3019,-134.4197",
    "21.3069,-157.8583",
    "19.7070,-155.0810",
    "35.2271,-80.8431",
    "32.7765,-79.9311",
    "36.1627,-86.7816",
    "39.1031,-84.5120",
    "42.3314,-83.0458",
    "43.0389,-87.9065",
    "30.3322,-81.6557",
    "27.9506,-82.4572",
    "35.7796,-78.6382",
    "38.2527,-85.7585",
    "39.9612,-82.9988",
    "40.4406,-79.9959",
    "39.2904,-76.6122",
    "25.7907,-80.1300",
    "21.1619,-86.8515",
    "20.9674,-89.5926",
    "23.1136,-82.3666",
    "18.0179,-76.8099",
    "32.8872,13.1913",
    "36.1408,-5.3536",
    "25.2854,51.5310",
    "26.2235,50.5876",
    "23.5859,58.4059",
    "15.3694,44.1910",
    "2.0469,45.3182",
    "11.5886,43.1456",
    "19.2866,-81.3675",
    "-12.4634,130.8456",
    "-35.2809,149.1300",
    "-42.8821,147.3272",
    "-16.9186,145.7781",
    "-19.2589,146.8169",
    "-3.6547,128.1906",
    "-0.7893,113.9213",
    "-8.4095,115.1889",
    "-1.2654,116.8312",
    "-5.1477,119.4327",
    "-6.9147,107.6098",
    "-7.2575,112.7521",
    "16.8409,96.1735",
    "17.9757,102.6331",
    "11.5564,104.9282",
    "-8.5569,125.5603",
    "27.7172,85.3240",
    "23.8103,90.4125",
    "24.8607,67.0011",
    "31.5204,74.3587",
    "33.6844,73.0479",
    "34.0151,71.5249",
    "41.2995,69.2401",
    "43.2389,76.8897",
    "51.1605,71.4704",
    "40.1872,44.5152",
    "41.7151,44.8271",
    "40.4093,49.8671",
    "25.2856,51.5264",
    "-35.6751,-71.5430",
    "-16.5000,-151.7500",
    "-27.1127,-109.3497",
    "-8.5211,179.1962",
    "-0.5477,166.9209",
    "7.3697,134.4853",
    "13.4443,144.7937",
    "15.2000,145.7500",
    "19.2925,-81.3670",
    "-29.0564,167.9597",
    "-44.0000,-176.5000",
    "-46.4132,168.3538",
    "71.2906,-156.7886",
    "72.0000,-40.0000",
    "-72.0000,170.0000",
    "-79.4000,106.9000",
]


def _find_native_cli() -> Path | None:
    candidates: list[Path] = []
    env_path = os.environ.get("MAIDENHEAD_CLI_PATH")
    if env_path:
        candidates.append(Path(env_path))
    for name in ("mh", "mh_cli"):
        resolved = shutil.which(name)
        if resolved:
            candidates.append(Path(resolved))
    candidates.extend(
        [
            ROOT / "build" / "mh_cli_staging" / "mh",
            ROOT / "build" / "mh_cli_staging" / "mh_cli",
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


def _run_cli(args: list[str], stdin: str | None = None) -> subprocess.CompletedProcess[str]:
    cli_path = _find_native_cli()
    if cli_path is None:
        raise RuntimeError("native CLI not found; set MAIDENHEAD_CLI_PATH or build mh_cli")
    return subprocess.run(
        [str(cli_path), *args],
        input=stdin,
        text=True,
        capture_output=True,
    )


def _run_bulk_csv(tmp_path: Path, lines: list[str]) -> list[list[str]]:
    file_path = tmp_path / "bulk_from_latlon.txt"
    file_path.write_text("\n".join(lines))
    args = ["bulk", "from-latlon", "--file", str(file_path), "--format", "csv", "--precision", "6"]
    proc = _run_cli(args)
    assert proc.returncode == 0, proc.stderr
    reader = csv.reader(io.StringIO(proc.stdout))
    return [row for row in reader if row]


def _parse_latlon(line: str) -> tuple[float, float]:
    lat_s, lon_s = line.split(",", 1)
    return float(lat_s), float(lon_s)

def _decimal_places(text: str) -> int:
    if "." not in text:
        return 0
    _, frac = text.split(".", 1)
    count = 0
    for ch in frac:
        if not ch.isdigit():
            break
        count += 1
    return count

def _precision_from_decimals(decimals: int) -> int:
    if decimals <= 0:
        return 2
    if decimals <= 2:
        return 4
    if decimals <= 4:
        return 6
    if decimals <= 6:
        return 8
    return 10


def _run_bulk_geojson_point_csv(tmp_path: Path, lines: list[str]) -> list[list[str]]:
    file_path = tmp_path / "bulk_geojson_point.txt"
    file_path.write_text("\n".join(lines))
    args = [
        "bulk",
        "geojson",
        "--file",
        str(file_path),
        "--format",
        "csv",
        "--geojson-format",
        "point",
    ]
    proc = _run_cli(args)
    assert proc.returncode == 0, proc.stderr
    reader = csv.reader(io.StringIO(proc.stdout))
    return [row for row in reader if row]


def test_bulk_from_latlon_csv_structure(tmp_path):
    rows = _run_bulk_csv(tmp_path, LATLON_LINES)
    assert rows, "missing CSV output"
    assert rows[0] == ["input_lat", "input_lon", "locator"]
    for row in rows[1:]:
        assert len(row) == 3


def test_bulk_from_latlon_csv_values(tmp_path):
    rows = _run_bulk_csv(tmp_path, LATLON_LINES)
    header = rows[0]
    data = rows[1:]
    assert header == ["input_lat", "input_lon", "locator"]
    assert len(data) == len(LATLON_LINES)
    for row, line in zip(data, LATLON_LINES):
        lat_text, lon_text = line.split(",", 1)
        lat = float(lat_text)
        lon = float(lon_text)
        expected = core.from_latlon(lat, lon, precision=6)
        assert float(row[0]) == pytest.approx(lat, abs=1e-9)
        assert float(row[1]) == pytest.approx(lon, abs=1e-9)
        assert row[2] == expected.locator


def test_bulk_geojson_point_csv_structure(tmp_path):
    rows = _run_bulk_geojson_point_csv(tmp_path, LATLON_LINES)
    assert rows, "missing CSV output"
    assert rows[0] == ["input_lat", "input_lon", "grid", "geojson"]
    for row in rows[1:]:
        assert len(row) == 4


def test_bulk_geojson_point_csv_values(tmp_path):
    rows = _run_bulk_geojson_point_csv(tmp_path, LATLON_LINES)
    header = rows[0]
    data = rows[1:]
    assert header == ["input_lat", "input_lon", "grid", "geojson"]
    assert len(data) == len(LATLON_LINES)
    for row, line in zip(data, LATLON_LINES):
        lat_text, lon_text = line.split(",", 1)
        lat = float(lat_text)
        lon = float(lon_text)
        precision = _precision_from_decimals(max(_decimal_places(lat_text), _decimal_places(lon_text)))
        expected = core.from_latlon(lat, lon, precision=precision)
        assert float(row[0]) == pytest.approx(lat, abs=1e-6)
        assert float(row[1]) == pytest.approx(lon, abs=1e-6)
        assert row[2] == expected.locator
        payload = json.loads(row[3])
        assert payload["type"] == "Point"
        coords = payload["coordinates"]
        assert coords[0] == pytest.approx(lon, abs=1e-6)
        assert coords[1] == pytest.approx(lat, abs=1e-6)
