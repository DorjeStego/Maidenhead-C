#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PYTHON_BIN="${PYTHON_BIN:-${1:-python}}"
WITH_SLEEF_SIMD="${WITH_SLEEF_SIMD:-0}"
EMIT_ASM="${EMIT_ASM:-0}"
BUILD_DIR="${BUILD_DIR:-$ROOT/cmake-build-native-$("$PYTHON_BIN" - <<'PY'
import sys
print(f"{sys.version_info.major}{sys.version_info.minor}")
PY
)}"

EXT_SUFFIX="$(${PYTHON_BIN} - <<'PY'
import sysconfig
print(sysconfig.get_config_var("EXT_SUFFIX") or "")
PY
)"

if [[ -z "$EXT_SUFFIX" ]]; then
  echo "Could not determine Python extension suffix for $PYTHON_BIN" >&2
  exit 1
fi

cmake -S "$ROOT" -B "$BUILD_DIR" -DPython_EXECUTABLE="$PYTHON_BIN" -DWITH_SLEEF_SIMD="$WITH_SLEEF_SIMD" -DEMIT_ASM="$EMIT_ASM"
cmake --build "$BUILD_DIR" --target _native

SRC_SO="$BUILD_DIR/_native${EXT_SUFFIX}"
ALT_SRC_SO="$BUILD_DIR/_native"
DST_SO="$ROOT/src/maidenhead/_native${EXT_SUFFIX}"

if [[ ! -f "$SRC_SO" ]]; then
  if [[ -f "$ALT_SRC_SO" ]]; then
    SRC_SO="$ALT_SRC_SO"
  else
    echo "Built extension not found at $SRC_SO or $ALT_SRC_SO" >&2
    exit 1
  fi
fi

cp -f "$SRC_SO" "$DST_SO"

echo "Installed $DST_SO"
