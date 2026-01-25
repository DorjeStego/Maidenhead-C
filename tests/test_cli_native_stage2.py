import os
import subprocess
import sys
from pathlib import Path

import pytest

try:
    import orjson  # type: ignore
except Exception:
    orjson = None

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
    for path in candidates:
        if path.is_file() and os.access(path, os.X_OK):
            return path
    return None


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
        ["cover-circle", "53.365418,-2.574069", "2.5", "--precision", "4"],
        ["cover-line", "53.365418,-2.574069", "48.8566,2.3522", "--precision", "4"],
        ["wkt", "IO83ri"],
        ["wkt", "53.365418,-2.574069", "--precision", "6"],
    ],
)
def test_native_cli_stage2(args: list[str]) -> None:
    proc = _run_cli(args)
    assert proc.returncode == 0, proc.stderr
    assert proc.stdout.strip()


@pytest.mark.skipif(orjson is None, reason="orjson not available")
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
def test_native_cli_geojson(args: list[str]) -> None:
    proc = _run_cli(args)
    assert proc.returncode == 0, proc.stderr
    payload = proc.stdout.strip()
    assert payload
    orjson.loads(payload)


def test_native_cli_geojson_batch_featurecollection() -> None:
    if orjson is None:
        pytest.skip("orjson not available")
    stdin = "IO83ri\nJN18ev\n"
    args = ["geojson", "--stdin", "--geojson-format", "featurecollection"]
    proc = _run_cli(args, stdin=stdin)
    assert proc.returncode == 0, proc.stderr
    out = orjson.loads(proc.stdout.strip())
    assert out["type"] == "FeatureCollection"
