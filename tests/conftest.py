from __future__ import annotations

import importlib.util
import json
import random
import sys
import sysconfig
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"

if str(SRC) not in sys.path:
    sys.path.insert(0, str(SRC))

def _load_native_extension() -> None:
    ext_suffix = sysconfig.get_config_var("EXT_SUFFIX") or ""
    if not ext_suffix:
        return
    native_so = ROOT / "src" / "maidenhead" / f"_native{ext_suffix}"
    if not native_so.exists():
        return
    spec = importlib.util.spec_from_file_location("maidenhead._native", native_so)
    if spec is None or spec.loader is None:
        return
    try:
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
    except ImportError:
        return
    sys.modules["maidenhead._native"] = module

_load_native_extension()

mod = sys.modules.get("maidenhead")
if mod is not None:
    mod_path = getattr(mod, "__file__", "")
    if mod_path and "site-packages" in mod_path:
        del sys.modules["maidenhead"]


def _load_locator_cases() -> dict:
    path = ROOT / "tests" / "locators.json"
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


@pytest.fixture(scope="session")
def locator_cases() -> dict:
    return _load_locator_cases()


@pytest.fixture(scope="session")
def valid_locators(locator_cases: dict) -> list[str]:
    out: list[str] = []
    for length_map in locator_cases["valid_locators"].values():
        for locs in length_map.values():
            out.extend(locs)
    return out


@pytest.fixture(scope="session")
def invalid_locator_groups(locator_cases: dict) -> dict:
    return locator_cases["invalid_locators"]


@pytest.fixture(scope="session")
def invalid_locators(invalid_locator_groups: dict) -> list[str]:
    out: list[str] = []
    for locs in invalid_locator_groups.values():
        out.extend(locs)
    return out


@pytest.fixture(scope="session")
def sample_valid_locators(valid_locators: list[str]):
    def _sample(*, lengths: list[int] | None = None, seed: int = 0, count: int = 3) -> list[str]:
        rng = random.Random(seed)
        locs = valid_locators
        if lengths is None:
            if not locs:
                return []
            if count >= len(locs):
                return locs[:]
            return rng.sample(locs, count)

        # When lengths are provided, interpret count as "per length" to
        # preserve older fixture behavior used by tests.
        lengths_set = set(lengths)
        candidates = [loc for loc in locs if len(loc) in lengths_set]
        if not candidates:
            return []
        by_len: dict[int, list[str]] = {}
        for loc in candidates:
            by_len.setdefault(len(loc), []).append(loc)
        out: list[str] = []
        for length in lengths:
            group = by_len.get(length, [])
            if not group:
                continue
            if count >= len(group):
                out.extend(group)
            else:
                out.extend(rng.sample(group, count))
        return out

    return _sample


@pytest.fixture(scope="session")
def sample_invalid_locators(invalid_locators: list[str]):
    def _sample(*, seed: int = 0, count: int = 1) -> list[str]:
        rng = random.Random(seed)
        if not invalid_locators:
            return []
        if count >= len(invalid_locators):
            return invalid_locators[:]
        return rng.sample(invalid_locators, count)

    return _sample
