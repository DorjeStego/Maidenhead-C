import json
import os
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]


def _find_native_cli() -> Path | None:
    env_path = os.environ.get("MAIDENHEAD_CLI_PATH")
    candidates: list[Path] = []
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
            ROOT / "cmake-build-debug" / "mh_cli",
            ROOT / "cmake-build-default" / "mh_cli",
        ]
    )
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
    json.loads(payload)


def test_native_cli_geojson_batch_featurecollection() -> None:
    stdin = "IO83ri\nJN18ev\n"
    args = ["geojson", "--stdin", "--geojson-format", "featurecollection"]
    proc = _run_cli(args, stdin=stdin)
    assert proc.returncode == 0, proc.stderr
    out = json.loads(proc.stdout.strip())
    assert out["type"] == "FeatureCollection"
