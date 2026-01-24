#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PREFIX="${MAIDENHEAD_PREFIX:-/tmp/maidenhead_prefix}"

REBUILD=0
if [[ "${1:-}" == "--rebuild" ]]; then
  REBUILD=1
  shift
fi

PYVER="${MAIDENHEAD_PYVER:-}"
if [[ -z "$PYVER" ]]; then
  PYVER="$(python - <<'PY'
import sys
print(f"{sys.version_info.major}.{sys.version_info.minor}")
PY
)"
fi

SITEPKG="$PREFIX/lib/python${PYVER}/site-packages"
NATIVE_SO="${SITEPKG}/maidenhead/_native"
SRC_NATIVE_SO="$ROOT/src/maidenhead/_native"

if [[ $REBUILD -eq 1 || ! -d "$SITEPKG" || -z "$(ls "$NATIVE_SO"* 2>/dev/null)" ]]; then
  python -m pip install -e "$ROOT" \
    --break-system-packages \
    --no-build-isolation \
    --prefix "$PREFIX"
fi

if [[ -z "$(ls "$NATIVE_SO"* 2>/dev/null)" && -z "$(ls "$SRC_NATIVE_SO"* 2>/dev/null)" ]]; then
  echo "Native extension not found at $NATIVE_SO or $SRC_NATIVE_SO; aborting."
  exit 1
fi

PYTHONPATH="$SITEPKG:$ROOT/src"
export PYTHONPATH

CLI_PATH=""
for CANDIDATE in "$ROOT/cmake-build-debug/mh_cli" "$ROOT/cmake-build-default/mh_cli"; do
  if [[ -x "$CANDIDATE" ]]; then
    CLI_PATH="$CANDIDATE"
    break
  fi
done

if [[ -z "$CLI_PATH" && -x "$(command -v cmake)" ]]; then
  BUILD_DIR="$ROOT/cmake-build-native"
  cmake -S "$ROOT" -B "$BUILD_DIR"
  cmake --build "$BUILD_DIR" --target mh_cli
  if [[ -x "$BUILD_DIR/mh_cli" ]]; then
    CLI_PATH="$BUILD_DIR/mh_cli"
  fi
fi

if [[ -n "$CLI_PATH" ]]; then
  export MAIDENHEAD_CLI_PATH="$CLI_PATH"
  echo "Using MAIDENHEAD_CLI_PATH=$MAIDENHEAD_CLI_PATH"
else
  echo "Native CLI not found; aborting."
  exit 1
fi

echo "Using PYTHONPATH=$PYTHONPATH"
pytest -q
