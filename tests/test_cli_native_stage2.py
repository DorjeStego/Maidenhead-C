import os
import shutil
import subprocess
from contextlib import contextmanager, redirect_stderr, redirect_stdout
from io import StringIO
from pathlib import Path

import pytest

import json


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"


def _find_native_cli() -> Path | None:
    env_path = os.environ.get("MAIDENHEAD_CLI_PATH")
    candidates = []
    if env_path:
        candidates.append(Path(env_path))
    candidates.extend(
        [
            ROOT / "cmake-build-debug" / "mh_cli",
            ROOT / "cmake-build-default" / "mh_cli",
        ]
    )
    which = shutil.which("mh_cli")
    if which:
        candidates.append(Path(which))
    for path in candidates:
        if path.is_file() and os.access(path, os.X_OK):
            return path
    return None


NATIVE_CLI = _find_native_cli()
try:
    import orjson  # type: ignore
except Exception:
    orjson = None


@contextmanager
def _redirect_stdin(stream: StringIO):
    old_stdin = os.sys.stdin
    os.sys.stdin = stream
    try:
        yield
    finally:
        os.sys.stdin = old_stdin


def _assert_native_python_same(
    args: list[str],
    monkeypatch: pytest.MonkeyPatch,
    stdin: str | None = None,
) -> None:
    native_out = _run_native_cli(args, stdin=stdin)
    python_out = _run_python_cli(args, monkeypatch, stdin=stdin)
    use_json_equiv = False
    if args and args[0] == "geojson":
        use_json_equiv = True
    if args and args[0] == "bulk":
        if "geojson" in args:
            use_json_equiv = True
        if "--format" in args:
            try:
                fmt = args[args.index("--format") + 1]
                if fmt == "json":
                    use_json_equiv = True
            except Exception:
                pass
    if use_json_equiv:
        _assert_json_equiv(native_out, python_out)
    else:
        assert native_out == python_out


def _assert_json_equiv(native_out: str, python_out: str, tol: float = 1e-9) -> None:
    def _compare(a, b) -> None:
        if isinstance(a, (int, float)) and isinstance(b, (int, float)):
            assert float(a) == pytest.approx(float(b), rel=tol, abs=tol)
            return
        if isinstance(a, list) and isinstance(b, list):
            assert len(a) == len(b)
            for av, bv in zip(a, b):
                _compare(av, bv)
            return
        if isinstance(a, dict) and isinstance(b, dict):
            assert set(a.keys()) == set(b.keys())
            for key in a:
                _compare(a[key], b[key])
            return
        assert a == b

    _compare(json.loads(native_out), json.loads(python_out))


def _run_native_cli(args: list[str], stdin: str | None = None) -> str:
    assert NATIVE_CLI is not None
    proc = subprocess.run(
        [str(NATIVE_CLI), *args],
        capture_output=True,
        text=True,
        input=stdin,
    )
    assert proc.returncode == 0, proc.stderr
    return proc.stdout.strip()


def _run_python_cli(args: list[str], monkeypatch: pytest.MonkeyPatch, stdin: str | None = None) -> str:
    if str(SRC) not in os.sys.path:
        os.sys.path.insert(0, str(SRC))
    from maidenhead.cli import main
    import maidenhead.core as core
    import maidenhead.geo as geo
    import maidenhead.bulk as bulk
    import maidenhead.vector as vector

    monkeypatch.delenv("MAIDENHEAD_CLI_FORCE_NATIVE", raising=False)
    core._native_mh = None
    geo._native_mh = None
    bulk._native_mh = None
    vector._native_mh = None
    out = StringIO()
    err = StringIO()
    input_stream = StringIO(stdin) if stdin is not None else StringIO("")
    with redirect_stdout(out), redirect_stderr(err), _redirect_stdin(input_stream):
        code = main(args)
    if code != 0 and "JSON output requires orjson" in err.getvalue():
        pytest.skip("orjson not available")
    assert code == 0, err.getvalue()
    return out.getvalue().strip()


@pytest.mark.skipif(NATIVE_CLI is None, reason="native CLI not available")
@pytest.mark.skipif(orjson is None, reason="orjson not available")
@pytest.mark.parametrize(
    "args",
    [
        ["corners", "IO83ri", "--digits", "6"],
        ["size", "IO83ri", "--digits", "6"],
        ["area", "IO83ri", "--digits", "6"],
        ["diagonal", "IO83ri", "--digits", "6"],
        ["distance", "53.365418,-2.574069", "48.8566,2.3522", "--digits", "6"],
        ["bearing", "53.365418,-2.574069", "48.8566,2.3522", "--digits", "6"],
        ["midpoint", "53.365418,-2.574069", "48.8566,2.3522", "--digits", "6"],
        [
            "great-circle",
            "53.365418,-2.574069",
            "48.8566,2.3522",
            "--points-count",
            "5",
            "--digits",
            "6",
        ],
        [
            "bearing-bin",
            "53.365418,-2.574069",
            "48.8566,2.3522",
            "--bin-size",
            "10",
            "--digits",
            "6",
        ],
        [
            "azimuthal-sector",
            "53.365418,-2.574069",
            "48.8566,2.3522",
            "--width",
            "20",
            "--digits",
            "6",
        ],
        ["azimuth", "IO83ri", "JN18ev", "--digits", "6"],
        ["initial-bearing", "IO83ri", "JN18ev", "--digits", "6"],
        ["neighbors", "IO83ri", "--no-diagonals"],
        ["adjacent", "IO83ri"],
        ["bbox-split", "53.333333", "-2.583333", "53.375", "-2.5", "--digits", "6"],
        ["children", "IO83ri", "--limit", "3"],
        ["parent", "IO83ri"],
        ["contains", "IO83ri", "IO83"],
        ["contains-point", "IO83ri", "53.365418", "-2.574069"],
        ["intersects-bbox", "IO83ri", "53.333333", "-2.583333", "53.375", "-2.5"],
        ["intersects-polygon", "IO83ri", "53.333333,-2.583333", "53.375,-2.583333", "53.375,-2.5"],
        [
            "cover-circle",
            "53.365418,-2.574069",
            "2.5",
            "--precision",
            "4",
        ],
        [
            "cover-line",
            "53.365418,-2.574069",
            "48.8566,2.3522",
            "--precision",
            "4",
        ],
        ["wkt", "IO83ri"],
        ["wkt", "53.365418,-2.574069", "--precision", "6"],
    ],
)
def test_native_cli_stage2_parity(args: list[str], monkeypatch: pytest.MonkeyPatch) -> None:
    _assert_native_python_same(args, monkeypatch)


@pytest.mark.skipif(NATIVE_CLI is None, reason="native CLI not available")
@pytest.mark.parametrize(
    "args",
    [
        ["geojson", "IO83ri"],
        ["geojson", "IO83ri", "--geojson-format", "featurecollection"],
        ["geojson", "IO83ri", "--geojson-format", "bbox"],
        ["geojson", "IO83ri", "--geojson-format", "bbox", "--split"],
        ["geojson", "IO83ri", "--geojson-format", "envelope"],
        ["geojson", "IO83ri", "--geojson-format", "envelope", "--split"],
    ],
)
def test_native_cli_geojson_parity(args: list[str], monkeypatch: pytest.MonkeyPatch) -> None:
    _assert_native_python_same(args, monkeypatch)


@pytest.mark.skipif(NATIVE_CLI is None, reason="native CLI not available")
@pytest.mark.skipif(orjson is None, reason="orjson not available")
def test_native_cli_geojson_batch_featurecollection(monkeypatch: pytest.MonkeyPatch) -> None:
    stdin = "IO83ri\nJN18ev\n"
    args = ["geojson", "--stdin", "--geojson-format", "featurecollection"]
    _assert_native_python_same(args, monkeypatch, stdin=stdin)


@pytest.mark.skipif(NATIVE_CLI is None, reason="native CLI not available")
@pytest.mark.skipif(orjson is None, reason="orjson not available")
def test_native_cli_wkt_batch_json(monkeypatch: pytest.MonkeyPatch) -> None:
    stdin = "IO83ri\n53.365418,-2.574069\n"
    args = ["wkt", "--stdin", "--format", "json", "--precision", "6"]
    _assert_native_python_same(args, monkeypatch, stdin=stdin)


@pytest.mark.skipif(NATIVE_CLI is None, reason="native CLI not available")
def test_native_cli_bulk_wkt(monkeypatch: pytest.MonkeyPatch) -> None:
    stdin = "IO83ri\n53.365418,-2.574069\n"
    args = ["bulk", "wkt", "--stdin"]
    _assert_native_python_same(args, monkeypatch, stdin=stdin)


@pytest.mark.skipif(NATIVE_CLI is None, reason="native CLI not available")
def test_native_cli_bulk_contains_point(monkeypatch: pytest.MonkeyPatch) -> None:
    stdin = "IO83ri 53.354167 -2.541667\nIO83ri 80 0\n"
    args = ["bulk", "contains-point", "--stdin"]
    _assert_native_python_same(args, monkeypatch, stdin=stdin)


@pytest.mark.skipif(NATIVE_CLI is None, reason="native CLI not available")
def test_native_cli_bulk_contains(monkeypatch: pytest.MonkeyPatch) -> None:
    stdin = "IO83 IO83ri\nIO83 JO22db\n"
    args = ["bulk", "contains", "--stdin"]
    _assert_native_python_same(args, monkeypatch, stdin=stdin)


@pytest.mark.skipif(NATIVE_CLI is None, reason="native CLI not available")
def test_native_cli_bulk_intersects_bbox(monkeypatch: pytest.MonkeyPatch) -> None:
    stdin = "IO83ri 53.0 -3.0 54.0 -2.0\nIO83ri 80 0 81 1\n"
    args = ["bulk", "intersects-bbox", "--stdin"]
    _assert_native_python_same(args, monkeypatch, stdin=stdin)


@pytest.mark.skipif(NATIVE_CLI is None, reason="native CLI not available")
def test_native_cli_bulk_azimuth(monkeypatch: pytest.MonkeyPatch) -> None:
    stdin = "IO83ri IO84aa\nIO83ri IO83ri\n"
    args = ["bulk", "azimuth", "--stdin"]
    _assert_native_python_same(args, monkeypatch, stdin=stdin)


@pytest.mark.skipif(NATIVE_CLI is None, reason="native CLI not available")
def test_native_cli_bulk_initial_bearing(monkeypatch: pytest.MonkeyPatch) -> None:
    stdin = "IO83ri IO84aa\nIO83ri IO83ri\n"
    args = ["bulk", "initial-bearing", "--stdin"]
    _assert_native_python_same(args, monkeypatch, stdin=stdin)


@pytest.mark.skipif(NATIVE_CLI is None, reason="native CLI not available")
def test_native_cli_bulk_intersects_polygon(monkeypatch: pytest.MonkeyPatch) -> None:
    stdin = "IO83ri 53.0,-3.0 53.0,-2.0 54.0,-2.0 54.0,-3.0\n"
    args = ["bulk", "intersects-polygon", "--stdin"]
    _assert_native_python_same(args, monkeypatch, stdin=stdin)


@pytest.mark.skipif(NATIVE_CLI is None, reason="native CLI not available")
def test_native_cli_bulk_neighbors(monkeypatch: pytest.MonkeyPatch) -> None:
    stdin = "IO83ri\n"
    args = ["bulk", "neighbors", "--stdin", "--no-diagonals"]
    _assert_native_python_same(args, monkeypatch, stdin=stdin)


@pytest.mark.skipif(NATIVE_CLI is None, reason="native CLI not available")
def test_native_cli_bulk_adjacent(monkeypatch: pytest.MonkeyPatch) -> None:
    stdin = "IO83ri\n"
    args = ["bulk", "adjacent", "--stdin"]
    _assert_native_python_same(args, monkeypatch, stdin=stdin)


@pytest.mark.skipif(NATIVE_CLI is None, reason="native CLI not available")
def test_native_cli_bulk_corners(monkeypatch: pytest.MonkeyPatch) -> None:
    stdin = "IO83ri\n"
    args = ["bulk", "corners", "--stdin", "--digits", "4"]
    _assert_native_python_same(args, monkeypatch, stdin=stdin)


@pytest.mark.skipif(NATIVE_CLI is None, reason="native CLI not available")
def test_native_cli_bulk_precision(monkeypatch: pytest.MonkeyPatch) -> None:
    stdin = "IO83ri\nIO83\n"
    args = ["bulk", "precision", "--stdin"]
    _assert_native_python_same(args, monkeypatch, stdin=stdin)


@pytest.mark.skipif(NATIVE_CLI is None, reason="native CLI not available")
def test_native_cli_bulk_parent(monkeypatch: pytest.MonkeyPatch) -> None:
    stdin = "IO83ri\n"
    args = ["bulk", "parent", "--stdin"]
    _assert_native_python_same(args, monkeypatch, stdin=stdin)


@pytest.mark.skipif(NATIVE_CLI is None, reason="native CLI not available")
def test_native_cli_bulk_children(monkeypatch: pytest.MonkeyPatch) -> None:
    stdin = "IO83\n"
    args = ["bulk", "children", "--stdin", "--limit", "3"]
    _assert_native_python_same(args, monkeypatch, stdin=stdin)


@pytest.mark.skipif(NATIVE_CLI is None, reason="native CLI not available")
def test_native_cli_bulk_size(monkeypatch: pytest.MonkeyPatch) -> None:
    stdin = "IO83ri\n"
    args = ["bulk", "size", "--stdin", "--unit", "km"]
    _assert_native_python_same(args, monkeypatch, stdin=stdin)


@pytest.mark.skipif(NATIVE_CLI is None, reason="native CLI not available")
def test_native_cli_bulk_area(monkeypatch: pytest.MonkeyPatch) -> None:
    stdin = "IO83ri\n"
    args = ["bulk", "area", "--stdin"]
    _assert_native_python_same(args, monkeypatch, stdin=stdin)


@pytest.mark.skipif(NATIVE_CLI is None, reason="native CLI not available")
def test_native_cli_bulk_diagonal(monkeypatch: pytest.MonkeyPatch) -> None:
    stdin = "IO83ri\n"
    args = ["bulk", "diagonal", "--stdin"]
    _assert_native_python_same(args, monkeypatch, stdin=stdin)


@pytest.mark.skipif(NATIVE_CLI is None, reason="native CLI not available")
def test_native_cli_bulk_utm(monkeypatch: pytest.MonkeyPatch) -> None:
    stdin = "IO83ri\n"
    args = ["bulk", "utm", "--stdin"]
    _assert_native_python_same(args, monkeypatch, stdin=stdin)


@pytest.mark.skipif(NATIVE_CLI is None, reason="native CLI not available")
def test_native_cli_bulk_bbox_split(monkeypatch: pytest.MonkeyPatch) -> None:
    stdin = "0 170 10 -170\n"
    args = ["bulk", "bbox-split", "--stdin"]
    _assert_native_python_same(args, monkeypatch, stdin=stdin)


@pytest.mark.skipif(NATIVE_CLI is None, reason="native CLI not available")
@pytest.mark.skipif(orjson is None, reason="orjson not available")
def test_native_cli_bulk_center_json(monkeypatch: pytest.MonkeyPatch) -> None:
    stdin = "IO83ri\nJO22db\n"
    args = ["bulk", "center", "--stdin", "--format", "json"]
    _assert_native_python_same(args, monkeypatch, stdin=stdin)


@pytest.mark.skipif(NATIVE_CLI is None, reason="native CLI not available")
@pytest.mark.skipif(orjson is None, reason="orjson not available")
def test_native_cli_bulk_geojson_featurecollection(monkeypatch: pytest.MonkeyPatch) -> None:
    stdin = "IO83ri\nJO22db\n"
    args = ["bulk", "geojson", "--stdin", "--geojson-format", "featurecollection"]
    _assert_native_python_same(args, monkeypatch, stdin=stdin)
